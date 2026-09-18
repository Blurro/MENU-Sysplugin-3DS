#include <3ds.h>
#include "sysplugin_menu.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id) __attribute__((section(".plugin_" #id), used))

#define MENU_SCRATCH_LOW  0x10000000u
#define MENU_SCRATCH_HIGH 0x1F000000u

extern Result PLUGIN_MENU_svcQueryMemory(MemInfo *memInfo, PageInfo *pageInfo, u32 address);
extern Result PLUGIN_MENU_svcMapProcessMemoryEx(
    Handle dstProcess,
    u32 dstAddress,
    Handle srcProcess,
    u32 srcAddress,
    u32 size,
    u32 flags
);
extern Result PLUGIN_MENU_svcUnmapProcessMemoryEx(Handle process, u32 address, u32 size);
extern Result PLUGIN_MENU_svcControlMemoryUnsafe(
    u32 *out,
    u32 address,
    u32 size,
    MemOp op,
    MemPerm perm
);

PLUGIN_CODE(MENU) bool PLUGIN_MENU_FindFreeRange(u32 size, u32 *outBase)
{
    MemInfo memInfo;
    PageInfo pageInfo;
    u32 scan = MENU_SCRATCH_LOW;

    if (!outBase || !size || (size & 0xFFFu))
        return false;

    while (scan < MENU_SCRATCH_HIGH)
    {
        u32 end;
        if (R_FAILED(PLUGIN_MENU_svcQueryMemory(&memInfo, &pageInfo, scan)))
            return false;

        end = memInfo.base_addr + memInfo.size;
        if (end <= scan)
            return false;

        if (memInfo.state == MEMSTATE_FREE)
        {
            u32 base = (memInfo.base_addr + 0xFFFu) & ~0xFFFu;
            if (base < MENU_SCRATCH_LOW)
                base = MENU_SCRATCH_LOW;

            if (base < end && size <= end - base && size <= MENU_SCRATCH_HIGH - base)
            {
                *outBase = base;
                return true;
            }
        }

        scan = end;
    }
    return false;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_TempAlloc(u32 size, u32 *outBase)
{
    u32 base;
    u32 allocated = 0;
    Result result;

    if (!outBase || !size || (size & 0xFFFu) || !PLUGIN_MENU_FindFreeRange(size, &base))
        return false;

    result = PLUGIN_MENU_svcControlMemoryUnsafe(
        &allocated,
        base,
        size,
        MEMOP_ALLOC | MEMOP_REGION_SYSTEM,
        MEMPERM_READWRITE
    );
    if (R_FAILED(result) || allocated != base)
        return false;

    *outBase = allocated;
    return true;
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_TempFree(u32 base, u32 size)
{
    u32 out;

    if (!base || !size || (size & 0xFFFu))
        return;

    (void)PLUGIN_MENU_svcControlMemoryUnsafe(
        &out,
        base,
        size,
        MEMOP_FREE | MEMOP_REGION_SYSTEM,
        MEMPERM_DONTCARE
    );
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_AllocAliasGuard(u32 address)
{
    u32 allocated = 0;
    Result result = PLUGIN_MENU_svcControlMemoryUnsafe(
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
    (void)PLUGIN_MENU_svcControlMemoryUnsafe(
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

    if (!mappedBase || !mappedAddress || !PLUGIN_MENU_FindFreeRange(0x3000u, &guardBase))
        return false;

    aliasBase = guardBase + 0x1000u;
    if (!PLUGIN_MENU_AllocAliasGuard(guardBase))
        return false;

    if (!PLUGIN_MENU_AllocAliasGuard(aliasBase + 0x1000u))
    {
        PLUGIN_MENU_FreeAliasGuard(guardBase);
        return false;
    }

    if (R_FAILED(PLUGIN_MENU_svcMapProcessMemoryEx(
            CUR_PROCESS_HANDLE,
            aliasBase,
            sourceProcess,
            page,
            0x1000u,
            0u)))
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

    if (R_SUCCEEDED(PLUGIN_MENU_svcUnmapProcessMemoryEx(
            CUR_PROCESS_HANDLE,
            mappedBase,
            0x1000u)))
    {
        PLUGIN_MENU_FreeAliasGuard(mappedBase - 0x1000u);
        PLUGIN_MENU_FreeAliasGuard(mappedBase + 0x1000u);
    }
}
