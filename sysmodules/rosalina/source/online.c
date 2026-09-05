#include <3ds.h>
#include "draw.h"
#include "menu.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define MENU_ONLINE_API_VERSION 5u
#define MENU_ONLINE_BORDER_COLOR RGB8_to_565(65, 105, 225)
#define MENU_ONLINE_TITLE_COLOR COLOR_CYAN
#define MENU_ONLINE_DIR_NAME_CAP 256u
#define MENU_ONLINE_DIR_NAME_COMPLETE 1u

typedef struct
{
    char name[MENU_ONLINE_DIR_NAME_CAP];
    u64 fileSize;
    u32 attributes;
    u32 flags;
} MENUOnlineDirEntry;

typedef Result (*MENUOnlineDirVisitor)(
    const MENUOnlineDirEntry *entry,
    void *context,
    bool *stop
);

typedef struct
{
    u32 version;
    void (*drawLock)(void);
    void (*drawUnlock)(void);
    void (*drawClear)(void);
    u32 (*drawString)(u32, u32, u32, const char *);
    void (*drawFlush)(void);
    u32 (*waitInputWithTimeout)(s32);
    volatile bool *menuShouldExit;
    Result (*downloadToFile)(const char *url, const char *path, u32 maxSize);
    Result (*downloadToMemory)(const char *url, void *buffer, u32 bufferSize, u32 *actualSize);
    Result (*getFileSize)(const char *path, u32 *size);
    Result (*readFile)(const char *path, u32 offset, void *buffer, u32 size, u32 *actualRead);
    Result (*writeFile)(const char *path, u32 offset, const void *buffer, u32 size, u32 *actualWritten);
    Result (*setFileSize)(const char *path, u32 size);
    Result (*deleteFile)(const char *path);
    Result (*renameFile)(const char *oldPath, const char *newPath);
    Result (*fileExists)(const char *path, bool *exists);
    Result (*enumerateDirectory)(
        const char *path,
        MENUOnlineDirVisitor visitor,
        void *context,
        u32 *entriesVisited
    );
    const char *sourceUrlPrefix;
} MENUOnlineApi;

PLUGIN_RODATA(onln) static const char g_onlineTitle[] = "Online Menu";
PLUGIN_RODATA(onln) static const char g_onlineUtcItem[] = "UTC Fetch Menu";
PLUGIN_RODATA(onln) static const char g_onlineSyspluginsItem[] = "Check Sysplugins...";
PLUGIN_RODATA(onln) static const char g_onlineCursor[] = ">";
PLUGIN_RODATA(onln) static const char g_onlineUtcTitle[] = "UTC Fetch Menu";
PLUGIN_RODATA(onln) static const char g_onlinePrompt[] = "A: refresh live UTC";
PLUGIN_RODATA(onln) static const char g_onlineReady[] = "Press A to fetch current UTC";
PLUGIN_RODATA(onln) static const char g_onlineFetching[] = "Fetching live HTTPS time...";
PLUGIN_RODATA(onln) static const char g_onlineSuccess[] = "Live HTTPS response:";
PLUGIN_RODATA(onln) static const char g_onlineFailure[] = "HTTPS fetch failed";
PLUGIN_RODATA(onln) static const char g_onlineParseFailure[] = "Could not parse utc_iso";
PLUGIN_RODATA(onln) static const char g_onlineResultPrefix[] = "result: 0x";
PLUGIN_RODATA(onln) static const char g_onlineHexDigits[] = "0123456789ABCDEF";
PLUGIN_RODATA(onln) static const char g_onlineBack[] = "B: go back";
PLUGIN_RODATA(onln) static const char g_onlineTimeUrl[] = "https://utctime.app/api/now";
PLUGIN_RODATA(onln) static const char g_onlineUtcKey[] = "\"utc_iso\"";
PLUGIN_RODATA(onln) static const char g_onlinePlus[] = "+";
PLUGIN_RODATA(onln) static const char g_onlinePipe[] = "|";
PLUGIN_RODATA(onln) static const char g_onlineRail[] = "----------------------";

PLUGIN_RODATA(onln) static const char g_updateTitle[] = "Available Sysplugin Updates";
PLUGIN_RODATA(onln) static const char g_updateManifestName[] = "plgpacks.txt";
PLUGIN_RODATA(onln) static const char g_updatePluginsDir[] = "/luma/plugins";
PLUGIN_RODATA(onln) static const char g_updatePluginsPrefix[] = "/luma/plugins/";
PLUGIN_RODATA(onln) static const char g_updateManageTempListPath[] = "/luma/plugins/menutemplist.txt";
PLUGIN_RODATA(onln) static const char g_updateManifestPath[] = "/luma/plugins/.plgpacks.tmp";
PLUGIN_RODATA(onln) static const char g_updateDownloadPath[] = "/luma/plugins/.plgupdate.download";
PLUGIN_RODATA(onln) static const char g_updateRebuildPath[] = "/luma/plugins/.plgupdate.rebuild";
PLUGIN_RODATA(onln) static const char g_updateBackupPath[] = "/luma/plugins/.plgupdate.backup";
PLUGIN_RODATA(onln) static const char g_updateJournalPath[] = "/luma/plugins/.plgupdate.path";
PLUGIN_RODATA(onln) static const char g_updateScanning[] = "Scanning selected sysplugins...";
PLUGIN_RODATA(onln) static const char g_updateFetching[] = "Fetching plgpacks.txt...";
PLUGIN_RODATA(onln) static const char g_updateUpdating[] = "Installing:";
PLUGIN_RODATA(onln) static const char g_updateInstalled[] = "Update installed";
PLUGIN_RODATA(onln) static const char g_updateAllDone[] = "Install All complete";
PLUGIN_RODATA(onln) static const char g_updateScanFailed[] = "Plugin scan failed";
PLUGIN_RODATA(onln) static const char g_updateManifestFailed[] = "Manifest fetch/parse failed";
PLUGIN_RODATA(onln) static const char g_updateRecovering[] = "Checking interrupted update...";
PLUGIN_RODATA(onln) static const char g_updateRecoverFailed[] = "Recovery of old update failed";
PLUGIN_RODATA(onln) static const char g_updateUpdatedLabel[] = "Updated: ";
PLUGIN_RODATA(onln) static const char g_updateFailedLabel[] = "Failed: ";
PLUGIN_RODATA(onln) static const char g_updateRestart[] = "Reboot to load installed sysplugins";
PLUGIN_RODATA(onln) static const char g_updatePressBack[] = "B: go back";
PLUGIN_RODATA(onln) static const char g_updateShowAll[] = "Y: Show all";
PLUGIN_RODATA(onln) static const char g_updateShowUpdates[] = "Y: Show only updates";
PLUGIN_RODATA(onln) static const char g_updateSlash[] = "/";
PLUGIN_RODATA(onln) static const char g_updateNoUpdates[] = "No updates available";
PLUGIN_RODATA(onln) static const char g_updateAll[] = "Install All";
PLUGIN_RODATA(onln) static const char g_updateLoaderName[] = "Loader";
PLUGIN_RODATA(onln) static const char g_updateRosalinaName[] = "Rosalina";
PLUGIN_RODATA(onln) static const char g_updateOpenModule[] = " (";
PLUGIN_RODATA(onln) static const char g_updateOpenLoaderModule[] = "   (";
PLUGIN_RODATA(onln) static const char g_updateCloseModule[] = "): ";
PLUGIN_RODATA(onln) static const char g_updateCloseOnly[] = ")";
PLUGIN_RODATA(onln) static const char g_updateNullVersion[] = "null";
PLUGIN_RODATA(onln) static const char g_updateVersionPrefix[] = "v";
PLUGIN_RODATA(onln) static const char g_updateArrow[] = " -> ";
PLUGIN_RODATA(onln) static const char g_updateDots[] = "...";
PLUGIN_RODATA(onln) static const char g_updateClearRow[] =
    "                                                  ";
PLUGIN_RODATA(onln) static const char g_updateWideRail[] = "------------------------------";
PLUGIN_RODATA(onln) static const char g_stackTitle[] = "Find New Sysplugins";
PLUGIN_RODATA(onln) static const char g_stackManifestName[] = "plgpacks.txt";
PLUGIN_RODATA(onln) static const char g_stackManifestPath[] = "/luma/plugins/.plgpacks.tmp";
PLUGIN_RODATA(onln) static const char g_stackScanning[] = "Scanning selected sysplugins...";
PLUGIN_RODATA(onln) static const char g_stackFetching[] = "Fetching plgpacks.txt...";
PLUGIN_RODATA(onln) static const char g_stackManifestFailed[] = "Pack manifest fetch/parse failed";
PLUGIN_RODATA(onln) static const char g_stackNoNew[] = "No new sysplugins available";
PLUGIN_RODATA(onln) static const char g_stackContains[] = "Contains:";
PLUGIN_RODATA(onln) static const char g_stackDownload[] = "A: Download";
PLUGIN_RODATA(onln) static const char g_stackCapacityTitle[] = "Not enough plugin slots";
PLUGIN_RODATA(onln) static const char g_stackRemoveFirst[] = "Remove some plugins first.";
PLUGIN_RODATA(onln) static const char g_stackDownloading[] = "Downloading plugin pack...";
PLUGIN_RODATA(onln) static const char g_stackValidating[] = "Validating plugin pack...";
PLUGIN_RODATA(onln) static const char g_stackInstalling[] = "Installing plugin pack...";
PLUGIN_RODATA(onln) static const char g_stackInstalled[] = "Sysplugin pack installed";
PLUGIN_RODATA(onln) static const char g_stackLoaderPrefix[] = "Loader: ";
PLUGIN_RODATA(onln) static const char g_stackRosalinaPrefix[] = "Rosalina: ";
PLUGIN_RODATA(onln) static const char g_stackNewText[] = " + ";
PLUGIN_RODATA(onln) static const char g_stackOverText[] = " new > 31";
PLUGIN_RODATA(onln) static const char g_packTitle[] = "Available Sysplugins";
PLUGIN_RODATA(onln) static const char g_packFetching[] = "Fetching sysplugin packs...";
PLUGIN_RODATA(onln) static const char g_packManifestFailed[] = "Pack manifest fetch/parse failed";
PLUGIN_RODATA(onln) static const char g_packNoEntries[] = "No sysplugins available";
PLUGIN_RODATA(onln) static const char g_packNotInstalled[] = "Not installed";
PLUGIN_RODATA(onln) static const char g_packUpdate[] = "A: Update";
PLUGIN_RODATA(onln) static const char g_packInstall[] = "A: Install";
PLUGIN_RODATA(onln) static const char g_packReinstall[] = "A: Re-install";
PLUGIN_RODATA(onln) static const char g_packUpdating[] = "Updating pack components...";
PLUGIN_RODATA(onln) static const char g_packDownloading[] = "Downloading...";
PLUGIN_RODATA(onln) static const char g_packInstalling[] = "Installing...";
PLUGIN_RODATA(onln) static const char g_packBuilding[] = "Building sysplugin pack...";
PLUGIN_RODATA(onln) static const char g_packInstalled[] = "Sysplugin pack installed";
PLUGIN_RODATA(onln) static const char g_packUpdated[] = "Sysplugin pack updated";
PLUGIN_RODATA(onln) static const char g_packAlreadyOutside[] = "Already installed outside selection";
PLUGIN_RODATA(onln) static const char g_packAlreadyOutside1[] = "All missing components exist on SD.";
PLUGIN_RODATA(onln) static const char g_packAlreadyOutside2[] = "They are outside the selected 31.";
PLUGIN_RODATA(onln) static const char g_packAlreadyDisabled[] = "Already installed but disabled";
PLUGIN_RODATA(onln) static const char g_packAlreadyDisabled1[] = "A required component is disabled.";
PLUGIN_RODATA(onln) static const char g_packAlreadyDisabled2[] = "Enable it before installing this pack.";
PLUGIN_RODATA(onln) static const char g_packCapacity[] = "Not enough selected plugin slots";
PLUGIN_RODATA(onln) static const char g_packCapacityWarn[] = "Some components may not be selected.";
PLUGIN_RODATA(onln) static const char g_packContinue[] = "A: Continue";
PLUGIN_RODATA(onln) static const char g_packOutput[] = "Output: ";
PLUGIN_RODATA(onln) static const char g_packDescription[] = "Description:";
PLUGIN_RODATA(onln) static const char g_packComponents[] = "Components:";
PLUGIN_RODATA(onln) static const u32 g_updateDecimalPowers[] = {
    1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
    10000u, 1000u, 100u, 10u, 1u
};

#define ONLN_3NX_HEADER_SIZE 0x30u
#define ONLN_VERSION_MAGIC 0x56584E33u
#define ONLN_ROSALINA_MAGIC 0x24584E33u
#define ONLN_LOADER_MAGIC 0x25584E33u
#define ONLN_MAX_SELECTED 31u
#define ONLN_UPDATE_PATH_CAP 272u
#define ONLN_UPDATE_IO_SIZE 0x2800u
#define ONLN_UPDATE_URL_CAP 2048u
#define ONLN_UPDATE_MAX_DOWNLOAD 0xFFFFFFFFu
#define ONLN_UPDATE_MANIFEST_MAX 0x20000u
#define ONLN_UPDATE_VISIBLE_ITEMS 14u
#define ONLN_UPDATE_ITEM_TOP_Y 45u
#define ONLN_UPDATE_ITEM_SPACING_Y 11u
#define ONLN_UPDATE_TOP_DOTS_Y 34u
#define ONLN_UPDATE_PROMPT_Y 221u
#define ONLN_UPDATE_NEWER_COLOR RGB565(20, 63, 21)
#define ONLN_UPDATE_OLDER_COLOR RGB565(31, 36, 18)
#define ONLN_UPDATE_BAD_FORMAT ((Result)0xD8A0A080u)
#define ONLN_UPDATE_BAD_FILE ((Result)0xD8A0A081u)
#define ONLN_FS_NOT_FOUND ((Result)0xC8804478u)
#define ONLN_FS_PATH_NOT_FOUND ((Result)0xC92044FAu)
#define ONLN_UPDATE_COPY_FAILED ((Result)0xD8A0A083u)
#define ONLN_UPDATE_SWAP_FAILED ((Result)0xD8A0A084u)
#define ONLN_UPDATE_RECOVERY_FAILED ((Result)0xD8A0A085u)
#define ONLN_STACK_MAX_LIST 31u
#define ONLN_STACK_MAX_PLUGINS 62u
#define ONLN_STACK_DISPLAY_NAME_MAX 46u
#define ONLN_STACK_MAX_DOWNLOAD 0xFFFFFFFFu
#define ONLN_STACK_DETAIL_VISIBLE 12u
#define ONLN_STACK_DETAIL_TOP_Y 64u
#define ONLN_STACK_BAD_FORMAT ((Result)0xD8A0A090u)
#define ONLN_STACK_BAD_FILE ((Result)0xD8A0A091u)
#define ONLN_REMOTE_MAX 96u
#define ONLN_PACK_MAX 31u
#define ONLN_PACK_MAX_COMPONENTS 16u
#define ONLN_PACK_TITLE_MAX 46u
#define ONLN_PACK_OUTPUT_MAX 96u
#define ONLN_PACK_DESC_MAX_LINES 8u
#define ONLN_PACK_DESC_LINE_MAX 46u
#define ONLN_PACK_MANIFEST_MAX 0x8000u
#define ONLN_PACK_STATE_GREEN 0u
#define ONLN_PACK_STATE_WHITE 1u
#define ONLN_PACK_STATE_RED 2u
#define ONLN_PACK_STATE_BLUE 3u
#define ONLN_PACK_MISSING_COLOR RGB8_to_565(120, 175, 255)
#define ONLN_PACK_BAD_FORMAT ((Result)0xD8A0A092u)

#pragma pack(push, 1)
typedef struct
{
    u32 magic;
    u32 plgid;
    u32 codeSize;
    u32 dataSize;
    u32 bssSize;
    u32 fastRelocSize;
    u32 repairSize;
    u32 ownAbiLo;
    u32 ownAbiHi;
    u32 expectedEnvLo;
    u32 expectedEnvHi;
    u32 metadataSize;
} Onln3nxHeader;
#pragma pack(pop)

typedef struct
{
    char path[ONLN_UPDATE_PATH_CAP];
    u32 priority;
    u32 fileOffset;
    u32 entryEnd;
    u32 plgid;
    u32 magic;
    u32 localVersion;
    u32 remoteVersion;
    u32 urlOffset;
    u32 urlLength;
    bool hasLocalVersion;
    bool hasRemote;
} OnlnSelectedPlugin;

typedef struct
{
    const MENUOnlineApi *api;
    u32 changeSize;
    bool disabledPass;
} OnlnScanContext;

typedef struct
{
    const char *afterName;
    bool found;
} OnlnInstallScanContext;

typedef struct
{
    u32 lineOffset;
    u32 lineLength;
    u32 urlOffset;
    u32 urlLength;
    u16 filenameOffset;
    u16 filenameLength;
    u8 pluginCount;
    u8 newLoader;
    u8 newRosalina;
    char displayName[ONLN_STACK_DISPLAY_NAME_MAX + 1u];
} OnlnStackListing;

typedef struct
{
    u32 plgid;
    u32 magic;
} OnlnStackId;

typedef struct
{
    u32 plgid;
    u32 magic;
    u32 version;
    u32 urlOffset;
    u32 urlLength;
} OnlnRemotePlugin;

typedef struct
{
    char title[ONLN_PACK_TITLE_MAX + 1u];
    char output[ONLN_PACK_OUTPUT_MAX + 1u];
    char description[ONLN_PACK_DESC_MAX_LINES][ONLN_PACK_DESC_LINE_MAX + 1u];
    OnlnStackId components[ONLN_PACK_MAX_COMPONENTS];
    u8 descriptionLines;
    u8 componentCount;
    bool outputComplete;
} OnlnPack;

typedef struct
{
    const MENUOnlineApi *api;
    const OnlnPack *pack;
    u32 changeSize;
    bool wanted[ONLN_PACK_MAX_COMPONENTS];
    bool foundDisabled[ONLN_PACK_MAX_COMPONENTS];
    bool foundEnabled[ONLN_PACK_MAX_COMPONENTS];
    bool disabledPass;
} OnlnPackPresenceContext;

PLUGIN_BSS(onln) static char g_onlineResultHex[9];
PLUGIN_BSS(onln) static char g_onlineTimeJson[512];
PLUGIN_BSS(onln) static char g_onlineUtc[25];
PLUGIN_BSS(onln) static OnlnSelectedPlugin g_updateLoader[ONLN_MAX_SELECTED];
PLUGIN_BSS(onln) static OnlnSelectedPlugin g_updateRosalina[ONLN_MAX_SELECTED];
PLUGIN_BSS(onln) static u32 g_updateLoaderCount;
PLUGIN_BSS(onln) static u32 g_updateRosalinaCount;
PLUGIN_BSS(onln) static u8 g_updateIo[ONLN_UPDATE_IO_SIZE];
PLUGIN_BSS(onln) static char g_updateUrl[ONLN_UPDATE_URL_CAP];
PLUGIN_BSS(onln) static char g_updateNumber[4];
PLUGIN_BSS(onln) static char g_updateId[5];
PLUGIN_BSS(onln) static char g_updateJournalRead[ONLN_UPDATE_PATH_CAP];
PLUGIN_BSS(onln) static char g_updateInstallNextName[MENU_ONLINE_DIR_NAME_CAP];
PLUGIN_BSS(onln) static char g_updateInstallAfterName[MENU_ONLINE_DIR_NAME_CAP];
PLUGIN_BSS(onln) static OnlnStackListing g_stackList[ONLN_STACK_MAX_LIST];
PLUGIN_BSS(onln) static OnlnStackId g_stackDeclared[ONLN_STACK_MAX_PLUGINS];
PLUGIN_BSS(onln) static u32 g_stackCount;
PLUGIN_BSS(onln) static char g_stackFilename[MENU_ONLINE_DIR_NAME_CAP];
PLUGIN_BSS(onln) static char g_stackDisplayName[ONLN_STACK_DISPLAY_NAME_MAX + 1u];
PLUGIN_BSS(onln) static char g_stackCapacityLine[48];
PLUGIN_BSS(onln) static OnlnRemotePlugin g_remotePlugins[ONLN_REMOTE_MAX];
PLUGIN_BSS(onln) static u32 g_remoteCount;
PLUGIN_BSS(onln) static OnlnPack g_packs[ONLN_PACK_MAX];
PLUGIN_BSS(onln) static u32 g_packCount;
PLUGIN_BSS(onln) static char g_packManifest[ONLN_PACK_MANIFEST_MAX + 1u];
PLUGIN_BSS(onln) static char g_packLine[96];

