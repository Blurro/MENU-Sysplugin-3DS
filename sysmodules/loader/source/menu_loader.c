#include <3ds.h>
#include "sysplugin_menu.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

extern void patchCode(
    u64 progId,
    u16 progVer,
    u8 *code,
    u32 size,
    u32 textSize,
    u32 roSize,
    u32 dataSize,
    u32 roAddress,
    u32 dataAddress
);
extern Result svcCloseHandle(Handle handle);
extern Result plgldrInit(void);
extern void svcSleepThread(s64 ns);
extern Result svcSendSyncRequest(Handle handle);
extern u32 menu_loader_patch_code_call;
extern u32 menu_loader_home_patch;
extern u32 menu_loader_createcodeset_call;
extern u32 menu_loader_createprocess_call;
extern u32 menu_loader_plgldr_exit_call;
extern u32 menu_loader_plgldr_init_call;

PLUGIN_DATA(MENU) void *pluginTable_MENU[] = {
    (void *)patchCode,
    (void *)svcCloseHandle,
    (void *)&menu_loader_patch_code_call,
    (void *)&menu_loader_home_patch,
    (void *)&menu_loader_createcodeset_call,
    (void *)&menu_loader_createprocess_call,
    (void *)&menu_loader_plgldr_exit_call,
    (void *)plgldrInit,
    (void *)svcSleepThread,
    (void *)svcConnectToPort,
    (void *)svcSendSyncRequest,
    (void *)&menu_loader_plgldr_init_call,
};

#define MENU_HOST__patchCode \
    ((void(*)(u64,u16,u8*,u32,u32,u32,u32,u32,u32))pluginTable_MENU[0])
#define MENU_HOST__svcCloseHandle \
    ((Result(*)(Handle))pluginTable_MENU[1])

PLUGIN_BSS(MENU) static PluginMenuLoaderTitlePatch *g_MENUFirstTitlePatch;
PLUGIN_BSS(MENU) static PluginMenuLoaderTitlePatch *g_MENULastTitlePatch;
PLUGIN_BSS(MENU) static PluginMenuLoaderHomePatch *g_MENUFirstHomePatch;
PLUGIN_BSS(MENU) static PluginMenuLoaderHomePatch *g_MENULastHomePatch;
PLUGIN_BSS(MENU) static PluginMenuLoaderContext g_MENUContext;
PLUGIN_BSS(MENU) static u32 g_MENULaunchEpoch;
PLUGIN_BSS(MENU) static bool g_MENUContextValid;

