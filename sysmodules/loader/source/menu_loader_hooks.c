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
#define MENU_HOST__patchCodeMarker         ((u32)pluginTable_MENU[2])
#define MENU_HOST__homeMarker              ((u32)pluginTable_MENU[3])
#define MENU_HOST__createCodeSetMarker     ((u32)pluginTable_MENU[4])
#define MENU_HOST__createProcessMarker     ((u32)pluginTable_MENU[5])
#define MENU_HOST__plgldrExitMarker        ((u32)pluginTable_MENU[6])
#define MENU_HOST__plgldrInitAddress        ((u32)pluginTable_MENU[7])
#define MENU_HOST__plgldrInitMarker         ((u32)pluginTable_MENU[11])

extern void PLUGIN_MENU_PatchCodeCallHook(void);
extern void PLUGIN_MENU_HomeHook(void);
extern void PLUGIN_MENU_CreateCodeSetHook(void);
extern void PLUGIN_MENU_CreateProcessHook(void);
extern void PLUGIN_MENU_PlgldrCloseHook(void);
extern void PLUGIN_MENU_PlgldrInitCallHook(void);
extern void PLUGIN_MENU_svcFlushEntireDataCache(void);
extern void PLUGIN_MENU_svcInvalidateEntireInstructionCache(void);

PLUGIN_DATA(MENU) u32 g_MENUPatchCodeCallReturn;
PLUGIN_DATA(MENU) u32 g_MENUHomeReturn;
PLUGIN_DATA(MENU) u32 g_MENUCreateCodeSetReturn;
PLUGIN_DATA(MENU) u32 g_MENUCreateProcessReturn;
PLUGIN_DATA(MENU) u32 g_MENUPlgldrCloseBranchTarget;
PLUGIN_DATA(MENU) u32 g_MENUPlgldrInitCallReturn;