PLUGIN_CODE(onln) static void PLUGIN_onln_FormatResult(Result result)
{
    u32 value = (u32)result;
    for (u32 i = 0; i < 8; i++)
        g_onlineResultHex[i] = g_onlineHexDigits[(value >> ((7u - i) * 4u)) & 0xFu];
    g_onlineResultHex[8] = 0;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParseUtc(const char *json, u32 size)
{
    u32 keySize = 9u;

    for (u32 i = 0; i + keySize < size; i++)
    {
        bool match = true;
        for (u32 j = 0; j < keySize; j++)
        {
            if (json[i + j] != g_onlineUtcKey[j])
            {
                match = false;
                break;
            }
        }
        if (!match)
            continue;

        u32 pos = i + keySize;
        while (pos < size && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':'))
            pos++;
        if (pos >= size || json[pos] != '"')
            return false;
        pos++;

        u32 out = 0;
        while (pos < size && json[pos] != '"' && out + 1u < sizeof(g_onlineUtc))
            g_onlineUtc[out++] = json[pos++];
        if (pos >= size || json[pos] != '"' || !out)
            return false;
        g_onlineUtc[out] = 0;
        return true;
    }
    return false;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawDevMarker(const MENUOnlineApi *api)
{
    (void)api;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawFrame(const MENUOnlineApi *api, const char *title)
{
    api->drawString(10, 8, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 8, MENU_ONLINE_BORDER_COLOR, g_onlineRail);
    api->drawString(148, 8, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(10, 16, MENU_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(148, 16, MENU_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(10, 24, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 24, MENU_ONLINE_BORDER_COLOR, g_onlineRail);
    api->drawString(148, 24, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(20, 16, MENU_ONLINE_TITLE_COLOR, title);
    PLUGIN_onln_DrawDevMarker(api);
}

PLUGIN_CODE(onln) static u32 PLUGIN_onln_RootItemY(u32 index)
{
    return index == 0u ? 48u : 68u;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawRootItem(
    const MENUOnlineApi *api,
    u32 index,
    bool selected)
{
    u32 y = PLUGIN_onln_RootItemY(index);

    if (index > 1u)
        return;
    if (selected)
        api->drawString(12, y, COLOR_CYAN, g_onlineCursor);
    api->drawString(24, y, selected ? COLOR_CYAN : COLOR_WHITE,
                    index == 0u ? g_onlineUtcItem : g_onlineSyspluginsItem);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawRoot(const MENUOnlineApi *api, u32 selected)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawFrame(api, g_onlineTitle);
    PLUGIN_onln_DrawRootItem(api, 0u, selected == 0u);
    PLUGIN_onln_DrawRootItem(api, 1u, selected == 1u);
    api->drawString(20, 120, COLOR_GRAY, g_onlineBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RedrawRootSelection(
    const MENUOnlineApi *api,
    u32 oldSelected,
    u32 selected)
{
    u32 oldY = PLUGIN_onln_RootItemY(oldSelected);
    u32 newY = PLUGIN_onln_RootItemY(selected);

    api->drawLock();
    api->drawString(10, oldY, COLOR_BLACK, g_updateClearRow);
    api->drawString(10, newY, COLOR_BLACK, g_updateClearRow);
    PLUGIN_onln_DrawRootItem(api, oldSelected, false);
    PLUGIN_onln_DrawRootItem(api, selected, true);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUtc(const MENUOnlineApi *api, s32 state, Result result)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawFrame(api, g_onlineUtcTitle);
    api->drawString(20, 45, COLOR_WHITE, g_onlinePrompt);

    if (state == 0)
        api->drawString(20, 70, COLOR_GRAY, g_onlineReady);
    else if (state == 1)
        api->drawString(20, 70, COLOR_GRAY, g_onlineFetching);
    else if (state == 2)
    {
        api->drawString(20, 70, COLOR_GREEN, g_onlineSuccess);
        api->drawString(20, 90, COLOR_WHITE, g_onlineUtc);
    }
    else if (state == -2)
        api->drawString(20, 70, COLOR_RED, g_onlineParseFailure);
    else
    {
        PLUGIN_onln_FormatResult(result);
        api->drawString(20, 70, COLOR_RED, g_onlineFailure);
        api->drawString(20, 90, COLOR_WHITE, g_onlineResultPrefix);
        api->drawString(80, 90, COLOR_WHITE, g_onlineResultHex);
    }

    api->drawString(20, 120, COLOR_GRAY, g_onlineBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunUtc(const MENUOnlineApi *api)
{
    PLUGIN_onln_DrawUtc(api, 0, 0);
    do
    {
        u32 pressed = api->waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return;
        if (pressed & KEY_A)
        {
            u32 actualSize = 0;
            Result result;

            PLUGIN_onln_DrawUtc(api, 1, 0);
            result = api->downloadToMemory(
                g_onlineTimeUrl,
                g_onlineTimeJson,
                sizeof(g_onlineTimeJson) - 1u,
                &actualSize);

            if (R_FAILED(result))
            {
                PLUGIN_onln_DrawUtc(api, -1, result);
                continue;
            }

            if (actualSize >= sizeof(g_onlineTimeJson))
            {
                PLUGIN_onln_DrawUtc(api, -2, 0);
                continue;
            }
            g_onlineTimeJson[actualSize] = 0;
            if (!PLUGIN_onln_ParseUtc(g_onlineTimeJson, actualSize))
            {
                PLUGIN_onln_DrawUtc(api, -2, 0);
                continue;
            }
            PLUGIN_onln_DrawUtc(api, 2, 0);
        }
    } while (!*api->menuShouldExit);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_Add32(u32 a, u32 b, u32 *out)
{
    u32 value = a + b;
    if (value < a)
        return false;
    *out = value;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_Align16(u32 value, u32 *out)
{
    if (value > 0xFFFFFFF0u)
        return false;
    *out = (value + 0xFu) & ~0xFu;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ReadExact(
    const MENUOnlineApi *api,
    const char *path,
    u32 offset,
    void *buffer,
    u32 size)
{
    u32 done = 0;
    u8 *out = (u8 *)buffer;

    while (done < size)
    {
        u32 got = 0;
        u32 currentOffset;
        Result result;
        if (!PLUGIN_onln_Add32(offset, done, &currentOffset))
            return false;
        result = api->readFile(path, currentOffset, out + done, size - done, &got);
        if (R_FAILED(result) || !got || got > size - done)
            return false;
        done += got;
    }
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_WriteExact(
    const MENUOnlineApi *api,
    const char *path,
    u32 offset,
    const void *buffer,
    u32 size)
{
    u32 done = 0;
    const u8 *input = (const u8 *)buffer;

    while (done < size)
    {
        u32 wrote = 0;
        u32 currentOffset;
        Result result;
        if (!PLUGIN_onln_Add32(offset, done, &currentOffset))
            return false;
        result = api->writeFile(path, currentOffset, input + done, size - done, &wrote);
        if (R_FAILED(result) || !wrote || wrote > size - done)
            return false;
        done += wrote;
    }
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_Read3nxHeader(
    const MENUOnlineApi *api,
    const char *path,
    u32 fileSize,
    u32 fileOffset,
    Onln3nxHeader *header,
    u32 *metadataOffset,
    u32 *nextOffset)
{
    u32 end;

    if (!header || !metadataOffset || !nextOffset ||
        fileOffset > fileSize || fileSize - fileOffset < ONLN_3NX_HEADER_SIZE ||
        !PLUGIN_onln_ReadExact(api, path, fileOffset, header, sizeof(*header)) ||
        !PLUGIN_onln_Add32(fileOffset, ONLN_3NX_HEADER_SIZE, &end) ||
        !PLUGIN_onln_Add32(end, header->fastRelocSize, &end) ||
        !PLUGIN_onln_Add32(end, header->codeSize, &end) ||
        !PLUGIN_onln_Add32(end, header->dataSize, &end) ||
        !PLUGIN_onln_Add32(end, header->repairSize, &end) ||
        !PLUGIN_onln_Align16(end, metadataOffset) ||
        !PLUGIN_onln_Add32(*metadataOffset, header->metadataSize, nextOffset) ||
        *nextOffset <= fileOffset)
    {
        return false;
    }
    return true;
}

PLUGIN_CODE(onln) static s32 PLUGIN_onln_StringCompare(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (s32)(u8)*a - (s32)(u8)*b;
}

PLUGIN_CODE(onln) static u32 PLUGIN_onln_CanonicalPluginNameLength(const char *name)
{
    u32 length = 0;
    volatile const char *scan = name;
    if (!name)
        return 0;
    while (scan[length])
        length++;
    if (length >= 6u && name[length - 2u] == '.' && name[length - 1u] == 'd' &&
        name[length - 6u] == '.' && name[length - 5u] == '3' &&
        name[length - 4u] == 'n' && name[length - 3u] == 'x')
        length -= 2u;
    return length;
}

PLUGIN_CODE(onln) static s32 PLUGIN_onln_ComparePluginNames(const char *a, const char *b)
{
    u32 aLength = PLUGIN_onln_CanonicalPluginNameLength(a);
    u32 bLength = PLUGIN_onln_CanonicalPluginNameLength(b);
    u32 common = aLength < bLength ? aLength : bLength;
    for (u32 i = 0; i < common; i++)
        if ((u8)a[i] != (u8)b[i])
            return (s32)(u8)a[i] - (s32)(u8)b[i];
    if (aLength == bLength)
        return 0;
    return aLength < bLength ? -1 : 1;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_EntryEarlier(
    const OnlnSelectedPlugin *current,
    const char *path,
    u32 priority,
    u32 fileOffset)
{
    const char *name = path;
    const char *currentName = current->path;
    s32 comparison;

    while (*name)
        name++;
    while (name > path && name[-1] != '/')
        name--;
    while (*currentName)
        currentName++;
    while (currentName > current->path && currentName[-1] != '/')
        currentName--;

    if (priority != current->priority)
        return priority < current->priority;
    comparison = PLUGIN_onln_ComparePluginNames(name, currentName);
    if (comparison != 0)
        return comparison < 0;
    return fileOffset < current->fileOffset;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_CopyString(char *out, u32 outSize, const char *text)
{
    u32 i = 0;
    if (!out || !outSize || !text)
        return false;
    while (text[i])
    {
        if (i + 1u >= outSize)
            return false;
        out[i] = text[i];
        i++;
    }
    out[i] = 0;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_MakePluginPath(char *out, u32 outSize, const char *name)
{
    u32 pos = 0;
    const char *prefix = g_updatePluginsPrefix;
    if (!out || !name)
        return false;
    while (*prefix)
    {
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = *prefix++;
    }
    while (*name)
    {
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = *name++;
    }
    out[pos] = 0;
    return true;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_CopySelected(
    OnlnSelectedPlugin *destination,
    const OnlnSelectedPlugin *source)
{
    u32 i = 0;
    while (i < sizeof(destination->path))
    {
        destination->path[i] = source->path[i];
        if (!source->path[i])
        {
            i++;
            while (i < sizeof(destination->path))
                destination->path[i++] = 0;
            break;
        }
        i++;
    }
    destination->priority = source->priority;
    destination->fileOffset = source->fileOffset;
    destination->entryEnd = source->entryEnd;
    destination->plgid = source->plgid;
    destination->magic = source->magic;
    destination->localVersion = source->localVersion;
    destination->remoteVersion = source->remoteVersion;
    destination->urlOffset = source->urlOffset;
    destination->urlLength = source->urlLength;
    destination->hasLocalVersion = source->hasLocalVersion;
    destination->hasRemote = source->hasRemote;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_AddSelected(
    OnlnSelectedPlugin *plugins,
    u32 *count,
    const char *path,
    u32 priority,
    u32 fileOffset,
    u32 entryEnd,
    const Onln3nxHeader *header,
    bool hasVersion,
    u32 version)
{
    u32 pos;
    OnlnSelectedPlugin incoming;

    if (!plugins || !count || !path || !header)
        return;

    if (!PLUGIN_onln_CopyString(incoming.path, sizeof(incoming.path), path))
        return;
    incoming.priority = priority;
    incoming.fileOffset = fileOffset;
    incoming.entryEnd = entryEnd;
    incoming.plgid = header->plgid;
    incoming.magic = header->magic;
    incoming.localVersion = version;
    incoming.remoteVersion = 0;
    incoming.urlOffset = 0;
    incoming.urlLength = 0;
    incoming.hasLocalVersion = hasVersion;
    incoming.hasRemote = false;

    for (pos = 0; pos < *count; pos++)
    {
        if (plugins[pos].plgid != header->plgid)
            continue;
        if (!PLUGIN_onln_EntryEarlier(&plugins[pos], path, priority, fileOffset))
            return;
        break;
    }

    if (pos == *count)
    {
        if (*count < ONLN_MAX_SELECTED)
            pos = (*count)++;
        else
        {
            pos = ONLN_MAX_SELECTED - 1u;
            if (!PLUGIN_onln_EntryEarlier(&plugins[pos], path, priority, fileOffset))
                return;
        }
    }

    while (pos > 0 && PLUGIN_onln_EntryEarlier(&plugins[pos - 1u], path, priority, fileOffset))
    {
        PLUGIN_onln_CopySelected(&plugins[pos], &plugins[pos - 1u]);
        pos--;
    }
    PLUGIN_onln_CopySelected(&plugins[pos], &incoming);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParsePluginName(
    const char *name, u32 *priority, bool *disabled)
{
    u32 length = 0;
    const char *extension;
    const char *priorityDot;
    u32 value = 0;
    bool isDisabled = false;

    if (!name)
        return false;
    while (name[length])
    {
        if (length + 1u >= MENU_ONLINE_DIR_NAME_CAP)
            return false;
        length++;
    }

    if (length >= 7u &&
        name[length - 4u] == '.' && name[length - 3u] == '3' &&
        name[length - 2u] == 'n' && name[length - 1u] == 'x')
    {
        extension = &name[length - 4u];
    }
    else if (length >= 9u &&
             name[length - 6u] == '.' && name[length - 5u] == '3' &&
             name[length - 4u] == 'n' && name[length - 3u] == 'x' &&
             name[length - 2u] == '.' && name[length - 1u] == 'd')
    {
        extension = &name[length - 6u];
        isDisabled = true;
    }
    else
    {
        return false;
    }

    priorityDot = extension - 1;
    while (priorityDot > name && *priorityDot != '.')
        priorityDot--;
    if (*priorityDot != '.' || priorityDot + 1 == extension)
        return false;

    for (const char *character = priorityDot + 1; character < extension; character++)
    {
        u32 digit;
        if (*character < '0' || *character > '9')
            return false;
        digit = (u32)(*character - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    if (priority)
        *priority = value;
    if (disabled)
        *disabled = isDisabled;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParsePriority(const char *name, u32 *priority)
{
    bool disabled = false;
    return PLUGIN_onln_ParsePluginName(name, priority, &disabled) && !disabled;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_IsInstallCandidateName(const char *name)
{
    return PLUGIN_onln_ParsePluginName(name, NULL, NULL);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ReadVersion(
    const MENUOnlineApi *api,
    const char *path,
    const Onln3nxHeader *header,
    u32 metadataOffset,
    u32 *version)
{
    u8 bytes[8];
    u32 magic;

    if (!header || !version || header->metadataSize < 8u ||
        !PLUGIN_onln_ReadExact(api, path, metadataOffset, bytes, sizeof(bytes)))
        return false;

    magic = (u32)bytes[0] |
            ((u32)bytes[1] << 8) |
            ((u32)bytes[2] << 16) |
            ((u32)bytes[3] << 24);
    if (magic != ONLN_VERSION_MAGIC)
        return false;

    *version = (u32)bytes[4] |
               ((u32)bytes[5] << 8) |
               ((u32)bytes[6] << 16) |
               ((u32)bytes[7] << 24);
    return true;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_LoadManageChanges(
    const MENUOnlineApi *api, u32 *changeSize)
{
    bool exists = false;
    u32 size = 0;
    Result result;
    if (!api || !changeSize)
        return ONLN_UPDATE_BAD_FORMAT;
    *changeSize = 0;
    result = api->fileExists(g_updateManageTempListPath, &exists);
    if (R_FAILED(result))
    {
        if (result == ONLN_FS_NOT_FOUND || result == ONLN_FS_PATH_NOT_FOUND)
            return 0;
        return result;
    }
    if (!exists)
        return 0;
    result = api->getFileSize(g_updateManageTempListPath, &size);
    if (R_FAILED(result))
        return result;
    if (size > sizeof(g_updateIo) ||
        (size && !PLUGIN_onln_ReadExact(api, g_updateManageTempListPath, 0, g_updateIo, size)))
        return ONLN_UPDATE_BAD_FILE;
    *changeSize = size;
    return 0;
}

PLUGIN_CODE(onln) static char PLUGIN_onln_FindManageChange(
    const char *name, u32 changeSize)
{
    u32 wanted = PLUGIN_onln_CanonicalPluginNameLength(name);
    u32 pos = 0;
    while (pos < changeSize)
    {
        u32 start = pos;
        while (pos < changeSize && g_updateIo[pos] != '\n')
            pos++;
        u32 textEnd = pos;
        u32 end = pos < changeSize ? pos + 1u : pos;
        if (textEnd >= start + 2u &&
            ((char)g_updateIo[start] == 'D' || (char)g_updateIo[start] == 'E') &&
            g_updateIo[start + 1u] == '|')
        {
            u32 textStart = start + 2u;
            u32 textLength = textEnd - textStart;
            bool match = textLength == wanted;
            for (u32 i = 0; match && i < wanted; i++)
                if ((char)g_updateIo[textStart + i] != name[i])
                    match = false;
            if (match)
                return (char)g_updateIo[start];
        }
        pos = end;
    }
    return 0;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_WasInBootSnapshot(
    const char *name, u32 changeSize)
{
    u32 wanted = PLUGIN_onln_CanonicalPluginNameLength(name);
    u32 pos = 0;
    while (pos < changeSize)
    {
        u32 start = pos;
        while (pos < changeSize && g_updateIo[pos] != '\n')
            pos++;
        u32 textEnd = pos;
        u32 end = pos < changeSize ? pos + 1u : pos;
        bool stateLine = textEnd >= start + 2u &&
            ((char)g_updateIo[start] == 'D' || (char)g_updateIo[start] == 'E') &&
            g_updateIo[start + 1u] == '|';
        bool match = !stateLine && textEnd - start == wanted;
        for (u32 i = 0; match && i < wanted; i++)
            if ((char)g_updateIo[start + i] != name[i])
                match = false;
        if (match)
            return true;
        pos = end;
    }
    return false;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ManageBootActive(
    const char *name, bool disabled, u32 changeSize)
{
    char change = PLUGIN_onln_FindManageChange(name, changeSize);
    if (disabled)
        return change == 'E';
    if (change == 'D')
        return false;
    return PLUGIN_onln_WasInBootSnapshot(name, changeSize);
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ScanOneFile(
    const MENUOnlineApi *api,
    const char *name,
    u32 priority)
{
    char path[ONLN_UPDATE_PATH_CAP];
    u32 fileSize = 0;
    u32 fileOffset = 0;
    Result result;

    if (!PLUGIN_onln_MakePluginPath(path, sizeof(path), name))
        return 0;
    result = api->getFileSize(path, &fileSize);
    if (R_FAILED(result))
        return 0;

    while (fileOffset < fileSize)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        u32 version = 0;
        bool hasVersion;

        if (!PLUGIN_onln_Read3nxHeader(api, path, fileSize, fileOffset, &header, &metadataOffset, &nextOffset) ||
            nextOffset > fileSize)
            break;
        if (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC)
            break;

        hasVersion = PLUGIN_onln_ReadVersion(api, path, &header, metadataOffset, &version);
        if (header.magic == ONLN_LOADER_MAGIC)
            PLUGIN_onln_AddSelected(g_updateLoader, &g_updateLoaderCount, path, priority, fileOffset, nextOffset, &header, hasVersion, version);
        else
            PLUGIN_onln_AddSelected(g_updateRosalina, &g_updateRosalinaCount, path, priority, fileOffset, nextOffset, &header, hasVersion, version);

        fileOffset = nextOffset;
    }
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ScanVisitor(
    const MENUOnlineDirEntry *entry,
    void *context,
    bool *stop)
{
    OnlnScanContext *scan = (OnlnScanContext *)context;
    u32 priority;
    bool disabled;
    (void)stop;

    if (!entry || !scan || !(entry->flags & MENU_ONLINE_DIR_NAME_COMPLETE) ||
        (entry->attributes & FS_ATTRIBUTE_DIRECTORY) ||
        !PLUGIN_onln_ParsePluginName(entry->name, &priority, &disabled) ||
        disabled != scan->disabledPass)
    {
        return 0;
    }

    if (!PLUGIN_onln_ManageBootActive(entry->name, disabled, scan->changeSize))
        return 0;
    return PLUGIN_onln_ScanOneFile(scan->api, entry->name, priority);
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ScanSelected(const MENUOnlineApi *api)
{
    OnlnScanContext context;
    u32 visited = 0;
    Result result;
    g_updateLoaderCount = 0;
    g_updateRosalinaCount = 0;
    context.api = api;
    result = PLUGIN_onln_LoadManageChanges(api, &context.changeSize);
    if (R_FAILED(result))
        return result;

    context.disabledPass = true;
    result = api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_ScanVisitor, &context, &visited);
    if (R_FAILED(result))
        return result;
    visited = 0;
    context.disabledPass = false;
    return api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_ScanVisitor, &context, &visited);
}

PLUGIN_CODE(onln) static OnlnSelectedPlugin *PLUGIN_onln_FindSelected(u32 magic, u32 plgid)
{
    OnlnSelectedPlugin *plugins = magic == ONLN_LOADER_MAGIC ? g_updateLoader : g_updateRosalina;
    u32 count = magic == ONLN_LOADER_MAGIC ? g_updateLoaderCount : g_updateRosalinaCount;
    for (u32 i = 0; i < count; i++)
    {
        if (plugins[i].plgid == plgid)
            return &plugins[i];
    }
    return NULL;
}

PLUGIN_CODE(onln) static OnlnRemotePlugin *PLUGIN_onln_FindRemote(u32 magic, u32 plgid)
{
    for (u32 i = 0; i < g_remoteCount; i++)
        if (g_remotePlugins[i].magic == magic && g_remotePlugins[i].plgid == plgid)
            return &g_remotePlugins[i];
    return NULL;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_LinkRemoteToSelected(void)
{
    for (u32 i = 0; i < g_updateLoaderCount; i++)
    {
        OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(g_updateLoader[i].magic, g_updateLoader[i].plgid);
        g_updateLoader[i].hasRemote = remote != NULL;
        if (remote)
        {
            g_updateLoader[i].remoteVersion = remote->version;
            g_updateLoader[i].urlOffset = remote->urlOffset;
            g_updateLoader[i].urlLength = remote->urlLength;
        }
    }
    for (u32 i = 0; i < g_updateRosalinaCount; i++)
    {
        OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(g_updateRosalina[i].magic, g_updateRosalina[i].plgid);
        g_updateRosalina[i].hasRemote = remote != NULL;
        if (remote)
        {
            g_updateRosalina[i].remoteVersion = remote->version;
            g_updateRosalina[i].urlOffset = remote->urlOffset;
            g_updateRosalina[i].urlLength = remote->urlLength;
        }
    }
}

static bool PLUGIN_onln_IsPackSeparator(const char *line, u32 length);

PLUGIN_CODE(onln) static s32 PLUGIN_onln_HexDigit(char value)
{
    if (value >= '0' && value <= '9')
        return (s32)(value - '0');
    if (value >= 'A' && value <= 'F')
        return (s32)(value - 'A' + 10);
    if (value >= 'a' && value <= 'f')
        return (s32)(value - 'a' + 10);
    return -1;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParseManifestPrefix(
    const char *prefix,
    u32 length,
    u32 *plgid,
    u32 *magic,
    u32 *version)
{
    u32 parsed = 0;

    if (!prefix || length < 13u || !plgid || !magic || !version)
        return false;

    for (u32 i = 0; i < 4u; i++)
    {
        char c = prefix[i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_'))
        {
            return false;
        }
    }

    *plgid = (u32)(u8)prefix[0] |
             ((u32)(u8)prefix[1] << 8) |
             ((u32)(u8)prefix[2] << 16) |
             ((u32)(u8)prefix[3] << 24);

    for (u32 i = 4u; i < 12u; i++)
    {
        s32 digit = PLUGIN_onln_HexDigit(prefix[i]);
        if (digit < 0)
            return false;
        parsed = (parsed << 4) | (u32)digit;
    }

    if (prefix[12] == ':')
        *magic = ONLN_LOADER_MAGIC;
    else if (prefix[12] == ';')
        *magic = ONLN_ROSALINA_MAGIC;
    else
        return false;

    *version = parsed;
    return true;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ApplyManifestLine(
    const char *prefix,
    u32 prefixLength,
    u32 lineStart,
    u32 lineLength,
    char lastCharacter)
{
    u32 plgid;
    u32 magic;
    u32 version;
    u32 effectiveLength = lineLength;
    OnlnSelectedPlugin *selected;
    OnlnRemotePlugin *remote;

    if (effectiveLength && lastCharacter == '\r')
        effectiveLength--;
    if (!effectiveLength)
        return 0;
    if (effectiveLength <= 13u || prefixLength < 13u ||
        !PLUGIN_onln_ParseManifestPrefix(prefix, prefixLength, &plgid, &magic, &version) ||
        effectiveLength - 13u >= ONLN_UPDATE_URL_CAP)
        return ONLN_UPDATE_BAD_FORMAT;

    if (PLUGIN_onln_FindRemote(magic, plgid) || g_remoteCount >= ONLN_REMOTE_MAX)
        return ONLN_UPDATE_BAD_FORMAT;
    remote = &g_remotePlugins[g_remoteCount++];
    remote->plgid = plgid;
    remote->magic = magic;
    remote->version = version;
    remote->urlOffset = lineStart + 13u;
    remote->urlLength = effectiveLength - 13u;

    selected = PLUGIN_onln_FindSelected(magic, plgid);
    if (selected)
    {
        selected->remoteVersion = version;
        selected->urlOffset = remote->urlOffset;
        selected->urlLength = remote->urlLength;
        selected->hasRemote = true;
    }
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ParseManifest(const MENUOnlineApi *api)
{
    u32 fileSize = 0;
    u32 fileOffset = 0;
    u32 lineStart = 0;
    u32 lineLength = 0;
    u32 prefixLength = 0;
    char prefix[13];
    char lastCharacter = 0;
    Result result;

    g_remoteCount = 0;
    result = api->getFileSize(g_updateManifestPath, &fileSize);

    if (R_FAILED(result) || !fileSize || fileSize > ONLN_UPDATE_MANIFEST_MAX)
        return R_FAILED(result) ? result : ONLN_UPDATE_BAD_FORMAT;

    while (fileOffset < fileSize)
    {
        u32 want = fileSize - fileOffset;
        u32 got = 0;

        if (want > sizeof(g_updateIo))
            want = sizeof(g_updateIo);

        result = api->readFile(g_updateManifestPath, fileOffset, g_updateIo, want, &got);
        if (R_FAILED(result) || !got || got > want)
            return R_FAILED(result) ? result : ONLN_UPDATE_BAD_FORMAT;

        for (u32 i = 0; i < got; i++)
        {
            char c = (char)g_updateIo[i];

            if (c == '\n')
            {
                u32 effectiveLength = lineLength;
                if (effectiveLength && lastCharacter == '\r')
                    effectiveLength--;
                if (PLUGIN_onln_IsPackSeparator(prefix, effectiveLength))
                    return g_remoteCount ? 0 : ONLN_UPDATE_BAD_FORMAT;
                result = PLUGIN_onln_ApplyManifestLine(
                    prefix,
                    prefixLength,
                    lineStart,
                    lineLength,
                    lastCharacter
                );
                if (R_FAILED(result))
                    return result;

                lineStart = fileOffset + i + 1u;
                lineLength = 0;
                prefixLength = 0;
                lastCharacter = 0;
                continue;
            }

            if (prefixLength < sizeof(prefix))
                prefix[prefixLength++] = c;

            lineLength++;
            lastCharacter = c;

            if (lineLength > 13u + ONLN_UPDATE_URL_CAP)
                return ONLN_UPDATE_BAD_FORMAT;
        }

        fileOffset += got;
    }

    if (lineLength)
    {
        u32 effectiveLength = lineLength;
        if (effectiveLength && lastCharacter == '\r')
            effectiveLength--;
        if (PLUGIN_onln_IsPackSeparator(prefix, effectiveLength))
            return g_remoteCount ? 0 : ONLN_UPDATE_BAD_FORMAT;
    }
    return ONLN_UPDATE_BAD_FORMAT;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_FileExists(const MENUOnlineApi *api, const char *path)
{
    bool exists = false;
    return R_SUCCEEDED(api->fileExists(path, &exists)) && exists;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DeleteIfExists(const MENUOnlineApi *api, const char *path)
{
    bool exists = false;
    if (R_SUCCEEDED(api->fileExists(path, &exists)) && exists)
        (void)api->deleteFile(path);
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_WriteJournal(const MENUOnlineApi *api, const char *path)
{
    u32 length = 0;
    while (path[length])
    {
        if (length + 1u >= sizeof(g_updateJournalRead))
            return ONLN_UPDATE_BAD_FORMAT;
        length++;
    }
    PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
    if (!PLUGIN_onln_WriteExact(api, g_updateJournalPath, 0, path, length) ||
        R_FAILED(api->setFileSize(g_updateJournalPath, length)))
        return ONLN_UPDATE_SWAP_FAILED;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_RecoverInterruptedUpdate(const MENUOnlineApi *api)
{
    bool backup = PLUGIN_onln_FileExists(api, g_updateBackupPath);
    bool journal = PLUGIN_onln_FileExists(api, g_updateJournalPath);

    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);

    if (!backup && !journal)
        return 0;
    if (!journal)
        return ONLN_UPDATE_RECOVERY_FAILED;

    {
        u32 size = 0;
        if (R_FAILED(api->getFileSize(g_updateJournalPath, &size)) || !size || size >= sizeof(g_updateJournalRead) ||
            !PLUGIN_onln_ReadExact(api, g_updateJournalPath, 0, g_updateJournalRead, size))
            return ONLN_UPDATE_RECOVERY_FAILED;
        g_updateJournalRead[size] = 0;
    }

    if (backup)
    {
        bool targetExists = PLUGIN_onln_FileExists(api, g_updateJournalRead);
        if (!targetExists)
        {
            if (R_FAILED(api->renameFile(g_updateBackupPath, g_updateJournalRead)))
                return ONLN_UPDATE_RECOVERY_FAILED;
        }
        else
        {
            if (R_FAILED(api->deleteFile(g_updateBackupPath)))
                return ONLN_UPDATE_RECOVERY_FAILED;
        }
    }
    PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_CopyRange(
    const MENUOnlineApi *api,
    const char *source,
    u32 sourceOffset,
    const char *destination,
    u32 destinationOffset,
    u32 size)
{
    u32 done = 0;
    while (done < size)
    {
        u32 chunk = size - done;
        u32 srcAt;
        u32 dstAt;
        if (chunk > ONLN_PACK_MANIFEST_MAX)
            chunk = ONLN_PACK_MANIFEST_MAX;
        if (!PLUGIN_onln_Add32(sourceOffset, done, &srcAt) ||
            !PLUGIN_onln_Add32(destinationOffset, done, &dstAt) ||
            !PLUGIN_onln_ReadExact(api, source, srcAt, g_packManifest, chunk) ||
            !PLUGIN_onln_WriteExact(api, destination, dstAt, g_packManifest, chunk))
            return ONLN_UPDATE_COPY_FAILED;
        done += chunk;
    }
    return 0;
}

static Result PLUGIN_onln_SwapRebuild(
    const MENUOnlineApi *api,
    const char *target);

PLUGIN_CODE(onln) static Result PLUGIN_onln_InstallNextVisitor(
    const MENUOnlineDirEntry *entry,
    void *context,
    bool *stop)
{
    OnlnInstallScanContext *scan = (OnlnInstallScanContext *)context;
    (void)stop;

    if (!entry || !scan || !(entry->flags & MENU_ONLINE_DIR_NAME_COMPLETE) ||
        (entry->attributes & FS_ATTRIBUTE_DIRECTORY) ||
        !PLUGIN_onln_IsInstallCandidateName(entry->name) ||
        (scan->afterName && scan->afterName[0] && PLUGIN_onln_StringCompare(entry->name, scan->afterName) <= 0))
    {
        return 0;
    }

    if (!scan->found || PLUGIN_onln_StringCompare(entry->name, g_updateInstallNextName) < 0)
    {
        if (!PLUGIN_onln_CopyString(g_updateInstallNextName, sizeof(g_updateInstallNextName), entry->name))
            return ONLN_UPDATE_BAD_FORMAT;
        scan->found = true;
    }
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_FindNextInstallFile(
    const MENUOnlineApi *api,
    bool *found)
{
    OnlnInstallScanContext context;
    u32 visited = 0;
    Result result;

    if (!found)
        return ONLN_UPDATE_BAD_FORMAT;
    g_updateInstallNextName[0] = 0;
    context.afterName = g_updateInstallAfterName;
    context.found = false;
    result = api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_InstallNextVisitor, &context, &visited);
    if (R_FAILED(result))
        return result;
    *found = context.found;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ReplaceMatchesInFile(
    const MENUOnlineApi *api,
    const char *name,
    u32 targetMagic,
    u32 targetPlgid,
    u32 downloadSize,
    u32 *updated)
{
    char path[ONLN_UPDATE_PATH_CAP];
    u32 fileSize = 0;
    u32 fileOffset = 0;
    u32 outputOffset = 0;
    u32 keepStart = 0;
    u32 matches = 0;
    Result result;

    if (!updated || !PLUGIN_onln_MakePluginPath(path, sizeof(path), name))
        return ONLN_UPDATE_BAD_FORMAT;
    result = api->getFileSize(path, &fileSize);
    if (R_FAILED(result))
        return result;

    // Require the whole physical file/stack to parse cleanly before replacing anything.
    while (fileOffset < fileSize)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        if (!PLUGIN_onln_Read3nxHeader(api, path, fileSize, fileOffset, &header, &metadataOffset, &nextOffset) ||
            nextOffset > fileSize ||
            (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC))
        {
            return 0;
        }
        if (header.magic == targetMagic && header.plgid == targetPlgid)
            matches++;
        fileOffset = nextOffset;
    }
    if (!matches)
        return 0;
    if (*updated > 0xFFFFFFFFu - matches)
        return ONLN_UPDATE_BAD_FORMAT;

    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    result = api->setFileSize(g_updateRebuildPath, 0);
    if (R_FAILED(result))
        return result;

    fileOffset = 0;
    while (fileOffset < fileSize)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;

        if (!PLUGIN_onln_Read3nxHeader(api, path, fileSize, fileOffset, &header, &metadataOffset, &nextOffset) ||
            nextOffset > fileSize)
        {
            result = ONLN_UPDATE_BAD_FILE;
            goto finish;
        }

        if (header.magic == targetMagic && header.plgid == targetPlgid)
        {
            u32 nextOutput;
            if (fileOffset > keepStart)
            {
                u32 keepSize = fileOffset - keepStart;
                if (!PLUGIN_onln_Add32(outputOffset, keepSize, &nextOutput))
                {
                    result = ONLN_UPDATE_BAD_FILE;
                    goto finish;
                }
                result = PLUGIN_onln_CopyRange(api, path, keepStart, g_updateRebuildPath, outputOffset, keepSize);
                if (R_FAILED(result))
                    goto finish;
                outputOffset = nextOutput;
            }
            if (!PLUGIN_onln_Add32(outputOffset, downloadSize, &nextOutput))
            {
                result = ONLN_UPDATE_BAD_FILE;
                goto finish;
            }
            result = PLUGIN_onln_CopyRange(api, g_updateDownloadPath, 0, g_updateRebuildPath, outputOffset, downloadSize);
            if (R_FAILED(result))
                goto finish;
            outputOffset = nextOutput;
            keepStart = nextOffset;
        }
        fileOffset = nextOffset;
    }

    if (keepStart < fileSize)
    {
        u32 keepSize = fileSize - keepStart;
        u32 nextOutput;
        if (!PLUGIN_onln_Add32(outputOffset, keepSize, &nextOutput))
        {
            result = ONLN_UPDATE_BAD_FILE;
            goto finish;
        }
        result = PLUGIN_onln_CopyRange(api, path, keepStart, g_updateRebuildPath, outputOffset, keepSize);
        if (R_FAILED(result))
            goto finish;
        outputOffset = nextOutput;
    }

    result = api->setFileSize(g_updateRebuildPath, outputOffset);
    if (R_FAILED(result))
        goto finish;
    result = PLUGIN_onln_SwapRebuild(api, path);
    if (R_FAILED(result))
        goto finish;

    *updated += matches;

finish:
    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    return result;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ReplaceAllOccurrences(
    const MENUOnlineApi *api,
    const OnlnSelectedPlugin *selected,
    u32 downloadSize)
{
    u32 updated = 0;
    Result result;

    g_updateInstallAfterName[0] = 0;
    for (;;)
    {
        bool found = false;
        result = PLUGIN_onln_FindNextInstallFile(api, &found);
        if (R_FAILED(result))
            return result;
        if (!found)
            break;
        if (!PLUGIN_onln_CopyString(g_updateInstallAfterName, sizeof(g_updateInstallAfterName), g_updateInstallNextName))
            return ONLN_UPDATE_BAD_FORMAT;

        result = PLUGIN_onln_ReplaceMatchesInFile(
            api,
            g_updateInstallNextName,
            selected->magic,
            selected->plgid,
            downloadSize,
            &updated);
        if (R_FAILED(result))
            return result;
    }

    return updated ? 0 : ONLN_UPDATE_BAD_FILE;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ValidateDownloaded(
    const MENUOnlineApi *api,
    const OnlnSelectedPlugin *selected,
    u32 manifestVersion,
    u32 *downloadSize)
{
    Onln3nxHeader header;
    u32 metadataOffset;
    u32 versionOffset;
    u32 nextOffset;
    u32 size = 0;
    u8 magicBytes[4];
    u8 versionBytes[4];
    u32 magic;
    Result result = api->getFileSize(g_updateDownloadPath, &size);

    if (R_FAILED(result))
        return result;
    if (!size || !PLUGIN_onln_Read3nxHeader(api, g_updateDownloadPath, size, 0, &header, &metadataOffset, &nextOffset) ||
        nextOffset != size || header.magic != selected->magic || header.plgid != selected->plgid ||
        header.metadataSize < 8u ||
        !PLUGIN_onln_ReadExact(api, g_updateDownloadPath, metadataOffset, magicBytes, sizeof(magicBytes)))
        return ONLN_UPDATE_BAD_FILE;

    magic = (u32)magicBytes[0] |
            ((u32)magicBytes[1] << 8) |
            ((u32)magicBytes[2] << 16) |
            ((u32)magicBytes[3] << 24);
    if (magic != ONLN_VERSION_MAGIC || !PLUGIN_onln_Add32(metadataOffset, 4u, &versionOffset))
        return ONLN_UPDATE_BAD_FILE;

    versionBytes[0] = (u8)(manifestVersion & 0xFFu);
    versionBytes[1] = (u8)((manifestVersion >> 8) & 0xFFu);
    versionBytes[2] = (u8)((manifestVersion >> 16) & 0xFFu);
    versionBytes[3] = (u8)((manifestVersion >> 24) & 0xFFu);
    if (!PLUGIN_onln_WriteExact(api, g_updateDownloadPath, versionOffset, versionBytes, sizeof(versionBytes)))
        return ONLN_UPDATE_COPY_FAILED;

    *downloadSize = size;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ReadManifestUrl(
    const MENUOnlineApi *api,
    const OnlnSelectedPlugin *selected)
{
    if (!selected->hasRemote || !selected->urlLength || selected->urlLength >= sizeof(g_updateUrl) ||
        !PLUGIN_onln_ReadExact(api, g_updateManifestPath, selected->urlOffset, g_updateUrl, selected->urlLength))
        return ONLN_UPDATE_BAD_FORMAT;
    g_updateUrl[selected->urlLength] = 0;
    if (selected->urlLength < 8u || g_updateUrl[0] != 'h' || g_updateUrl[1] != 't' ||
        g_updateUrl[2] != 't' || g_updateUrl[3] != 'p' || g_updateUrl[4] != 's' ||
        g_updateUrl[5] != ':' || g_updateUrl[6] != '/' || g_updateUrl[7] != '/')
        return ONLN_UPDATE_BAD_FORMAT;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_SwapRebuild(
    const MENUOnlineApi *api,
    const char *target)
{
    Result result;
    PLUGIN_onln_DeleteIfExists(api, g_updateBackupPath);
    result = PLUGIN_onln_WriteJournal(api, target);
    if (R_FAILED(result))
        return result;

    result = api->renameFile(target, g_updateBackupPath);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
        return result;
    }

    result = api->renameFile(g_updateRebuildPath, target);
    if (R_FAILED(result))
    {
        Result restore = api->renameFile(g_updateBackupPath, target);
        if (R_SUCCEEDED(restore))
            PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
        return ONLN_UPDATE_SWAP_FAILED;
    }

    PLUGIN_onln_DeleteIfExists(api, g_updateBackupPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_DownloadUpdate(
    const MENUOnlineApi *api,
    const OnlnSelectedPlugin *selected,
    u32 *downloadSize)
{
    Result result;
    if (!downloadSize)
        return ONLN_UPDATE_BAD_FORMAT;
    result = PLUGIN_onln_ReadManifestUrl(api, selected);
    if (R_FAILED(result))
        return result;
    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    result = api->downloadToFile(g_updateUrl, g_updateDownloadPath, ONLN_UPDATE_MAX_DOWNLOAD);
    if (R_FAILED(result))
        return result;
    return PLUGIN_onln_ValidateDownloaded(api, selected, selected->remoteVersion, downloadSize);
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_InstallDownloadedUpdate(
    const MENUOnlineApi *api,
    const OnlnSelectedPlugin *selected,
    u32 downloadSize)
{
    return PLUGIN_onln_ReplaceAllOccurrences(api, selected, downloadSize);
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_InstallUpdate(
    const MENUOnlineApi *api,
    const OnlnSelectedPlugin *selected)
{
    u32 downloadSize = 0;
    Result result = PLUGIN_onln_DownloadUpdate(api, selected, &downloadSize);
    if (R_SUCCEEDED(result))
        result = PLUGIN_onln_InstallDownloadedUpdate(api, selected, downloadSize);
    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    return result;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_FormatCount(u32 value)
{
    u32 tens = 0;
    if (value > 99u)
        value = 99u;
    while (value >= 10u)
    {
        value -= 10u;
        tens++;
    }
    if (tens)
    {
        g_updateNumber[0] = (char)('0' + tens);
        g_updateNumber[1] = (char)('0' + value);
        g_updateNumber[2] = 0;
    }
    else
    {
        g_updateNumber[0] = (char)('0' + value);
        g_updateNumber[1] = 0;
    }
}

PLUGIN_CODE(onln) static void PLUGIN_onln_FormatId(u32 plgid)
{
    g_updateId[0] = (char)(plgid & 0xFFu);
    g_updateId[1] = (char)((plgid >> 8) & 0xFFu);
    g_updateId[2] = (char)((plgid >> 16) & 0xFFu);
    g_updateId[3] = (char)((plgid >> 24) & 0xFFu);
    g_updateId[4] = 0;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_BuildSourceUrl(
    const MENUOnlineApi *api,
    const char *name,
    char *out,
    u32 outSize
)
{
    const char *base;
    u32 position = 0;

    if (!api || !api->sourceUrlPrefix || !name || !out || outSize < 2u)
        return false;

    base = api->sourceUrlPrefix;
    while (*base)
    {
        if (position + 1u >= outSize)
            return false;
        out[position++] = *base++;
    }

    if (!position)
        return false;
    if (out[position - 1u] != '/')
    {
        if (position + 1u >= outSize)
            return false;
        out[position++] = '/';
    }

    while (*name)
    {
        if (position + 1u >= outSize)
            return false;
        out[position++] = *name++;
    }

    out[position] = 0;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_AppendText(char *out, u32 outSize, u32 *position, const char *text)
{
    u32 pos;
    if (!out || !outSize || !position || !text)
        return false;
    pos = *position;
    while (*text)
    {
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = *text++;
    }
    out[pos] = 0;
    *position = pos;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_AppendId(char *out, u32 outSize, u32 *position, u32 plgid)
{
    u32 pos;
    if (!out || !outSize || !position)
        return false;
    pos = *position;
    if (pos + 5u > outSize)
        return false;
    out[pos++] = (char)(plgid & 0xFFu);
    out[pos++] = (char)((plgid >> 8) & 0xFFu);
    out[pos++] = (char)((plgid >> 16) & 0xFFu);
    out[pos++] = (char)((plgid >> 24) & 0xFFu);
    out[pos] = 0;
    *position = pos;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_FormatVersionText(char *out, u32 outSize, u32 version)
{
    char digits[10];
    u32 digitCount = 0;
    bool started = false;
    u32 value = version;
    u32 pos = 0;

    for (u32 i = 0; i < 10u; i++)
    {
        u32 power = g_updateDecimalPowers[i];
        u32 digit = 0;
        while (value >= power)
        {
            value -= power;
            digit++;
        }
        if (digit || started || power <= 100u)
        {
            started = true;
            digits[digitCount++] = (char)('0' + digit);
        }
    }

    if (!PLUGIN_onln_AppendText(out, outSize, &pos, g_updateVersionPrefix))
        return false;
    for (u32 i = 0; i < digitCount; i++)
    {
        if (i + 2u == digitCount)
        {
            if (pos + 1u >= outSize)
                return false;
            out[pos++] = '.';
            out[pos] = 0;
        }
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = digits[i];
        out[pos] = 0;
    }
    return true;
}

PLUGIN_CODE(onln) static const char *PLUGIN_onln_ModuleName(u32 magic)
{
    return magic == ONLN_LOADER_MAGIC ? g_updateLoaderName : g_updateRosalinaName;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_BuildUpdateLine(
    const OnlnSelectedPlugin *plugin,
    char *out,
    u32 outSize)
{
    char localVersion[16];
    char remoteVersion[16];
    u32 pos = 0;

    if (!plugin || !out || !outSize)
        return false;
    out[0] = 0;

    if (!PLUGIN_onln_FormatVersionText(remoteVersion, sizeof(remoteVersion), plugin->remoteVersion))
        return false;
    if (plugin->hasLocalVersion &&
        !PLUGIN_onln_FormatVersionText(localVersion, sizeof(localVersion), plugin->localVersion))
        return false;

    return PLUGIN_onln_AppendId(out, outSize, &pos, plugin->plgid) &&
           PLUGIN_onln_AppendText(out, outSize, &pos,
                                  plugin->magic == ONLN_LOADER_MAGIC ? g_updateOpenLoaderModule : g_updateOpenModule) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, PLUGIN_onln_ModuleName(plugin->magic)) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, g_updateCloseModule) &&
           PLUGIN_onln_AppendText(out, outSize, &pos,
                                  plugin->hasLocalVersion ? localVersion : g_updateNullVersion) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, g_updateArrow) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, remoteVersion);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_Listed(const OnlnSelectedPlugin *plugin, bool showAll)
{
    return plugin && plugin->hasRemote &&
           (showAll || !plugin->hasLocalVersion || plugin->remoteVersion > plugin->localVersion);
}

PLUGIN_CODE(onln) static u32 PLUGIN_onln_CountListed(bool showAll)
{
    u32 count = 0;
    for (u32 i = 0; i < g_updateLoaderCount; i++)
        if (PLUGIN_onln_Listed(&g_updateLoader[i], showAll))
            count++;
    for (u32 i = 0; i < g_updateRosalinaCount; i++)
        if (PLUGIN_onln_Listed(&g_updateRosalina[i], showAll))
            count++;
    return count;
}

PLUGIN_CODE(onln) static OnlnSelectedPlugin *PLUGIN_onln_GetListed(u32 index, bool showAll)
{
    for (u32 i = 0; i < g_updateLoaderCount; i++)
    {
        if (!PLUGIN_onln_Listed(&g_updateLoader[i], showAll))
            continue;
        if (!index)
            return &g_updateLoader[i];
        index--;
    }
    for (u32 i = 0; i < g_updateRosalinaCount; i++)
    {
        if (!PLUGIN_onln_Listed(&g_updateRosalina[i], showAll))
            continue;
        if (!index)
            return &g_updateRosalina[i];
        index--;
    }
    return NULL;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateFrame(const MENUOnlineApi *api)
{
    api->drawString(10, 8, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 8, MENU_ONLINE_BORDER_COLOR, g_updateWideRail);
    api->drawString(196, 8, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(10, 16, MENU_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(196, 16, MENU_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(10, 24, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 24, MENU_ONLINE_BORDER_COLOR, g_updateWideRail);
    api->drawString(196, 24, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(22, 16, MENU_ONLINE_TITLE_COLOR, g_updateTitle);
    PLUGIN_onln_DrawDevMarker(api);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateStatus(
    const MENUOnlineApi *api,
    const char *message,
    const OnlnSelectedPlugin *plugin,
    u32 index,
    u32 total,
    Result result)
{
    char line[48];
    u32 pos = 0;

    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawUpdateFrame(api);
    api->drawString(20, 46, COLOR_WHITE, message);
    if (plugin)
    {
        line[0] = 0;
        if (PLUGIN_onln_AppendId(line, sizeof(line), &pos, plugin->plgid) &&
            PLUGIN_onln_AppendText(line, sizeof(line), &pos,
                                   plugin->magic == ONLN_LOADER_MAGIC ? g_updateOpenLoaderModule : g_updateOpenModule) &&
            PLUGIN_onln_AppendText(line, sizeof(line), &pos, PLUGIN_onln_ModuleName(plugin->magic)) &&
            PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateCloseOnly))
            api->drawString(20, 68, COLOR_CYAN, line);

        if (total)
        {
            pos = 0;
            PLUGIN_onln_FormatCount(index);
            if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateNumber) &&
                PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateSlash))
            {
                PLUGIN_onln_FormatCount(total);
                if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateNumber))
                    api->drawString(190, 68, COLOR_GRAY, line);
            }
        }
    }
    if (message == g_updateInstalled)
        api->drawString(20, 92, COLOR_YELLOW, g_updateRestart);
    else if (R_FAILED(result))
    {
        PLUGIN_onln_FormatResult(result);
        api->drawString(20, 92, COLOR_RED, g_onlineResultPrefix);
        api->drawString(80, 92, COLOR_RED, g_onlineResultHex);
    }
    api->drawString(20, 120, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static u16 PLUGIN_onln_UpdateItemColor(const OnlnSelectedPlugin *plugin)
{
    if (!plugin || !plugin->hasLocalVersion)
        return ONLN_UPDATE_NEWER_COLOR;
    if (plugin->remoteVersion > plugin->localVersion)
        return ONLN_UPDATE_NEWER_COLOR;
    if (plugin->remoteVersion < plugin->localVersion)
        return ONLN_UPDATE_OLDER_COLOR;
    return COLOR_WHITE;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateItem(
    const MENUOnlineApi *api,
    u32 itemIndex,
    u32 y,
    bool selected,
    bool showAll,
    u32 available,
    bool hasInstallAll,
    u32 installAllIndex)
{
    char line[64];
    const char *text = g_updateAll;
    u16 color = selected ? COLOR_CYAN : COLOR_WHITE;

    if (itemIndex < available)
    {
        OnlnSelectedPlugin *plugin = PLUGIN_onln_GetListed(itemIndex, showAll);
        if (!plugin || !PLUGIN_onln_BuildUpdateLine(plugin, line, sizeof(line)))
            return;
        text = line;
        color = PLUGIN_onln_UpdateItemColor(plugin);
    }
    else if (!hasInstallAll || itemIndex != installAllIndex)
        return;

    if (selected)
        api->drawString(12, y, COLOR_CYAN, g_onlineCursor);
    api->drawString(24, y, color, text);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateList(
    const MENUOnlineApi *api,
    u32 first,
    u32 selected,
    bool showAll)
{
    u32 available = PLUGIN_onln_CountListed(showAll);
    bool hasInstallAll = available != 0u;
    u32 installAllIndex = available;
    u32 total = available + (hasInstallAll ? 1u : 0u);
    u32 shown = total - first;

    if (shown > ONLN_UPDATE_VISIBLE_ITEMS)
        shown = ONLN_UPDATE_VISIBLE_ITEMS;

    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawUpdateFrame(api);

    if (!total)
        api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y, COLOR_GRAY, g_updateNoUpdates);

    if (first)
        api->drawString(24, ONLN_UPDATE_TOP_DOTS_Y, COLOR_GRAY, g_updateDots);

    for (u32 i = 0; i < shown; i++)
        PLUGIN_onln_DrawUpdateItem(
            api,
            first + i,
            ONLN_UPDATE_ITEM_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y,
            first + i == selected,
            showAll,
            available,
            hasInstallAll,
            installAllIndex
        );

    if (first + shown < total)
        api->drawString(24,
                        ONLN_UPDATE_ITEM_TOP_Y + ONLN_UPDATE_VISIBLE_ITEMS * ONLN_UPDATE_ITEM_SPACING_Y,
                        COLOR_GRAY,
                        g_updateDots);

    api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY,
                    showAll ? g_updateShowUpdates : g_updateShowAll);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RedrawUpdateSelection(
    const MENUOnlineApi *api,
    u32 first,
    u32 oldSelected,
    u32 selected,
    bool showAll)
{
    u32 available = PLUGIN_onln_CountListed(showAll);
    bool hasInstallAll = available != 0u;
    u32 installAllIndex = available;
    u32 oldY = ONLN_UPDATE_ITEM_TOP_Y + (oldSelected - first) * ONLN_UPDATE_ITEM_SPACING_Y;
    u32 newY = ONLN_UPDATE_ITEM_TOP_Y + (selected - first) * ONLN_UPDATE_ITEM_SPACING_Y;

    api->drawLock();
    api->drawString(10, oldY, COLOR_BLACK, g_updateClearRow);
    api->drawString(10, newY, COLOR_BLACK, g_updateClearRow);
    PLUGIN_onln_DrawUpdateItem(
        api, oldSelected, oldY, false, showAll, available, hasInstallAll, installAllIndex
    );
    PLUGIN_onln_DrawUpdateItem(
        api, selected, newY, true, showAll, available, hasInstallAll, installAllIndex
    );
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateAllSummary(
    const MENUOnlineApi *api,
    u32 updated,
    u32 failed)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawUpdateFrame(api);
    api->drawString(20, 42, COLOR_GREEN, g_updateAllDone);
    api->drawString(20, 66, COLOR_WHITE, g_updateUpdatedLabel);
    PLUGIN_onln_FormatCount(updated);
    api->drawString(86, 66, COLOR_WHITE, g_updateNumber);
    api->drawString(20, 84, failed ? COLOR_RED : COLOR_WHITE, g_updateFailedLabel);
    PLUGIN_onln_FormatCount(failed);
    api->drawString(86, 84, failed ? COLOR_RED : COLOR_WHITE, g_updateNumber);
    if (updated)
        api->drawString(20, 106, COLOR_YELLOW, g_updateRestart);
    api->drawString(20, 128, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_WaitBack(const MENUOnlineApi *api)
{
    do
    {
        if (api->waitInputWithTimeout(50) & KEY_B)
            return;
    } while (!*api->menuShouldExit);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_MarkInstalled(OnlnSelectedPlugin *plugin)
{
    if (!plugin)
        return;
    plugin->localVersion = plugin->remoteVersion;
    plugin->hasLocalVersion = true;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_UpdateAll(
    const MENUOnlineApi *api,
    bool *installedAny,
    bool showAll)
{
    u32 total = PLUGIN_onln_CountListed(showAll);
    u32 position = 0;
    u32 updated = 0;
    u32 failed = 0;

    for (u32 pass = 0; pass < 2u && !*api->menuShouldExit; pass++)
    {
        OnlnSelectedPlugin *plugins = pass == 0 ? g_updateLoader : g_updateRosalina;
        u32 count = pass == 0 ? g_updateLoaderCount : g_updateRosalinaCount;

        for (u32 i = 0; i < count && !*api->menuShouldExit; i++)
        {
            OnlnSelectedPlugin *plugin = &plugins[i];
            Result result;
            if (!PLUGIN_onln_Listed(plugin, showAll))
                continue;

            position++;
            PLUGIN_onln_DrawUpdateStatus(api, g_updateUpdating, plugin, position, total, 0);
            result = PLUGIN_onln_InstallUpdate(api, plugin);
            if (R_FAILED(result))
            {
                failed++;
                continue;
            }

            PLUGIN_onln_MarkInstalled(plugin);
            updated++;
            if (installedAny)
                *installedAny = true;
        }
    }

    if (!*api->menuShouldExit)
    {
        PLUGIN_onln_DrawUpdateAllSummary(api, updated, failed);
        PLUGIN_onln_WaitBack(api);
    }
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunUpdateList(const MENUOnlineApi *api)
{
    u32 selected = 0;
    u32 first = 0;
    bool installedAny = false;
    bool showAll = false;
    bool redraw = true;

    for (;;)
    {
        u32 available = PLUGIN_onln_CountListed(showAll);
        bool hasInstallAll = available != 0u;
        u32 installAllIndex = available;
        u32 total = available + (hasInstallAll ? 1u : 0u);

        if (*api->menuShouldExit)
            return;

        if (!total)
        {
            selected = 0;
            first = 0;
        }
        else
        {
            if (selected >= total)
                selected = total - 1u;
            if (total <= ONLN_UPDATE_VISIBLE_ITEMS)
                first = 0;
            else
            {
                if (first > selected)
                    first = selected;
                if (selected >= first + ONLN_UPDATE_VISIBLE_ITEMS)
                    first = selected - ONLN_UPDATE_VISIBLE_ITEMS + 1u;
                if (first > total - ONLN_UPDATE_VISIBLE_ITEMS)
                    first = total - ONLN_UPDATE_VISIBLE_ITEMS;
            }
        }

        if (redraw)
        {
            PLUGIN_onln_DrawUpdateList(api, first, selected, showAll);
            redraw = false;
        }

        {
            u32 pressed = api->waitInputWithTimeout(50);
            if (pressed & KEY_B)
                return;
            if (pressed & KEY_Y)
            {
                showAll = !showAll;
                selected = 0;
                first = 0;
                redraw = true;
                continue;
            }
            if (!total)
                continue;
            if (pressed & KEY_DOWN)
            {
                u32 oldSelected = selected;
                u32 oldFirst = first;

                if (selected + 1u >= total)
                {
                    selected = 0;
                    first = 0;
                }
                else
                {
                    selected++;
                    if (selected >= first + ONLN_UPDATE_VISIBLE_ITEMS)
                        first++;
                }

                if (first != oldFirst)
                    redraw = true;
                else if (selected != oldSelected)
                    PLUGIN_onln_RedrawUpdateSelection(api, first, oldSelected, selected, showAll);
                continue;
            }
            if (pressed & KEY_UP)
            {
                u32 oldSelected = selected;
                u32 oldFirst = first;

                if (!selected)
                {
                    selected = total - 1u;
                    first = total > ONLN_UPDATE_VISIBLE_ITEMS ? total - ONLN_UPDATE_VISIBLE_ITEMS : 0u;
                }
                else
                {
                    selected--;
                    if (selected < first)
                        first--;
                }

                if (first != oldFirst)
                    redraw = true;
                else if (selected != oldSelected)
                    PLUGIN_onln_RedrawUpdateSelection(api, first, oldSelected, selected, showAll);
                continue;
            }
            if (pressed & KEY_A)
            {
                if (hasInstallAll && selected == installAllIndex)
                {
                    PLUGIN_onln_UpdateAll(api, &installedAny, showAll);
                    redraw = true;
                    continue;
                }
                else
                {
                    OnlnSelectedPlugin *plugin;
                    plugin = PLUGIN_onln_GetListed(selected, showAll);
                    Result result;
                    if (!plugin)
                        continue;

                    PLUGIN_onln_DrawUpdateStatus(api, g_updateUpdating, plugin, 0, 0, 0);
                    result = PLUGIN_onln_InstallUpdate(api, plugin);
                    if (R_FAILED(result))
                    {
                        PLUGIN_onln_DrawUpdateStatus(api, g_updateUpdating, plugin, 0, 0, result);
                        PLUGIN_onln_WaitBack(api);
                        redraw = true;
                        continue;
                    }

                    PLUGIN_onln_MarkInstalled(plugin);
                    installedAny = true;
                    PLUGIN_onln_DrawUpdateStatus(api, g_updateInstalled, plugin, 0, 0, 0);
                    PLUGIN_onln_WaitBack(api);
                    redraw = true;
                    continue;
                }
            }
        }
    }
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawStackFrame(const MENUOnlineApi *api)
{
    api->drawString(10, 8, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 8, MENU_ONLINE_BORDER_COLOR, g_updateWideRail);
    api->drawString(196, 8, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(10, 16, MENU_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(196, 16, MENU_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(10, 24, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 24, MENU_ONLINE_BORDER_COLOR, g_updateWideRail);
    api->drawString(196, 24, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(22, 16, MENU_ONLINE_TITLE_COLOR, g_stackTitle);
    PLUGIN_onln_DrawDevMarker(api);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ValidPluginIdBytes(const char *bytes)
{
    for (u32 i = 0; i < 4u; i++)
    {
        char c = bytes[i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParseStackDescriptor(const char *bytes, OnlnStackId *out)
{
    if (!bytes || !out || !PLUGIN_onln_ValidPluginIdBytes(bytes))
        return false;
    out->plgid = (u32)(u8)bytes[0] |
                 ((u32)(u8)bytes[1] << 8) |
                 ((u32)(u8)bytes[2] << 16) |
                 ((u32)(u8)bytes[3] << 24);
    if (bytes[4] == ':')
        out->magic = ONLN_LOADER_MAGIC;
    else if (bytes[4] == ';')
        out->magic = ONLN_ROSALINA_MAGIC;
    else
        return false;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_StackIdEqual(const OnlnStackId *a, const OnlnStackId *b)
{
    return a && b && a->plgid == b->plgid && a->magic == b->magic;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ReadStackFilename(
    const MENUOnlineApi *api,
    const OnlnStackListing *stack,
    char *out,
    u32 outSize,
    bool display)
{
    u32 wanted;
    u32 readLength;
    u32 filenameAt;

    if (!stack || !out || !outSize || !stack->filenameLength ||
        !PLUGIN_onln_Add32(stack->urlOffset, stack->filenameOffset, &filenameAt))
        return false;

    wanted = stack->filenameLength;
    if (!display)
    {
        if (wanted + 1u > outSize)
            return false;
        if (!PLUGIN_onln_ReadExact(api, g_stackManifestPath, filenameAt, out, wanted))
            return false;
        out[wanted] = 0;
        return true;
    }

    if (outSize < 4u)
        return false;
    readLength = wanted;
    if (readLength > ONLN_STACK_DISPLAY_NAME_MAX)
        readLength = ONLN_STACK_DISPLAY_NAME_MAX - 3u;
    if (readLength + 1u > outSize)
        return false;
    if (!PLUGIN_onln_ReadExact(api, g_stackManifestPath, filenameAt, out, readLength))
        return false;
    if (wanted > ONLN_STACK_DISPLAY_NAME_MAX)
    {
        out[readLength++] = '.';
        out[readLength++] = '.';
        out[readLength++] = '.';
    }
    out[readLength] = 0;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_StackFilenameValid(const char *name)
{
    u32 priority;
    u32 length = 0;

    if (!name || !PLUGIN_onln_ParsePriority(name, &priority))
        return false;
    while (name[length])
    {
        if (name[length] == '/' || name[length] == '\\' || name[length] == '?' || name[length] == '#')
            return false;
        if (length + 1u >= MENU_ONLINE_DIR_NAME_CAP)
            return false;
        length++;
    }
    if (length < 4u || name[length - 4u] != '.' || name[length - 3u] != '3' ||
        name[length - 2u] != 'n' || name[length - 1u] != 'x')
        return false;
    return true;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ApplyStackManifestLine(
    const MENUOnlineApi *api,
    u32 lineStart,
    u32 lineLength,
    char lastCharacter)
{
    char countBytes[2];
    char descriptor[5];
    char urlPrefix[8];
    s32 high;
    s32 low;
    u32 pluginCount;
    u32 descriptorBytes;
    u32 urlOffset;
    u32 urlLength;
    u32 filenameOffset = 0;
    u32 filenameLength;
    u32 newLoader = 0;
    u32 newRosalina = 0;

    if (lineLength && lastCharacter == '\r')
        lineLength--;
    if (!lineLength)
        return 0;
    if (lineLength < 2u + 5u + 8u ||
        !PLUGIN_onln_ReadExact(api, g_stackManifestPath, lineStart, countBytes, sizeof(countBytes)))
        return ONLN_STACK_BAD_FORMAT;

    high = PLUGIN_onln_HexDigit(countBytes[0]);
    low = PLUGIN_onln_HexDigit(countBytes[1]);
    if (high < 0 || low < 0)
        return ONLN_STACK_BAD_FORMAT;
    pluginCount = ((u32)high << 4) | (u32)low;
    if (!pluginCount || pluginCount > ONLN_STACK_MAX_PLUGINS)
        return ONLN_STACK_BAD_FORMAT;
    descriptorBytes = pluginCount * 5u;
    if (lineLength <= 2u + descriptorBytes)
        return ONLN_STACK_BAD_FORMAT;

    for (u32 i = 0; i < pluginCount; i++)
    {
        u32 descriptorAt = lineStart + 2u + i * 5u;
        if (!PLUGIN_onln_ReadExact(api, g_stackManifestPath, descriptorAt, descriptor, sizeof(descriptor)) ||
            !PLUGIN_onln_ParseStackDescriptor(descriptor, &g_stackDeclared[i]))
            return ONLN_STACK_BAD_FORMAT;
        for (u32 j = 0; j < i; j++)
            if (PLUGIN_onln_StackIdEqual(&g_stackDeclared[i], &g_stackDeclared[j]))
                return ONLN_STACK_BAD_FORMAT;

        if (!PLUGIN_onln_FindSelected(g_stackDeclared[i].magic, g_stackDeclared[i].plgid))
        {
            if (g_stackDeclared[i].magic == ONLN_LOADER_MAGIC)
                newLoader++;
            else
                newRosalina++;
        }
    }

    if (newLoader > ONLN_MAX_SELECTED || newRosalina > ONLN_MAX_SELECTED)
        return ONLN_STACK_BAD_FORMAT;

    urlOffset = lineStart + 2u + descriptorBytes;
    urlLength = lineLength - 2u - descriptorBytes;
    if (urlLength < 8u || urlLength >= ONLN_UPDATE_URL_CAP ||
        !PLUGIN_onln_ReadExact(api, g_stackManifestPath, urlOffset, urlPrefix, sizeof(urlPrefix)) ||
        urlPrefix[0] != 'h' || urlPrefix[1] != 't' || urlPrefix[2] != 't' || urlPrefix[3] != 'p' ||
        urlPrefix[4] != 's' || urlPrefix[5] != ':' || urlPrefix[6] != '/' || urlPrefix[7] != '/')
        return ONLN_STACK_BAD_FORMAT;

    for (u32 i = 0; i < urlLength; i++)
    {
        char c;
        if (!PLUGIN_onln_ReadExact(api, g_stackManifestPath, urlOffset + i, &c, 1u))
            return ONLN_STACK_BAD_FORMAT;
        if (c == '?' || c == '#')
            return ONLN_STACK_BAD_FORMAT;
        if (c == '/')
            filenameOffset = i + 1u;
    }
    filenameLength = urlLength - filenameOffset;
    if (!filenameLength || filenameLength >= MENU_ONLINE_DIR_NAME_CAP)
        return ONLN_STACK_BAD_FORMAT;

    if (newLoader || newRosalina)
    {
        OnlnStackListing *stack;
        if (g_stackCount >= ONLN_STACK_MAX_LIST)
            return ONLN_STACK_BAD_FORMAT;
        stack = &g_stackList[g_stackCount];
        stack->lineOffset = lineStart;
        stack->lineLength = lineLength;
        stack->urlOffset = urlOffset;
        stack->urlLength = urlLength;
        stack->filenameOffset = (u16)filenameOffset;
        stack->filenameLength = (u16)filenameLength;
        stack->pluginCount = (u8)pluginCount;
        stack->newLoader = (u8)newLoader;
        stack->newRosalina = (u8)newRosalina;
        if (!PLUGIN_onln_ReadStackFilename(api, stack, g_stackFilename, sizeof(g_stackFilename), false) ||
            !PLUGIN_onln_StackFilenameValid(g_stackFilename) ||
            !PLUGIN_onln_ReadStackFilename(api, stack, stack->displayName, sizeof(stack->displayName), true))
            return ONLN_STACK_BAD_FORMAT;
        g_stackCount++;
    }
    else
    {
        OnlnStackListing temporary;
        temporary.urlOffset = urlOffset;
        temporary.filenameOffset = (u16)filenameOffset;
        temporary.filenameLength = (u16)filenameLength;
        if (!PLUGIN_onln_ReadStackFilename(api, &temporary, g_stackFilename, sizeof(g_stackFilename), false) ||
            !PLUGIN_onln_StackFilenameValid(g_stackFilename))
            return ONLN_STACK_BAD_FORMAT;
    }
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ParseStackManifest(const MENUOnlineApi *api)
{
    u32 fileSize = 0;
    u32 fileOffset = 0;
    u32 lineStart = 0;
    u32 lineLength = 0;
    char lastCharacter = 0;
    Result result = api->getFileSize(g_stackManifestPath, &fileSize);

    g_stackCount = 0;
    if (R_FAILED(result) || !fileSize || fileSize > ONLN_UPDATE_MANIFEST_MAX)
        return R_FAILED(result) ? result : ONLN_STACK_BAD_FORMAT;

    while (fileOffset < fileSize)
    {
        u32 want = fileSize - fileOffset;
        u32 got = 0;
        if (want > sizeof(g_updateIo))
            want = sizeof(g_updateIo);
        result = api->readFile(g_stackManifestPath, fileOffset, g_updateIo, want, &got);
        if (R_FAILED(result) || !got || got > want)
            return R_FAILED(result) ? result : ONLN_STACK_BAD_FORMAT;
        for (u32 i = 0; i < got; i++)
        {
            char c = (char)g_updateIo[i];
            if (c == '\n')
            {
                result = PLUGIN_onln_ApplyStackManifestLine(api, lineStart, lineLength, lastCharacter);
                if (R_FAILED(result))
                    return result;
                lineStart = fileOffset + i + 1u;
                lineLength = 0;
                lastCharacter = 0;
                continue;
            }
            lineLength++;
            lastCharacter = c;
            if (lineLength > 2u + ONLN_STACK_MAX_PLUGINS * 5u + ONLN_UPDATE_URL_CAP)
                return ONLN_STACK_BAD_FORMAT;
        }
        fileOffset += got;
    }
    if (lineLength)
        return PLUGIN_onln_ApplyStackManifestLine(api, lineStart, lineLength, lastCharacter);
    return 0;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_LoadStackDeclared(
    const MENUOnlineApi *api,
    const OnlnStackListing *stack)
{
    char descriptor[5];
    if (!stack || !stack->pluginCount || stack->pluginCount > ONLN_STACK_MAX_PLUGINS)
        return false;
    for (u32 i = 0; i < stack->pluginCount; i++)
    {
        if (!PLUGIN_onln_ReadExact(api, g_stackManifestPath, stack->lineOffset + 2u + i * 5u,
                                   descriptor, sizeof(descriptor)) ||
            !PLUGIN_onln_ParseStackDescriptor(descriptor, &g_stackDeclared[i]))
            return false;
    }
    return true;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawStackItem(
    const MENUOnlineApi *api,
    u32 index,
    u32 y,
    bool selected)
{
    if (index >= g_stackCount)
        return;
    if (selected)
        api->drawString(12, y, COLOR_CYAN, g_onlineCursor);
    api->drawString(24, y, COLOR_WHITE, g_stackList[index].displayName);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawStackList(
    const MENUOnlineApi *api,
    u32 first,
    u32 selected)
{
    u32 shown = g_stackCount > first ? g_stackCount - first : 0u;
    if (shown > ONLN_UPDATE_VISIBLE_ITEMS)
        shown = ONLN_UPDATE_VISIBLE_ITEMS;

    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawStackFrame(api);
    if (!g_stackCount)
        api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y, COLOR_GRAY, g_stackNoNew);
    if (first)
        api->drawString(24, ONLN_UPDATE_TOP_DOTS_Y, COLOR_GRAY, g_updateDots);
    for (u32 i = 0; i < shown; i++)
        PLUGIN_onln_DrawStackItem(api, first + i,
                                  ONLN_UPDATE_ITEM_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y,
                                  first + i == selected);
    if (first + shown < g_stackCount)
        api->drawString(24,
                        ONLN_UPDATE_ITEM_TOP_Y + ONLN_UPDATE_VISIBLE_ITEMS * ONLN_UPDATE_ITEM_SPACING_Y,
                        COLOR_GRAY, g_updateDots);
    api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RedrawStackSelection(
    const MENUOnlineApi *api,
    u32 first,
    u32 oldSelected,
    u32 selected)
{
    u32 oldY = ONLN_UPDATE_ITEM_TOP_Y + (oldSelected - first) * ONLN_UPDATE_ITEM_SPACING_Y;
    u32 newY = ONLN_UPDATE_ITEM_TOP_Y + (selected - first) * ONLN_UPDATE_ITEM_SPACING_Y;

    api->drawLock();
    api->drawString(10, oldY, COLOR_BLACK, g_updateClearRow);
    api->drawString(10, newY, COLOR_BLACK, g_updateClearRow);
    PLUGIN_onln_DrawStackItem(api, oldSelected, oldY, false);
    PLUGIN_onln_DrawStackItem(api, selected, newY, true);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_BuildCapacityLine(
    const char *prefix,
    u32 selectedCount,
    u32 newCount)
{
    u32 pos = 0;
    g_stackCapacityLine[0] = 0;
    if (!PLUGIN_onln_AppendText(g_stackCapacityLine, sizeof(g_stackCapacityLine), &pos, prefix))
        return false;
    PLUGIN_onln_FormatCount(selectedCount);
    if (!PLUGIN_onln_AppendText(g_stackCapacityLine, sizeof(g_stackCapacityLine), &pos, g_updateNumber) ||
        !PLUGIN_onln_AppendText(g_stackCapacityLine, sizeof(g_stackCapacityLine), &pos, g_stackNewText))
        return false;
    PLUGIN_onln_FormatCount(newCount);
    return PLUGIN_onln_AppendText(g_stackCapacityLine, sizeof(g_stackCapacityLine), &pos, g_updateNumber) &&
           PLUGIN_onln_AppendText(g_stackCapacityLine, sizeof(g_stackCapacityLine), &pos, g_stackOverText);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_StackFitsSelected(const OnlnStackListing *stack)
{
    return stack &&
           g_updateLoaderCount + stack->newLoader <= ONLN_MAX_SELECTED &&
           g_updateRosalinaCount + stack->newRosalina <= ONLN_MAX_SELECTED;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawStackCapacityError(
    const MENUOnlineApi *api,
    const OnlnStackListing *stack)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawStackFrame(api);
    api->drawString(20, 44, COLOR_RED, g_stackCapacityTitle);
    if (stack && g_updateLoaderCount + stack->newLoader > ONLN_MAX_SELECTED &&
        PLUGIN_onln_BuildCapacityLine(g_stackLoaderPrefix, g_updateLoaderCount, stack->newLoader))
        api->drawString(20, 68, COLOR_WHITE, g_stackCapacityLine);
    if (stack && g_updateRosalinaCount + stack->newRosalina > ONLN_MAX_SELECTED &&
        PLUGIN_onln_BuildCapacityLine(g_stackRosalinaPrefix, g_updateRosalinaCount, stack->newRosalina))
        api->drawString(20, 86, COLOR_WHITE, g_stackCapacityLine);
    api->drawString(20, 112, COLOR_YELLOW, g_stackRemoveFirst);
    api->drawString(20, 136, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_BuildStackIdLine(
    const OnlnStackId *id,
    char *out,
    u32 outSize)
{
    u32 pos = 0;
    if (!id || !out || !outSize)
        return false;
    out[0] = 0;
    return PLUGIN_onln_AppendId(out, outSize, &pos, id->plgid) &&
           PLUGIN_onln_AppendText(out, outSize, &pos,
                                  id->magic == ONLN_LOADER_MAGIC ? g_updateOpenLoaderModule : g_updateOpenModule) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, PLUGIN_onln_ModuleName(id->magic)) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, g_updateCloseOnly);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_RunStackDetails(
    const MENUOnlineApi *api,
    const OnlnStackListing *stack)
{
    u32 first = 0;
    bool redraw = true;
    char line[48];

    if (!PLUGIN_onln_ReadStackFilename(api, stack, g_stackFilename, sizeof(g_stackFilename), false) ||
        !PLUGIN_onln_LoadStackDeclared(api, stack))
        return false;

    for (;;)
    {
        if (*api->menuShouldExit)
            return false;
        if (redraw)
        {
            u32 shown = stack->pluginCount - first;
            if (shown > ONLN_STACK_DETAIL_VISIBLE)
                shown = ONLN_STACK_DETAIL_VISIBLE;
            api->drawLock();
            api->drawClear();
            PLUGIN_onln_DrawStackFrame(api);
            if (PLUGIN_onln_ReadStackFilename(api, stack, g_stackDisplayName, sizeof(g_stackDisplayName), true))
                api->drawString(20, 34, COLOR_WHITE, g_stackDisplayName);
            api->drawString(20, 48, COLOR_GRAY, g_stackContains);
            if (first)
                api->drawString(280, 48, COLOR_GRAY, g_updateDots);
            for (u32 i = 0; i < shown; i++)
                if (PLUGIN_onln_BuildStackIdLine(&g_stackDeclared[first + i], line, sizeof(line)))
                    api->drawString(24, ONLN_STACK_DETAIL_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y,
                                    COLOR_WHITE, line);
            if (first + shown < stack->pluginCount)
                api->drawString(280, 198, COLOR_GRAY, g_updateDots);
            api->drawString(24, 209, COLOR_GREEN, g_stackDownload);
            api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY, g_updatePressBack);
            api->drawFlush();
            api->drawUnlock();
            redraw = false;
        }
        {
            u32 pressed = api->waitInputWithTimeout(50);
            if (pressed & KEY_B)
                return false;
            if (pressed & KEY_A)
                return true;
            if ((pressed & KEY_DOWN) && first + ONLN_STACK_DETAIL_VISIBLE < stack->pluginCount)
            {
                first++;
                redraw = true;
            }
            else if ((pressed & KEY_UP) && first)
            {
                first--;
                redraw = true;
            }
        }
    }
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ReadStackUrl(
    const MENUOnlineApi *api,
    const OnlnStackListing *stack)
{
    if (!stack || !stack->urlLength || stack->urlLength >= sizeof(g_updateUrl) ||
        !PLUGIN_onln_ReadExact(api, g_stackManifestPath, stack->urlOffset, g_updateUrl, stack->urlLength))
        return ONLN_STACK_BAD_FORMAT;
    g_updateUrl[stack->urlLength] = 0;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ValidateDownloadedStack(const MENUOnlineApi *api)
{
    u32 size = 0;
    u32 offset = 0;
    Result result = api->getFileSize(g_updateDownloadPath, &size);

    if (R_FAILED(result) || !size)
        return R_FAILED(result) ? result : ONLN_STACK_BAD_FILE;

    while (offset < size)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        if (!PLUGIN_onln_Read3nxHeader(api, g_updateDownloadPath, size, offset,
                                       &header, &metadataOffset, &nextOffset) ||
            nextOffset <= offset || nextOffset > size ||
            (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC))
            return ONLN_STACK_BAD_FILE;
        offset = nextOffset;
    }
    return offset == size ? 0 : ONLN_STACK_BAD_FILE;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_InstallDownloadedStack(
    const MENUOnlineApi *api,
    const char *target)
{
    Result result;
    bool exists = PLUGIN_onln_FileExists(api, target);

    if (!exists)
        return api->renameFile(g_updateDownloadPath, target);

    PLUGIN_onln_DeleteIfExists(api, g_updateBackupPath);
    result = PLUGIN_onln_WriteJournal(api, target);
    if (R_FAILED(result))
        return result;
    result = api->renameFile(target, g_updateBackupPath);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
        return result;
    }
    result = api->renameFile(g_updateDownloadPath, target);
    if (R_FAILED(result))
    {
        if (R_FAILED(api->renameFile(g_updateBackupPath, target)))
            return ONLN_UPDATE_SWAP_FAILED;
        PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
        return result;
    }
    PLUGIN_onln_DeleteIfExists(api, g_updateBackupPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
    return 0;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawStackStatus(
    const MENUOnlineApi *api,
    const char *message,
    const OnlnStackListing *stack,
    Result result)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawStackFrame(api);
    api->drawString(20, 46, R_FAILED(result) ? COLOR_RED : COLOR_WHITE, message);
    if (stack && PLUGIN_onln_ReadStackFilename(api, stack, g_stackDisplayName, sizeof(g_stackDisplayName), true))
        api->drawString(20, 70, COLOR_CYAN, g_stackDisplayName);
    if (R_FAILED(result))
    {
        PLUGIN_onln_FormatResult(result);
        api->drawString(20, 94, COLOR_RED, g_onlineResultPrefix);
        api->drawString(80, 94, COLOR_RED, g_onlineResultHex);
    }
    api->drawString(20, 120, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_InstallStack(
    const MENUOnlineApi *api,
    const OnlnStackListing *stack)
{
    char target[ONLN_UPDATE_PATH_CAP];
    Result result;

    if (!stack || !PLUGIN_onln_ReadStackFilename(api, stack, g_stackFilename, sizeof(g_stackFilename), false) ||
        !PLUGIN_onln_MakePluginPath(target, sizeof(target), g_stackFilename))
        return ONLN_STACK_BAD_FORMAT;
    result = PLUGIN_onln_ReadStackUrl(api, stack);
    if (R_FAILED(result))
        return result;

    PLUGIN_onln_DrawStackStatus(api, g_stackDownloading, stack, 0);
    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    result = api->downloadToFile(g_updateUrl, g_updateDownloadPath, ONLN_STACK_MAX_DOWNLOAD);
    if (R_FAILED(result))
        return result;

    PLUGIN_onln_DrawStackStatus(api, g_stackValidating, stack, 0);
    result = PLUGIN_onln_ValidateDownloadedStack(api);
    if (R_FAILED(result))
        goto finish;

    PLUGIN_onln_DrawStackStatus(api, g_stackInstalling, stack, 0);
    result = PLUGIN_onln_InstallDownloadedStack(api, target);

finish:
    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    return result;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunStackList(const MENUOnlineApi *api)
{
    u32 selected = 0;
    u32 first = 0;
    bool redraw = true;

    for (;;)
    {
        if (*api->menuShouldExit)
            return;
        if (!g_stackCount)
        {
            selected = 0;
            first = 0;
        }
        else
        {
            if (selected >= g_stackCount)
                selected = g_stackCount - 1u;
            if (g_stackCount <= ONLN_UPDATE_VISIBLE_ITEMS)
                first = 0;
            else
            {
                if (first > selected)
                    first = selected;
                if (selected >= first + ONLN_UPDATE_VISIBLE_ITEMS)
                    first = selected - ONLN_UPDATE_VISIBLE_ITEMS + 1u;
                if (first > g_stackCount - ONLN_UPDATE_VISIBLE_ITEMS)
                    first = g_stackCount - ONLN_UPDATE_VISIBLE_ITEMS;
            }
        }

        if (redraw)
        {
            PLUGIN_onln_DrawStackList(api, first, selected);
            redraw = false;
        }
        {
            u32 pressed = api->waitInputWithTimeout(50);
            if (pressed & KEY_B)
                return;
            if (!g_stackCount)
                continue;
            if (pressed & KEY_DOWN)
            {
                u32 oldSelected = selected;
                u32 oldFirst = first;
                if (selected + 1u >= g_stackCount)
                {
                    selected = 0;
                    first = 0;
                }
                else
                {
                    selected++;
                    if (selected >= first + ONLN_UPDATE_VISIBLE_ITEMS)
                        first++;
                }
                if (first != oldFirst)
                    redraw = true;
                else if (selected != oldSelected)
                    PLUGIN_onln_RedrawStackSelection(api, first, oldSelected, selected);
                continue;
            }
            if (pressed & KEY_UP)
            {
                u32 oldSelected = selected;
                u32 oldFirst = first;
                if (!selected)
                {
                    selected = g_stackCount - 1u;
                    first = g_stackCount > ONLN_UPDATE_VISIBLE_ITEMS ?
                            g_stackCount - ONLN_UPDATE_VISIBLE_ITEMS : 0u;
                }
                else
                {
                    selected--;
                    if (selected < first)
                        first--;
                }
                if (first != oldFirst)
                    redraw = true;
                else if (selected != oldSelected)
                    PLUGIN_onln_RedrawStackSelection(api, first, oldSelected, selected);
                continue;
            }
            if (pressed & KEY_A)
            {
                const OnlnStackListing *chosen = &g_stackList[selected];
                Result result;
                if (!PLUGIN_onln_StackFitsSelected(chosen))
                {
                    PLUGIN_onln_DrawStackCapacityError(api, chosen);
                    PLUGIN_onln_WaitBack(api);
                    redraw = true;
                    continue;
                }
                if (!PLUGIN_onln_RunStackDetails(api, chosen))
                {
                    redraw = true;
                    continue;
                }
                result = PLUGIN_onln_InstallStack(api, chosen);
                if (R_FAILED(result))
                {
                    PLUGIN_onln_DrawStackStatus(api, g_stackInstalling, chosen, result);
                    PLUGIN_onln_WaitBack(api);
                    redraw = true;
                    continue;
                }
                PLUGIN_onln_DrawStackStatus(api, g_stackInstalled, chosen, 0);
                api->drawLock();
                api->drawString(20, 96, COLOR_YELLOW, g_updateRestart);
                api->drawFlush();
                api->drawUnlock();
                PLUGIN_onln_WaitBack(api);

                result = PLUGIN_onln_ScanSelected(api);
                if (R_FAILED(result) || R_FAILED(PLUGIN_onln_ParseStackManifest(api)))
                    return;
                selected = 0;
                first = 0;
                redraw = true;
            }
        }
    }
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunFindNew(const MENUOnlineApi *api)
{
    Result result;

    PLUGIN_onln_DrawStackStatus(api, g_updateRecovering, NULL, 0);
    result = PLUGIN_onln_RecoverInterruptedUpdate(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawStackStatus(api, g_updateRecoverFailed, NULL, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    PLUGIN_onln_DrawStackStatus(api, g_stackScanning, NULL, 0);
    result = PLUGIN_onln_ScanSelected(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawStackStatus(api, g_updateScanFailed, NULL, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    PLUGIN_onln_DrawStackStatus(api, g_stackFetching, NULL, 0);
    PLUGIN_onln_DeleteIfExists(api, g_stackManifestPath);
    if (!PLUGIN_onln_BuildSourceUrl(api, g_stackManifestName, g_updateUrl, sizeof(g_updateUrl)))
        result = ONLN_STACK_BAD_FORMAT;
    else
        result = api->downloadToFile(g_updateUrl, g_stackManifestPath, ONLN_UPDATE_MANIFEST_MAX);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawStackStatus(api, g_stackManifestFailed, NULL, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }
    result = PLUGIN_onln_ParseStackManifest(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DeleteIfExists(api, g_stackManifestPath);
        PLUGIN_onln_DrawStackStatus(api, g_stackManifestFailed, NULL, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    PLUGIN_onln_RunStackList(api);
    PLUGIN_onln_DeleteIfExists(api, g_stackManifestPath);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunUpdates(const MENUOnlineApi *api)
{
    Result result;

    PLUGIN_onln_DrawUpdateStatus(api, g_updateRecovering, NULL, 0, 0, 0);
    result = PLUGIN_onln_RecoverInterruptedUpdate(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawUpdateStatus(api, g_updateRecoverFailed, NULL, 0, 0, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    PLUGIN_onln_DrawUpdateStatus(api, g_updateScanning, NULL, 0, 0, 0);
    result = PLUGIN_onln_ScanSelected(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawUpdateStatus(api, g_updateScanFailed, NULL, 0, 0, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    PLUGIN_onln_DrawUpdateStatus(api, g_updateFetching, NULL, 0, 0, 0);
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
    if (!PLUGIN_onln_BuildSourceUrl(api, g_updateManifestName, g_updateUrl, sizeof(g_updateUrl)))
        result = ONLN_UPDATE_BAD_FORMAT;
    else
        result = api->downloadToFile(g_updateUrl, g_updateManifestPath, ONLN_UPDATE_MANIFEST_MAX);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawUpdateStatus(api, g_updateManifestFailed, NULL, 0, 0, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    result = PLUGIN_onln_ParseManifest(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
        PLUGIN_onln_DrawUpdateStatus(api, g_updateManifestFailed, NULL, 0, 0, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    PLUGIN_onln_RunUpdateList(api);
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
}


PLUGIN_CODE(onln) static bool PLUGIN_onln_CopySpan(char *out, u32 outSize, const char *text, u32 length)
{
    if (!out || !outSize || !text || length + 1u > outSize)
        return false;
    for (u32 i = 0; i < length; i++)
    {
        if ((u8)text[i] < 0x20u || (u8)text[i] == 0x7Fu)
            return false;
        out[i] = text[i];
    }
    out[length] = 0;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_IsPackSeparator(const char *line, u32 length)
{
    return line && length == 3u && line[0] == '-' && line[1] == '-' && line[2] == '-';
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParsePackU8(const char *line, u32 length, u8 *value)
{
    u32 parsed = 0;
    if (!line || !value || !length || length > 3u)
        return false;
    for (u32 i = 0; i < length; i++)
    {
        if (line[i] < '0' || line[i] > '9')
            return false;
        parsed = parsed * 10u + (u32)(line[i] - '0');
        if (parsed > 255u)
            return false;
    }
    *value = (u8)parsed;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParsePackId(const char *line, u32 length, OnlnStackId *id)
{
    if (!line || !id || length != 6u || line[1] != ':' ||
        (line[0] != 'R' && line[0] != 'L') || !PLUGIN_onln_ValidPluginIdBytes(line + 2))
        return false;
    id->magic = line[0] == 'L' ? ONLN_LOADER_MAGIC : ONLN_ROSALINA_MAGIC;
    id->plgid = (u32)(u8)line[2] | ((u32)(u8)line[3] << 8) |
                ((u32)(u8)line[4] << 16) | ((u32)(u8)line[5] << 24);
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_PackOutputUsed(const char *output)
{
    for (u32 i = 0; i < g_packCount; i++)
        if (PLUGIN_onln_StringCompare(g_packs[i].output, output) == 0)
            return true;
    return false;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_FinishPack(OnlnPack *pack, u32 stage, u32 descriptionRead)
{
    if (!pack || stage < 3u || descriptionRead != pack->descriptionLines || !pack->componentCount ||
        !PLUGIN_onln_StackFilenameValid(pack->output) || PLUGIN_onln_PackOutputUsed(pack->output))
        return ONLN_PACK_BAD_FORMAT;
    for (u32 i = 0; i < pack->componentCount; i++)
    {
        if (!PLUGIN_onln_FindRemote(pack->components[i].magic, pack->components[i].plgid))
            return ONLN_PACK_BAD_FORMAT;
        for (u32 j = 0; j < i; j++)
            if (PLUGIN_onln_StackIdEqual(&pack->components[i], &pack->components[j]))
                return ONLN_PACK_BAD_FORMAT;
    }
    if (g_packCount >= ONLN_PACK_MAX)
        return ONLN_PACK_BAD_FORMAT;
    g_packCount++;
    return 0;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_PackPathStatus(
    const MENUOnlineApi *api,
    const OnlnPack *pack,
    const char *path,
    u32 *matchedOut,
    bool *subsetOnlyOut)
{
    bool found[ONLN_PACK_MAX_COMPONENTS];
    u32 size = 0;
    u32 offset = 0;
    u32 matched = 0;
    bool subsetOnly = true;

    if (!api || !pack || !path || R_FAILED(api->getFileSize(path, &size)) || !size)
        return false;

    for (u32 i = 0; i < ONLN_PACK_MAX_COMPONENTS; i++)
        ((volatile bool *)found)[i] = false;

    while (offset < size)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        bool belongs = false;

        if (!PLUGIN_onln_Read3nxHeader(api, path, size, offset, &header, &metadataOffset, &nextOffset) ||
            nextOffset <= offset || nextOffset > size ||
            (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC))
            return false;

        for (u32 i = 0; i < pack->componentCount; i++)
        {
            if (pack->components[i].magic != header.magic ||
                pack->components[i].plgid != header.plgid)
                continue;
            belongs = true;
            if (!found[i])
            {
                found[i] = true;
                matched++;
            }
            break;
        }

        if (!belongs)
            subsetOnly = false;
        offset = nextOffset;
    }

    if (matchedOut)
        *matchedOut = matched;
    if (subsetOnlyOut)
        *subsetOnlyOut = subsetOnly;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_PackOutputComplete(
    const MENUOnlineApi *api, const OnlnPack *pack)
{
    char path[ONLN_UPDATE_PATH_CAP];
    u32 matched = 0;
    bool subsetOnly = false;

    if (!api || !pack)
        return false;

    if (PLUGIN_onln_MakePluginPath(path, sizeof(path), pack->output) &&
        PLUGIN_onln_PackPathStatus(api, pack, path, &matched, &subsetOnly) &&
        matched == pack->componentCount)
        return true;

    for (u32 i = 0; i < pack->componentCount; i++)
    {
        OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(
            pack->components[i].magic, pack->components[i].plgid);
        if (!local)
            continue;
        if (PLUGIN_onln_PackPathStatus(api, pack, local->path, &matched, &subsetOnly) &&
            matched == pack->componentCount)
            return true;
    }
    return false;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_FindPackRebuildTarget(
    const MENUOnlineApi *api,
    const OnlnPack *pack,
    char *target,
    u32 targetSize)
{
    char outputPath[ONLN_UPDATE_PATH_CAP];
    u32 matched = 0;
    u32 bestMatched = 0;
    bool subsetOnly = false;
    bool found = false;

    if (!api || !pack || !target || !targetSize)
        return false;

    if (PLUGIN_onln_MakePluginPath(outputPath, sizeof(outputPath), pack->output) &&
        PLUGIN_onln_PackPathStatus(api, pack, outputPath, &matched, &subsetOnly) &&
        subsetOnly && matched && matched < pack->componentCount)
        return PLUGIN_onln_CopyString(target, targetSize, outputPath);

    for (u32 pass = 0; pass < 2u; pass++)
    {
        for (u32 i = 0; i < pack->componentCount; i++)
        {
            const OnlnStackId *id = &pack->components[i];
            OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(id->magic, id->plgid);
            OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(id->magic, id->plgid);
            bool needsUpdate = local && remote &&
                (!local->hasLocalVersion || remote->version > local->localVersion);

            if (!local || (!pass && !needsUpdate))
                continue;
            if (!PLUGIN_onln_PackPathStatus(api, pack, local->path, &matched, &subsetOnly) ||
                !subsetOnly || !matched || matched >= pack->componentCount)
                continue;
            if (!found || matched > bestMatched)
            {
                if (!PLUGIN_onln_CopyString(target, targetSize, local->path))
                    return false;
                bestMatched = matched;
                found = true;
            }
        }
        if (found)
            return true;
    }

    return PLUGIN_onln_MakePluginPath(target, targetSize, pack->output);
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ParsePackManifest(const MENUOnlineApi *api)
{
    u32 size = 0;
    u32 pos = 0;
    u32 stage = 0;
    u32 descriptionRead = 0;
    bool foundSeparator = false;
    Result result = api->getFileSize(g_updateManifestPath, &size);

    g_packCount = 0;
    if (R_FAILED(result) || !size || size > ONLN_PACK_MANIFEST_MAX)
        return R_FAILED(result) ? result : ONLN_PACK_BAD_FORMAT;
    if (!PLUGIN_onln_ReadExact(api, g_updateManifestPath, 0, g_packManifest, size))
        return ONLN_PACK_BAD_FORMAT;
    g_packManifest[size] = 0;

    while (pos <= size)
    {
        u32 start = pos;
        u32 length;
        OnlnPack *pack;
        while (pos < size && g_packManifest[pos] != '\n')
            pos++;
        length = pos - start;
        if (length && g_packManifest[start + length - 1u] == '\r')
            length--;
        if (pos < size)
            pos++;
        else if (start == size)
            break;

        if (!foundSeparator)
        {
            if (PLUGIN_onln_IsPackSeparator(g_packManifest + start, length))
                foundSeparator = true;
            continue;
        }

        if (!length)
        {
            if (!stage)
                continue;
            if (g_packCount >= ONLN_PACK_MAX)
                return ONLN_PACK_BAD_FORMAT;
            pack = &g_packs[g_packCount];
            if (stage >= 3u && descriptionRead < pack->descriptionLines)
            {
                if (!PLUGIN_onln_CopySpan(pack->description[descriptionRead],
                                          sizeof(pack->description[descriptionRead]),
                                          g_packManifest + start, length))
                    return ONLN_PACK_BAD_FORMAT;
                descriptionRead++;
                continue;
            }
            result = PLUGIN_onln_FinishPack(pack, stage, descriptionRead);
            if (R_FAILED(result))
                return result;
            stage = 0;
            descriptionRead = 0;
            continue;
        }

        if (g_packCount >= ONLN_PACK_MAX)
            return ONLN_PACK_BAD_FORMAT;
        pack = &g_packs[g_packCount];
        if (!stage)
        {
            pack->componentCount = 0;
            pack->descriptionLines = 0;
            descriptionRead = 0;
            if (!PLUGIN_onln_CopySpan(pack->title, sizeof(pack->title), g_packManifest + start, length))
                return ONLN_PACK_BAD_FORMAT;
            stage = 1;
        }
        else if (stage == 1u)
        {
            if (!PLUGIN_onln_CopySpan(pack->output, sizeof(pack->output), g_packManifest + start, length))
                return ONLN_PACK_BAD_FORMAT;
            stage = 2;
        }
        else if (stage == 2u)
        {
            if (!PLUGIN_onln_ParsePackU8(g_packManifest + start, length, &pack->descriptionLines) ||
                pack->descriptionLines > ONLN_PACK_DESC_MAX_LINES)
                return ONLN_PACK_BAD_FORMAT;
            stage = 3;
        }
        else if (descriptionRead < pack->descriptionLines)
        {
            if (!PLUGIN_onln_CopySpan(pack->description[descriptionRead],
                                      sizeof(pack->description[descriptionRead]),
                                      g_packManifest + start, length))
                return ONLN_PACK_BAD_FORMAT;
            descriptionRead++;
        }
        else
        {
            if (pack->componentCount >= ONLN_PACK_MAX_COMPONENTS ||
                !PLUGIN_onln_ParsePackId(g_packManifest + start, length,
                                         &pack->components[pack->componentCount]))
                return ONLN_PACK_BAD_FORMAT;
            pack->componentCount++;
            stage = 4;
        }
    }

    if (!foundSeparator)
        return ONLN_PACK_BAD_FORMAT;
    if (stage)
    {
        result = PLUGIN_onln_FinishPack(&g_packs[g_packCount], stage, descriptionRead);
        if (R_FAILED(result))
            return result;
    }
    if (!g_packCount)
        return ONLN_PACK_BAD_FORMAT;
    for (u32 i = 0; i < g_packCount; i++)
        g_packs[i].outputComplete = PLUGIN_onln_PackOutputComplete(api, &g_packs[i]);
    return 0;
}

PLUGIN_CODE(onln) static u32 PLUGIN_onln_PackState(const OnlnPack *pack)
{
    bool hasUpdate = false;
    bool hasOlder = false;
    if (!pack)
        return ONLN_PACK_STATE_WHITE;
    for (u32 i = 0; i < pack->componentCount; i++)
    {
        const OnlnStackId *id = &pack->components[i];
        OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(id->magic, id->plgid);
        OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(id->magic, id->plgid);
        if (!local)
            return ONLN_PACK_STATE_BLUE;
        if (!remote)
            continue;
        if (!local->hasLocalVersion || remote->version > local->localVersion)
            hasUpdate = true;
        else if (remote->version < local->localVersion)
            hasOlder = true;
    }
    if (!pack->outputComplete || hasUpdate)
        return ONLN_PACK_STATE_GREEN;
    if (hasOlder)
        return ONLN_PACK_STATE_RED;
    return ONLN_PACK_STATE_WHITE;
}

PLUGIN_CODE(onln) static u16 PLUGIN_onln_PackColor(u32 state)
{
    if (state == ONLN_PACK_STATE_GREEN)
        return ONLN_UPDATE_NEWER_COLOR;
    if (state == ONLN_PACK_STATE_RED)
        return ONLN_UPDATE_OLDER_COLOR;
    if (state == ONLN_PACK_STATE_BLUE)
        return ONLN_PACK_MISSING_COLOR;
    return COLOR_WHITE;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_PackVisible(const OnlnPack *pack, bool showAll)
{
    u32 state = PLUGIN_onln_PackState(pack);
    return showAll || state == ONLN_PACK_STATE_GREEN || state == ONLN_PACK_STATE_BLUE;
}

PLUGIN_CODE(onln) static u32 PLUGIN_onln_CountPacks(bool showAll)
{
    u32 count = 0;
    for (u32 state = ONLN_PACK_STATE_GREEN; state <= ONLN_PACK_STATE_BLUE; state++)
        for (u32 i = 0; i < g_packCount; i++)
            if (PLUGIN_onln_PackVisible(&g_packs[i], showAll) && PLUGIN_onln_PackState(&g_packs[i]) == state)
                count++;
    return count;
}

PLUGIN_CODE(onln) static OnlnPack *PLUGIN_onln_GetPack(u32 index, bool showAll)
{
    for (u32 state = ONLN_PACK_STATE_GREEN; state <= ONLN_PACK_STATE_BLUE; state++)
    {
        for (u32 i = 0; i < g_packCount; i++)
        {
            if (!PLUGIN_onln_PackVisible(&g_packs[i], showAll) || PLUGIN_onln_PackState(&g_packs[i]) != state)
                continue;
            if (!index)
                return &g_packs[i];
            index--;
        }
    }
    return NULL;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawPackFrame(const MENUOnlineApi *api)
{
    api->drawString(10, 8, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 8, MENU_ONLINE_BORDER_COLOR, g_updateWideRail);
    api->drawString(196, 8, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(10, 16, MENU_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(196, 16, MENU_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(10, 24, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 24, MENU_ONLINE_BORDER_COLOR, g_updateWideRail);
    api->drawString(196, 24, MENU_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(22, 16, MENU_ONLINE_TITLE_COLOR, g_packTitle);
    PLUGIN_onln_DrawDevMarker(api);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawPackItem(
    const MENUOnlineApi *api, u32 index, u32 y, bool selected, bool showAll)
{
    OnlnPack *pack = PLUGIN_onln_GetPack(index, showAll);
    if (!pack)
        return;
    if (selected)
        api->drawString(12, y, COLOR_CYAN, g_onlineCursor);
    api->drawString(24, y, PLUGIN_onln_PackColor(PLUGIN_onln_PackState(pack)), pack->title);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawPackList(
    const MENUOnlineApi *api, u32 first, u32 selected, bool showAll)
{
    u32 total = PLUGIN_onln_CountPacks(showAll);
    u32 shown = total > first ? total - first : 0u;
    if (shown > ONLN_UPDATE_VISIBLE_ITEMS)
        shown = ONLN_UPDATE_VISIBLE_ITEMS;

    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawPackFrame(api);
    if (!total)
        api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y, COLOR_GRAY, g_packNoEntries);
    if (first)
        api->drawString(24, ONLN_UPDATE_TOP_DOTS_Y, COLOR_GRAY, g_updateDots);
    for (u32 i = 0; i < shown; i++)
        PLUGIN_onln_DrawPackItem(api, first + i,
                                 ONLN_UPDATE_ITEM_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y,
                                 first + i == selected, showAll);
    if (first + shown < total)
        api->drawString(24,
                        ONLN_UPDATE_ITEM_TOP_Y + ONLN_UPDATE_VISIBLE_ITEMS * ONLN_UPDATE_ITEM_SPACING_Y,
                        COLOR_GRAY, g_updateDots);
    api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY,
                    showAll ? g_updateShowUpdates : g_updateShowAll);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RedrawPackSelection(
    const MENUOnlineApi *api, u32 first, u32 oldSelected, u32 selected, bool showAll)
{
    u32 oldY = ONLN_UPDATE_ITEM_TOP_Y + (oldSelected - first) * ONLN_UPDATE_ITEM_SPACING_Y;
    u32 newY = ONLN_UPDATE_ITEM_TOP_Y + (selected - first) * ONLN_UPDATE_ITEM_SPACING_Y;
    api->drawLock();
    api->drawString(10, oldY, COLOR_BLACK, g_updateClearRow);
    api->drawString(10, newY, COLOR_BLACK, g_updateClearRow);
    PLUGIN_onln_DrawPackItem(api, oldSelected, oldY, false, showAll);
    PLUGIN_onln_DrawPackItem(api, selected, newY, true, showAll);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_BuildPackComponentLine(
    const OnlnStackId *id, char *out, u32 outSize)
{
    OnlnSelectedPlugin *local;
    OnlnRemotePlugin *remote;
    char localVersion[16];
    char remoteVersion[16];
    u32 pos = 0;

    if (!id || !out || !outSize)
        return false;
    local = PLUGIN_onln_FindSelected(id->magic, id->plgid);
    remote = PLUGIN_onln_FindRemote(id->magic, id->plgid);
    if (!remote || !PLUGIN_onln_FormatVersionText(remoteVersion, sizeof(remoteVersion), remote->version))
        return false;
    if (local && local->hasLocalVersion &&
        !PLUGIN_onln_FormatVersionText(localVersion, sizeof(localVersion), local->localVersion))
        return false;
    out[0] = 0;
    return PLUGIN_onln_AppendId(out, outSize, &pos, id->plgid) &&
           PLUGIN_onln_AppendText(out, outSize, &pos,
                                  id->magic == ONLN_LOADER_MAGIC ? g_updateOpenLoaderModule : g_updateOpenModule) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, PLUGIN_onln_ModuleName(id->magic)) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, g_updateCloseModule) &&
           PLUGIN_onln_AppendText(out, outSize, &pos,
                                  !local ? g_packNotInstalled :
                                  (local->hasLocalVersion ? localVersion : g_updateNullVersion)) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, g_updateArrow) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, remoteVersion);
}

PLUGIN_CODE(onln) static u16 PLUGIN_onln_PackComponentColor(const OnlnStackId *id)
{
    OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(id->magic, id->plgid);
    OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(id->magic, id->plgid);
    if (!local)
        return ONLN_PACK_MISSING_COLOR;
    if (!remote)
        return COLOR_WHITE;
    if (!local->hasLocalVersion || remote->version > local->localVersion)
        return ONLN_UPDATE_NEWER_COLOR;
    if (remote->version < local->localVersion)
        return ONLN_UPDATE_OLDER_COLOR;
    return COLOR_WHITE;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_RunPackDetails(const MENUOnlineApi *api, const OnlnPack *pack)
{
    u32 first = 0;
    bool redraw = true;
    if (!pack)
        return false;
    for (;;)
    {
        u32 state = PLUGIN_onln_PackState(pack);
        u32 descriptionTop = 70u;
        u32 descriptionBottom = pack->descriptionLines ?
                                descriptionTop + ((u32)pack->descriptionLines - 1u) * ONLN_UPDATE_ITEM_SPACING_Y :
                                59u;
        u32 componentsY = descriptionBottom + 14u;
        u32 componentTopY = componentsY + ONLN_UPDATE_ITEM_SPACING_Y;
        u32 visible = 0;
        if (componentTopY <= 198u)
            visible = ((198u - componentTopY) / ONLN_UPDATE_ITEM_SPACING_Y) + 1u;
        if (visible > pack->componentCount)
            visible = pack->componentCount;
        if (!visible)
            return false;
        if (first + visible > pack->componentCount)
            first = pack->componentCount - visible;
        if (*api->menuShouldExit)
            return false;
        if (redraw)
        {
            u32 shown = pack->componentCount - first;
            if (shown > visible)
                shown = visible;
            api->drawLock();
            api->drawClear();
            PLUGIN_onln_DrawPackFrame(api);
            api->drawString(20, 45, PLUGIN_onln_PackColor(state), pack->title);
            api->drawString(20, 59, COLOR_GRAY, g_packDescription);
            for (u32 i = 0; i < pack->descriptionLines; i++)
                api->drawString(24, descriptionTop + i * ONLN_UPDATE_ITEM_SPACING_Y,
                                COLOR_WHITE, pack->description[i]);
            api->drawString(20, componentsY, COLOR_GRAY, g_packComponents);
            if (first)
                api->drawString(280, componentsY, COLOR_GRAY, g_updateDots);
            for (u32 i = 0; i < shown; i++)
            {
                const OnlnStackId *id = &pack->components[first + i];
                if (PLUGIN_onln_BuildPackComponentLine(id, g_packLine, sizeof(g_packLine)))
                    api->drawString(24, componentTopY + i * ONLN_UPDATE_ITEM_SPACING_Y,
                                    PLUGIN_onln_PackComponentColor(id), g_packLine);
            }
            if (first + shown < pack->componentCount)
                api->drawString(280, 198, COLOR_GRAY, g_updateDots);
            if (state == ONLN_PACK_STATE_GREEN)
                api->drawString(24, 209, ONLN_UPDATE_NEWER_COLOR, g_packUpdate);
            else if (state == ONLN_PACK_STATE_BLUE)
                api->drawString(24, 209, ONLN_PACK_MISSING_COLOR, g_packInstall);
            else
                api->drawString(24, 209, COLOR_WHITE, g_packReinstall);
            api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY, g_updatePressBack);
            api->drawFlush();
            api->drawUnlock();
            redraw = false;
        }
        {
            u32 pressed = api->waitInputWithTimeout(50);
            if (pressed & KEY_B)
                return false;
            if (pressed & KEY_A)
                return true;
            if ((pressed & KEY_DOWN) && first + visible < pack->componentCount)
            {
                first++;
                redraw = true;
            }
            else if ((pressed & KEY_UP) && first)
            {
                first--;
                redraw = true;
            }
        }
    }
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ScanPackPresenceFile(
    const MENUOnlineApi *api, const char *name, OnlnPackPresenceContext *scan,
    bool bootDisabled)
{
    char path[ONLN_UPDATE_PATH_CAP];
    u32 size = 0;
    u32 offset = 0;
    Result result;
    if (!api || !name || !scan || !PLUGIN_onln_MakePluginPath(path, sizeof(path), name))
        return 0;
    result = api->getFileSize(path, &size);
    if (R_FAILED(result))
        return 0;
    while (offset < size)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        if (!PLUGIN_onln_Read3nxHeader(api, path, size, offset, &header, &metadataOffset, &nextOffset) ||
            nextOffset > size || (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC))
            break;
        for (u32 i = 0; i < scan->pack->componentCount; i++)
        {
            if (!scan->wanted[i] ||
                scan->pack->components[i].magic != header.magic ||
                scan->pack->components[i].plgid != header.plgid)
                continue;
            if (bootDisabled)
                scan->foundDisabled[i] = true;
            else
                scan->foundEnabled[i] = true;
        }
        offset = nextOffset;
    }
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_PackPresenceVisitor(
    const MENUOnlineDirEntry *entry, void *context, bool *stop)
{
    OnlnPackPresenceContext *scan = (OnlnPackPresenceContext *)context;
    u32 priority;
    bool disabled;
    bool bootDisabled;
    (void)stop;
    if (!entry || !scan || !(entry->flags & MENU_ONLINE_DIR_NAME_COMPLETE) ||
        (entry->attributes & FS_ATTRIBUTE_DIRECTORY) ||
        !PLUGIN_onln_ParsePluginName(entry->name, &priority, &disabled) ||
        disabled != scan->disabledPass)
        return 0;
    bootDisabled = !PLUGIN_onln_ManageBootActive(
        entry->name, disabled, scan->changeSize);
    return PLUGIN_onln_ScanPackPresenceFile(
        scan->api, entry->name, scan, bootDisabled);
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ClassifyMissingPresence(
    const MENUOnlineApi *api, const OnlnPack *pack,
    bool *hasAbsent, bool *hasDisabled, bool *hasEnabled)
{
    OnlnPackPresenceContext scan;
    u32 visited = 0;
    Result result;
    if (!api || !pack || !hasAbsent || !hasDisabled || !hasEnabled)
        return ONLN_PACK_BAD_FORMAT;
    scan.api = api;
    scan.pack = pack;
    result = PLUGIN_onln_LoadManageChanges(api, &scan.changeSize);
    if (R_FAILED(result))
        return result;
    for (u32 i = 0; i < ONLN_PACK_MAX_COMPONENTS; i++)
    {
        ((volatile bool *)scan.wanted)[i] = false;
        ((volatile bool *)scan.foundDisabled)[i] = false;
        ((volatile bool *)scan.foundEnabled)[i] = false;
    }
    for (u32 i = 0; i < pack->componentCount; i++)
        if (!PLUGIN_onln_FindSelected(pack->components[i].magic, pack->components[i].plgid))
            scan.wanted[i] = true;

    scan.disabledPass = true;
    result = api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_PackPresenceVisitor, &scan, &visited);
    if (R_FAILED(result))
        return result;
    visited = 0;
    scan.disabledPass = false;
    result = api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_PackPresenceVisitor, &scan, &visited);
    if (R_FAILED(result))
        return result;

    *hasAbsent = false;
    *hasDisabled = false;
    *hasEnabled = false;
    for (u32 i = 0; i < pack->componentCount; i++)
    {
        if (!scan.wanted[i])
            continue;
        if (!scan.foundDisabled[i] && !scan.foundEnabled[i])
            *hasAbsent = true;
        if (scan.foundDisabled[i])
            *hasDisabled = true;
        if (scan.foundEnabled[i])
            *hasEnabled = true;
    }
    return 0;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_PackFitsSelected(const OnlnPack *pack)
{
    u32 loaderMissing = 0;
    u32 rosalinaMissing = 0;
    if (!pack)
        return false;
    for (u32 i = 0; i < pack->componentCount; i++)
    {
        const OnlnStackId *id = &pack->components[i];
        if (PLUGIN_onln_FindSelected(id->magic, id->plgid))
            continue;
        if (id->magic == ONLN_LOADER_MAGIC)
            loaderMissing++;
        else
            rosalinaMissing++;
    }
    return g_updateLoaderCount + loaderMissing <= ONLN_MAX_SELECTED &&
           g_updateRosalinaCount + rosalinaMissing <= ONLN_MAX_SELECTED;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawPackMessage(
    const MENUOnlineApi *api, const OnlnPack *pack, const char *message, Result result)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawPackFrame(api);
    if (pack)
        api->drawString(20, 42, PLUGIN_onln_PackColor(PLUGIN_onln_PackState(pack)), pack->title);
    api->drawString(20, 68, R_FAILED(result) ? COLOR_RED : COLOR_WHITE, message);
    if (R_FAILED(result))
    {
        PLUGIN_onln_FormatResult(result);
        api->drawString(20, 92, COLOR_RED, g_onlineResultPrefix);
        api->drawString(80, 92, COLOR_RED, g_onlineResultHex);
        api->drawString(20, 120, COLOR_GRAY, g_updatePressBack);
    }
    else if (message == g_packUpdated || message == g_packInstalled)
    {
        api->drawString(20, 92, COLOR_YELLOW, g_updateRestart);
        api->drawString(20, 120, COLOR_GRAY, g_updatePressBack);
    }
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawPackOutsideError(const MENUOnlineApi *api, const OnlnPack *pack)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawPackFrame(api);
    if (pack)
        api->drawString(20, 40, ONLN_PACK_MISSING_COLOR, pack->title);
    api->drawString(20, 62, COLOR_RED, g_packAlreadyOutside);
    api->drawString(20, 84, COLOR_WHITE, g_packAlreadyOutside1);
    api->drawString(20, 102, COLOR_WHITE, g_packAlreadyOutside2);
    api->drawString(20, 128, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawPackDisabledError(
    const MENUOnlineApi *api, const OnlnPack *pack)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawPackFrame(api);
    if (pack)
        api->drawString(20, 40, ONLN_PACK_MISSING_COLOR, pack->title);
    api->drawString(20, 62, COLOR_RED, g_packAlreadyDisabled);
    api->drawString(20, 84, COLOR_WHITE, g_packAlreadyDisabled1);
    api->drawString(20, 102, COLOR_WHITE, g_packAlreadyDisabled2);
    api->drawString(20, 128, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ConfirmPackCapacity(
    const MENUOnlineApi *api, const OnlnPack *pack)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawPackFrame(api);
    if (pack)
        api->drawString(20, 40, ONLN_PACK_MISSING_COLOR, pack->title);
    api->drawString(20, 62, COLOR_YELLOW, g_packCapacity);
    api->drawString(20, 84, COLOR_WHITE, g_packCapacityWarn);
    api->drawString(20, 110, COLOR_GREEN, g_packContinue);
    api->drawString(20, 128, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
    for (;;)
    {
        u32 pressed;
        if (*api->menuShouldExit)
            return false;
        pressed = api->waitInputWithTimeout(50);
        if (pressed & KEY_A)
            return true;
        if (pressed & KEY_B)
            return false;
    }
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ReadRemoteUrl(
    const MENUOnlineApi *api, const OnlnRemotePlugin *remote)
{
    if (!remote || !remote->urlLength || remote->urlLength >= sizeof(g_updateUrl) ||
        !PLUGIN_onln_ReadExact(api, g_updateManifestPath, remote->urlOffset, g_updateUrl, remote->urlLength))
        return ONLN_PACK_BAD_FORMAT;
    g_updateUrl[remote->urlLength] = 0;
    if (remote->urlLength < 8u || g_updateUrl[0] != 'h' || g_updateUrl[1] != 't' ||
        g_updateUrl[2] != 't' || g_updateUrl[3] != 'p' || g_updateUrl[4] != 's' ||
        g_updateUrl[5] != ':' || g_updateUrl[6] != '/' || g_updateUrl[7] != '/')
        return ONLN_PACK_BAD_FORMAT;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ValidateRemoteDownload(
    const MENUOnlineApi *api, const OnlnRemotePlugin *remote, u32 *downloadSize)
{
    Onln3nxHeader header;
    u32 metadataOffset;
    u32 nextOffset;
    u32 versionOffset;
    u32 size = 0;
    u8 bytes[4];
    u8 versionBytes[4];
    Result result;
    if (!remote || !downloadSize)
        return ONLN_PACK_BAD_FORMAT;
    result = api->getFileSize(g_updateDownloadPath, &size);
    if (R_FAILED(result))
        return result;
    if (!size || !PLUGIN_onln_Read3nxHeader(api, g_updateDownloadPath, size, 0,
                                            &header, &metadataOffset, &nextOffset) ||
        nextOffset != size || header.magic != remote->magic || header.plgid != remote->plgid ||
        header.metadataSize < 8u ||
        !PLUGIN_onln_ReadExact(api, g_updateDownloadPath, metadataOffset, bytes, sizeof(bytes)))
        return ONLN_STACK_BAD_FILE;
    if (((u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24)) != ONLN_VERSION_MAGIC ||
        !PLUGIN_onln_Add32(metadataOffset, 4u, &versionOffset))
        return ONLN_STACK_BAD_FILE;
    versionBytes[0] = (u8)(remote->version & 0xFFu);
    versionBytes[1] = (u8)((remote->version >> 8) & 0xFFu);
    versionBytes[2] = (u8)((remote->version >> 16) & 0xFFu);
    versionBytes[3] = (u8)((remote->version >> 24) & 0xFFu);
    if (!PLUGIN_onln_WriteExact(api, g_updateDownloadPath, versionOffset, versionBytes, sizeof(versionBytes)))
        return ONLN_UPDATE_COPY_FAILED;
    *downloadSize = size;
    return 0;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawPackProgress(
    const MENUOnlineApi *api, const OnlnPack *pack, const char *phase,
    const OnlnStackId *id, u32 index, u32 total, Result result)
{
    char line[32];
    u32 pos = 0;
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawPackFrame(api);
    if (pack)
        api->drawString(20, 42, PLUGIN_onln_PackColor(PLUGIN_onln_PackState(pack)), pack->title);
    api->drawString(20, 68, R_FAILED(result) ? COLOR_RED : COLOR_WHITE, phase);
    if (id)
    {
        PLUGIN_onln_FormatId(id->plgid);
        if (g_updateId[0] >= 'a' && g_updateId[0] <= 'z')
            g_updateId[0] = (char)(g_updateId[0] - ('a' - 'A'));
        if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateId) &&
            PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateOpenModule) )
        {
            PLUGIN_onln_FormatCount(index);
            if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateNumber) &&
                PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateSlash))
            {
                PLUGIN_onln_FormatCount(total);
                if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateNumber) &&
                    PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateCloseOnly))
                    api->drawString(24, 90, COLOR_WHITE, line);
            }
        }
    }
    if (R_FAILED(result))
    {
        PLUGIN_onln_FormatResult(result);
        api->drawString(20, 114, COLOR_RED, g_onlineResultPrefix);
        api->drawString(80, 114, COLOR_RED, g_onlineResultHex);
        api->drawString(20, 140, COLOR_GRAY, g_updatePressBack);
    }
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_UpdatePackComponents(
    const MENUOnlineApi *api, const OnlnPack *pack, bool force)
{
    u32 total = 0;
    u32 position = 0;
    if (!pack)
        return ONLN_PACK_BAD_FORMAT;
    for (u32 i = 0; i < pack->componentCount; i++)
    {
        const OnlnStackId *id = &pack->components[i];
        OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(id->magic, id->plgid);
        OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(id->magic, id->plgid);
        if (!local || !remote)
            continue;
        if (!force && local->hasLocalVersion && remote->version == local->localVersion)
            continue;
        total++;
    }
    for (u32 i = 0; i < pack->componentCount; i++)
    {
        const OnlnStackId *id = &pack->components[i];
        OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(id->magic, id->plgid);
        OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(id->magic, id->plgid);
        u32 downloadSize = 0;
        Result result;
        if (!local || !remote)
            continue;
        if (!force && local->hasLocalVersion && remote->version == local->localVersion)
            continue;
        position++;
        PLUGIN_onln_DrawPackProgress(api, pack, g_packDownloading, id, position, total, 0);
        result = PLUGIN_onln_DownloadUpdate(api, local, &downloadSize);
        if (R_FAILED(result))
        {
            PLUGIN_onln_DrawPackProgress(api, pack, g_packDownloading, id, position, total, result);
            PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
            PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
            return result;
        }
        PLUGIN_onln_DrawPackProgress(api, pack, g_packInstalling, id, position, total, 0);
        result = PLUGIN_onln_InstallDownloadedUpdate(api, local, downloadSize);
        PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
        PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
        if (R_FAILED(result))
        {
            PLUGIN_onln_DrawPackProgress(api, pack, g_packInstalling, id, position, total, result);
            return result;
        }
        PLUGIN_onln_MarkInstalled(local);
    }
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_BuildPack(
    const MENUOnlineApi *api,
    const OnlnPack *pack,
    const char *targetOverride)
{
    char target[ONLN_UPDATE_PATH_CAP];
    u32 outputOffset = 0;
    Result result;

    if (!pack ||
        (targetOverride ?
            !PLUGIN_onln_CopyString(target, sizeof(target), targetOverride) :
            !PLUGIN_onln_MakePluginPath(target, sizeof(target), pack->output)))
        return ONLN_PACK_BAD_FORMAT;
    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    result = api->setFileSize(g_updateRebuildPath, 0);
    if (R_FAILED(result))
        return result;

    for (u32 i = 0; i < pack->componentCount; i++)
    {
        OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(pack->components[i].magic, pack->components[i].plgid);
        u32 size = 0;
        u32 nextOffset;
        if (!remote)
        {
            result = ONLN_PACK_BAD_FORMAT;
            goto finish;
        }
        result = PLUGIN_onln_ReadRemoteUrl(api, remote);
        if (R_FAILED(result))
            goto finish;
        PLUGIN_onln_DrawPackProgress(api, pack, g_packDownloading, &pack->components[i],
                                     i + 1u, pack->componentCount, 0);
        PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
        result = api->downloadToFile(g_updateUrl, g_updateDownloadPath, ONLN_STACK_MAX_DOWNLOAD);
        if (R_FAILED(result))
        {
            PLUGIN_onln_DrawPackProgress(api, pack, g_packDownloading, &pack->components[i],
                                         i + 1u, pack->componentCount, result);
            goto finish;
        }
        result = PLUGIN_onln_ValidateRemoteDownload(api, remote, &size);
        if (R_FAILED(result) || !PLUGIN_onln_Add32(outputOffset, size, &nextOffset))
        {
            if (R_SUCCEEDED(result))
                result = ONLN_PACK_BAD_FORMAT;
            goto finish;
        }
        PLUGIN_onln_DrawPackProgress(api, pack, g_packInstalling, &pack->components[i],
                                     i + 1u, pack->componentCount, 0);
        result = PLUGIN_onln_CopyRange(api, g_updateDownloadPath, 0,
                                       g_updateRebuildPath, outputOffset, size);
        if (R_FAILED(result))
            goto finish;
        outputOffset = nextOffset;
    }

    result = api->setFileSize(g_updateRebuildPath, outputOffset);
    if (R_FAILED(result))
        goto finish;
    if (PLUGIN_onln_FileExists(api, target))
        result = PLUGIN_onln_SwapRebuild(api, target);
    else
        result = api->renameFile(g_updateRebuildPath, target);

finish:
    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    return result;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ActOnPack(
    const MENUOnlineApi *api, OnlnPack *pack)
{
    u32 state = PLUGIN_onln_PackState(pack);
    Result result;
    if (state == ONLN_PACK_STATE_GREEN)
    {
        if (!pack->outputComplete)
        {
            char target[ONLN_UPDATE_PATH_CAP];
            if (!PLUGIN_onln_FindPackRebuildTarget(api, pack, target, sizeof(target)))
                return ONLN_PACK_BAD_FORMAT;
            PLUGIN_onln_DrawPackMessage(api, pack, g_packBuilding, 0);
            result = PLUGIN_onln_BuildPack(api, pack, target);
            if (R_FAILED(result))
                return result;
            pack->outputComplete = true;
            result = PLUGIN_onln_ScanSelected(api);
            if (R_FAILED(result))
                return result;
            PLUGIN_onln_LinkRemoteToSelected();
            PLUGIN_onln_DrawPackMessage(api, pack, g_packUpdated, 0);
            return 0;
        }
        result = PLUGIN_onln_UpdatePackComponents(api, pack, false);
        if (R_SUCCEEDED(result))
            PLUGIN_onln_DrawPackMessage(api, pack, g_packUpdated, 0);
        return result;
    }
    if (state == ONLN_PACK_STATE_WHITE || state == ONLN_PACK_STATE_RED)
    {
        result = PLUGIN_onln_UpdatePackComponents(api, pack, true);
        if (R_SUCCEEDED(result))
            PLUGIN_onln_DrawPackMessage(api, pack, g_packUpdated, 0);
        return result;
    }
    if (state != ONLN_PACK_STATE_BLUE)
        return 0;
    {
        bool hasAbsent = false;
        bool hasDisabled = false;
        bool hasEnabled = false;
        result = PLUGIN_onln_ClassifyMissingPresence(
            api, pack, &hasAbsent, &hasDisabled, &hasEnabled);
        if (R_FAILED(result))
            return result;

        if (!hasAbsent)
        {
            if (hasDisabled)
            {
                PLUGIN_onln_DrawPackDisabledError(api, pack);
                return ONLN_PACK_BAD_FORMAT;
            }
            if (hasEnabled)
            {
                PLUGIN_onln_DrawPackOutsideError(api, pack);
                return ONLN_PACK_BAD_FORMAT;
            }
        }
        else if (!PLUGIN_onln_PackFitsSelected(pack) &&
                 !PLUGIN_onln_ConfirmPackCapacity(api, pack))
        {
            return 0;
        }
    }
    if (pack->outputComplete)
    {
        result = PLUGIN_onln_UpdatePackComponents(api, pack, false);
        if (R_FAILED(result))
            return result;
    }
    PLUGIN_onln_DrawPackMessage(api, pack, g_packBuilding, 0);
    result = PLUGIN_onln_BuildPack(api, pack, NULL);
    if (R_FAILED(result))
        return result;
    pack->outputComplete = true;
    result = PLUGIN_onln_ScanSelected(api);
    if (R_FAILED(result))
        return result;
    PLUGIN_onln_LinkRemoteToSelected();
    PLUGIN_onln_DrawPackMessage(api, pack, g_packInstalled, 0);
    return 0;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunPackList(const MENUOnlineApi *api)
{
    u32 selected = 0;
    u32 first = 0;
    bool showAll = false;
    bool redraw = true;
    for (;;)
    {
        u32 total = PLUGIN_onln_CountPacks(showAll);
        if (*api->menuShouldExit)
            return;
        if (!total)
        {
            selected = 0;
            first = 0;
        }
        else
        {
            if (selected >= total)
                selected = total - 1u;
            if (total <= ONLN_UPDATE_VISIBLE_ITEMS)
                first = 0;
            else
            {
                if (first > selected)
                    first = selected;
                if (selected >= first + ONLN_UPDATE_VISIBLE_ITEMS)
                    first = selected - ONLN_UPDATE_VISIBLE_ITEMS + 1u;
                if (first > total - ONLN_UPDATE_VISIBLE_ITEMS)
                    first = total - ONLN_UPDATE_VISIBLE_ITEMS;
            }
        }
        if (redraw)
        {
            PLUGIN_onln_DrawPackList(api, first, selected, showAll);
            redraw = false;
        }
        {
            u32 pressed = api->waitInputWithTimeout(50);
            if (pressed & KEY_B)
                return;
            if (pressed & KEY_Y)
            {
                showAll = !showAll;
                selected = 0;
                first = 0;
                redraw = true;
                continue;
            }
            if (!total)
                continue;
            if (pressed & KEY_DOWN)
            {
                u32 oldSelected = selected;
                u32 oldFirst = first;
                if (selected + 1u >= total)
                {
                    selected = 0;
                    first = 0;
                }
                else
                {
                    selected++;
                    if (selected >= first + ONLN_UPDATE_VISIBLE_ITEMS)
                        first++;
                }
                if (first != oldFirst)
                    redraw = true;
                else if (selected != oldSelected)
                    PLUGIN_onln_RedrawPackSelection(api, first, oldSelected, selected, showAll);
                continue;
            }
            if (pressed & KEY_UP)
            {
                u32 oldSelected = selected;
                u32 oldFirst = first;
                if (!selected)
                {
                    selected = total - 1u;
                    first = total > ONLN_UPDATE_VISIBLE_ITEMS ? total - ONLN_UPDATE_VISIBLE_ITEMS : 0u;
                }
                else
                {
                    selected--;
                    if (selected < first)
                        first--;
                }
                if (first != oldFirst)
                    redraw = true;
                else if (selected != oldSelected)
                    PLUGIN_onln_RedrawPackSelection(api, first, oldSelected, selected, showAll);
                continue;
            }
            if (pressed & KEY_A)
            {
                OnlnPack *pack = PLUGIN_onln_GetPack(selected, showAll);
                Result result;
                if (!pack)
                    continue;
                if (!PLUGIN_onln_RunPackDetails(api, pack))
                {
                    redraw = true;
                    continue;
                }
                result = PLUGIN_onln_ActOnPack(api, pack);
                if (R_FAILED(result))
                {
                    if (result != ONLN_PACK_BAD_FORMAT || PLUGIN_onln_PackState(pack) != ONLN_PACK_STATE_BLUE)
                        PLUGIN_onln_DrawPackMessage(api, pack, g_packBuilding, result);
                    PLUGIN_onln_WaitBack(api);
                }
                else
                {
                    PLUGIN_onln_WaitBack(api);
                }
                selected = 0;
                first = 0;
                redraw = true;
            }
        }
    }
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunPacks(const MENUOnlineApi *api)
{
    Result result;
    PLUGIN_onln_DrawPackMessage(api, NULL, g_updateRecovering, 0);
    result = PLUGIN_onln_RecoverInterruptedUpdate(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawPackMessage(api, NULL, g_updateRecoverFailed, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }
    PLUGIN_onln_DrawPackMessage(api, NULL, g_updateScanning, 0);
    result = PLUGIN_onln_ScanSelected(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawPackMessage(api, NULL, g_updateScanFailed, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }
    PLUGIN_onln_DrawPackMessage(api, NULL, g_updateFetching, 0);
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
    if (!PLUGIN_onln_BuildSourceUrl(api, g_updateManifestName, g_updateUrl, sizeof(g_updateUrl)))
        result = ONLN_UPDATE_BAD_FORMAT;
    else
        result = api->downloadToFile(g_updateUrl, g_updateManifestPath, ONLN_PACK_MANIFEST_MAX);
    if (R_FAILED(result) || R_FAILED((result = PLUGIN_onln_ParseManifest(api))))
    {
        PLUGIN_onln_DrawPackMessage(api, NULL, g_updateManifestFailed, result);
        PLUGIN_onln_WaitBack(api);
        goto finish;
    }
    PLUGIN_onln_LinkRemoteToSelected();
    if (R_FAILED((result = PLUGIN_onln_ParsePackManifest(api))))
    {
        PLUGIN_onln_DrawPackMessage(api, NULL, g_packManifestFailed, result);
        PLUGIN_onln_WaitBack(api);
        goto finish;
    }
    PLUGIN_onln_RunPackList(api);

finish:
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
}

PLUGIN_MAIN(onln) void PLUGIN_onln_Main(const MENUOnlineApi *api)
{
    u32 selected = 0;

    if (!api ||
        api->version != MENU_ONLINE_API_VERSION ||
        !api->drawLock ||
        !api->drawUnlock ||
        !api->drawClear ||
        !api->drawString ||
        !api->drawFlush ||
        !api->waitInputWithTimeout ||
        !api->menuShouldExit ||
        !api->downloadToFile ||
        !api->downloadToMemory ||
        !api->getFileSize ||
        !api->readFile ||
        !api->writeFile ||
        !api->setFileSize ||
        !api->deleteFile ||
        !api->renameFile ||
        !api->fileExists ||
        !api->enumerateDirectory ||
        !api->sourceUrlPrefix ||
        !*api->sourceUrlPrefix)
    {
        return;
    }

    PLUGIN_onln_DrawRoot(api, selected);
    do
    {
        u32 pressed = api->waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return;
        if (pressed & KEY_DOWN)
        {
            u32 oldSelected = selected;
            selected = selected < 1u ? selected + 1u : 0u;
            PLUGIN_onln_RedrawRootSelection(api, oldSelected, selected);
            continue;
        }
        if (pressed & KEY_UP)
        {
            u32 oldSelected = selected;
            selected = selected ? selected - 1u : 1u;
            PLUGIN_onln_RedrawRootSelection(api, oldSelected, selected);
            continue;
        }
        if (pressed & KEY_A)
        {
            if (selected == 0)
                PLUGIN_onln_RunUtc(api);
            else
                PLUGIN_onln_RunPacks(api);
            if (*api->menuShouldExit)
                return;
            PLUGIN_onln_DrawRoot(api, selected);
        }
    } while (!*api->menuShouldExit);
}
