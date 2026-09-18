#include <3ds.h>
#include "sysplugin_menu.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id) __attribute__((section(".plugin_" #id), used))
#define PLUGIN_DATA(id) __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)  __attribute__((section(".pluginbss_" #id), used))

#define MENU_BRIDGE_IPC_COMMAND        0x4D42u
#define MENU_BRIDGE_IPC_ABI            1u
#define MENU_BRIDGE_STATUS_DELIVERED   0u
#define MENU_BRIDGE_STATUS_RETRY       1u
#define MENU_BRIDGE_STATUS_DROP        2u

extern void *pluginTable_MENU[];
#define MENU_HOST__svcSleepThread            ((void(*)(s64))pluginTable_MENU[0])
#define MENU_HOST__svcFlushEntireDataCache   ((void(*)(void))pluginTable_MENU[25])
#define MENU_HOST__svcInvalidateEntireInstructionCache ((void(*)(void))pluginTable_MENU[26])
#define MENU_HOST__PluginLoaderHandleCommands ((u32)pluginTable_MENU[42])

extern bool PLUGIN_MENU_MapPage(Handle sourceProcess, u32 sourceAddress, u32 *mappedBase, u32 *mappedAddress);
extern void PLUGIN_MENU_UnmapPage(u32 mappedBase);
extern void PLUGIN_MENU_BridgeHandlerTrampoline(void *ctx);

PLUGIN_BSS(MENU) static PluginMenuBridgeRegistration *g_MENUBridgeFirst;
PLUGIN_BSS(MENU) static PluginMenuBridgeRegistration *g_MENUBridgeLast;
PLUGIN_BSS(MENU) static volatile s32 g_MENUBridgeRegistryLock;
PLUGIN_BSS(MENU) static u32 g_MENUBridgeHookAddress;
PLUGIN_BSS(MENU) static u32 g_MENUBridgeHookOriginal[2];
PLUGIN_DATA(MENU) u32 g_MENUBridgeHandlerReturn;

