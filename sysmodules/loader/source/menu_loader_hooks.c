#include <3ds.h>
#include "sysplugin_menu.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id) __attribute__((section(".plugin_" #id), used))
#define PLUGIN_DATA(id) __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)  __attribute__((section(".pluginbss_" #id), used))

extern void *pluginTable_MENU[];
#define MENU_HOST__patchCodeAddress        ((u32)pluginTable_MENU[0])
#define MENU_HOST__svcCloseHandleAddress   ((u32)pluginTable_MENU[1])
#define MENU_HOST__homeMarker              ((u32)pluginTable_MENU[3])
#define MENU_HOST__createCodeSetMarker     ((u32)pluginTable_MENU[4])
#define MENU_HOST__createProcessMarker     ((u32)pluginTable_MENU[5])
#define MENU_HOST__plgldrExitMarker        ((u32)pluginTable_MENU[6])
#define MENU_HOST__plgldrInitAddress        ((u32)pluginTable_MENU[7])
#define MENU_HOST__plgldrInitMarker         ((u32)pluginTable_MENU[11])
#define MENU_HOST__svcCreateCodeSetAddress  ((u32)pluginTable_MENU[12])
#define MENU_HOST__svcCreateProcessAddress  ((u32)pluginTable_MENU[13])

extern void PLUGIN_MENU_PatchCode(
    u64 progId, u16 progVer, u8 *code, u32 size,
    u32 textSize, u32 roSize, u32 dataSize,
    u32 roAddress, u32 dataAddress
);
extern void PLUGIN_MENU_HomeHook(void);
extern void PLUGIN_MENU_CreateCodeSetHook(void);
extern void PLUGIN_MENU_CreateProcessHook(void);
extern void PLUGIN_MENU_PlgldrCloseHook(void);
extern void PLUGIN_MENU_PlgldrInitCallHook(void);
extern void PLUGIN_MENU_svcFlushEntireDataCache(void);
extern void PLUGIN_MENU_svcInvalidateEntireInstructionCache(void);

PLUGIN_DATA(MENU) u32 g_MENUPatchCodeResume;
PLUGIN_DATA(MENU) u32 g_MENUHomeReturn;
PLUGIN_DATA(MENU) u32 g_MENUHomeBranchTarget;
PLUGIN_DATA(MENU) u32 g_MENUCreateCodeSetReturn;
PLUGIN_DATA(MENU) u32 g_MENUCreateProcessReturn;
PLUGIN_DATA(MENU) u32 g_MENUPlgldrCloseBranchTarget;
PLUGIN_DATA(MENU) u32 g_MENUPlgldrInitCallReturn;