extern bool PLUGIN_MENU_InstallLoaderHooks(void);
extern bool g_MENUHooksReady;

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_TitleMatches(
    const PluginMenuLoaderTitlePatch *registration,
    u64 titleId
)
{
    if (!registration || !registration->titleIds || !registration->titleIdCount)
        return false;

    for (u32 i = 0; i < registration->titleIdCount; i++)
    {
        if (registration->titleIds[i] == titleId)
            return true;
    }
    return false;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_RegisterTitlePatch(
    PluginMenuLoaderTitlePatch *registration
)
{
    PluginMenuLoaderTitlePatch *current;

    if (!g_MENUHooksReady || !registration || !registration->titleIds || !registration->titleIdCount ||
        (!registration->prepare && !registration->processCreated && !registration->loaderFinished))
    {
        return false;
    }

    for (current = g_MENUFirstTitlePatch; current; current = current->next)
    {
        if (current == registration)
            return false;
    }

    registration->next = NULL;
    registration->menuEpoch = 0;
    if (g_MENULastTitlePatch)
        g_MENULastTitlePatch->next = registration;
    else
        g_MENUFirstTitlePatch = registration;
    g_MENULastTitlePatch = registration;
    return true;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_UnregisterTitlePatch(
    PluginMenuLoaderTitlePatch *registration
)
{
    PluginMenuLoaderTitlePatch *previous = NULL;
    PluginMenuLoaderTitlePatch *current = g_MENUFirstTitlePatch;

    while (current)
    {
        if (current == registration)
        {
            if (previous)
                previous->next = current->next;
            else
                g_MENUFirstTitlePatch = current->next;

            if (g_MENULastTitlePatch == current)
                g_MENULastTitlePatch = previous;

            current->next = NULL;
            current->menuEpoch = 0;
            return true;
        }
        previous = current;
        current = current->next;
    }
    return false;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_RegisterHomePatch(
    PluginMenuLoaderHomePatch *registration
)
{
    PluginMenuLoaderHomePatch *current;

    if (!g_MENUHooksReady || !registration || !registration->prepare)
        return false;

    for (current = g_MENUFirstHomePatch; current; current = current->next)
    {
        if (current == registration)
            return false;
    }

    registration->next = NULL;
    if (g_MENULastHomePatch)
        g_MENULastHomePatch->next = registration;
    else
        g_MENUFirstHomePatch = registration;
    g_MENULastHomePatch = registration;
    return true;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_UnregisterHomePatch(
    PluginMenuLoaderHomePatch *registration
)
{
    PluginMenuLoaderHomePatch *previous = NULL;
    PluginMenuLoaderHomePatch *current = g_MENUFirstHomePatch;

    while (current)
    {
        if (current == registration)
        {
            if (previous)
                previous->next = current->next;
            else
                g_MENUFirstHomePatch = current->next;

            if (g_MENULastHomePatch == current)
                g_MENULastHomePatch = previous;

            current->next = NULL;
            return true;
        }
        previous = current;
        current = current->next;
    }
    return false;
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_PatchCodeCallHandler(
    u64 progId,
    u16 progVer,
    u8 *code,
    u32 size,
    u32 textSize,
    u32 roSize,
    u32 dataSize,
    u32 roAddress,
    u32 dataAddress
)
{
    g_MENULaunchEpoch++;
    if (!g_MENULaunchEpoch)
        g_MENULaunchEpoch = 1;

    g_MENUContext.titleId = progId;
    g_MENUContext.titleVersion = progVer;
    g_MENUContext.reserved = 0;
    g_MENUContext.code = code;
    g_MENUContext.codeSize = size;
    g_MENUContext.textSize = textSize;
    g_MENUContext.roSize = roSize;
    g_MENUContext.dataSize = dataSize;
    g_MENUContext.roAddress = roAddress;
    g_MENUContext.dataAddress = dataAddress;
    g_MENUContext.codeSet = NULL;
    g_MENUContext.process = 0;
    g_MENUContextValid = true;

    MENU_HOST__patchCode(
        progId,
        progVer,
        code,
        size,
        textSize,
        roSize,
        dataSize,
        roAddress,
        dataAddress
    );
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_DispatchHome(void)
{
    PluginMenuLoaderHomePatch *registration;

    if (!g_MENUContextValid)
        return;

    for (registration = g_MENUFirstHomePatch; registration; registration = registration->next)
        (void)registration->prepare(&g_MENUContext);
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_PreCreateCodeSet(CodeSetHeader *header)
{
    PluginMenuLoaderTitlePatch *registration;

    if (!g_MENUContextValid || !header || header->program_id != g_MENUContext.titleId)
        return;

    g_MENUContext.codeSet = header;
    g_MENUContext.process = 0;

    for (registration = g_MENUFirstTitlePatch; registration; registration = registration->next)
    {
        bool active;

        if (!PLUGIN_MENU_TitleMatches(registration, g_MENUContext.titleId))
        {
            registration->menuEpoch = 0;
            continue;
        }

        active = !registration->prepare || registration->prepare(&g_MENUContext);
        registration->menuEpoch = active ? g_MENULaunchEpoch : 0;
    }
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_PostCreateProcess(
    Result createResult,
    Handle *outProcessHandle
)
{
    PluginMenuLoaderTitlePatch *registration;

    if (!g_MENUContextValid)
        return;

    g_MENUContext.codeSet = NULL;
    if (R_FAILED(createResult) || !outProcessHandle || !*outProcessHandle)
    {
        g_MENUContext.process = 0;
        return;
    }

    g_MENUContext.process = *outProcessHandle;
    for (registration = g_MENUFirstTitlePatch; registration; registration = registration->next)
    {
        if (registration->menuEpoch == g_MENULaunchEpoch && registration->processCreated)
            registration->processCreated(&g_MENUContext);
    }
}

extern void PLUGIN_MENU_BridgeStockClosed(void);
extern bool PLUGIN_MENU_StartBridgeWorker(void);
extern bool PLUGIN_MENU_RollbackLoaderHooks(void);

PLUGIN_CODE(MENU) Result PLUGIN_MENU_PlgldrCloseHandler(Handle handle)
{
    PluginMenuLoaderTitlePatch *registration;
    Result closeResult = MENU_HOST__svcCloseHandle(handle);

    if (g_MENUContextValid)
    {
        for (registration = g_MENUFirstTitlePatch; registration; registration = registration->next)
        {
            if (registration->menuEpoch == g_MENULaunchEpoch && registration->loaderFinished)
                registration->loaderFinished(&g_MENUContext);
        }

        g_MENUContextValid = false;
        g_MENUContext.codeSet = NULL;
        g_MENUContext.process = 0;
    }

    PLUGIN_MENU_BridgeStockClosed();
    return closeResult;
}

PLUGIN_MAIN(MENU) bool PLUGIN_MENU_Main(void)
{
    bool installResult = PLUGIN_MENU_InstallLoaderHooks();
    if (!installResult)
        return false;

    // A failed rollback intentionally leaves us resident with hooks disabled.
    if (!g_MENUHooksReady)
        return true;

    if (!PLUGIN_MENU_StartBridgeWorker())
    {
        g_MENUHooksReady = false;
        // If rollback itself fails, stay resident so no host branch points at unloaded code.
        return !PLUGIN_MENU_RollbackLoaderHooks();
    }

    return true;
}
