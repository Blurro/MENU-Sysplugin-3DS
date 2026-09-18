#include <3ds.h>
#include "sysplugin_menu.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define MENU_BRIDGE_IPC_COMMAND        0x4D42u
#define MENU_BRIDGE_IPC_ABI            1u
#define MENU_BRIDGE_STATUS_DELIVERED   0u
#define MENU_BRIDGE_STATUS_RETRY       1u
#define MENU_BRIDGE_STATUS_DROP        2u
#define MENU_BRIDGE_QUEUE_COUNT        16u
#define MENU_BRIDGE_RETRY_NS           (100LL * 1000LL * 1000LL)
#define MENU_BRIDGE_PORT_FULL          ((Result)0xD0401834u)
#define MENU_PLGLDR_RETRY_NS           (1000LL * 1000LL)
#define MENU_PLGLDR_RETRY_COUNT        200u

extern void *pluginTable_MENU[];
#define MENU_HOST__svcCloseHandle      ((Result(*)(Handle))pluginTable_MENU[1])
#define MENU_HOST__plgldrInit          ((Result(*)(void))pluginTable_MENU[7])
#define MENU_HOST__svcSleepThread      ((void(*)(s64))pluginTable_MENU[8])
#define MENU_HOST__svcConnectToPort    ((Result(*)(volatile Handle*,const char*))pluginTable_MENU[9])
#define MENU_HOST__svcSendSyncRequest  ((Result(*)(Handle))pluginTable_MENU[10])

extern Result PLUGIN_MENU_svcCreateThread(Handle *thread, void (*entrypoint)(void *), u32 arg, u32 *stackTop, s32 priority, s32 processorId);
extern Result PLUGIN_MENU_svcGetThreadPriority(s32 *priority, Handle thread);
extern Result PLUGIN_MENU_svcCreateEvent(Handle *event, ResetType resetType);
extern Result PLUGIN_MENU_svcSignalEvent(Handle event);
extern Result PLUGIN_MENU_svcWaitSynchronization(Handle handle, s64 timeout);

PLUGIN_RODATA(MENU) static const char g_MENUBridgePort[] = "plg:ldr";

typedef struct
{
    u32 targetPluginId;
    u32 command;
    u32 payloadSize;
    u8 payload[SYSPLUGIN_MENU_BRIDGE_MAX_PAYLOAD];
} MenuBridgeMessage;

PLUGIN_BSS(MENU) static MenuBridgeMessage g_MENUBridgeQueue[MENU_BRIDGE_QUEUE_COUNT];
PLUGIN_BSS(MENU) static volatile s32 g_MENUBridgeQueueLock;
PLUGIN_BSS(MENU) static u32 g_MENUBridgeHead;
PLUGIN_BSS(MENU) static u32 g_MENUBridgeTail;
PLUGIN_BSS(MENU) static u32 g_MENUBridgeCount;
PLUGIN_BSS(MENU) static Handle g_MENUBridgeEvent;
PLUGIN_BSS(MENU) static bool g_MENUBridgeReady;
PLUGIN_BSS(MENU) static volatile bool g_MENUStockPlgldrBusy;
PLUGIN_BSS(MENU) static u8 CTR_ALIGN(8) g_MENUBridgeStack[0x1000];