PLUGIN_BSS(MENU) bool g_MENUHooksReady;
PLUGIN_BSS(MENU) static u32 g_MENUPatchCodeAddress;
PLUGIN_BSS(MENU) static u32 g_MENUHomeAddress;
PLUGIN_BSS(MENU) static u32 g_MENUHomeOriginal[2];
PLUGIN_BSS(MENU) static u32 g_MENUCreateCodeSetAddress;
PLUGIN_BSS(MENU) static u32 g_MENUCreateCodeSetOriginal[2];
PLUGIN_BSS(MENU) static u32 g_MENUCreateProcessAddress;
PLUGIN_BSS(MENU) static u32 g_MENUCreateProcessOriginal[2];
PLUGIN_BSS(MENU) static u32 g_MENUPlgldrCloseAddress;
PLUGIN_BSS(MENU) static u32 g_MENUPlgldrCloseOriginal[2];
PLUGIN_BSS(MENU) static u32 g_MENUPlgldrInitCallAddress;
PLUGIN_BSS(MENU) static u32 g_MENUPlgldrInitCallOriginal[2];

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_DecodeBl(
    u32 instruction,
    u32 address,
    u32 *target
)
{
    s32 offset;

    if ((instruction & 0xFF000000u) != 0xEB000000u)
        return false;

    offset = (s32)(instruction << 8) >> 6;
    if (target)
        *target = address + 8u + (u32)offset;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_DecodeB(
    u32 instruction,
    u32 address,
    u32 *target
)
{
    s32 offset;

    if ((instruction & 0xFF000000u) != 0xEA000000u)
        return false;

    offset = (s32)(instruction << 8) >> 6;
    if (target)
        *target = address + 8u + (u32)offset;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReplaceHookPair(
    u32 address,
    u32 expectedFirst,
    u32 expectedSecond,
    u32 first,
    u32 second
)
{
    u32 mapBase = 0;
    u32 mapped = 0;
    volatile u32 *site;
    bool ok;

    if (!address || (address & 3u) || (address & 0xFFFu) > 0xFF8u ||
        !PLUGIN_MENU_MapPage(CUR_PROCESS_HANDLE, address, &mapBase, &mapped))
    {
        return false;
    }

    site = (volatile u32 *)mapped;
    ok = site[0] == expectedFirst && site[1] == expectedSecond;
    if (ok)
    {
        site[0] = first;
        site[1] = second;
        PLUGIN_MENU_svcFlushEntireDataCache();
        PLUGIN_MENU_svcInvalidateEntireInstructionCache();

        ok = site[0] == first && site[1] == second;
        if (!ok)
        {
            site[0] = expectedFirst;
            site[1] = expectedSecond;
            PLUGIN_MENU_svcFlushEntireDataCache();
            PLUGIN_MENU_svcInvalidateEntireInstructionCache();
        }
    }

    PLUGIN_MENU_UnmapPage(mapBase);
    return ok;
}

// Hook the host patchCode() entry, not its call site: another Loader path
// branches to the instruction immediately after that call. Leave it intact.
PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InstallPatchCodeHook(void)
{
    const u32 expectedPush = 0xE92D4FF0u; // push {r4-r11, lr}
    const u32 expectedMov = 0xE1A09002u;  // mov r9, r2
    u32 address = MENU_HOST__patchCodeAddress;

    if (!address || (address & 3u) || (address & 0xFFFu) > 0xFF8u)
        return false;

    // Static assembly trampoline replays these two instructions, then resumes here.
    g_MENUPatchCodeResume = address + 8u;
    if (!PLUGIN_MENU_ReplaceHookPair(
            address,
            expectedPush,
            expectedMov,
            0xE51FF004u,
            (u32)PLUGIN_MENU_PatchCode))
    {
        g_MENUPatchCodeResume = 0;
        return false;
    }

    g_MENUPatchCodeAddress = address;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_RestorePatchCodeHook(void)
{
    if (!g_MENUPatchCodeAddress)
        return true;

    if (!PLUGIN_MENU_ReplaceHookPair(
            g_MENUPatchCodeAddress,
            0xE51FF004u,
            (u32)PLUGIN_MENU_PatchCode,
            0xE92D4FF0u,
            0xE1A09002u))
    {
        return false;
    }

    g_MENUPatchCodeAddress = 0;
    g_MENUPatchCodeResume = 0;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadR2Literal(
    u32 instruction,
    u32 address,
    u32 *value
)
{
    u32 literal;

    if ((instruction & 0xFFFFF000u) != 0xE59F2000u)
        return false;

    literal = address + 8u + (instruction & 0xFFFu);
    if (literal < address || (literal & 3u))
        return false;

    *value = *(volatile u32 *)literal;
    return true;
}

// The semantic HOME marker anchors the whitelist writes. Match the complete
// stock guard around the second write; do not guess an interior continuation.
PLUGIN_CODE(MENU) static volatile u32 *PLUGIN_MENU_FindHomePatch(
    volatile u32 *anchor,
    u32 maxWords,
    u32 *branchTarget
)
{
    volatile u32 *found = NULL;

    for (u32 i = 7; i + 3u < maxWords; i++)
    {
        volatile u32 *p = anchor + i;
        u32 firstValue, secondValue, configAddress;

        if (p[-6] != 0xE7872003u || p[-4] != 0xE0873003u ||
            p[-3] != 0xE5922000u || p[-2] != 0xE3120004u ||
            p[0] != 0xE5832004u || (p[1] & 0xFF000000u) != 0x0A000000u ||
            p[2] != 0xE3A09001u || p[3] != 0xE3A08000u ||
            !PLUGIN_MENU_ReadR2Literal(p[-7], (u32)(p - 7), &firstValue) ||
            firstValue != 0xE3A00000u ||
            !PLUGIN_MENU_ReadR2Literal(p[-5], (u32)(p - 5), &configAddress) ||
            !configAddress ||
            !PLUGIN_MENU_ReadR2Literal(p[-1], (u32)(p - 1), &secondValue) ||
            secondValue != 0xE12FFF1Eu)
        {
            continue;
        }

        if (found)
            return NULL;

        // BEQ has the same signed imm24 encoding as B; its condition is checked above.
        s32 offset = (s32)(p[1] << 8) >> 6;
        *branchTarget = (u32)(p + 1) + 8u + (u32)offset;
        found = p;
    }
    return found;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InstallHomeHook(void)
{
    u32 marker = MENU_HOST__homeMarker;
    u32 address, words, branchTarget;
    volatile u32 *site;

    if (!marker || (marker & 3u))
        return false;

    words = (0x1000u - (marker & 0xFFFu)) / 4u;
    if (words > 0x40u)
        words = 0x40u;
    site = PLUGIN_MENU_FindHomePatch((volatile u32 *)marker, words, &branchTarget);
    if (!site)
        return false;

    address = (u32)site;
    g_MENUHomeAddress = address;
    g_MENUHomeOriginal[0] = site[0];
    g_MENUHomeOriginal[1] = site[1];
    g_MENUHomeReturn = address + 8u;
    g_MENUHomeBranchTarget = branchTarget;

    if (!PLUGIN_MENU_ReplaceHookPair(
            address,
            g_MENUHomeOriginal[0],
            g_MENUHomeOriginal[1],
            0xE51FF004u,
            (u32)PLUGIN_MENU_HomeHook))
    {
        g_MENUHomeAddress = 0;
        g_MENUHomeReturn = 0;
        g_MENUHomeBranchTarget = 0;
        return false;
    }
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_RestoreHomeHook(void)
{
    if (!g_MENUHomeAddress)
        return true;

    if (!PLUGIN_MENU_ReplaceHookPair(
            g_MENUHomeAddress,
            0xE51FF004u,
            (u32)PLUGIN_MENU_HomeHook,
            g_MENUHomeOriginal[0],
            g_MENUHomeOriginal[1]))
    {
        return false;
    }

    g_MENUHomeAddress = 0;
    g_MENUHomeReturn = 0;
    g_MENUHomeBranchTarget = 0;
    return true;
}

PLUGIN_CODE(MENU) static volatile u32 *PLUGIN_MENU_FindCreateCodeSetPatch(
    volatile u32 *anchor,
    u32 maxWords
)
{
    volatile u32 *found = NULL;

    for (u32 i = 0; i + 2u < maxWords; i++)
    {
        volatile u32 *p = anchor + i;
        u32 target;
        if (PLUGIN_MENU_DecodeBl(p[0], (u32)p, &target) &&
            target == MENU_HOST__svcCreateCodeSetAddress &&
            p[1] == 0xE3500000u &&
            (p[2] & 0xFF000000u) == 0xAA000000u)
        {
            if (found)
                return NULL;
            found = p;
        }
    }
    return found;
}

PLUGIN_CODE(MENU) static volatile u32 *PLUGIN_MENU_FindCreateProcessPatch(
    volatile u32 *anchor,
    u32 maxWords
)
{
    volatile u32 *found = NULL;

    for (u32 i = 0; i + 3u < maxWords; i++)
    {
        volatile u32 *p = anchor + i;
        u32 target;
        if (PLUGIN_MENU_DecodeBl(p[0], (u32)p, &target) &&
            target == MENU_HOST__svcCreateProcessAddress &&
            p[1] == 0xE1A05000u &&
            (p[2] & 0xFFFFF000u) == 0xE59D0000u &&
            (p[3] & 0xFF000000u) == 0xEB000000u)
        {
            if (found)
                return NULL;
            found = p;
        }
    }
    return found;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InstallCreateCodeSetHook(void)
{
    u32 marker = MENU_HOST__createCodeSetMarker;
    u32 words;
    volatile u32 *site;
    u32 address;

    if (!marker)
        return false;

    words = (0x1000u - (marker & 0xFFFu)) / 4u;
    if (words > 0x40u)
        words = 0x40u;
    site = PLUGIN_MENU_FindCreateCodeSetPatch((volatile u32 *)marker, words);
    if (!site)
        return false;

    address = (u32)site;
    g_MENUCreateCodeSetAddress = address;
    g_MENUCreateCodeSetOriginal[0] = site[0];
    g_MENUCreateCodeSetOriginal[1] = site[1];
    g_MENUCreateCodeSetReturn = address + 8u;

    if (!PLUGIN_MENU_ReplaceHookPair(
            address,
            g_MENUCreateCodeSetOriginal[0],
            g_MENUCreateCodeSetOriginal[1],
            0xE51FF004u,
            (u32)PLUGIN_MENU_CreateCodeSetHook))
    {
        g_MENUCreateCodeSetAddress = 0;
        g_MENUCreateCodeSetReturn = 0;
        return false;
    }
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_RestoreCreateCodeSetHook(void)
{
    if (!g_MENUCreateCodeSetAddress)
        return true;

    if (!PLUGIN_MENU_ReplaceHookPair(
            g_MENUCreateCodeSetAddress,
            0xE51FF004u,
            (u32)PLUGIN_MENU_CreateCodeSetHook,
            g_MENUCreateCodeSetOriginal[0],
            g_MENUCreateCodeSetOriginal[1]))
    {
        return false;
    }

    g_MENUCreateCodeSetAddress = 0;
    g_MENUCreateCodeSetReturn = 0;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InstallCreateProcessHook(void)
{
    u32 marker = MENU_HOST__createProcessMarker;
    u32 words;
    volatile u32 *site;
    u32 address;

    if (!marker)
        return false;

    words = (0x1000u - (marker & 0xFFFu)) / 4u;
    if (words > 0x40u)
        words = 0x40u;
    site = PLUGIN_MENU_FindCreateProcessPatch((volatile u32 *)marker, words);
    if (!site)
        return false;

    address = (u32)site;
    g_MENUCreateProcessAddress = address;
    g_MENUCreateProcessOriginal[0] = site[0];
    g_MENUCreateProcessOriginal[1] = site[1];
    g_MENUCreateProcessReturn = address + 8u;

    if (!PLUGIN_MENU_ReplaceHookPair(
            address,
            g_MENUCreateProcessOriginal[0],
            g_MENUCreateProcessOriginal[1],
            0xE51FF004u,
            (u32)PLUGIN_MENU_CreateProcessHook))
    {
        g_MENUCreateProcessAddress = 0;
        g_MENUCreateProcessReturn = 0;
        return false;
    }
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_RestoreCreateProcessHook(void)
{
    if (!g_MENUCreateProcessAddress)
        return true;

    if (!PLUGIN_MENU_ReplaceHookPair(
            g_MENUCreateProcessAddress,
            0xE51FF004u,
            (u32)PLUGIN_MENU_CreateProcessHook,
            g_MENUCreateProcessOriginal[0],
            g_MENUCreateProcessOriginal[1]))
    {
        return false;
    }

    g_MENUCreateProcessAddress = 0;
    g_MENUCreateProcessReturn = 0;
    return true;
}


PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InstallPlgldrInitHook(void)
{
    const u32 expectedNext = 0xE3500000u;
    u32 marker = MENU_HOST__plgldrInitMarker;
    u32 target = MENU_HOST__plgldrInitAddress;
    u32 first;
    u32 last;
    u32 address = 0;
    u32 originalCall = 0;
    u32 foundCount = 0;

    if (!marker || !target)
        return false;

    first = marker >= 0x10u ? marker - 0x10u : marker;
    last = marker + 0x30u;
    first &= ~3u;

    for (u32 current = first; current <= last; current += 4u)
    {
        u32 decoded = 0;
        u32 instruction = *(volatile u32 *)current;
        if (PLUGIN_MENU_DecodeBl(instruction, current, &decoded) && decoded == target &&
            *(volatile u32 *)(current + 4u) == expectedNext)
        {
            address = current;
            originalCall = instruction;
            foundCount++;
        }
    }

    if (foundCount != 1u || !PLUGIN_MENU_ReplaceHookPair(
            address,
            originalCall,
            expectedNext,
            0xE51FF004u,
            (u32)PLUGIN_MENU_PlgldrInitCallHook))
    {
        return false;
    }

    g_MENUPlgldrInitCallAddress = address;
    g_MENUPlgldrInitCallOriginal[0] = originalCall;
    g_MENUPlgldrInitCallOriginal[1] = expectedNext;
    g_MENUPlgldrInitCallReturn = address + 8u;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_RestorePlgldrInitHook(void)
{
    if (!g_MENUPlgldrInitCallAddress)
        return true;

    if (!PLUGIN_MENU_ReplaceHookPair(
            g_MENUPlgldrInitCallAddress,
            0xE51FF004u,
            (u32)PLUGIN_MENU_PlgldrInitCallHook,
            g_MENUPlgldrInitCallOriginal[0],
            g_MENUPlgldrInitCallOriginal[1]))
    {
        return false;
    }

    g_MENUPlgldrInitCallAddress = 0;
    g_MENUPlgldrInitCallReturn = 0;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InstallPlgldrCloseHook(void)
{
    u32 marker = MENU_HOST__plgldrExitMarker;
    u32 target = MENU_HOST__svcCloseHandleAddress;
    u32 first;
    u32 last;
    u32 address = 0;
    u32 originalCall = 0;
    u32 foundCount = 0;
    u32 branchTarget = 0;

    if (!marker || !target)
        return false;

    first = marker & ~3u;
    last = first + 0x60u;
    for (u32 scan = first; scan <= last; scan += 4u)
    {
        u32 decodedTarget = 0;
        u32 instruction = *(volatile u32 *)scan;
        if (PLUGIN_MENU_DecodeBl(instruction, scan, &decodedTarget) && decodedTarget == target)
        {
            address = scan;
            originalCall = instruction;
            foundCount++;
        }
    }

    if (foundCount != 1u || (address & 0xFFFu) > 0xFF8u ||
        !PLUGIN_MENU_DecodeB(*(volatile u32 *)(address + 4u), address + 4u, &branchTarget))
    {
        return false;
    }

    g_MENUPlgldrCloseAddress = address;
    g_MENUPlgldrCloseOriginal[0] = originalCall;
    g_MENUPlgldrCloseOriginal[1] = *(volatile u32 *)(address + 4u);
    g_MENUPlgldrCloseBranchTarget = branchTarget;

    if (!PLUGIN_MENU_ReplaceHookPair(
            address,
            g_MENUPlgldrCloseOriginal[0],
            g_MENUPlgldrCloseOriginal[1],
            0xE51FF004u,
            (u32)PLUGIN_MENU_PlgldrCloseHook))
    {
        g_MENUPlgldrCloseAddress = 0;
        g_MENUPlgldrCloseBranchTarget = 0;
        return false;
    }
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_RestorePlgldrCloseHook(void)
{
    if (!g_MENUPlgldrCloseAddress)
        return true;

    if (!PLUGIN_MENU_ReplaceHookPair(
            g_MENUPlgldrCloseAddress,
            0xE51FF004u,
            (u32)PLUGIN_MENU_PlgldrCloseHook,
            g_MENUPlgldrCloseOriginal[0],
            g_MENUPlgldrCloseOriginal[1]))
    {
        return false;
    }

    g_MENUPlgldrCloseAddress = 0;
    g_MENUPlgldrCloseBranchTarget = 0;
    return true;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_RollbackLoaderHooks(void)
{
    bool ok = true;

    if (!PLUGIN_MENU_RestorePlgldrCloseHook())
        ok = false;
    if (!PLUGIN_MENU_RestorePlgldrInitHook())
        ok = false;
    if (!PLUGIN_MENU_RestoreCreateProcessHook())
        ok = false;
    if (!PLUGIN_MENU_RestoreCreateCodeSetHook())
        ok = false;
    if (!PLUGIN_MENU_RestoreHomeHook())
        ok = false;
    if (!PLUGIN_MENU_RestorePatchCodeHook())
        ok = false;
    return ok;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_InstallLoaderHooks(void)
{
    g_MENUHooksReady = false;

    if (PLUGIN_MENU_InstallPatchCodeHook() &&
        PLUGIN_MENU_InstallHomeHook() &&
        PLUGIN_MENU_InstallCreateCodeSetHook() &&
        PLUGIN_MENU_InstallCreateProcessHook() &&
        PLUGIN_MENU_InstallPlgldrInitHook() &&
        PLUGIN_MENU_InstallPlgldrCloseHook())
    {
        g_MENUHooksReady = true;
        return true;
    }

    // If rollback fails, stay resident so no host branch can point at unloaded code.
    return !PLUGIN_MENU_RollbackLoaderHooks();
}
