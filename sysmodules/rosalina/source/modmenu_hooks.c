#include <3ds.h>
#include "memory.h"
#include "draw.h"

#define PLUGIN_CODE(id) __attribute__((section(".plugin_" #id), used))
#define PLUGIN_BSS(id)  __attribute__((section(".pluginbss_" #id), used))

#define MENU_SCRATCH_LOW  0x10000000u
#define MENU_SCRATCH_HIGH 0x14000000u
#define MENU_ENTRY_COLOR  RGB565(9, 34, 31)

extern void *pluginTable_MENU[];
extern const char g_MENUEntryTitle[];
extern const char g_MENUUnreadText[];
extern bool g_MENUUnread;
extern bool g_MENUInsideMenu;

#define MENU_HOST__Draw_DrawString \
    ((u32(*)(u32,u32,u32,const char*))pluginTable_MENU[15])
#define MENU_HOST__svcMapProcessMemoryEx \
    ((Result(*)(Handle,u32,Handle,u32,u32,MapExFlags))pluginTable_MENU[22])
#define MENU_HOST__svcUnmapProcessMemoryEx \
    ((Result(*)(Handle,u32,u32))pluginTable_MENU[23])
#define MENU_HOST__svcQueryMemory \
    ((Result(*)(MemInfo*,PageInfo*,u32))pluginTable_MENU[24])
#define MENU_HOST__svcFlushEntireDataCache \
    ((void(*)(void))pluginTable_MENU[25])
#define MENU_HOST__svcInvalidateEntireInstructionCache \
    ((void(*)(void))pluginTable_MENU[26])
#define MENU_HOST__svcControlMemoryUnsafe \
    ((Result(*)(u32*,u32,u32,MemOp,MemPerm))pluginTable_MENU[29])

PLUGIN_BSS(MENU) static u32 g_MENUDrawStringContinue;

extern bool PLUGIN_MENU_FindFreeRange(u32 size, u32 *outBase);

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_AllocAliasGuard(u32 address)
{
    u32 allocated = 0;
    Result result = MENU_HOST__svcControlMemoryUnsafe(
        &allocated,
        address,
        0x1000u,
        MEMOP_ALLOC | MEMOP_REGION_SYSTEM,
        MEMPERM_READWRITE
    );
    return R_SUCCEEDED(result) && allocated == address;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_FreeAliasGuard(u32 address)
{
    u32 out;
    (void)MENU_HOST__svcControlMemoryUnsafe(
        &out,
        address,
        0x1000u,
        MEMOP_FREE | MEMOP_REGION_SYSTEM,
        MEMPERM_DONTCARE
    );
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_MapPage(
    Handle sourceProcess,
    u32 sourceAddress,
    u32 *mappedBase,
    u32 *mappedAddress
)
{
    u32 guardBase;
    u32 aliasBase;
    u32 page = sourceAddress & ~0xFFFu;

    if (!mappedBase || !mappedAddress || !
        PLUGIN_MENU_FindFreeRange(0x3000u, &guardBase))
    {
        return false;
    }

    aliasBase = guardBase + 0x1000u;
    if (!PLUGIN_MENU_AllocAliasGuard(guardBase))
        return false;

    if (!PLUGIN_MENU_AllocAliasGuard(aliasBase + 0x1000u))
    {
        PLUGIN_MENU_FreeAliasGuard(guardBase);
        return false;
    }

    if (R_FAILED(MENU_HOST__svcMapProcessMemoryEx(
            CUR_PROCESS_HANDLE,
            aliasBase,
            sourceProcess,
            page,
            0x1000u,
            (MapExFlags)0)))
    {
        PLUGIN_MENU_FreeAliasGuard(aliasBase + 0x1000u);
        PLUGIN_MENU_FreeAliasGuard(guardBase);
        return false;
    }

    *mappedBase = aliasBase;
    *mappedAddress = aliasBase + (sourceAddress & 0xFFFu);
    return true;
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_UnmapPage(u32 mappedBase)
{
    if (!mappedBase)
        return;

    if (R_SUCCEEDED(MENU_HOST__svcUnmapProcessMemoryEx(
            CUR_PROCESS_HANDLE,
            mappedBase,
            0x1000u)))
    {
        PLUGIN_MENU_FreeAliasGuard(mappedBase - 0x1000u);
        PLUGIN_MENU_FreeAliasGuard(mappedBase + 0x1000u);
    }
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_StringLength(const char *text)
{
    const volatile char *p = text;
    u32 length = 0;

    while (p[length])
        length++;

    return length;
}

PLUGIN_CODE(MENU) __attribute__((naked, noinline)) static u32 PLUGIN_MENU_OriginalDrawString(
    u32 posX __attribute__((unused)),
    u32 posY __attribute__((unused)),
    u32 color __attribute__((unused)),
    const char *text __attribute__((unused))
)
{
    __asm__ volatile(
        "push {r4, r5, r6, r7, r8, r9, r10, r11, lr}\n"
        "mov r9, r0\n"
        "ldr r12, 1f\n"
        "ldr r12, [r12]\n"
        "bx r12\n"
        "1:\n"
        ".word g_MENUDrawStringContinue\n"
    );
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_DrawStringHook(
    u32 posX,
    u32 posY,
    u32 color,
    const char *text
)
{
    bool isEntry = !g_MENUInsideMenu && text == g_MENUEntryTitle;

    if (isEntry && color == COLOR_WHITE)
        color = MENU_ENTRY_COLOR;

    u32 result = PLUGIN_MENU_OriginalDrawString(posX, posY, color, text);

    if (isEntry && g_MENUUnread)
    {
        PLUGIN_MENU_OriginalDrawString(
            posX + (PLUGIN_MENU_StringLength(text) + 1u) * SPACING_X,
            posY,
            COLOR_RED,
            g_MENUUnreadText
        );
    }

    return result;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_InstallDrawStringHook(void)
{
    u32 address = (u32)MENU_HOST__Draw_DrawString;
    u32 mappedBase = 0;
    u32 mappedAddress = 0;

    if ((address & 0xFFFu) > 0xFF8u ||
        !PLUGIN_MENU_MapPage(CUR_PROCESS_HANDLE, address, &mappedBase, &mappedAddress))
    {
        return false;
    }

    u32 instruction0 = *(volatile u32*)mappedAddress;
    u32 instruction1 = *(volatile u32*)(mappedAddress + 4u);

    if (instruction0 != 0xE92D4FF0u || instruction1 != 0xE1A09000u)
    {
        PLUGIN_MENU_UnmapPage(mappedBase);
        return false;
    }

    g_MENUDrawStringContinue = address + 8u;
    *(volatile u32*)(mappedAddress + 4u) = (u32)PLUGIN_MENU_DrawStringHook;
    *(volatile u32*)mappedAddress = 0xE51FF004u;
    PLUGIN_MENU_UnmapPage(mappedBase);

    MENU_HOST__svcFlushEntireDataCache();
    MENU_HOST__svcInvalidateEntireInstructionCache();
    return true;
}