PLUGIN_BSS(MENU) bool g_MENUHooksReady;
PLUGIN_BSS(MENU) static u32 g_MENUPatchCodeCallAddress;
PLUGIN_BSS(MENU) static u32 g_MENUPatchCodeCallOriginal[2];
PLUGIN_BSS(MENU) static u32 g_MENUAltContinuationMovAddress;
PLUGIN_BSS(MENU) static u32 g_MENUAltContinuationBranchAddress;
PLUGIN_BSS(MENU) static u32 g_MENUAltContinuationOriginalBranch;
PLUGIN_BSS(MENU) static u32 g_MENUAltContinuationPatchedBranch;
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

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_EncodeB(
    u32 address,
    u32 target,
    u32 *instruction
)
{
    s32 delta = (s32)(target - (address + 8u));

    if (!instruction || (delta & 3) != 0 ||
        delta < -0x02000000 || delta > 0x01FFFFFC)
    {
        return false;
    }

    *instruction = 0xEA000000u | (((u32)delta >> 2) & 0x00FFFFFFu);
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

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_FindPatchCodeCall(
    u32 *outAddress,
    u32 *outInstruction
)
{
    u32 marker = MENU_HOST__patchCodeMarker;
    u32 target = MENU_HOST__patchCodeAddress;
    u32 first;
    u32 last;
    u32 foundAddress = 0;
    u32 foundInstruction = 0;
    u32 foundCount = 0;

    if (!marker || !target)
        return false;

    first = marker >= 0x10u ? marker - 0x10u : marker;
    last = marker + 0x50u;
    first &= ~3u;

    for (u32 address = first; address <= last; address += 4u)
    {
        u32 decodedTarget = 0;
        u32 instruction = *(volatile u32 *)address;
        if (PLUGIN_MENU_DecodeBl(instruction, address, &decodedTarget) &&
            decodedTarget == target)
        {
            foundAddress = address;
            foundInstruction = instruction;
            foundCount++;
        }
    }

    if (foundCount != 1u)
        return false;

    *outAddress = foundAddress;
    *outInstruction = foundInstruction;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_FindAlternateContinuation(
    u32 patchCallAddress,
    u32 *movAddressOut,
    u32 *branchAddressOut,
    u32 *branchInstructionOut
)
{
    u32 target;
    u32 page;
    u32 foundMov = 0;
    u32 foundBranch = 0;
    u32 foundInstruction = 0;
    u32 foundCount = 0;

    if (!patchCallAddress || (patchCallAddress & 3u))
        return false;

    target = patchCallAddress + 4u;
    page = patchCallAddress & ~0xFFFu;

    for (u32 address = page + 12u; address <= page + 0xFFCu; address += 4u)
    {
        u32 decodedTarget = 0;
        u32 instruction = *(volatile u32 *)address;
        u32 movAddress;

        if (!PLUGIN_MENU_DecodeB(instruction, address, &decodedTarget) ||
            decodedTarget != target)
        {
            continue;
        }

        movAddress = address - 12u;
        if (*(volatile u32 *)movAddress != 0xE3A01000u ||
            (*(volatile u32 *)(movAddress + 4u) & 0xFFFFF000u) != 0xE51F2000u ||
            *(volatile u32 *)(movAddress + 8u) != 0xE1C200F0u)
        {
            continue;
        }

        foundMov = movAddress;
        foundBranch = address;
        foundInstruction = instruction;
        foundCount++;
    }

    if (foundCount != 1u)
        return false;

    *movAddressOut = foundMov;
    *branchAddressOut = foundBranch;
    *branchInstructionOut = foundInstruction;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReplaceAlternateContinuation(
    u32 movAddress,
    u32 branchAddress,
    u32 expectedMov,
    u32 expectedBranch,
    u32 newMov,
    u32 newBranch
)
{
    u32 mapBase = 0;
    u32 mapped = 0;
    volatile u32 *movSite;
    volatile u32 *branchSite;
    bool ok;

    if (!movAddress || !branchAddress || (movAddress & 3u) || (branchAddress & 3u) ||
        (movAddress & ~0xFFFu) != (branchAddress & ~0xFFFu) ||
        !PLUGIN_MENU_MapPage(CUR_PROCESS_HANDLE, movAddress, &mapBase, &mapped))
    {
        return false;
    }

    movSite = (volatile u32 *)mapped;
    branchSite = (volatile u32 *)(mapped + branchAddress - movAddress);
    ok = *movSite == expectedMov && *branchSite == expectedBranch;
    if (ok)
    {
        *movSite = newMov;
        *branchSite = newBranch;
        PLUGIN_MENU_svcFlushEntireDataCache();
        PLUGIN_MENU_svcInvalidateEntireInstructionCache();

        ok = *movSite == newMov && *branchSite == newBranch;
        if (!ok)
        {
            *movSite = expectedMov;
            *branchSite = expectedBranch;
            PLUGIN_MENU_svcFlushEntireDataCache();
            PLUGIN_MENU_svcInvalidateEntireInstructionCache();
        }
    }

    PLUGIN_MENU_UnmapPage(mapBase);
    return ok;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InstallPatchCodeHook(void)
{
    const u32 expectedNext = 0xE3A01201u;
    u32 address = 0;
    u32 originalCall = 0;
    u32 altMovAddress = 0;
    u32 altBranchAddress = 0;
    u32 altOriginalBranch = 0;
    u32 altPatchedBranch = 0;

    if (!PLUGIN_MENU_FindPatchCodeCall(&address, &originalCall) ||
        (address & 0xFFFu) > 0xFF8u ||
        *(volatile u32 *)(address + 4u) != expectedNext ||
        !PLUGIN_MENU_FindAlternateContinuation(
            address,
            &altMovAddress,
            &altBranchAddress,
            &altOriginalBranch) ||
        !PLUGIN_MENU_EncodeB(altBranchAddress, address + 8u, &altPatchedBranch))
    {
        return false;
    }

    if (!PLUGIN_MENU_ReplaceAlternateContinuation(
            altMovAddress,
            altBranchAddress,
            0xE3A01000u,
            altOriginalBranch,
            expectedNext,
            altPatchedBranch))
    {
        return false;
    }

    if (!PLUGIN_MENU_ReplaceHookPair(
            address,
            originalCall,
            expectedNext,
            0xE51FF004u,
            (u32)PLUGIN_MENU_PatchCodeCallHook))
    {
        (void)PLUGIN_MENU_ReplaceAlternateContinuation(
            altMovAddress,
            altBranchAddress,
            expectedNext,
            altPatchedBranch,
            0xE3A01000u,
            altOriginalBranch
        );
        return false;
    }

    g_MENUPatchCodeCallAddress = address;
    g_MENUPatchCodeCallOriginal[0] = originalCall;
    g_MENUPatchCodeCallOriginal[1] = expectedNext;
    g_MENUPatchCodeCallReturn = address + 8u;
    g_MENUAltContinuationMovAddress = altMovAddress;
    g_MENUAltContinuationBranchAddress = altBranchAddress;
    g_MENUAltContinuationOriginalBranch = altOriginalBranch;
    g_MENUAltContinuationPatchedBranch = altPatchedBranch;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_RestorePatchCodeHook(void)
{
    bool pairOk = true;
    bool altOk = true;

    if (g_MENUPatchCodeCallAddress)
    {
        pairOk = PLUGIN_MENU_ReplaceHookPair(
            g_MENUPatchCodeCallAddress,
            0xE51FF004u,
            (u32)PLUGIN_MENU_PatchCodeCallHook,
            g_MENUPatchCodeCallOriginal[0],
            g_MENUPatchCodeCallOriginal[1]
        );
    }

    if (g_MENUAltContinuationMovAddress && g_MENUAltContinuationBranchAddress)
    {
        altOk = PLUGIN_MENU_ReplaceAlternateContinuation(
            g_MENUAltContinuationMovAddress,
            g_MENUAltContinuationBranchAddress,
            0xE3A01201u,
            g_MENUAltContinuationPatchedBranch,
            0xE3A01000u,
            g_MENUAltContinuationOriginalBranch
        );
    }

    if (pairOk && altOk)
    {
        g_MENUPatchCodeCallAddress = 0;
        g_MENUAltContinuationMovAddress = 0;
        g_MENUAltContinuationBranchAddress = 0;
        return true;
    }
    return false;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InstallHomeHook(void)
{
    u32 address = MENU_HOST__homeMarker;
    volatile u32 *site;

    if (!address || (address & 3u) || (address & 0xFFFu) > 0xFD4u)
        return false;

    site = (volatile u32 *)address;
    if ((site[0] & 0xFFFFF000u) != 0xE59F2000u || site[1] != 0xE7872003u)
        return false;

    g_MENUHomeAddress = address;
    g_MENUHomeOriginal[0] = site[0];
    g_MENUHomeOriginal[1] = site[1];
    g_MENUHomeReturn = address + 0x2Cu;

    if (!PLUGIN_MENU_ReplaceHookPair(
            address,
            g_MENUHomeOriginal[0],
            g_MENUHomeOriginal[1],
            0xE51FF004u,
            (u32)PLUGIN_MENU_HomeHook))
    {
        g_MENUHomeAddress = 0;
        g_MENUHomeReturn = 0;
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
        if ((p[0] & 0xFF000000u) == 0xEB000000u &&
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
        if ((p[0] & 0xFF000000u) == 0xEB000000u &&
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