PLUGIN_CODE(MENU) static u32 *PLUGIN_MENU_BridgeCommandBuffer(void)
{
    u8 *tls;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    return (u32 *)(tls + 0x80);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_BridgeLock(void)
{
    s32 *lock = (s32 *)&g_MENUBridgeRegistryLock;
    for (;;)
    {
        if (__ldrex(lock) == 0)
        {
            if (!__strex(lock, 1))
            {
                __dmb();
                return;
            }
        }
        else
        {
            __clrex();
        }
        MENU_HOST__svcSleepThread(1000);
    }
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_BridgeUnlock(void)
{
    __dmb();
    g_MENUBridgeRegistryLock = 0;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_RegisterBridgeReceiver(PluginMenuBridgeRegistration *registration)
{
    PluginMenuBridgeRegistration *current;

    if (!registration || !registration->pluginId || !registration->callback)
        return false;

    PLUGIN_MENU_BridgeLock();
    for (current = g_MENUBridgeFirst; current; current = current->next)
    {
        if (current == registration || current->pluginId == registration->pluginId)
        {
            PLUGIN_MENU_BridgeUnlock();
            return false;
        }
    }

    registration->next = NULL;
    if (g_MENUBridgeLast)
        g_MENUBridgeLast->next = registration;
    else
        g_MENUBridgeFirst = registration;
    g_MENUBridgeLast = registration;
    PLUGIN_MENU_BridgeUnlock();
    return true;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_UnregisterBridgeReceiver(PluginMenuBridgeRegistration *registration)
{
    PluginMenuBridgeRegistration *previous = NULL;
    PluginMenuBridgeRegistration *current;

    if (!registration)
        return false;

    PLUGIN_MENU_BridgeLock();
    current = g_MENUBridgeFirst;
    while (current)
    {
        if (current == registration)
        {
            if (previous)
                previous->next = current->next;
            else
                g_MENUBridgeFirst = current->next;
            if (g_MENUBridgeLast == current)
                g_MENUBridgeLast = previous;
            current->next = NULL;
            PLUGIN_MENU_BridgeUnlock();
            return true;
        }
        previous = current;
        current = current->next;
    }
    PLUGIN_MENU_BridgeUnlock();
    return false;
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_BridgeDispatch(
    u32 targetPluginId,
    u32 command,
    const void *payload,
    u32 payloadSize
)
{
    PluginMenuBridgeReceiverCallback callback = NULL;
    PluginMenuBridgeRegistration *current;

    PLUGIN_MENU_BridgeLock();
    for (current = g_MENUBridgeFirst; current; current = current->next)
    {
        if (current->pluginId == targetPluginId)
        {
            callback = current->callback;
            break;
        }
    }
    PLUGIN_MENU_BridgeUnlock();

    if (!callback)
        return MENU_BRIDGE_STATUS_RETRY;
    return callback(command, payload, payloadSize) ?
        MENU_BRIDGE_STATUS_DELIVERED : MENU_BRIDGE_STATUS_RETRY;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_HandleBridgeCommand(u32 *cmdbuf)
{
    u32 payloadSize;
    u32 payloadWords;
    u32 status = MENU_BRIDGE_STATUS_DROP;

    if (!cmdbuf || (cmdbuf[0] >> 16) != MENU_BRIDGE_IPC_COMMAND)
        return false;

    payloadSize = cmdbuf[4];
    payloadWords = (payloadSize + 3u) >> 2;
    if (cmdbuf[1] == MENU_BRIDGE_IPC_ABI && cmdbuf[2] != 0 &&
        payloadSize <= SYSPLUGIN_MENU_BRIDGE_MAX_PAYLOAD &&
        cmdbuf[0] == IPC_MakeHeader(MENU_BRIDGE_IPC_COMMAND, 4u + payloadWords, 0))
    {
        status = PLUGIN_MENU_BridgeDispatch(cmdbuf[2], cmdbuf[3], &cmdbuf[5], payloadSize);
    }

    cmdbuf[0] = IPC_MakeHeader(MENU_BRIDGE_IPC_COMMAND, 2, 0);
    cmdbuf[1] = 0;
    cmdbuf[2] = status;
    return true;
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_PluginLoaderCommandsWrapper(void *ctx)
{
    u32 *cmdbuf = PLUGIN_MENU_BridgeCommandBuffer();
    if (PLUGIN_MENU_HandleBridgeCommand(cmdbuf))
        return;
    PLUGIN_MENU_BridgeHandlerTrampoline(ctx);
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_InstallBridgeHook(void)
{
    const u32 expected0 = 0xE92D43F0u;
    const u32 expected1 = 0xEE1D4F70u;
    u32 address = MENU_HOST__PluginLoaderHandleCommands;
    u32 mappedBase = 0;
    u32 mapped = 0;

    if (!address || (address & 3u) || (address & 0xFFFu) > 0xFF8u ||
        !PLUGIN_MENU_MapPage(CUR_PROCESS_HANDLE, address, &mappedBase, &mapped))
    {
        return false;
    }

    if (*(volatile u32 *)mapped != expected0 || *(volatile u32 *)(mapped + 4u) != expected1)
    {
        PLUGIN_MENU_UnmapPage(mappedBase);
        return false;
    }

    g_MENUBridgeHookAddress = address;
    g_MENUBridgeHookOriginal[0] = expected0;
    g_MENUBridgeHookOriginal[1] = expected1;
    g_MENUBridgeHandlerReturn = address + 8u;
    *(volatile u32 *)mapped = 0xE51FF004u;
    *(volatile u32 *)(mapped + 4u) = (u32)PLUGIN_MENU_PluginLoaderCommandsWrapper;
    PLUGIN_MENU_UnmapPage(mappedBase);
    MENU_HOST__svcFlushEntireDataCache();
    MENU_HOST__svcInvalidateEntireInstructionCache();
    return true;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_RemoveBridgeHook(void)
{
    u32 mappedBase = 0;
    u32 mapped = 0;

    if (!g_MENUBridgeHookAddress)
        return true;
    if (!PLUGIN_MENU_MapPage(CUR_PROCESS_HANDLE, g_MENUBridgeHookAddress, &mappedBase, &mapped))
        return false;
    if (*(volatile u32 *)mapped != 0xE51FF004u ||
        *(volatile u32 *)(mapped + 4u) != (u32)PLUGIN_MENU_PluginLoaderCommandsWrapper)
    {
        PLUGIN_MENU_UnmapPage(mappedBase);
        return false;
    }

    *(volatile u32 *)mapped = g_MENUBridgeHookOriginal[0];
    *(volatile u32 *)(mapped + 4u) = g_MENUBridgeHookOriginal[1];
    PLUGIN_MENU_UnmapPage(mappedBase);
    MENU_HOST__svcFlushEntireDataCache();
    MENU_HOST__svcInvalidateEntireInstructionCache();
    g_MENUBridgeHookAddress = 0;
    g_MENUBridgeHandlerReturn = 0;
    return true;
}