PLUGIN_CODE(MENU) static u32 *PLUGIN_MENU_BridgeCommandBuffer(void)
{
    u8 *tls;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    return (u32 *)(tls + 0x80);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_BridgeLock(void)
{
    s32 *lock = (s32 *)&g_MENUBridgeQueueLock;

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
    g_MENUBridgeQueueLock = 0;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_BridgeCopy(void *destination, const void *source, u32 size)
{
    volatile u8 *dst = (volatile u8 *)destination;
    const volatile u8 *src = (const volatile u8 *)source;
    for (u32 i = 0; i < size; i++)
        dst[i] = src[i];
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_BridgePeek(u32 *indexOut)
{
    bool available;
    PLUGIN_MENU_BridgeLock();
    available = g_MENUBridgeCount != 0;
    if (available && indexOut)
        *indexOut = g_MENUBridgeHead;
    PLUGIN_MENU_BridgeUnlock();
    return available;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_BridgePop(void)
{
    PLUGIN_MENU_BridgeLock();
    if (g_MENUBridgeCount)
    {
        g_MENUBridgeHead = (g_MENUBridgeHead + 1u) % MENU_BRIDGE_QUEUE_COUNT;
        g_MENUBridgeCount--;
    }
    PLUGIN_MENU_BridgeUnlock();
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_BridgeTrySend(const MenuBridgeMessage *message)
{
    Handle session = 0;
    Result result;
    u32 payloadWords;
    u32 *cmdbuf;

    if (!message || g_MENUStockPlgldrBusy)
        return MENU_BRIDGE_STATUS_RETRY;

    result = MENU_HOST__svcConnectToPort(&session, g_MENUBridgePort);
    if (R_FAILED(result))
        return MENU_BRIDGE_STATUS_RETRY;

    cmdbuf = PLUGIN_MENU_BridgeCommandBuffer();
    payloadWords = (message->payloadSize + 3u) >> 2;
    cmdbuf[0] = IPC_MakeHeader(MENU_BRIDGE_IPC_COMMAND, 4u + payloadWords, 0);
    cmdbuf[1] = MENU_BRIDGE_IPC_ABI;
    cmdbuf[2] = message->targetPluginId;
    cmdbuf[3] = message->command;
    cmdbuf[4] = message->payloadSize;

    volatile u32 *payloadDst = (volatile u32 *)&cmdbuf[5];
    for (u32 i = 0; i < payloadWords; i++)
        payloadDst[i] = 0;
    PLUGIN_MENU_BridgeCopy((void *)&cmdbuf[5], message->payload, message->payloadSize);

    result = MENU_HOST__svcSendSyncRequest(session);
    if (R_SUCCEEDED(result) &&
        cmdbuf[0] == IPC_MakeHeader(MENU_BRIDGE_IPC_COMMAND, 2, 0) &&
        (Result)cmdbuf[1] == 0)
    {
        u32 status = cmdbuf[2];
        MENU_HOST__svcCloseHandle(session);
        if (status == MENU_BRIDGE_STATUS_DELIVERED || status == MENU_BRIDGE_STATUS_DROP)
            return status;
        return MENU_BRIDGE_STATUS_RETRY;
    }

    MENU_HOST__svcCloseHandle(session);
    return MENU_BRIDGE_STATUS_RETRY;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_BridgeWorker(void *arg)
{
    (void)arg;

    for (;;)
    {
        u32 index = 0;
        if (!PLUGIN_MENU_BridgePeek(&index))
        {
            (void)PLUGIN_MENU_svcWaitSynchronization(g_MENUBridgeEvent, -1LL);
            continue;
        }

        if (g_MENUStockPlgldrBusy)
        {
            (void)PLUGIN_MENU_svcWaitSynchronization(g_MENUBridgeEvent, -1LL);
            continue;
        }

        u32 status = PLUGIN_MENU_BridgeTrySend(&g_MENUBridgeQueue[index]);
        if (status == MENU_BRIDGE_STATUS_DELIVERED || status == MENU_BRIDGE_STATUS_DROP)
        {
            PLUGIN_MENU_BridgePop();
            continue;
        }

        (void)PLUGIN_MENU_svcWaitSynchronization(g_MENUBridgeEvent, MENU_BRIDGE_RETRY_NS);
    }
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_BridgeSend(
    u32 targetPluginId,
    u32 command,
    const void *payload,
    u32 payloadSize
)
{
    MenuBridgeMessage *message;

    if (!g_MENUBridgeReady || !targetPluginId || payloadSize > SYSPLUGIN_MENU_BRIDGE_MAX_PAYLOAD ||
        (payloadSize && !payload))
    {
        return false;
    }

    PLUGIN_MENU_BridgeLock();
    if (g_MENUBridgeCount >= MENU_BRIDGE_QUEUE_COUNT)
    {
        PLUGIN_MENU_BridgeUnlock();
        return false;
    }

    message = &g_MENUBridgeQueue[g_MENUBridgeTail];
    message->targetPluginId = targetPluginId;
    message->command = command;
    message->payloadSize = payloadSize;
    if (payloadSize)
        PLUGIN_MENU_BridgeCopy(message->payload, payload, payloadSize);

    g_MENUBridgeTail = (g_MENUBridgeTail + 1u) % MENU_BRIDGE_QUEUE_COUNT;
    g_MENUBridgeCount++;
    PLUGIN_MENU_BridgeUnlock();

    (void)PLUGIN_MENU_svcSignalEvent(g_MENUBridgeEvent);
    return true;
}

PLUGIN_CODE(MENU) Result PLUGIN_MENU_PlgldrInitHandler(void)
{
    Result result = MENU_BRIDGE_PORT_FULL;

    g_MENUStockPlgldrBusy = true;
    for (u32 attempt = 0; attempt < MENU_PLGLDR_RETRY_COUNT; attempt++)
    {
        result = MENU_HOST__plgldrInit();
        if (result != MENU_BRIDGE_PORT_FULL)
            break;
        MENU_HOST__svcSleepThread(MENU_PLGLDR_RETRY_NS);
    }

    if (R_FAILED(result))
    {
        g_MENUStockPlgldrBusy = false;
        if (g_MENUBridgeEvent)
            (void)PLUGIN_MENU_svcSignalEvent(g_MENUBridgeEvent);
    }
    return result;
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_BridgeStockClosed(void)
{
    g_MENUStockPlgldrBusy = false;
    if (g_MENUBridgeEvent)
        (void)PLUGIN_MENU_svcSignalEvent(g_MENUBridgeEvent);
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_StartBridgeWorker(void)
{
    Handle thread = 0;
    s32 priority = 0x30;
    Result result;

    if (g_MENUBridgeReady)
        return true;

    result = PLUGIN_MENU_svcCreateEvent(&g_MENUBridgeEvent, RESET_ONESHOT);
    if (R_FAILED(result))
        return false;

    if (R_SUCCEEDED(PLUGIN_MENU_svcGetThreadPriority(&priority, CUR_THREAD_HANDLE)) && priority < 0x3F)
        priority++;

    result = PLUGIN_MENU_svcCreateThread(
        &thread,
        PLUGIN_MENU_BridgeWorker,
        0,
        (u32 *)(g_MENUBridgeStack + sizeof(g_MENUBridgeStack)),
        priority,
        -2
    );
    if (R_FAILED(result))
    {
        MENU_HOST__svcCloseHandle(g_MENUBridgeEvent);
        g_MENUBridgeEvent = 0;
        return false;
    }

    MENU_HOST__svcCloseHandle(thread);
    g_MENUBridgeReady = true;
    return true;
}
