#include <3ds.h>
#include "draw.h"
#include "menu.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define MENU_ONLINE_API_VERSION 6u
#define MENU_ONLINE_API_MIN_VERSION 6u
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
    bool (*addSysplugin)(const char *name);
    bool (*disableSysplugin)(const char *name);
    bool (*enableSysplugin)(const char *name);
    bool (*deleteSysplugin)(const char *name);
} MENUOnlineApi;

// shared drawing bits

PLUGIN_RODATA(onln) static const char g_onlineCursor[] = ">";
PLUGIN_RODATA(onln) static const char g_onlineResultPrefix[] = "result: 0x";
PLUGIN_RODATA(onln) static const char g_onlineHexDigits[] = "0123456789ABCDEF";
PLUGIN_RODATA(onln) static const char g_onlinePlus[] = "+";
PLUGIN_RODATA(onln) static const char g_onlinePipe[] = "|";

#define ONLN_MENU_PLUGIN_ID 0x554E454Du

// MENU update prompt

PLUGIN_RODATA(onln) static const char g_packUpdatePrompt[] = "A: Update";

// updater files and messages

PLUGIN_RODATA(onln) static const char g_updateTitle[] = "Available Sysplugin Updates";
PLUGIN_RODATA(onln) static const char g_updateManifestName[] = "plgpacks.txt";

// sd paths

#define ONLN_PLUGINS_PATH "/luma/plugins"
PLUGIN_RODATA(onln) static const char g_updatePluginsDir[] = ONLN_PLUGINS_PATH;
PLUGIN_RODATA(onln) static const char g_updatePluginsPrefix[] = ONLN_PLUGINS_PATH "/";
PLUGIN_RODATA(onln) static const char g_updateManageTempListPath[] = ONLN_PLUGINS_PATH "/menutemplist.txt";
PLUGIN_RODATA(onln) static const char g_updateManifestPath[] = ONLN_PLUGINS_PATH "/.plgpacks.tmp";
PLUGIN_RODATA(onln) static const char g_updateDownloadPath[] = ONLN_PLUGINS_PATH "/.plgupdate.download";
PLUGIN_RODATA(onln) static const char g_updateRebuildPath[] = ONLN_PLUGINS_PATH "/.plgupdate.rebuild";
PLUGIN_RODATA(onln) static const char g_updateBackupPath[] = ONLN_PLUGINS_PATH "/.plgupdate.backup";
PLUGIN_RODATA(onln) static const char g_updateJournalPath[] = ONLN_PLUGINS_PATH "/.plgupdate.path";

// scan and install status

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
PLUGIN_RODATA(onln) static const char g_updateRestart[] = "Reboot to load installed Sysplugins";
PLUGIN_RODATA(onln) static const char g_updatePressBack[] = "B: go back";
PLUGIN_RODATA(onln) static const char g_updateShowAll[] = "Y: Show all";
PLUGIN_RODATA(onln) static const char g_updateShowUpdates[] = "Y: Show only updates";

// update list labels

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

// find-new messages

PLUGIN_RODATA(onln) static const char g_stackTitle[] = "Find New Sysplugins";
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

// pack list and actions

PLUGIN_RODATA(onln) static const char g_packTitle[] = "Available Sysplugins";
PLUGIN_RODATA(onln) static const char g_packNoEntries[] = "No sysplugins available";
PLUGIN_RODATA(onln) static const char g_packNotInstalled[] = "Not installed";
PLUGIN_RODATA(onln) static const char g_packDisabled[] = "Disabled";
PLUGIN_RODATA(onln) static const char g_packInstall[] = "A: Install";
PLUGIN_RODATA(onln) static const char g_packReinstall[] = "A: Re-install";
PLUGIN_RODATA(onln) static const char g_packDownloading[] = "Downloading...";
PLUGIN_RODATA(onln) static const char g_packInstalling[] = "Installing...";
PLUGIN_RODATA(onln) static const char g_packBuilding[] = "Building sysplugin pack...";
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
PLUGIN_RODATA(onln) static const char g_packDescription[] = "Description:";
PLUGIN_RODATA(onln) static const char g_packComponents[] = "Components:";
PLUGIN_RODATA(onln) static const char g_packChangelog[] = "X: Changelog";

// changelog

PLUGIN_RODATA(onln) static const char g_changeFetchPrefix[] = "Fetching ";
PLUGIN_RODATA(onln) static const char g_changeTitle[] = "Changelog: ";
PLUGIN_RODATA(onln) static const char g_changeNone[] = "No changelog found";
PLUGIN_RODATA(onln) static const char g_changeNav[] = "Up/Down: versions";
PLUGIN_RODATA(onln) static const char g_changeBack[] = "B: back";
PLUGIN_RODATA(onln) static const char g_changeSuffix[] = "_change.txt";
PLUGIN_RODATA(onln) static const u32 g_updateDecimalPowers[] = {
    1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
    10000u, 1000u, 100u, 10u, 1u
};

// file format and update limits

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

// stack limits

#define ONLN_STACK_MAX_LIST 31u
#define ONLN_STACK_MAX_PLUGINS 62u
#define ONLN_STACK_DISPLAY_NAME_MAX 46u
#define ONLN_STACK_MAX_DOWNLOAD 0xFFFFFFFFu
#define ONLN_STACK_DETAIL_VISIBLE 12u
#define ONLN_STACK_DETAIL_TOP_Y 64u
#define ONLN_STACK_BAD_FORMAT ((Result)0xD8A0A090u)
#define ONLN_STACK_BAD_FILE ((Result)0xD8A0A091u)

// pack limits

#define ONLN_REMOTE_MAX 96u
#define ONLN_PACK_MAX 31u
#define ONLN_PACK_MAX_COMPONENTS 16u
#define ONLN_PACK_TITLE_MAX 46u
#define ONLN_PACK_OUTPUT_MAX 96u
#define ONLN_PACK_DESC_MAX_LINES 8u
#define ONLN_PACK_DESC_LINE_MAX 46u
#define ONLN_PACK_MANIFEST_MAX 0x8000u
#define ONLN_PACK_STATE_YELLOW 0u
#define ONLN_PACK_STATE_GREEN 1u
#define ONLN_PACK_STATE_WHITE 2u
#define ONLN_PACK_STATE_RED 3u
#define ONLN_PACK_STATE_BLUE 4u
#define ONLN_PACK_PENDING_COLOR RGB565(31, 63, 20)
#define ONLN_PACK_MISSING_COLOR RGB8_to_565(120, 175, 255)
#define ONLN_PACK_BAD_FORMAT ((Result)0xD8A0A092u)
#define ONLN_PACK_DISABLED ((Result)0xD8A0A093u)

// changelog layout

#define ONLN_CHANGE_MAX_ENTRIES 32u
#define ONLN_CHANGE_LINE_MAX 46u
#define ONLN_CHANGE_VISIBLE_LINES 12u
#define ONLN_CHANGE_TEXT_TOP_Y 78u

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
    u16 disabledComponents;
    u16 enabledComponents;
    bool outputComplete;
    bool outputExists;
    bool pendingReboot;
} OnlnPack;

typedef struct
{
    u32 version;
    u32 textOffset;
    u32 textLength;
} OnlnChangeEntry;

typedef struct
{
    const MENUOnlineApi *api;
    const OnlnPack *pack;
    bool wanted[ONLN_PACK_MAX_COMPONENTS];
    bool foundDisabled[ONLN_PACK_MAX_COMPONENTS];
    bool foundEnabled[ONLN_PACK_MAX_COMPONENTS];
} OnlnPackPresenceContext;

PLUGIN_BSS(onln) static char g_onlineResultHex[9];
PLUGIN_BSS(onln) static OnlnSelectedPlugin g_updateLoader[ONLN_MAX_SELECTED];
PLUGIN_BSS(onln) static OnlnSelectedPlugin g_updateRosalina[ONLN_MAX_SELECTED];
PLUGIN_BSS(onln) static u32 g_updateLoaderCount;
PLUGIN_BSS(onln) static u32 g_updateRosalinaCount;
PLUGIN_BSS(onln) static u8 g_updateIo[ONLN_UPDATE_IO_SIZE];
PLUGIN_BSS(onln) static char g_updateUrl[ONLN_UPDATE_URL_CAP];
PLUGIN_BSS(onln) static char g_updateResolvedUrl[ONLN_UPDATE_URL_CAP];
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
PLUGIN_BSS(onln) static OnlnChangeEntry g_changeEntries[ONLN_CHANGE_MAX_ENTRIES];
PLUGIN_BSS(onln) static u32 g_changeCount;
PLUGIN_BSS(onln) static char g_changeFilename[ONLN_PACK_OUTPUT_MAX + 16u];
PLUGIN_BSS(onln) static char g_changeStatus[128];
PLUGIN_BSS(onln) static char g_changeLine[ONLN_CHANGE_LINE_MAX + 1u];

PLUGIN_CODE(onln) static void PLUGIN_onln_FormatResult(Result result)
{
    u32 value = (u32)result;
    for (u32 i = 0; i < 8; i++)
        g_onlineResultHex[i] = g_onlineHexDigits[(value >> ((7u - i) * 4u)) & 0xFu];
    g_onlineResultHex[8] = 0;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawDevMarker(const MENUOnlineApi *api)
{
    (void)api;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_Add32(u32 a, u32 b, u32 *out)
{
    u32 value = a + b;
    if (value < a)
        return false;
    *out = value;
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
    bool align16Condition = (!header || !metadataOffset || !nextOffset || fileOffset > fileSize || (fileSize - fileOffset) < ONLN_3NX_HEADER_SIZE || !PLUGIN_onln_ReadExact(api, path, fileOffset, header, sizeof(*header)) || !PLUGIN_onln_Add32(fileOffset, ONLN_3NX_HEADER_SIZE, &end) || !PLUGIN_onln_Add32(end, header->fastRelocSize, &end) || !PLUGIN_onln_Add32(end, header->codeSize, &end) || !PLUGIN_onln_Add32(end, header->dataSize, &end) || !PLUGIN_onln_Add32(end, header->repairSize, &end)) != 0;
    if (!align16Condition)
    {
        bool align16Result;
        {
            u32 align16Value = end;
            if (align16Value > 0xFFFFFFF0u)
            {
                align16Result = false;
                goto align16Done;
            }
            *metadataOffset = (align16Value + 0xFu) & ~0xFu;
            align16Result = true;
            align16Done:;

        }
        align16Condition = !align16Result != 0;
    }
    if (align16Condition || !PLUGIN_onln_Add32(*metadataOffset, header->metadataSize, nextOffset) || *nextOffset <= fileOffset)
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
    s32 pluginNamesResult;
    {
        u32 aLength = PLUGIN_onln_CanonicalPluginNameLength(name);
        u32 bLength = PLUGIN_onln_CanonicalPluginNameLength(currentName);
        u32 common = (aLength < bLength) ? (aLength) : (bLength);
        for (u32 i = 0; i < common; i++)
            if ((u8) name[i] != (u8) currentName[i])
        {
            pluginNamesResult = (s32) ((u8) name[i]) - (s32) ((u8) currentName[i]);
            goto pluginNamesDone;
        }

        if (aLength == bLength)
        {
            pluginNamesResult = 0;
            goto pluginNamesDone;
        }
        pluginNamesResult = (aLength < bLength) ? (-1) : (1);
        pluginNamesDone:;

    }
    comparison = pluginNamesResult;
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

PLUGIN_CODE(onln) static bool PLUGIN_onln_ManageHasPending(
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
            (char)g_updateIo[start] == 'P' &&
            g_updateIo[start + 1u] == '|')
        {
            u32 textStart = start + 2u;
            u32 textLength = textEnd - textStart;
            bool match = textLength == wanted;
            for (u32 i = 0; match && i < wanted; i++)
                if ((char)g_updateIo[textStart + i] != name[i])
                    match = false;
            if (match)
                return true;
        }
        pos = end;
    }
    return false;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ScanVisitor(
    const MENUOnlineDirEntry *entry,
    void *context,
    bool *stop)
{
    OnlnScanContext *scan = (OnlnScanContext *) context;
    u32 priority;
    bool disabled;
    (void) stop;
    if (!entry || !scan || !(entry->flags & MENU_ONLINE_DIR_NAME_COMPLETE) || entry->attributes & FS_ATTRIBUTE_DIRECTORY || !PLUGIN_onln_ParsePluginName(entry->name, &priority, &disabled) || disabled)
    {
        return 0;
    }
    {
        const MENUOnlineApi *oneFileApi = scan->api;
        const char *oneFileName = entry->name;
        u32 oneFilePriority = priority;
        char path[ONLN_UPDATE_PATH_CAP];
        u32 fileSize = 0;
        u32 fileOffset = 0;
        Result result;
        if (!PLUGIN_onln_MakePluginPath(path, sizeof(path), oneFileName))
            return 0;
        result = oneFileApi->getFileSize(path, &fileSize);
        if (R_FAILED(result))
            return 0;
        while (fileOffset < fileSize)
        {
            Onln3nxHeader header;
            u32 metadataOffset;
            u32 nextOffset;
            u32 version = 0;
            bool hasVersion;
            if (!PLUGIN_onln_Read3nxHeader(oneFileApi, path, fileSize, fileOffset, &header, &metadataOffset, &nextOffset) || nextOffset > fileSize)
                break;
            if (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC)
                break;
            bool readVersionResult;
            {
                const MENUOnlineApi *readVersionApi = oneFileApi;
                const char *readVersionPath = path;
                u32 readVersionMetadataOffset = metadataOffset;
                u8 bytes[8];
                u32 magic;
                if (header.metadataSize < 8u || !PLUGIN_onln_ReadExact(readVersionApi, readVersionPath, readVersionMetadataOffset, bytes, sizeof(bytes)))
                {
                    readVersionResult = false;
                    goto readVersionDone;
                }
                magic = (((u32) bytes[0] | ((u32) bytes[1] << 8)) | ((u32) bytes[2] << 16)) | ((u32) bytes[3] << 24);
                if (magic != ONLN_VERSION_MAGIC)
                {
                    readVersionResult = false;
                    goto readVersionDone;
                }
                version = (((u32) bytes[4] | ((u32) bytes[5] << 8)) | ((u32) bytes[6] << 16)) | ((u32) bytes[7] << 24);
                readVersionResult = true;
                readVersionDone:;

            }
            hasVersion = readVersionResult;
            if (header.magic == ONLN_LOADER_MAGIC)
                PLUGIN_onln_AddSelected(g_updateLoader, &g_updateLoaderCount, path, oneFilePriority, fileOffset, nextOffset, &header, hasVersion, version);
            else
                PLUGIN_onln_AddSelected(g_updateRosalina, &g_updateRosalinaCount, path, oneFilePriority, fileOffset, nextOffset, &header, hasVersion, version);
            fileOffset = nextOffset;
        }

        return 0;
    }
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ScanSelected(const MENUOnlineApi *api)
{
    OnlnScanContext context;
    u32 visited = 0;
    g_updateLoaderCount = 0;
    g_updateRosalinaCount = 0;
    context.api = api;
    // online actions use the files enabled now, including pending enables
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
static bool PLUGIN_onln_ResolveSourceAssetUrl(const MENUOnlineApi *api, char *url, u32 urlSize);

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
        return (R_FAILED(result)) ? (result) : (ONLN_UPDATE_BAD_FORMAT);
    while (fileOffset < fileSize)
    {
        u32 want = fileSize - fileOffset;
        u32 got = 0;
        if (want > sizeof(g_updateIo))
            want = sizeof(g_updateIo);
        result = api->readFile(g_updateManifestPath, fileOffset, g_updateIo, want, &got);
        if (R_FAILED(result) || !got || got > want)
            return (R_FAILED(result)) ? (result) : (ONLN_UPDATE_BAD_FORMAT);
        for (u32 i = 0; i < got; i++)
        {
            char c = (char) g_updateIo[i];
            if (c == '\n')
            {
                u32 effectiveLength = lineLength;
                if (effectiveLength && lastCharacter == '\r')
                    effectiveLength--;
                if (PLUGIN_onln_IsPackSeparator(prefix, effectiveLength))
                    return (g_remoteCount) ? (0) : (ONLN_UPDATE_BAD_FORMAT);
                Result manifestLineResult;
                {
                    const char *manifestLinePrefix = prefix;
                    u32 plgid;
                    u32 magic;
                    u32 version;
                    u32 effectiveLength = lineLength;
                    OnlnSelectedPlugin *selected;
                    OnlnRemotePlugin *remote;
                    if (effectiveLength && lastCharacter == '\r')
                        effectiveLength--;
                    if (!effectiveLength)
                    {
                        manifestLineResult = 0;
                        goto manifestLineDone;
                    }
                    bool manifestPrefixCondition = (effectiveLength <= 13u || prefixLength < 13u) != 0;
                    if (!manifestPrefixCondition)
                    {
                        bool manifestPrefixResult;
                        {
                            const char *manifestPrefixPrefix = manifestLinePrefix;
                            u32 parsed = 0;
                            if (!manifestPrefixPrefix || prefixLength < 13u)
                            {
                                manifestPrefixResult = false;
                                goto manifestPrefixDone;
                            }
                            for (u32 i = 0; i < 4u; i++)
                            {
                                char c = manifestPrefixPrefix[i];
                                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                                {
                                    {
                                        manifestPrefixResult = false;
                                        goto manifestPrefixDone;
                                    }
                                }
                            }

                            plgid = (((u32) ((u8) manifestPrefixPrefix[0]) | ((u32) ((u8) manifestPrefixPrefix[1]) << 8)) | ((u32) ((u8) manifestPrefixPrefix[2]) << 16)) | ((u32) ((u8) manifestPrefixPrefix[3]) << 24);
                            for (u32 i = 4u; i < 12u; i++)
                            {
                                s32 digit = PLUGIN_onln_HexDigit(manifestPrefixPrefix[i]);
                                if (digit < 0)
                                {
                                    manifestPrefixResult = false;
                                    goto manifestPrefixDone;
                                }
                                parsed = (parsed << 4) | (u32) digit;
                            }

                            if (manifestPrefixPrefix[12] == ':')
                                magic = ONLN_LOADER_MAGIC;
                            else if (manifestPrefixPrefix[12] == ';')
                                magic = ONLN_ROSALINA_MAGIC;
                            else
                            {
                                manifestPrefixResult = false;
                                goto manifestPrefixDone;
                            }
                            version = parsed;
                            manifestPrefixResult = true;
                            manifestPrefixDone:;

                        }
                        manifestPrefixCondition = !manifestPrefixResult != 0;
                    }
                    if (manifestPrefixCondition || (effectiveLength - 13u) >= ONLN_UPDATE_URL_CAP)
                    {
                        manifestLineResult = ONLN_UPDATE_BAD_FORMAT;
                        goto manifestLineDone;
                    }
                    if (PLUGIN_onln_FindRemote(magic, plgid) || g_remoteCount >= ONLN_REMOTE_MAX)
                    {
                        manifestLineResult = ONLN_UPDATE_BAD_FORMAT;
                        goto manifestLineDone;
                    }
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
                    manifestLineResult = 0;
                    manifestLineDone:;

                }
                result = manifestLineResult;
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
            if (lineLength > (13u + ONLN_UPDATE_URL_CAP))
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
            return (g_remoteCount) ? (0) : (ONLN_UPDATE_BAD_FORMAT);
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
    OnlnInstallScanContext *scan = (OnlnInstallScanContext *) context;
    bool disabled;
    (void) stop;
    bool candidateNameCondition = (!entry || !scan || !(entry->flags & MENU_ONLINE_DIR_NAME_COMPLETE) || entry->attributes & FS_ATTRIBUTE_DIRECTORY) != 0;
    if (!candidateNameCondition)
    {
        bool candidateNameResult;
        {
            const char *candidateNameName = entry->name;
            candidateNameResult = PLUGIN_onln_ParsePluginName(candidateNameName, NULL, &disabled) && !disabled;
        }
        candidateNameCondition = !candidateNameResult != 0;
    }
    if (candidateNameCondition || (scan->afterName && scan->afterName[0] && PLUGIN_onln_StringCompare(entry->name, scan->afterName) <= 0))
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
    Result manifestUrlResult;
    {
        if (!selected->hasRemote || !selected->urlLength || selected->urlLength >= sizeof(g_updateUrl) || !PLUGIN_onln_ReadExact(api, g_updateManifestPath, selected->urlOffset, g_updateUrl, selected->urlLength))
        {
            manifestUrlResult = ONLN_UPDATE_BAD_FORMAT;
            goto manifestUrlDone;
        }
        g_updateUrl[selected->urlLength] = 0;
        if (!PLUGIN_onln_ResolveSourceAssetUrl(api, g_updateUrl, sizeof(g_updateUrl)))
        {
            manifestUrlResult = ONLN_UPDATE_BAD_FORMAT;
            goto manifestUrlDone;
        }
        manifestUrlResult = 0;
        manifestUrlDone:;

    }
    result = manifestUrlResult;
    if (R_FAILED(result))
        return result;
    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    result = api->downloadToFile(g_updateUrl, g_updateDownloadPath, ONLN_UPDATE_MAX_DOWNLOAD);
    if (R_FAILED(result))
        return result;
    {
        u32 validateDownloadedManifestVersion = selected->remoteVersion;
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
        if (!size || !PLUGIN_onln_Read3nxHeader(api, g_updateDownloadPath, size, 0, &header, &metadataOffset, &nextOffset) || nextOffset != size || header.magic != selected->magic || header.plgid != selected->plgid || header.metadataSize < 8u || !PLUGIN_onln_ReadExact(api, g_updateDownloadPath, metadataOffset, magicBytes, sizeof(magicBytes)))
            return ONLN_UPDATE_BAD_FILE;
        magic = (((u32) magicBytes[0] | ((u32) magicBytes[1] << 8)) | ((u32) magicBytes[2] << 16)) | ((u32) magicBytes[3] << 24);
        if (magic != ONLN_VERSION_MAGIC || !PLUGIN_onln_Add32(metadataOffset, 4u, &versionOffset))
            return ONLN_UPDATE_BAD_FILE;
        versionBytes[0] = (u8) (validateDownloadedManifestVersion & 0xFFu);
        versionBytes[1] = (u8) ((validateDownloadedManifestVersion >> 8) & 0xFFu);
        versionBytes[2] = (u8) ((validateDownloadedManifestVersion >> 16) & 0xFFu);
        versionBytes[3] = (u8) ((validateDownloadedManifestVersion >> 24) & 0xFFu);
        if (!PLUGIN_onln_WriteExact(api, g_updateDownloadPath, versionOffset, versionBytes, sizeof(versionBytes)))
            return ONLN_UPDATE_COPY_FAILED;
        *downloadSize = size;
        return 0;
    }
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_InstallDownloadedUpdate(
    const MENUOnlineApi *api,
    const OnlnSelectedPlugin *selected,
    u32 downloadSize)
{
    {
        u32 updated = 0;
        Result result;
        g_updateInstallAfterName[0] = 0;
        for (;;)
        {
            bool found = false;
            Result installFileResult;
            {
                OnlnInstallScanContext context;
                u32 visited = 0;
                Result result;
                ;
                g_updateInstallNextName[0] = 0;
                context.afterName = g_updateInstallAfterName;
                context.found = false;
                result = api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_InstallNextVisitor, &context, &visited);
                if (R_FAILED(result))
                {
                    installFileResult = result;
                    goto installFileDone;
                }
                found = context.found;
                installFileResult = 0;
                installFileDone:;

            }
            result = installFileResult;
            if (R_FAILED(result))
                return result;
            if (!found)
                break;
            if (!PLUGIN_onln_CopyString(g_updateInstallAfterName, sizeof(g_updateInstallAfterName), g_updateInstallNextName))
                return ONLN_UPDATE_BAD_FORMAT;
            Result inFileResult;
            {
                const char *inFileName = g_updateInstallNextName;
                u32 inFileTargetMagic = selected->magic;
                u32 inFileTargetPlgid = selected->plgid;
                char path[ONLN_UPDATE_PATH_CAP];
                u32 fileSize = 0;
                u32 fileOffset = 0;
                u32 outputOffset = 0;
                u32 keepStart = 0;
                u32 matches = 0;
                Result result;
                if (!PLUGIN_onln_MakePluginPath(path, sizeof(path), inFileName))
                {
                    inFileResult = ONLN_UPDATE_BAD_FORMAT;
                    goto inFileDone;
                }
                result = api->getFileSize(path, &fileSize);
                if (R_FAILED(result))
                {
                    inFileResult = result;
                    goto inFileDone;
                }
                // Require the whole physical file/stack to parse cleanly before replacing anything.
                while (fileOffset < fileSize)
                {
                    Onln3nxHeader header;
                    u32 metadataOffset;
                    u32 nextOffset;
                    if (!PLUGIN_onln_Read3nxHeader(api, path, fileSize, fileOffset, &header, &metadataOffset, &nextOffset) || nextOffset > fileSize || (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC))
                    {
                        {
                            inFileResult = 0;
                            goto inFileDone;
                        }
                    }
                    if (header.magic == inFileTargetMagic && header.plgid == inFileTargetPlgid)
                        matches++;
                    fileOffset = nextOffset;
                }

                if (!matches)
                {
                    inFileResult = 0;
                    goto inFileDone;
                }
                if (updated > (0xFFFFFFFFu - matches))
                {
                    inFileResult = ONLN_UPDATE_BAD_FORMAT;
                    goto inFileDone;
                }
                PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
                result = api->setFileSize(g_updateRebuildPath, 0);
                if (R_FAILED(result))
                {
                    inFileResult = result;
                    goto inFileDone;
                }
                fileOffset = 0;
                while (fileOffset < fileSize)
                {
                    Onln3nxHeader header;
                    u32 metadataOffset;
                    u32 nextOffset;
                    if (!PLUGIN_onln_Read3nxHeader(api, path, fileSize, fileOffset, &header, &metadataOffset, &nextOffset) || nextOffset > fileSize)
                    {
                        result = ONLN_UPDATE_BAD_FILE;
                        goto inFileFinish;
                    }
                    if (header.magic == inFileTargetMagic && header.plgid == inFileTargetPlgid)
                    {
                        u32 nextOutput;
                        if (fileOffset > keepStart)
                        {
                            u32 keepSize = fileOffset - keepStart;
                            if (!PLUGIN_onln_Add32(outputOffset, keepSize, &nextOutput))
                            {
                                result = ONLN_UPDATE_BAD_FILE;
                                goto inFileFinish;
                            }
                            result = PLUGIN_onln_CopyRange(api, path, keepStart, g_updateRebuildPath, outputOffset, keepSize);
                            if (R_FAILED(result))
                                goto inFileFinish;
                            outputOffset = nextOutput;
                        }
                        if (!PLUGIN_onln_Add32(outputOffset, downloadSize, &nextOutput))
                        {
                            result = ONLN_UPDATE_BAD_FILE;
                            goto inFileFinish;
                        }
                        result = PLUGIN_onln_CopyRange(api, g_updateDownloadPath, 0, g_updateRebuildPath, outputOffset, downloadSize);
                        if (R_FAILED(result))
                            goto inFileFinish;
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
                        goto inFileFinish;
                    }
                    result = PLUGIN_onln_CopyRange(api, path, keepStart, g_updateRebuildPath, outputOffset, keepSize);
                    if (R_FAILED(result))
                        goto inFileFinish;
                    outputOffset = nextOutput;
                }
                result = api->setFileSize(g_updateRebuildPath, outputOffset);
                if (R_FAILED(result))
                    goto inFileFinish;
                result = PLUGIN_onln_SwapRebuild(api, path);
                if (R_FAILED(result))
                    goto inFileFinish;
                updated += matches;
                inFileFinish:
                PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);

                inFileResult = result;
                inFileDone:;

            }
            result = inFileResult;
            if (R_FAILED(result))
                return result;
        }

        return (updated) ? (0) : (ONLN_UPDATE_BAD_FILE);
    }
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

PLUGIN_CODE(onln) static bool PLUGIN_onln_ResolveSourceAssetUrl(
    const MENUOnlineApi *api,
    char *url,
    u32 urlSize
)
{
    u32 length = 0;
    if (!api || !url || !urlSize)
        return false;
    while (length < urlSize && url[length])
        length++;

    if (!length || length >= urlSize)
        return false;
    bool httpsUrlResult;
    {
        const char *httpsUrlUrl = url;
        httpsUrlResult = httpsUrlUrl && length >= 8u && httpsUrlUrl[0] == 'h' && httpsUrlUrl[1] == 't' && httpsUrlUrl[2] == 't' && httpsUrlUrl[3] == 'p' && httpsUrlUrl[4] == 's' && httpsUrlUrl[5] == ':' && httpsUrlUrl[6] == '/' && httpsUrlUrl[7] == '/';
    }
    if (httpsUrlResult)
        return true;
    for (u32 i = 0; (i + 2u) < length; i++)
    {
        if (url[i] == ':' && url[i + 1u] == '/' && url[i + 2u] == '/')
            return false;
    }

    if (!PLUGIN_onln_BuildSourceUrl(api, url, g_updateResolvedUrl, sizeof(g_updateResolvedUrl)))
        return false;
    return PLUGIN_onln_CopyString(url, urlSize, g_updateResolvedUrl);
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
    u16 color = (selected) ? (COLOR_CYAN) : (COLOR_WHITE);
    if (itemIndex < available)
    {
        OnlnSelectedPlugin *plugin = PLUGIN_onln_GetListed(itemIndex, showAll);
        bool updateLineCondition = !plugin != 0;
        if (!updateLineCondition)
        {
            bool updateLineResult;
            {
                const OnlnSelectedPlugin *updateLinePlugin = plugin;
                char *updateLineOut = line;
                u32 updateLineOutSize = sizeof(line);
                char localVersion[16];
                char remoteVersion[16];
                u32 pos = 0;
                if (!updateLinePlugin || !updateLineOut || !updateLineOutSize)
                {
                    updateLineResult = false;
                    goto updateLineDone;
                }
                updateLineOut[0] = 0;
                if (!PLUGIN_onln_FormatVersionText(remoteVersion, sizeof(remoteVersion), updateLinePlugin->remoteVersion))
                {
                    updateLineResult = false;
                    goto updateLineDone;
                }
                if (updateLinePlugin->hasLocalVersion && !PLUGIN_onln_FormatVersionText(localVersion, sizeof(localVersion), updateLinePlugin->localVersion))
                {
                    updateLineResult = false;
                    goto updateLineDone;
                }
                updateLineResult = PLUGIN_onln_AppendId(updateLineOut, updateLineOutSize, &pos, updateLinePlugin->plgid) && PLUGIN_onln_AppendText(updateLineOut, updateLineOutSize, &pos, (updateLinePlugin->magic == ONLN_LOADER_MAGIC) ? (g_updateOpenLoaderModule) : (g_updateOpenModule)) && PLUGIN_onln_AppendText(updateLineOut, updateLineOutSize, &pos, PLUGIN_onln_ModuleName(updateLinePlugin->magic)) && PLUGIN_onln_AppendText(updateLineOut, updateLineOutSize, &pos, g_updateCloseModule) && PLUGIN_onln_AppendText(updateLineOut, updateLineOutSize, &pos, (updateLinePlugin->hasLocalVersion) ? (localVersion) : (g_updateNullVersion)) && PLUGIN_onln_AppendText(updateLineOut, updateLineOutSize, &pos, g_updateArrow) && PLUGIN_onln_AppendText(updateLineOut, updateLineOutSize, &pos, remoteVersion);
                updateLineDone:;

            }
            updateLineCondition = !updateLineResult != 0;
        }
        if (updateLineCondition)
            return;
        text = line;
        u16 itemColorResult;
        {
            const OnlnSelectedPlugin *itemColorPlugin = plugin;
            if (!itemColorPlugin || !itemColorPlugin->hasLocalVersion)
            {
                itemColorResult = ONLN_UPDATE_NEWER_COLOR;
                goto itemColorDone;
            }
            if (itemColorPlugin->remoteVersion > itemColorPlugin->localVersion)
            {
                itemColorResult = ONLN_UPDATE_NEWER_COLOR;
                goto itemColorDone;
            }
            if (itemColorPlugin->remoteVersion < itemColorPlugin->localVersion)
            {
                itemColorResult = ONLN_UPDATE_OLDER_COLOR;
                goto itemColorDone;
            }
            itemColorResult = COLOR_WHITE;
            itemColorDone:;

        }
        color = itemColorResult;
    }
    else if (!hasInstallAll || itemIndex != installAllIndex)
        return;
    if (selected)
        api->drawString(12, y, COLOR_CYAN, g_onlineCursor);
    api->drawString(24, y, color, text);
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
        if (!PLUGIN_onln_ReadExact(api, g_updateManifestPath, filenameAt, out, wanted))
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
    if (!PLUGIN_onln_ReadExact(api, g_updateManifestPath, filenameAt, out, readLength))
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
    bool parsePriorityCondition = !name != 0;
    if (!parsePriorityCondition)
    {
        bool parsePriorityResult;
        {
            bool disabled = false;
            parsePriorityResult = PLUGIN_onln_ParsePluginName(name, &priority, &disabled) && !disabled;
        }
        parsePriorityCondition = !parsePriorityResult != 0;
    }
    if (parsePriorityCondition)
        return false;
    while (name[length])
    {
        if (name[length] == '/' || name[length] == '\\' || name[length] == '?' || name[length] == '#')
            return false;
        if ((length + 1u) >= MENU_ONLINE_DIR_NAME_CAP)
            return false;
        length++;
    }

    if (length < 4u || name[length - 4u] != '.' || name[length - 3u] != '3' || name[length - 2u] != 'n' || name[length - 1u] != 'x')
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
    if (lineLength < 2u + 5u + 1u ||
        !PLUGIN_onln_ReadExact(api, g_updateManifestPath, lineStart, countBytes, sizeof(countBytes)))
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
        if (!PLUGIN_onln_ReadExact(api, g_updateManifestPath, descriptorAt, descriptor, sizeof(descriptor)) ||
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
    if (!urlLength || urlLength >= ONLN_UPDATE_URL_CAP)
        return ONLN_STACK_BAD_FORMAT;

    for (u32 i = 0; i < urlLength; i++)
    {
        char c;
        if (!PLUGIN_onln_ReadExact(api, g_updateManifestPath, urlOffset + i, &c, 1u))
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
    Result result = api->getFileSize(g_updateManifestPath, &fileSize);

    g_stackCount = 0;
    if (R_FAILED(result) || !fileSize || fileSize > ONLN_UPDATE_MANIFEST_MAX)
        return R_FAILED(result) ? result : ONLN_STACK_BAD_FORMAT;

    while (fileOffset < fileSize)
    {
        u32 want = fileSize - fileOffset;
        u32 got = 0;
        if (want > sizeof(g_updateIo))
            want = sizeof(g_updateIo);
        result = api->readFile(g_updateManifestPath, fileOffset, g_updateIo, want, &got);
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
    PLUGIN_onln_DrawStackStatus(api, g_updateScanning, NULL, 0);
    result = PLUGIN_onln_ScanSelected(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawStackStatus(api, g_updateScanFailed, NULL, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }
    PLUGIN_onln_DrawStackStatus(api, g_updateFetching, NULL, 0);
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
    if (!PLUGIN_onln_BuildSourceUrl(api, g_updateManifestName, g_updateUrl, sizeof(g_updateUrl)))
        result = ONLN_STACK_BAD_FORMAT;
    else
        result = api->downloadToFile(g_updateUrl, g_updateManifestPath, ONLN_UPDATE_MANIFEST_MAX);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawStackStatus(api, g_stackManifestFailed, NULL, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }
    result = PLUGIN_onln_ParseStackManifest(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
        PLUGIN_onln_DrawStackStatus(api, g_stackManifestFailed, NULL, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }
    {
        u32 selected = 0;
        u32 first = 0;
        bool redraw = true;
        for (;;)
        {
            if (*api->menuShouldExit)
                goto stackListDone2;
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
                    if (selected >= (first + ONLN_UPDATE_VISIBLE_ITEMS))
                        first = selected - ONLN_UPDATE_VISIBLE_ITEMS + 1u;
                    if (first > (g_stackCount - ONLN_UPDATE_VISIBLE_ITEMS))
                        first = g_stackCount - ONLN_UPDATE_VISIBLE_ITEMS;
                }
            }
            if (redraw)
            {
                {
                    u32 stackListFirst = first;
                    u32 stackListSelected = selected;
                    u32 shown = (g_stackCount > stackListFirst) ? (g_stackCount - stackListFirst) : (0u);
                    if (shown > ONLN_UPDATE_VISIBLE_ITEMS)
                        shown = ONLN_UPDATE_VISIBLE_ITEMS;
                    api->drawLock();
                    api->drawClear();
                    PLUGIN_onln_DrawStackFrame(api);
                    if (!g_stackCount)
                        api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y, COLOR_GRAY, g_stackNoNew);
                    if (stackListFirst)
                        api->drawString(24, ONLN_UPDATE_TOP_DOTS_Y, COLOR_GRAY, g_updateDots);
                    for (u32 i = 0; i < shown; i++)
                        PLUGIN_onln_DrawStackItem(api, stackListFirst + i, ONLN_UPDATE_ITEM_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y, (stackListFirst + i) == stackListSelected);

                    if ((stackListFirst + shown) < g_stackCount)
                        api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y + ONLN_UPDATE_VISIBLE_ITEMS * ONLN_UPDATE_ITEM_SPACING_Y, COLOR_GRAY, g_updateDots);
                    api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY, g_updatePressBack);
                    api->drawFlush();
                    api->drawUnlock();
                }
                redraw = false;
            }
            {
                u32 pressed = api->waitInputWithTimeout(50);
                if (pressed & KEY_B)
                    goto stackListDone2;
                if (!g_stackCount)
                    continue;
                if (pressed & KEY_DOWN)
                {
                    u32 oldSelected = selected;
                    u32 oldFirst = first;
                    if ((selected + 1u) >= g_stackCount)
                    {
                        selected = 0;
                        first = 0;
                    }
                    else
                    {
                        selected++;
                        if (selected >= (first + ONLN_UPDATE_VISIBLE_ITEMS))
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
                        first = (g_stackCount > ONLN_UPDATE_VISIBLE_ITEMS) ? (g_stackCount - ONLN_UPDATE_VISIBLE_ITEMS) : (0u);
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
                    bool fitsSelectedResult;
                    {
                        const OnlnStackListing *fitsSelectedStack = chosen;
                        fitsSelectedResult = fitsSelectedStack && (g_updateLoaderCount + fitsSelectedStack->newLoader) <= ONLN_MAX_SELECTED && (g_updateRosalinaCount + fitsSelectedStack->newRosalina) <= ONLN_MAX_SELECTED;
                    }
                    if (!fitsSelectedResult)
                    {
                        {
                            const OnlnStackListing *capacityErrorStack = chosen;
                            api->drawLock();
                            api->drawClear();
                            PLUGIN_onln_DrawStackFrame(api);
                            api->drawString(20, 44, COLOR_RED, g_stackCapacityTitle);
                            if (capacityErrorStack && (g_updateLoaderCount + capacityErrorStack->newLoader) > ONLN_MAX_SELECTED && PLUGIN_onln_BuildCapacityLine(g_stackLoaderPrefix, g_updateLoaderCount, capacityErrorStack->newLoader))
                                api->drawString(20, 68, COLOR_WHITE, g_stackCapacityLine);
                            if (capacityErrorStack && (g_updateRosalinaCount + capacityErrorStack->newRosalina) > ONLN_MAX_SELECTED && PLUGIN_onln_BuildCapacityLine(g_stackRosalinaPrefix, g_updateRosalinaCount, capacityErrorStack->newRosalina))
                                api->drawString(20, 86, COLOR_WHITE, g_stackCapacityLine);
                            api->drawString(20, 112, COLOR_YELLOW, g_stackRemoveFirst);
                            api->drawString(20, 136, COLOR_GRAY, g_updatePressBack);
                            api->drawFlush();
                            api->drawUnlock();
                        }
                        PLUGIN_onln_WaitBack(api);
                        redraw = true;
                        continue;
                    }
                    bool stackDetailsResult;
                    {
                        const OnlnStackListing *stackDetailsStack = chosen;
                        u32 first = 0;
                        bool redraw = true;
                        char line[48];
                        bool stackDeclaredCondition = !PLUGIN_onln_ReadStackFilename(api, stackDetailsStack, g_stackFilename, sizeof(g_stackFilename), false) != 0;
                        if (!stackDeclaredCondition)
                        {
                            bool stackDeclaredResult;
                            {
                                const OnlnStackListing *stackDeclaredStack = stackDetailsStack;
                                char descriptor[5];
                                if (!stackDeclaredStack || !stackDeclaredStack->pluginCount || stackDeclaredStack->pluginCount > ONLN_STACK_MAX_PLUGINS)
                                {
                                    stackDeclaredResult = false;
                                    goto stackDeclaredDone;
                                }
                                for (u32 i = 0; i < stackDeclaredStack->pluginCount; i++)
                                {
                                    if (!PLUGIN_onln_ReadExact(api, g_updateManifestPath, stackDeclaredStack->lineOffset + 2u + i * 5u, descriptor, sizeof(descriptor)) || !PLUGIN_onln_ParseStackDescriptor(descriptor, &g_stackDeclared[i]))
                                    {
                                        stackDeclaredResult = false;
                                        goto stackDeclaredDone;
                                    }
                                }

                                stackDeclaredResult = true;
                                stackDeclaredDone:;

                            }
                            stackDeclaredCondition = !stackDeclaredResult != 0;
                        }
                        if (stackDeclaredCondition)
                        {
                            stackDetailsResult = false;
                            goto stackDetailsDone;
                        }
                        for (;;)
                        {
                            if (*api->menuShouldExit)
                            {
                                stackDetailsResult = false;
                                goto stackDetailsDone;
                            }
                            if (redraw)
                            {
                                u32 shown = stackDetailsStack->pluginCount - first;
                                if (shown > ONLN_STACK_DETAIL_VISIBLE)
                                    shown = ONLN_STACK_DETAIL_VISIBLE;
                                api->drawLock();
                                api->drawClear();
                                PLUGIN_onln_DrawStackFrame(api);
                                if (PLUGIN_onln_ReadStackFilename(api, stackDetailsStack, g_stackDisplayName, sizeof(g_stackDisplayName), true))
                                    api->drawString(20, 34, COLOR_WHITE, g_stackDisplayName);
                                api->drawString(20, 48, COLOR_GRAY, g_stackContains);
                                if (first)
                                    api->drawString(280, 48, COLOR_GRAY, g_updateDots);
                                for (u32 i = 0; i < shown; i++)
                                {
                                    bool idLineResult;
                                    {
                                        const OnlnStackId *idLineId = &g_stackDeclared[first + i];
                                        char *idLineOut = line;
                                        u32 idLineOutSize = sizeof(line);
                                        u32 pos = 0;
                                        if (!idLineId || !idLineOut || !idLineOutSize)
                                        {
                                            idLineResult = false;
                                            goto idLineDone;
                                        }
                                        idLineOut[0] = 0;
                                        idLineResult = PLUGIN_onln_AppendId(idLineOut, idLineOutSize, &pos, idLineId->plgid) && PLUGIN_onln_AppendText(idLineOut, idLineOutSize, &pos, (idLineId->magic == ONLN_LOADER_MAGIC) ? (g_updateOpenLoaderModule) : (g_updateOpenModule)) && PLUGIN_onln_AppendText(idLineOut, idLineOutSize, &pos, PLUGIN_onln_ModuleName(idLineId->magic)) && PLUGIN_onln_AppendText(idLineOut, idLineOutSize, &pos, g_updateCloseOnly);
                                        idLineDone:;

                                    }
                                    if (idLineResult)
                                        api->drawString(24, ONLN_STACK_DETAIL_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y, COLOR_WHITE, line);
                                }

                                if ((first + shown) < stackDetailsStack->pluginCount)
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
                                {
                                    stackDetailsResult = false;
                                    goto stackDetailsDone;
                                }
                                if (pressed & KEY_A)
                                {
                                    stackDetailsResult = true;
                                    goto stackDetailsDone;
                                }
                                if (pressed & KEY_DOWN && (first + ONLN_STACK_DETAIL_VISIBLE) < stackDetailsStack->pluginCount)
                                {
                                    first++;
                                    redraw = true;
                                }
                                else if (pressed & KEY_UP && first)
                                {
                                    first--;
                                    redraw = true;
                                }
                            }
                        }

                        stackDetailsDone:;

                    }
                    if (!stackDetailsResult)
                    {
                        redraw = true;
                        continue;
                    }
                    Result installStackResult;
                    {
                        const OnlnStackListing *installStackStack = chosen;
                        char target[ONLN_UPDATE_PATH_CAP];
                        Result result;
                        if (!installStackStack || !PLUGIN_onln_ReadStackFilename(api, installStackStack, g_stackFilename, sizeof(g_stackFilename), false) || !PLUGIN_onln_MakePluginPath(target, sizeof(target), g_stackFilename))
                        {
                            installStackResult = ONLN_STACK_BAD_FORMAT;
                            goto installStackDone;
                        }
                        Result stackUrlResult;
                        {
                            const OnlnStackListing *stackUrlStack = installStackStack;
                            if (!stackUrlStack || !stackUrlStack->urlLength || stackUrlStack->urlLength >= sizeof(g_updateUrl) || !PLUGIN_onln_ReadExact(api, g_updateManifestPath, stackUrlStack->urlOffset, g_updateUrl, stackUrlStack->urlLength))
                            {
                                stackUrlResult = ONLN_STACK_BAD_FORMAT;
                                goto stackUrlDone;
                            }
                            g_updateUrl[stackUrlStack->urlLength] = 0;
                            if (!PLUGIN_onln_ResolveSourceAssetUrl(api, g_updateUrl, sizeof(g_updateUrl)))
                            {
                                stackUrlResult = ONLN_STACK_BAD_FORMAT;
                                goto stackUrlDone;
                            }
                            stackUrlResult = 0;
                            stackUrlDone:;

                        }
                        result = stackUrlResult;
                        if (R_FAILED(result))
                        {
                            installStackResult = result;
                            goto installStackDone;
                        }
                        PLUGIN_onln_DrawStackStatus(api, g_stackDownloading, installStackStack, 0);
                        PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
                        result = api->downloadToFile(g_updateUrl, g_updateDownloadPath, ONLN_STACK_MAX_DOWNLOAD);
                        if (R_FAILED(result))
                        {
                            installStackResult = result;
                            goto installStackDone;
                        }
                        PLUGIN_onln_DrawStackStatus(api, g_stackValidating, installStackStack, 0);
                        Result downloadedStackResult;
                        {
                            u32 size = 0;
                            u32 offset = 0;
                            Result result = api->getFileSize(g_updateDownloadPath, &size);
                            if (R_FAILED(result) || !size)
                            {
                                downloadedStackResult = (R_FAILED(result)) ? (result) : (ONLN_STACK_BAD_FILE);
                                goto downloadedStackDone;
                            }
                            while (offset < size)
                            {
                                Onln3nxHeader header;
                                u32 metadataOffset;
                                u32 nextOffset;
                                if (!PLUGIN_onln_Read3nxHeader(api, g_updateDownloadPath, size, offset, &header, &metadataOffset, &nextOffset) || nextOffset <= offset || nextOffset > size || (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC))
                                {
                                    downloadedStackResult = ONLN_STACK_BAD_FILE;
                                    goto downloadedStackDone;
                                }
                                offset = nextOffset;
                            }

                            downloadedStackResult = (offset == size) ? (0) : (ONLN_STACK_BAD_FILE);
                            downloadedStackDone:;

                        }
                        result = downloadedStackResult;
                        if (R_FAILED(result))
                            goto installStackFinish;
                        PLUGIN_onln_DrawStackStatus(api, g_stackInstalling, installStackStack, 0);
                        Result downloadedStackResult2;
                        {
                            const char *downloadedStackTarget = target;
                            Result result;
                            bool exists = PLUGIN_onln_FileExists(api, downloadedStackTarget);
                            if (!exists)
                            {
                                downloadedStackResult2 = api->renameFile(g_updateDownloadPath, downloadedStackTarget);
                                goto downloadedStackDone2;
                            }
                            PLUGIN_onln_DeleteIfExists(api, g_updateBackupPath);
                            result = PLUGIN_onln_WriteJournal(api, downloadedStackTarget);
                            if (R_FAILED(result))
                            {
                                downloadedStackResult2 = result;
                                goto downloadedStackDone2;
                            }
                            result = api->renameFile(downloadedStackTarget, g_updateBackupPath);
                            if (R_FAILED(result))
                            {
                                PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
                                {
                                    downloadedStackResult2 = result;
                                    goto downloadedStackDone2;
                                }
                            }
                            result = api->renameFile(g_updateDownloadPath, downloadedStackTarget);
                            if (R_FAILED(result))
                            {
                                if (R_FAILED(api->renameFile(g_updateBackupPath, downloadedStackTarget)))
                                {
                                    downloadedStackResult2 = ONLN_UPDATE_SWAP_FAILED;
                                    goto downloadedStackDone2;
                                }
                                PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
                                {
                                    downloadedStackResult2 = result;
                                    goto downloadedStackDone2;
                                }
                            }
                            PLUGIN_onln_DeleteIfExists(api, g_updateBackupPath);
                            PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
                            downloadedStackResult2 = 0;
                            downloadedStackDone2:;

                        }
                        result = downloadedStackResult2;
                        installStackFinish:
                        PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);

                        installStackResult = result;
                        installStackDone:;

                    }
                    result = installStackResult;
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
                        goto stackListDone2;
                    selected = 0;
                    first = 0;
                    redraw = true;
                }
            }
        }

        stackListDone2:;

    }
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
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
    {
        u32 selected = 0;
        u32 first = 0;
        bool showAll = false;
        bool redraw = true;
        for (;;)
        {
            u32 available = PLUGIN_onln_CountListed(showAll);
            bool hasInstallAll = available != 0u;
            u32 installAllIndex = available;
            u32 total = available + ((hasInstallAll) ? (1u) : (0u));
            if (*api->menuShouldExit)
                goto updateListDone2;
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
                    if (selected >= (first + ONLN_UPDATE_VISIBLE_ITEMS))
                        first = selected - ONLN_UPDATE_VISIBLE_ITEMS + 1u;
                    if (first > (total - ONLN_UPDATE_VISIBLE_ITEMS))
                        first = total - ONLN_UPDATE_VISIBLE_ITEMS;
                }
            }
            if (redraw)
            {
                {
                    u32 updateListFirst = first;
                    u32 updateListSelected = selected;
                    bool updateListShowAll = showAll;
                    u32 available = PLUGIN_onln_CountListed(updateListShowAll);
                    bool hasInstallAll = available != 0u;
                    u32 installAllIndex = available;
                    u32 total = available + ((hasInstallAll) ? (1u) : (0u));
                    u32 shown = total - updateListFirst;
                    if (shown > ONLN_UPDATE_VISIBLE_ITEMS)
                        shown = ONLN_UPDATE_VISIBLE_ITEMS;
                    api->drawLock();
                    api->drawClear();
                    PLUGIN_onln_DrawUpdateFrame(api);
                    if (!total)
                        api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y, COLOR_GRAY, g_updateNoUpdates);
                    if (updateListFirst)
                        api->drawString(24, ONLN_UPDATE_TOP_DOTS_Y, COLOR_GRAY, g_updateDots);
                    for (u32 i = 0; i < shown; i++)
                        PLUGIN_onln_DrawUpdateItem(api, updateListFirst + i, ONLN_UPDATE_ITEM_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y, (updateListFirst + i) == updateListSelected, updateListShowAll, available, hasInstallAll, installAllIndex);

                    if ((updateListFirst + shown) < total)
                        api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y + ONLN_UPDATE_VISIBLE_ITEMS * ONLN_UPDATE_ITEM_SPACING_Y, COLOR_GRAY, g_updateDots);
                    api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY, (updateListShowAll) ? (g_updateShowUpdates) : (g_updateShowAll));
                    api->drawFlush();
                    api->drawUnlock();
                }
                redraw = false;
            }
            {
                u32 pressed = api->waitInputWithTimeout(50);
                if (pressed & KEY_B)
                    goto updateListDone2;
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
                    if ((selected + 1u) >= total)
                    {
                        selected = 0;
                        first = 0;
                    }
                    else
                    {
                        selected++;
                        if (selected >= (first + ONLN_UPDATE_VISIBLE_ITEMS))
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
                        first = (total > ONLN_UPDATE_VISIBLE_ITEMS) ? (total - ONLN_UPDATE_VISIBLE_ITEMS) : (0u);
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
                        {
                            bool updateAllShowAll = showAll;
                            u32 total = PLUGIN_onln_CountListed(updateAllShowAll);
                            u32 position = 0;
                            u32 updated = 0;
                            u32 failed = 0;
                            for (u32 pass = 0; pass < 2u && !(*api->menuShouldExit); pass++)
                            {
                                OnlnSelectedPlugin *plugins = (pass == 0) ? (g_updateLoader) : (g_updateRosalina);
                                u32 count = (pass == 0) ? (g_updateLoaderCount) : (g_updateRosalinaCount);
                                for (u32 i = 0; i < count && !(*api->menuShouldExit); i++)
                                {
                                    OnlnSelectedPlugin *plugin = &plugins[i];
                                    Result result;
                                    if (!PLUGIN_onln_Listed(plugin, updateAllShowAll))
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
                                }

                            }

                            if (!(*api->menuShouldExit))
                            {
                                {
                                    u32 allSummaryUpdated = updated;
                                    u32 allSummaryFailed = failed;
                                    api->drawLock();
                                    api->drawClear();
                                    PLUGIN_onln_DrawUpdateFrame(api);
                                    api->drawString(20, 42, COLOR_GREEN, g_updateAllDone);
                                    api->drawString(20, 66, COLOR_WHITE, g_updateUpdatedLabel);
                                    PLUGIN_onln_FormatCount(allSummaryUpdated);
                                    api->drawString(86, 66, COLOR_WHITE, g_updateNumber);
                                    api->drawString(20, 84, (allSummaryFailed) ? (COLOR_RED) : (COLOR_WHITE), g_updateFailedLabel);
                                    PLUGIN_onln_FormatCount(allSummaryFailed);
                                    api->drawString(86, 84, (allSummaryFailed) ? (COLOR_RED) : (COLOR_WHITE), g_updateNumber);
                                    if (allSummaryUpdated)
                                        api->drawString(20, 106, COLOR_YELLOW, g_updateRestart);
                                    api->drawString(20, 128, COLOR_GRAY, g_updatePressBack);
                                    api->drawFlush();
                                    api->drawUnlock();
                                }
                                PLUGIN_onln_WaitBack(api);
                            }
                        }
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
                        PLUGIN_onln_DrawUpdateStatus(api, g_updateInstalled, plugin, 0, 0, 0);
                        PLUGIN_onln_WaitBack(api);
                        redraw = true;
                        continue;
                    }
                }
            }
        }

        updateListDone2:;

    }
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

PLUGIN_CODE(onln) static Result PLUGIN_onln_FinishPack(OnlnPack *pack, u32 stage, u32 descriptionRead)
{
    bool outputUsedCondition = (!pack || stage < 3u || descriptionRead != pack->descriptionLines || !pack->componentCount || !PLUGIN_onln_StackFilenameValid(pack->output)) != 0;
    if (!outputUsedCondition)
    {
        bool outputUsedResult;
        {
            const char *outputUsedOutput = pack->output;
            for (u32 i = 0; i < g_packCount; i++)
                if (PLUGIN_onln_StringCompare(g_packs[i].output, outputUsedOutput) == 0)
            {
                outputUsedResult = true;
                goto outputUsedDone;
            }

            outputUsedResult = false;
            outputUsedDone:;

        }
        outputUsedCondition = outputUsedResult != 0;
    }
    if (outputUsedCondition)
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

PLUGIN_CODE(onln) static Result PLUGIN_onln_PackPresenceVisitor(
    const MENUOnlineDirEntry *entry, void *context, bool *stop)
{
    OnlnPackPresenceContext *scan = (OnlnPackPresenceContext *)context;
    u32 priority;
    bool disabled;
    char path[ONLN_UPDATE_PATH_CAP];
    u32 size = 0;
    u32 offset = 0;
    (void)stop;
    if (!entry || !scan || !scan->api ||
        !(entry->flags & MENU_ONLINE_DIR_NAME_COMPLETE) ||
        entry->attributes & FS_ATTRIBUTE_DIRECTORY ||
        !PLUGIN_onln_ParsePluginName(entry->name, &priority, &disabled) ||
        !PLUGIN_onln_MakePluginPath(path, sizeof(path), entry->name) ||
        R_FAILED(scan->api->getFileSize(path, &size)))
        return 0;
    while (offset < size)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        if (!PLUGIN_onln_Read3nxHeader(scan->api, path, size, offset, &header, &metadataOffset, &nextOffset) ||
            nextOffset > size || (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC))
            break;
        for (u32 p = 0; p < (scan->pack ? 1u : g_packCount); p++)
        {
            const OnlnPack *pack = scan->pack ? scan->pack : &g_packs[p];
            for (u32 i = 0; i < pack->componentCount; i++)
            {
                if ((scan->pack && !scan->wanted[i]) ||
                    pack->components[i].magic != header.magic || pack->components[i].plgid != header.plgid)
                    continue;
                if (scan->pack)
                {
                    if (disabled) scan->foundDisabled[i] = true;
                    else scan->foundEnabled[i] = true;
                }
                else
                {
                    if (disabled) g_packs[p].disabledComponents |= (u16)(1u << i);
                    else g_packs[p].enabledComponents |= (u16)(1u << i);
                }
            }
        }
        offset = nextOffset;
    }
    return 0;
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
        return (R_FAILED(result)) ? (result) : (ONLN_PACK_BAD_FORMAT);
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
                if (!PLUGIN_onln_CopySpan(pack->description[descriptionRead], sizeof(pack->description[descriptionRead]), g_packManifest + start, length))
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
            pack->disabledComponents = 0;
            pack->enabledComponents = 0;
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
            bool u8Result;
            {
                const char *u8Line = g_packManifest + start;
                u8 *u8Value = &pack->descriptionLines;
                u32 parsed = 0;
                if (!u8Line || !u8Value || !length || length > 3u)
                {
                    u8Result = false;
                    goto u8Done;
                }
                for (u32 i = 0; i < length; i++)
                {
                    if (u8Line[i] < '0' || u8Line[i] > '9')
                    {
                        u8Result = false;
                        goto u8Done;
                    }
                    parsed = parsed * 10u + (u32) (u8Line[i] - '0');
                    if (parsed > 255u)
                    {
                        u8Result = false;
                        goto u8Done;
                    }
                }

                *u8Value = (u8) parsed;
                u8Result = true;
                u8Done:;

            }
            if (!u8Result || pack->descriptionLines > ONLN_PACK_DESC_MAX_LINES)
                return ONLN_PACK_BAD_FORMAT;
            stage = 3;
        }
        else if (descriptionRead < pack->descriptionLines)
        {
            if (!PLUGIN_onln_CopySpan(pack->description[descriptionRead], sizeof(pack->description[descriptionRead]), g_packManifest + start, length))
                return ONLN_PACK_BAD_FORMAT;
            descriptionRead++;
        }
        else
        {
            bool packIdCondition = (pack->componentCount >= ONLN_PACK_MAX_COMPONENTS) != 0;
            if (!packIdCondition)
            {
                bool packIdResult;
                {
                    const char *packIdLine = g_packManifest + start;
                    OnlnStackId *packIdId = &pack->components[pack->componentCount];
                    if (!packIdLine || !packIdId || length != 6u || packIdLine[1] != ':' || (packIdLine[0] != 'R' && packIdLine[0] != 'L') || !PLUGIN_onln_ValidPluginIdBytes(packIdLine + 2))
                    {
                        packIdResult = false;
                        goto packIdDone;
                    }
                    packIdId->magic = (packIdLine[0] == 'L') ? (ONLN_LOADER_MAGIC) : (ONLN_ROSALINA_MAGIC);
                    packIdId->plgid = (((u32) ((u8) packIdLine[2]) | ((u32) ((u8) packIdLine[3]) << 8)) | ((u32) ((u8) packIdLine[4]) << 16)) | ((u32) ((u8) packIdLine[5]) << 24);
                    packIdResult = true;
                    packIdDone:;

                }
                packIdCondition = !packIdResult != 0;
            }
            if (packIdCondition)
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
    {
        OnlnPackPresenceContext scan;
        u32 visited = 0;
        scan.api = api;
        scan.pack = NULL;
        result = api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_PackPresenceVisitor, &scan, &visited);
        if (R_FAILED(result))
            return result;
        for (u32 i = 0; i < g_packCount; i++)
            g_packs[i].disabledComponents &= (u16)~g_packs[i].enabledComponents;
    }
    {
        u32 changeSize = 0;
        bool changesExist = false;
        result = api->fileExists(g_updateManageTempListPath, &changesExist);
        if (R_FAILED(result))
        {
            if (result != ONLN_FS_NOT_FOUND && result != ONLN_FS_PATH_NOT_FOUND)
                return result;
        }
        else if (changesExist)
        {
            result = api->getFileSize(g_updateManageTempListPath, &changeSize);
            if (R_FAILED(result))
                return result;
            if (changeSize > sizeof(g_updateIo) ||
                (changeSize && !PLUGIN_onln_ReadExact(api, g_updateManageTempListPath, 0, g_updateIo, changeSize)))
                return ONLN_UPDATE_BAD_FILE;
        }
        for (u32 i = 0; i < g_packCount; i++)
        {
            char outputPath[ONLN_UPDATE_PATH_CAP];
            OnlnPack *pack = &g_packs[i];
            pack->outputExists = PLUGIN_onln_MakePluginPath(outputPath, sizeof(outputPath), pack->output) && PLUGIN_onln_FileExists(api, outputPath);
            bool outputCompleteResult;
            {
                const OnlnPack *outputCompletePack = pack;
                char path[ONLN_UPDATE_PATH_CAP];
                u32 matched = 0;
                bool subsetOnly = false;
                if (!api || !outputCompletePack)
                {
                    outputCompleteResult = false;
                    goto outputCompleteDone;
                }
                if (PLUGIN_onln_MakePluginPath(path, sizeof(path), outputCompletePack->output) && PLUGIN_onln_PackPathStatus(api, outputCompletePack, path, &matched, &subsetOnly) && matched == outputCompletePack->componentCount)
                {
                    outputCompleteResult = true;
                    goto outputCompleteDone;
                }
                for (u32 i = 0; i < outputCompletePack->componentCount; i++)
                {
                    OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(outputCompletePack->components[i].magic, outputCompletePack->components[i].plgid);
                    if (!local)
                        continue;
                    if (PLUGIN_onln_PackPathStatus(api, outputCompletePack, local->path, &matched, &subsetOnly) && matched == outputCompletePack->componentCount)
                    {
                        outputCompleteResult = true;
                        goto outputCompleteDone;
                    }
                }

                outputCompleteResult = false;
                outputCompleteDone:;

            }
            pack->outputComplete = outputCompleteResult;
            pack->pendingReboot = PLUGIN_onln_ManageHasPending(pack->output, changeSize);
            if (!pack->pendingReboot)
            {
                for (u32 j = 0; j < pack->componentCount; j++)
                {
                    OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(pack->components[j].magic, pack->components[j].plgid);
                    bool pathFilenameCondition = local != 0;
                    if (pathFilenameCondition)
                    {
                        const char *pathFilenameResult;
                        {
                            const char *pathFilenamePath = local->path;
                            const char *name = pathFilenamePath;
                            if (!pathFilenamePath)
                            {
                                pathFilenameResult = NULL;
                                goto pathFilenameDone;
                            }
                            for (const char *p = pathFilenamePath; *p; p++)
                                if (*p == '/')
                                name = p + 1;

                            pathFilenameResult = name;
                            pathFilenameDone:;

                        }
                        pathFilenameCondition = PLUGIN_onln_ManageHasPending(pathFilenameResult, changeSize) != 0;
                    }
                    if (pathFilenameCondition)
                    {
                        pack->pendingReboot = true;
                        break;
                    }
                }

            }
        }

    }
    return 0;
}

PLUGIN_CODE(onln) static u32 PLUGIN_onln_PackState(const OnlnPack *pack)
{
    bool hasMissing = false;
    bool hasUpdate = false;
    bool hasOlder = false;
    if (!pack || pack->disabledComponents)
        return ONLN_PACK_STATE_WHITE;
    if (pack->pendingReboot && pack->outputComplete)
        return ONLN_PACK_STATE_YELLOW;
    for (u32 i = 0; i < pack->componentCount; i++)
    {
        const OnlnStackId *id = &pack->components[i];
        OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(id->magic, id->plgid);
        OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(id->magic, id->plgid);
        if (!local)
        {
            hasMissing = true;
            continue;
        }
        if (!remote)
            continue;
        if (!local->hasLocalVersion || remote->version > local->localVersion)
            hasUpdate = true;
        else if (remote->version < local->localVersion)
            hasOlder = true;
    }
    if (hasMissing)
        return pack->outputExists ? ONLN_PACK_STATE_GREEN : ONLN_PACK_STATE_BLUE;
    if (!pack->outputComplete || hasUpdate)
        return ONLN_PACK_STATE_GREEN;
    if (hasOlder)
        return ONLN_PACK_STATE_RED;
    return ONLN_PACK_STATE_WHITE;
}

PLUGIN_CODE(onln) static u16 PLUGIN_onln_PackColor(u32 state)
{
    if (state == ONLN_PACK_STATE_YELLOW)
        return ONLN_PACK_PENDING_COLOR;
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
    return showAll || state == ONLN_PACK_STATE_YELLOW || state == ONLN_PACK_STATE_GREEN || state == ONLN_PACK_STATE_BLUE;
}

PLUGIN_CODE(onln) static u32 PLUGIN_onln_CountPacks(bool showAll)
{
    u32 count = 0;
    for (u32 state = ONLN_PACK_STATE_YELLOW; state <= ONLN_PACK_STATE_BLUE; state++)
        for (u32 i = 0; i < g_packCount; i++)
            if (PLUGIN_onln_PackVisible(&g_packs[i], showAll) && PLUGIN_onln_PackState(&g_packs[i]) == state)
                count++;
    return count;
}

PLUGIN_CODE(onln) static OnlnPack *PLUGIN_onln_GetPack(u32 index, bool showAll)
{
    for (u32 state = ONLN_PACK_STATE_YELLOW; state <= ONLN_PACK_STATE_BLUE; state++)
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

PLUGIN_CODE(onln) static void PLUGIN_onln_TrimChangelogEntry(
    OnlnChangeEntry *entry, u32 end)
{
    if (!entry || end < entry->textOffset)
        return;
    while (end > entry->textOffset &&
           (g_packManifest[end - 1u] == '\n' || g_packManifest[end - 1u] == '\r'))
        end--;
    entry->textLength = end - entry->textOffset;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawChangelogBody(
    const MENUOnlineApi *api, u32 version)
{
    const OnlnChangeEntry *entry;
    {
        for (u32 i = 0; i < g_changeCount; i++)
            if (g_changeEntries[i].version == version)
        {
            entry = &g_changeEntries[i];
            goto changelogEntryDone;
        }

        entry = NULL;
        changelogEntryDone:;

    }
    if (!entry || !entry->textLength)
    {
        api->drawString(24, ONLN_CHANGE_TEXT_TOP_Y, COLOR_GRAY, g_changeNone);
        return;
    }
    {
        u32 row = 0;
        u32 position = 0;
        if (!entry || !entry->textLength)
            return;
        while (position < entry->textLength)
        {
            u32 lineStart = position;
            u32 lineEnd;
            u32 lineLength;
            u32 linePosition = 0;
            while (position < entry->textLength && g_packManifest[entry->textOffset + position] != '\n')
                position++;

            lineEnd = position;
            if (lineEnd > lineStart && g_packManifest[entry->textOffset + lineEnd - 1u] == '\r')
                lineEnd--;
            lineLength = lineEnd - lineStart;
            if (!lineLength)
            {
                row++;
            }
            else
            {
                while (linePosition < lineLength)
                {
                    u32 remaining = lineLength - linePosition;
                    u32 available = (remaining > ONLN_CHANGE_LINE_MAX) ? (ONLN_CHANGE_LINE_MAX) : (remaining);
                    u32 displayLength = available;
                    u32 consumed = available;
                    if (remaining > ONLN_CHANGE_LINE_MAX)
                    {
                        u32 split = available;
                        while (split > 0u && g_packManifest[entry->textOffset + lineStart + linePosition + split - 1u] != ' ')
                            split--;

                        if (split > 1u)
                        {
                            displayLength = split - 1u;
                            consumed = split;
                        }
                    }
                    while (displayLength && g_packManifest[entry->textOffset + lineStart + linePosition + displayLength - 1u] == ' ')
                        displayLength--;

                    if (row < ONLN_CHANGE_VISIBLE_LINES && displayLength && PLUGIN_onln_CopySpan(g_changeLine, sizeof(g_changeLine), g_packManifest + entry->textOffset + lineStart + linePosition, displayLength))
                    {
                        api->drawString(24, ONLN_CHANGE_TEXT_TOP_Y + row * ONLN_UPDATE_ITEM_SPACING_Y, COLOR_WHITE, g_changeLine);
                    }
                    row++;
                    linePosition += consumed;
                    while (linePosition < lineLength && g_packManifest[entry->textOffset + lineStart + linePosition] == ' ')
                        linePosition++;

                }

            }
            if (position < entry->textLength && g_packManifest[entry->textOffset + position] == '\n')
                position++;
        }

    }
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawChangelogVersion(
    const MENUOnlineApi *api, u32 version)
{
    char versionText[16];
    u32 pos = 0;

    g_packLine[0] = 0;
    (void)PLUGIN_onln_AppendText(g_packLine, sizeof(g_packLine), &pos, g_changeTitle);
    if (PLUGIN_onln_FormatVersionText(versionText, sizeof(versionText), version))
        (void)PLUGIN_onln_AppendText(g_packLine, sizeof(g_packLine), &pos, versionText);
    api->drawString(20, 59, COLOR_CYAN, g_packLine);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawChangelogPage(
    const MENUOnlineApi *api, const OnlnPack *pack, u32 version)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawPackFrame(api);
    api->drawString(20, 42, PLUGIN_onln_PackColor(PLUGIN_onln_PackState(pack)), pack->title);
    PLUGIN_onln_DrawChangelogVersion(api, version);
    PLUGIN_onln_DrawChangelogBody(api, version);
    api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY, g_changeNav);
    api->drawString(190, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY, g_changeBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RedrawChangelog(
    const MENUOnlineApi *api, u32 version)
{
    api->drawLock();
    api->drawString(10, 59, COLOR_BLACK, g_updateClearRow);
    PLUGIN_onln_DrawChangelogVersion(api, version);
    for (u32 i = 0; i < ONLN_CHANGE_VISIBLE_LINES; i++)
        api->drawString(10, ONLN_CHANGE_TEXT_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y,
                        COLOR_BLACK, g_updateClearRow);
    PLUGIN_onln_DrawChangelogBody(api, version);
    api->drawFlush();
    api->drawUnlock();
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
    else if (message == g_packUpdated || message == g_stackInstalled)
    {
        api->drawString(20, 92, COLOR_YELLOW, g_updateRestart);
        api->drawString(20, 120, COLOR_GRAY, g_updatePressBack);
    }
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawPackDisabled(const MENUOnlineApi *api, const OnlnPack *pack)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawPackFrame(api);
    if (pack)
        api->drawString(20, 40, COLOR_WHITE, pack->title);
    api->drawString(20, 62, COLOR_RED, g_packAlreadyDisabled);
    api->drawString(20, 84, COLOR_WHITE, g_packAlreadyDisabled1);
    api->drawString(20, 102, COLOR_WHITE, g_packAlreadyDisabled2);
    api->drawString(20, 128, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
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
    api->drawString(20, 68, (R_FAILED(result)) ? (COLOR_RED) : (COLOR_WHITE), phase);
    if (id)
    {
        {
            u32 formatIdPlgid = id->plgid;
            g_updateId[0] = (char) (formatIdPlgid & 0xFFu);
            g_updateId[1] = (char) ((formatIdPlgid >> 8) & 0xFFu);
            g_updateId[2] = (char) ((formatIdPlgid >> 16) & 0xFFu);
            g_updateId[3] = (char) ((formatIdPlgid >> 24) & 0xFFu);
            g_updateId[4] = 0;
        }
        if (g_updateId[0] >= 'a' && g_updateId[0] <= 'z')
            g_updateId[0] = (char) (g_updateId[0] - ('a' - 'A'));
        if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateId) && PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateOpenModule))
        {
            PLUGIN_onln_FormatCount(index);
            if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateNumber) && PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateSlash))
            {
                PLUGIN_onln_FormatCount(total);
                if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateNumber) && PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateCloseOnly))
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
    const MENUOnlineApi *api, OnlnPack *pack, bool force)
{
    u32 total = 0;
    u32 position = 0;
    if (!pack)
        return ONLN_PACK_BAD_FORMAT;
    if (pack->disabledComponents)
    {
        PLUGIN_onln_DrawPackDisabled(api, pack);
        return ONLN_PACK_DISABLED;
    }
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
    if (!total)
        return ONLN_UPDATE_BAD_FILE;
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
        if (!api->addSysplugin || !api->addSysplugin(local->path))
            return ONLN_UPDATE_BAD_FILE;
        pack->pendingReboot = true;
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
    if (!pack || ((targetOverride) ? (!PLUGIN_onln_CopyString(target, sizeof(target), targetOverride)) : (!PLUGIN_onln_MakePluginPath(target, sizeof(target), pack->output))))
        return ONLN_PACK_BAD_FORMAT;
    if (pack->disabledComponents)
    {
        PLUGIN_onln_DrawPackDisabled(api, pack);
        return ONLN_PACK_DISABLED;
    }
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
        Result remoteUrlResult;
        {
            const OnlnRemotePlugin *remoteUrlRemote = remote;
            if (!remoteUrlRemote || !remoteUrlRemote->urlLength || remoteUrlRemote->urlLength >= sizeof(g_updateUrl) || !PLUGIN_onln_ReadExact(api, g_updateManifestPath, remoteUrlRemote->urlOffset, g_updateUrl, remoteUrlRemote->urlLength))
            {
                remoteUrlResult = ONLN_PACK_BAD_FORMAT;
                goto remoteUrlDone;
            }
            g_updateUrl[remoteUrlRemote->urlLength] = 0;
            if (!PLUGIN_onln_ResolveSourceAssetUrl(api, g_updateUrl, sizeof(g_updateUrl)))
            {
                remoteUrlResult = ONLN_PACK_BAD_FORMAT;
                goto remoteUrlDone;
            }
            remoteUrlResult = 0;
            remoteUrlDone:;

        }
        result = remoteUrlResult;
        if (R_FAILED(result))
            goto finish;
        PLUGIN_onln_DrawPackProgress(api, pack, g_packDownloading, &pack->components[i], i + 1u, pack->componentCount, 0);
        PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
        result = api->downloadToFile(g_updateUrl, g_updateDownloadPath, ONLN_STACK_MAX_DOWNLOAD);
        if (R_FAILED(result))
        {
            PLUGIN_onln_DrawPackProgress(api, pack, g_packDownloading, &pack->components[i], i + 1u, pack->componentCount, result);
            goto finish;
        }
        Result remoteDownloadResult;
        {
            const OnlnRemotePlugin *remoteDownloadRemote = remote;
            u32 *remoteDownloadDownloadSize = &size;
            Onln3nxHeader header;
            u32 metadataOffset;
            u32 nextOffset;
            u32 versionOffset;
            u32 size = 0;
            u8 bytes[4];
            u8 versionBytes[4];
            Result result;
            if (!remoteDownloadRemote || !remoteDownloadDownloadSize)
            {
                remoteDownloadResult = ONLN_PACK_BAD_FORMAT;
                goto remoteDownloadDone;
            }
            result = api->getFileSize(g_updateDownloadPath, &size);
            if (R_FAILED(result))
            {
                remoteDownloadResult = result;
                goto remoteDownloadDone;
            }
            if (!size || !PLUGIN_onln_Read3nxHeader(api, g_updateDownloadPath, size, 0, &header, &metadataOffset, &nextOffset) || nextOffset != size || header.magic != remoteDownloadRemote->magic || header.plgid != remoteDownloadRemote->plgid || header.metadataSize < 8u || !PLUGIN_onln_ReadExact(api, g_updateDownloadPath, metadataOffset, bytes, sizeof(bytes)))
            {
                remoteDownloadResult = ONLN_STACK_BAD_FILE;
                goto remoteDownloadDone;
            }
            if (((((u32) bytes[0] | ((u32) bytes[1] << 8)) | ((u32) bytes[2] << 16)) | ((u32) bytes[3] << 24)) != ONLN_VERSION_MAGIC || !PLUGIN_onln_Add32(metadataOffset, 4u, &versionOffset))
            {
                remoteDownloadResult = ONLN_STACK_BAD_FILE;
                goto remoteDownloadDone;
            }
            versionBytes[0] = (u8) (remoteDownloadRemote->version & 0xFFu);
            versionBytes[1] = (u8) ((remoteDownloadRemote->version >> 8) & 0xFFu);
            versionBytes[2] = (u8) ((remoteDownloadRemote->version >> 16) & 0xFFu);
            versionBytes[3] = (u8) ((remoteDownloadRemote->version >> 24) & 0xFFu);
            if (!PLUGIN_onln_WriteExact(api, g_updateDownloadPath, versionOffset, versionBytes, sizeof(versionBytes)))
            {
                remoteDownloadResult = ONLN_UPDATE_COPY_FAILED;
                goto remoteDownloadDone;
            }
            *remoteDownloadDownloadSize = size;
            remoteDownloadResult = 0;
            remoteDownloadDone:;

        }
        result = remoteDownloadResult;
        if (R_FAILED(result) || !PLUGIN_onln_Add32(outputOffset, size, &nextOffset))
        {
            if (R_SUCCEEDED(result))
                result = ONLN_PACK_BAD_FORMAT;
            goto finish;
        }
        PLUGIN_onln_DrawPackProgress(api, pack, g_packInstalling, &pack->components[i], i + 1u, pack->componentCount, 0);
        result = PLUGIN_onln_CopyRange(api, g_updateDownloadPath, 0, g_updateRebuildPath, outputOffset, size);
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

PLUGIN_MAIN(onln) void PLUGIN_onln_Main(const MENUOnlineApi *api)
{
    if (!api || api->version < MENU_ONLINE_API_MIN_VERSION || !api->drawLock || !api->drawUnlock || !api->drawClear || !api->drawString || !api->drawFlush || !api->waitInputWithTimeout || !api->menuShouldExit || !api->downloadToFile || !api->downloadToMemory || !api->getFileSize || !api->readFile || !api->writeFile || !api->setFileSize || !api->deleteFile || !api->renameFile || !api->fileExists || !api->enumerateDirectory || !api->sourceUrlPrefix || !(*api->sourceUrlPrefix) || !api->addSysplugin || !api->disableSysplugin || !api->enableSysplugin || !api->deleteSysplugin)
    {
        return;
    }
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
        if (R_FAILED(result) || R_FAILED(result = PLUGIN_onln_ParseManifest(api)))
        {
            PLUGIN_onln_DrawPackMessage(api, NULL, g_updateManifestFailed, result);
            PLUGIN_onln_WaitBack(api);
            goto runPacksFinish;
        }
        PLUGIN_onln_LinkRemoteToSelected();
        if (R_FAILED(result = PLUGIN_onln_ParsePackManifest(api)))
        {
            PLUGIN_onln_DrawPackMessage(api, NULL, g_stackManifestFailed, result);
            PLUGIN_onln_WaitBack(api);
            goto runPacksFinish;
        }
        {
            u32 selected = 0;
            u32 first = 0;
            bool showAll = false;
            bool redraw = true;
            for (;;)
            {
                u32 total = PLUGIN_onln_CountPacks(showAll);
                if (*api->menuShouldExit)
                    goto packListDone2;
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
                        if (selected >= (first + ONLN_UPDATE_VISIBLE_ITEMS))
                            first = selected - ONLN_UPDATE_VISIBLE_ITEMS + 1u;
                        if (first > (total - ONLN_UPDATE_VISIBLE_ITEMS))
                            first = total - ONLN_UPDATE_VISIBLE_ITEMS;
                    }
                }
                if (redraw)
                {
                    {
                        u32 packListFirst = first;
                        u32 packListSelected = selected;
                        bool packListShowAll = showAll;
                        u32 total = PLUGIN_onln_CountPacks(packListShowAll);
                        u32 shown = (total > packListFirst) ? (total - packListFirst) : (0u);
                        if (shown > ONLN_UPDATE_VISIBLE_ITEMS)
                            shown = ONLN_UPDATE_VISIBLE_ITEMS;
                        api->drawLock();
                        api->drawClear();
                        PLUGIN_onln_DrawPackFrame(api);
                        if (!total)
                            api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y, COLOR_GRAY, g_packNoEntries);
                        if (packListFirst)
                            api->drawString(24, ONLN_UPDATE_TOP_DOTS_Y, COLOR_GRAY, g_updateDots);
                        for (u32 i = 0; i < shown; i++)
                            PLUGIN_onln_DrawPackItem(api, packListFirst + i, ONLN_UPDATE_ITEM_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y, (packListFirst + i) == packListSelected, packListShowAll);

                        if ((packListFirst + shown) < total)
                            api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y + ONLN_UPDATE_VISIBLE_ITEMS * ONLN_UPDATE_ITEM_SPACING_Y, COLOR_GRAY, g_updateDots);
                        api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY, (packListShowAll) ? (g_updateShowUpdates) : (g_updateShowAll));
                        api->drawFlush();
                        api->drawUnlock();
                    }
                    redraw = false;
                }
                {
                    u32 pressed = api->waitInputWithTimeout(50);
                    if (pressed & KEY_B)
                        goto packListDone2;
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
                        if ((selected + 1u) >= total)
                        {
                            selected = 0;
                            first = 0;
                        }
                        else
                        {
                            selected++;
                            if (selected >= (first + ONLN_UPDATE_VISIBLE_ITEMS))
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
                            first = (total > ONLN_UPDATE_VISIBLE_ITEMS) ? (total - ONLN_UPDATE_VISIBLE_ITEMS) : (0u);
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
                        if (PLUGIN_onln_PackState(pack) == ONLN_PACK_STATE_YELLOW)
                        {
                            {
                                const OnlnPack *packRebootPack = pack;
                                api->drawLock();
                                api->drawClear();
                                PLUGIN_onln_DrawPackFrame(api);
                                if (packRebootPack)
                                    api->drawString(20, 42, ONLN_PACK_PENDING_COLOR, packRebootPack->title);
                                api->drawString(20, 68, COLOR_YELLOW, g_updateRestart);
                                api->drawString(20, 120, COLOR_GRAY, g_updatePressBack);
                                api->drawFlush();
                                api->drawUnlock();
                            }
                            PLUGIN_onln_WaitBack(api);
                            redraw = true;
                            continue;
                        }
                        bool packDetailsResult;
                        {
                            const OnlnPack *packDetailsPack = pack;
                            u32 first = 0;
                            bool redraw = true;
                            if (!packDetailsPack)
                            {
                                packDetailsResult = false;
                                goto packDetailsDone;
                            }
                            for (;;)
                            {
                                u32 state = PLUGIN_onln_PackState(packDetailsPack);
                                u32 descriptionTop = 70u;
                                u32 descriptionBottom = (packDetailsPack->descriptionLines) ? (descriptionTop + ((u32) packDetailsPack->descriptionLines - 1u) * ONLN_UPDATE_ITEM_SPACING_Y) : (59u);
                                u32 componentsY = descriptionBottom + 14u;
                                u32 componentTopY = componentsY + ONLN_UPDATE_ITEM_SPACING_Y;
                                u32 visible = 0;
                                if (componentTopY <= 198u)
                                    visible = (198u - componentTopY) / ONLN_UPDATE_ITEM_SPACING_Y + 1u;
                                if (visible > packDetailsPack->componentCount)
                                    visible = packDetailsPack->componentCount;
                                if (!visible)
                                {
                                    packDetailsResult = false;
                                    goto packDetailsDone;
                                }
                                if ((first + visible) > packDetailsPack->componentCount)
                                    first = packDetailsPack->componentCount - visible;
                                if (*api->menuShouldExit)
                                {
                                    packDetailsResult = false;
                                    goto packDetailsDone;
                                }
                                if (redraw)
                                {
                                    u32 shown = packDetailsPack->componentCount - first;
                                    if (shown > visible)
                                        shown = visible;
                                    api->drawLock();
                                    api->drawClear();
                                    PLUGIN_onln_DrawPackFrame(api);
                                    api->drawString(20, 45, PLUGIN_onln_PackColor(state), packDetailsPack->title);
                                    api->drawString(20, 59, COLOR_GRAY, g_packDescription);
                                    for (u32 i = 0; i < packDetailsPack->descriptionLines; i++)
                                        api->drawString(24, descriptionTop + i * ONLN_UPDATE_ITEM_SPACING_Y, COLOR_WHITE, packDetailsPack->description[i]);

                                    api->drawString(20, componentsY, COLOR_GRAY, g_packComponents);
                                    if (first)
                                        api->drawString(280, componentsY, COLOR_GRAY, g_updateDots);
                                    for (u32 i = 0; i < shown; i++)
                                    {
                                        const OnlnStackId *id = &packDetailsPack->components[first + i];
                                        bool componentLineResult;
                                        {
                                            const OnlnPack *componentLinePack = packDetailsPack;
                                            const OnlnStackId *componentLineId = id;
                                            char *componentLineOut = g_packLine;
                                            u32 componentLineOutSize = sizeof(g_packLine);
                                            OnlnSelectedPlugin *local;
                                            OnlnRemotePlugin *remote;
                                            char localVersion[16];
                                            char remoteVersion[16];
                                            u32 pos = 0;
                                            if (!componentLineId || !componentLineOut || !componentLineOutSize)
                                            {
                                                componentLineResult = false;
                                                goto componentLineDone;
                                            }
                                            local = PLUGIN_onln_FindSelected(componentLineId->magic, componentLineId->plgid);
                                            remote = PLUGIN_onln_FindRemote(componentLineId->magic, componentLineId->plgid);
                                            if (!remote || !PLUGIN_onln_FormatVersionText(remoteVersion, sizeof(remoteVersion), remote->version))
                                            {
                                                componentLineResult = false;
                                                goto componentLineDone;
                                            }
                                            if (componentLinePack && componentLinePack->pendingReboot && componentLinePack->outputComplete)
                                            {
                                                if (!PLUGIN_onln_FormatVersionText(localVersion, sizeof(localVersion), remote->version))
                                                {
                                                    componentLineResult = false;
                                                    goto componentLineDone;
                                                }
                                            }
                                            else if (local && local->hasLocalVersion && !PLUGIN_onln_FormatVersionText(localVersion, sizeof(localVersion), local->localVersion))
                                            {
                                                componentLineResult = false;
                                                goto componentLineDone;
                                            }
                                            componentLineOut[0] = 0;
                                            componentLineResult = PLUGIN_onln_AppendId(componentLineOut, componentLineOutSize, &pos, componentLineId->plgid) && PLUGIN_onln_AppendText(componentLineOut, componentLineOutSize, &pos, (componentLineId->magic == ONLN_LOADER_MAGIC) ? (g_updateOpenLoaderModule) : (g_updateOpenModule)) && PLUGIN_onln_AppendText(componentLineOut, componentLineOutSize, &pos, PLUGIN_onln_ModuleName(componentLineId->magic)) && PLUGIN_onln_AppendText(componentLineOut, componentLineOutSize, &pos, g_updateCloseModule) && PLUGIN_onln_AppendText(componentLineOut, componentLineOutSize, &pos, (componentLinePack && (componentLinePack->disabledComponents & (1u << (first + i)))) ? (g_packDisabled) : ((componentLinePack && componentLinePack->pendingReboot && componentLinePack->outputComplete) ? (localVersion) : ((!local) ? (g_packNotInstalled) : ((local->hasLocalVersion) ? (localVersion) : (g_updateNullVersion))))) && PLUGIN_onln_AppendText(componentLineOut, componentLineOutSize, &pos, g_updateArrow) && PLUGIN_onln_AppendText(componentLineOut, componentLineOutSize, &pos, remoteVersion);
                                            componentLineDone:;

                                        }
                                        if (componentLineResult)
                                        {
                                            u16 componentColorResult;
                                            {
                                                const OnlnPack *componentColorPack = packDetailsPack;
                                                const OnlnStackId *componentColorId = id;
                                                OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(componentColorId->magic, componentColorId->plgid);
                                                OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(componentColorId->magic, componentColorId->plgid);
                                                if (componentColorPack && (componentColorPack->disabledComponents & (1u << (first + i))))
                                                {
                                                    componentColorResult = COLOR_WHITE;
                                                    goto componentColorDone;
                                                }
                                                if (componentColorPack && componentColorPack->pendingReboot && componentColorPack->outputComplete)
                                                {
                                                    componentColorResult = COLOR_WHITE;
                                                    goto componentColorDone;
                                                }
                                                if (!local)
                                                {
                                                    componentColorResult = ONLN_PACK_MISSING_COLOR;
                                                    goto componentColorDone;
                                                }
                                                if (!remote)
                                                {
                                                    componentColorResult = COLOR_WHITE;
                                                    goto componentColorDone;
                                                }
                                                if (!local->hasLocalVersion || remote->version > local->localVersion)
                                                {
                                                    componentColorResult = ONLN_UPDATE_NEWER_COLOR;
                                                    goto componentColorDone;
                                                }
                                                if (remote->version < local->localVersion)
                                                {
                                                    componentColorResult = ONLN_UPDATE_OLDER_COLOR;
                                                    goto componentColorDone;
                                                }
                                                componentColorResult = COLOR_WHITE;
                                                componentColorDone:;

                                            }
                                            api->drawString(24, componentTopY + i * ONLN_UPDATE_ITEM_SPACING_Y, componentColorResult, g_packLine);
                                        }
                                    }

                                    if ((first + shown) < packDetailsPack->componentCount)
                                        api->drawString(280, 198, COLOR_GRAY, g_updateDots);
                                    if (state == ONLN_PACK_STATE_GREEN)
                                        api->drawString(24, 209, ONLN_UPDATE_NEWER_COLOR, g_packUpdatePrompt);
                                    else if (state == ONLN_PACK_STATE_BLUE)
                                        api->drawString(24, 209, ONLN_PACK_MISSING_COLOR, g_packInstall);
                                    else
                                        api->drawString(24, 209, COLOR_WHITE, g_packReinstall);
                                    api->drawString(130, 209, COLOR_CYAN, g_packChangelog);
                                    api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY, g_updatePressBack);
                                    api->drawFlush();
                                    api->drawUnlock();
                                    redraw = false;
                                }
                                {
                                    u32 pressed = api->waitInputWithTimeout(50);
                                    if (pressed & KEY_B)
                                    {
                                        packDetailsResult = false;
                                        goto packDetailsDone;
                                    }
                                    if (pressed & KEY_X)
                                    {
                                        {
                                            const OnlnPack *runChangelogPack = packDetailsPack;
                                            u32 latestVersionResult;
                                            {
                                                const OnlnPack *latestVersionPack = runChangelogPack;
                                                u32 latest = 0;
                                                if (!latestVersionPack)
                                                {
                                                    latestVersionResult = 0;
                                                    goto latestVersionDone;
                                                }
                                                for (u32 i = 0; i < latestVersionPack->componentCount; i++)
                                                {
                                                    OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(latestVersionPack->components[i].magic, latestVersionPack->components[i].plgid);
                                                    if (remote && remote->version > latest)
                                                        latest = remote->version;
                                                }

                                                latestVersionResult = latest;
                                                latestVersionDone:;

                                            }
                                            u32 latest = latestVersionResult;
                                            u32 current = latest;
                                            u32 actualSize = 0;
                                            Result result = ONLN_PACK_BAD_FORMAT;
                                            g_changeCount = 0;
                                            bool changelogFilenameCondition = !latest != 0;
                                            if (!changelogFilenameCondition)
                                            {
                                                bool changelogFilenameResult;
                                                {
                                                    const OnlnPack *changelogFilenamePack = runChangelogPack;
                                                    char *changelogFilenameOut = g_changeFilename;
                                                    u32 changelogFilenameOutSize = sizeof(g_changeFilename);
                                                    u32 length = 0;
                                                    u32 extension;
                                                    u32 priorityDot;
                                                    u32 position = 0;
                                                    if (!changelogFilenamePack || !changelogFilenameOut || !changelogFilenameOutSize)
                                                    {
                                                        changelogFilenameResult = false;
                                                        goto changelogFilenameDone;
                                                    }
                                                    while (changelogFilenamePack->output[length])
                                                    {
                                                        if ((length + 1u) >= (ONLN_PACK_OUTPUT_MAX + 1u))
                                                        {
                                                            changelogFilenameResult = false;
                                                            goto changelogFilenameDone;
                                                        }
                                                        length++;
                                                    }

                                                    if (length < 6u || changelogFilenamePack->output[length - 4u] != '.' || changelogFilenamePack->output[length - 3u] != '3' || changelogFilenamePack->output[length - 2u] != 'n' || changelogFilenamePack->output[length - 1u] != 'x')
                                                    {
                                                        changelogFilenameResult = false;
                                                        goto changelogFilenameDone;
                                                    }
                                                    extension = length - 4u;
                                                    priorityDot = extension;
                                                    while (priorityDot > 0u && changelogFilenamePack->output[priorityDot - 1u] != '.')
                                                        priorityDot--;

                                                    if (!priorityDot)
                                                    {
                                                        changelogFilenameResult = false;
                                                        goto changelogFilenameDone;
                                                    }
                                                    priorityDot--;
                                                    if (!priorityDot)
                                                    {
                                                        changelogFilenameResult = false;
                                                        goto changelogFilenameDone;
                                                    }
                                                    for (u32 i = 0; i < priorityDot; i++)
                                                    {
                                                        if ((position + 1u) >= changelogFilenameOutSize)
                                                        {
                                                            changelogFilenameResult = false;
                                                            goto changelogFilenameDone;
                                                        }
                                                        changelogFilenameOut[position++] = changelogFilenamePack->output[i];
                                                    }

                                                    changelogFilenameOut[position] = 0;
                                                    changelogFilenameResult = PLUGIN_onln_AppendText(changelogFilenameOut, changelogFilenameOutSize, &position, g_changeSuffix);
                                                    changelogFilenameDone:;

                                                }
                                                changelogFilenameCondition = !changelogFilenameResult != 0;
                                            }
                                            if (changelogFilenameCondition)
                                            {
                                                PLUGIN_onln_DrawChangelogPage(api, runChangelogPack, current);
                                            }
                                            else
                                            {
                                                {
                                                    const OnlnPack *changelogFetchPack = runChangelogPack;
                                                    const char *changelogFetchFilename = g_changeFilename;
                                                    u32 pos = 0;
                                                    g_changeStatus[0] = 0;
                                                    (void) PLUGIN_onln_AppendText(g_changeStatus, sizeof(g_changeStatus), &pos, g_changeFetchPrefix);
                                                    (void) PLUGIN_onln_AppendText(g_changeStatus, sizeof(g_changeStatus), &pos, changelogFetchFilename);
                                                    (void) PLUGIN_onln_AppendText(g_changeStatus, sizeof(g_changeStatus), &pos, g_updateDots);
                                                    api->drawLock();
                                                    api->drawClear();
                                                    PLUGIN_onln_DrawPackFrame(api);
                                                    api->drawString(20, 45, PLUGIN_onln_PackColor(PLUGIN_onln_PackState(changelogFetchPack)), changelogFetchPack->title);
                                                    api->drawString(20, 70, COLOR_GRAY, g_changeStatus);
                                                    api->drawFlush();
                                                    api->drawUnlock();
                                                }
                                                if (PLUGIN_onln_BuildSourceUrl(api, g_changeFilename, g_updateUrl, sizeof(g_updateUrl)))
                                                    result = api->downloadToMemory(g_updateUrl, g_packManifest, ONLN_PACK_MANIFEST_MAX, &actualSize);
                                                if (R_SUCCEEDED(result) && actualSize <= ONLN_PACK_MANIFEST_MAX)
                                                {
                                                    u32 parseChangelogSize = actualSize;
                                                    u32 parseChangelogLatestVersion = latest;
                                                    s32 active = -1;
                                                    u32 position = 0;
                                                    g_changeCount = 0;
                                                    while (position < parseChangelogSize)
                                                    {
                                                        u32 lineStart = position;
                                                        u32 lineEnd;
                                                        u32 next;
                                                        u32 length;
                                                        u32 version;
                                                        while (position < parseChangelogSize && g_packManifest[position] != '\n')
                                                            position++;

                                                        lineEnd = position;
                                                        if (lineEnd > lineStart && g_packManifest[lineEnd - 1u] == '\r')
                                                            lineEnd--;
                                                        length = lineEnd - lineStart;
                                                        next = (position < parseChangelogSize) ? (position + 1u) : (position);
                                                        bool changelogVersionResult;
                                                        {
                                                            const char *changelogVersionLine = g_packManifest + lineStart;
                                                            u32 changelogVersionLength = length;
                                                            u32 dot = 0;
                                                            u32 major = 0;
                                                            u32 minor;
                                                            if (!changelogVersionLine || changelogVersionLength < 7u || changelogVersionLine[0] != '[' || changelogVersionLine[1] != 'v' || changelogVersionLine[changelogVersionLength - 1u] != ']')
                                                            {
                                                                changelogVersionResult = false;
                                                                goto changelogVersionDone;
                                                            }
                                                            for (u32 i = 2u; (i + 1u) < changelogVersionLength; i++)
                                                            {
                                                                if (changelogVersionLine[i] == '.')
                                                                {
                                                                    if (dot)
                                                                    {
                                                                        changelogVersionResult = false;
                                                                        goto changelogVersionDone;
                                                                    }
                                                                    dot = i;
                                                                    continue;
                                                                }
                                                                if (changelogVersionLine[i] < '0' || changelogVersionLine[i] > '9')
                                                                {
                                                                    changelogVersionResult = false;
                                                                    goto changelogVersionDone;
                                                                }
                                                            }

                                                            if (!dot || dot == 2u || (dot + 3u) != (changelogVersionLength - 1u))
                                                            {
                                                                changelogVersionResult = false;
                                                                goto changelogVersionDone;
                                                            }
                                                            for (u32 i = 2u; i < dot; i++)
                                                            {
                                                                u32 digit = (u32) (changelogVersionLine[i] - '0');
                                                                if (major > ((0xFFFFFFFFu - digit) / 10u))
                                                                {
                                                                    changelogVersionResult = false;
                                                                    goto changelogVersionDone;
                                                                }
                                                                major = major * 10u + digit;
                                                            }

                                                            if (major > ((0xFFFFFFFFu - 99u) / 100u))
                                                            {
                                                                changelogVersionResult = false;
                                                                goto changelogVersionDone;
                                                            }
                                                            minor = (u32) (changelogVersionLine[dot + 1u] - '0') * 10u + (u32) (changelogVersionLine[dot + 2u] - '0');
                                                            version = major * 100u + minor;
                                                            changelogVersionResult = true;
                                                            changelogVersionDone:;

                                                        }
                                                        if (changelogVersionResult)
                                                        {
                                                            bool duplicate = false;
                                                            if (active >= 0)
                                                                PLUGIN_onln_TrimChangelogEntry(&g_changeEntries[(u32) active], lineStart);
                                                            active = -1;
                                                            if (version <= parseChangelogLatestVersion)
                                                            {
                                                                for (u32 i = 0; i < g_changeCount; i++)
                                                                    if (g_changeEntries[i].version == version)
                                                                    duplicate = true;

                                                                if (!duplicate && g_changeCount < ONLN_CHANGE_MAX_ENTRIES)
                                                                {
                                                                    OnlnChangeEntry *entry = &g_changeEntries[g_changeCount];
                                                                    entry->version = version;
                                                                    entry->textOffset = next;
                                                                    entry->textLength = 0;
                                                                    active = (s32) g_changeCount;
                                                                    g_changeCount++;
                                                                }
                                                            }
                                                        }
                                                        position = next;
                                                    }

                                                    if (active >= 0)
                                                        PLUGIN_onln_TrimChangelogEntry(&g_changeEntries[(u32) active], parseChangelogSize);
                                                }
                                                PLUGIN_onln_DrawChangelogPage(api, runChangelogPack, current);
                                            }
                                            while (!(*api->menuShouldExit))
                                            {
                                                u32 pressed = api->waitInputWithTimeout(50);
                                                u32 next;
                                                if (pressed & KEY_B)
                                                    goto runChangelogDone;
                                                bool changelogVersionCondition = (pressed & KEY_DOWN) != 0;
                                                if (changelogVersionCondition)
                                                {
                                                    bool changelogVersionResult2;
                                                    {
                                                        u32 changelogVersionCurrent = current;
                                                        bool found = false;
                                                        u32 best = 0;
                                                        for (u32 i = 0; i < g_changeCount; i++)
                                                        {
                                                            u32 candidate = g_changeEntries[i].version;
                                                            if (candidate >= changelogVersionCurrent || (found && candidate <= best))
                                                                continue;
                                                            best = candidate;
                                                            found = true;
                                                        }

                                                        if (found)
                                                            next = best;
                                                        changelogVersionResult2 = found;
                                                    }
                                                    changelogVersionCondition = changelogVersionResult2 != 0;
                                                }
                                                if (changelogVersionCondition)
                                                {
                                                    current = next;
                                                    PLUGIN_onln_RedrawChangelog(api, current);
                                                    continue;
                                                }
                                                bool changelogVersionCondition2 = (pressed & KEY_UP) != 0;
                                                if (changelogVersionCondition2)
                                                {
                                                    bool changelogVersionResult3;
                                                    {
                                                        u32 changelogVersionCurrent2 = current;
                                                        u32 changelogVersionLatest = latest;
                                                        u32 best;
                                                        bool found;
                                                        if (changelogVersionCurrent2 >= changelogVersionLatest)
                                                        {
                                                            changelogVersionResult3 = false;
                                                            goto changelogVersionDone3;
                                                        }
                                                        best = changelogVersionLatest;
                                                        found = true;
                                                        for (u32 i = 0; i < g_changeCount; i++)
                                                        {
                                                            u32 candidate = g_changeEntries[i].version;
                                                            if (candidate <= changelogVersionCurrent2 || candidate >= best)
                                                                continue;
                                                            best = candidate;
                                                        }

                                                        if (found)
                                                            next = best;
                                                        changelogVersionResult3 = found;
                                                        changelogVersionDone3:;

                                                    }
                                                    changelogVersionCondition2 = changelogVersionResult3 != 0;
                                                }
                                                if (changelogVersionCondition2)
                                                {
                                                    current = next;
                                                    PLUGIN_onln_RedrawChangelog(api, current);
                                                    continue;
                                                }
                                            }

                                            runChangelogDone:;

                                        }
                                        if (*api->menuShouldExit)
                                        {
                                            packDetailsResult = false;
                                            goto packDetailsDone;
                                        }
                                        redraw = true;
                                        continue;
                                    }
                                    if (pressed & KEY_A)
                                    {
                                        packDetailsResult = true;
                                        goto packDetailsDone;
                                    }
                                    if (pressed & KEY_DOWN && (first + visible) < packDetailsPack->componentCount)
                                    {
                                        first++;
                                        redraw = true;
                                    }
                                    else if (pressed & KEY_UP && first)
                                    {
                                        first--;
                                        redraw = true;
                                    }
                                }
                            }

                            packDetailsDone:;

                        }
                        if (!packDetailsResult)
                        {
                            redraw = true;
                            continue;
                        }
                        Result onPackResult;
                        {
                            OnlnPack *onPackPack = pack;
                            u32 state = PLUGIN_onln_PackState(onPackPack);
                            Result result;
                            if (state == ONLN_PACK_STATE_YELLOW)
                            {
                                onPackResult = 0;
                                goto onPackDone;
                            }
                            if (state == ONLN_PACK_STATE_GREEN)
                            {
                                if (!onPackPack->outputComplete)
                                {
                                    char target[ONLN_UPDATE_PATH_CAP];
                                    bool haveTarget;
                                    if (onPackPack->outputExists)
                                    {
                                        haveTarget = PLUGIN_onln_MakePluginPath(target, sizeof(target), onPackPack->output);
                                    }
                                    else
                                    {
                                        bool rebuildTargetResult;
                                        {
                                            const OnlnPack *rebuildTargetPack = onPackPack;
                                            char *rebuildTargetTarget = target;
                                            u32 rebuildTargetTargetSize = sizeof(target);
                                            char outputPath[ONLN_UPDATE_PATH_CAP];
                                            u32 matched = 0;
                                            u32 bestMatched = 0;
                                            bool subsetOnly = false;
                                            bool found = false;
                                            if (!api || !rebuildTargetPack || !rebuildTargetTarget || !rebuildTargetTargetSize)
                                            {
                                                rebuildTargetResult = false;
                                                goto rebuildTargetDone;
                                            }
                                            if (PLUGIN_onln_MakePluginPath(outputPath, sizeof(outputPath), rebuildTargetPack->output) && PLUGIN_onln_PackPathStatus(api, rebuildTargetPack, outputPath, &matched, &subsetOnly) && subsetOnly && matched && matched < rebuildTargetPack->componentCount)
                                            {
                                                rebuildTargetResult = PLUGIN_onln_CopyString(rebuildTargetTarget, rebuildTargetTargetSize, outputPath);
                                                goto rebuildTargetDone;
                                            }
                                            for (u32 pass = 0; pass < 2u; pass++)
                                            {
                                                for (u32 i = 0; i < rebuildTargetPack->componentCount; i++)
                                                {
                                                    const OnlnStackId *id = &rebuildTargetPack->components[i];
                                                    OnlnSelectedPlugin *local = PLUGIN_onln_FindSelected(id->magic, id->plgid);
                                                    OnlnRemotePlugin *remote = PLUGIN_onln_FindRemote(id->magic, id->plgid);
                                                    bool needsUpdate = local && remote && (!local->hasLocalVersion || remote->version > local->localVersion);
                                                    if (!local || (!pass && !needsUpdate))
                                                        continue;
                                                    if (!PLUGIN_onln_PackPathStatus(api, rebuildTargetPack, local->path, &matched, &subsetOnly) || !subsetOnly || !matched || matched >= rebuildTargetPack->componentCount)
                                                        continue;
                                                    if (!found || matched > bestMatched)
                                                    {
                                                        if (!PLUGIN_onln_CopyString(rebuildTargetTarget, rebuildTargetTargetSize, local->path))
                                                        {
                                                            rebuildTargetResult = false;
                                                            goto rebuildTargetDone;
                                                        }
                                                        bestMatched = matched;
                                                        found = true;
                                                    }
                                                }

                                                if (found)
                                                {
                                                    rebuildTargetResult = true;
                                                    goto rebuildTargetDone;
                                                }
                                            }

                                            rebuildTargetResult = PLUGIN_onln_MakePluginPath(rebuildTargetTarget, rebuildTargetTargetSize, rebuildTargetPack->output);
                                            rebuildTargetDone:;

                                        }
                                        haveTarget = rebuildTargetResult;
                                    }
                                    if (!haveTarget)
                                    {
                                        onPackResult = ONLN_PACK_BAD_FORMAT;
                                        goto onPackDone;
                                    }
                                    PLUGIN_onln_DrawPackMessage(api, onPackPack, g_packBuilding, 0);
                                    result = PLUGIN_onln_BuildPack(api, onPackPack, target);
                                    if (R_FAILED(result))
                                    {
                                        onPackResult = result;
                                        goto onPackDone;
                                    }
                                    if (!api->addSysplugin || !api->addSysplugin(target))
                                    {
                                        onPackResult = ONLN_UPDATE_BAD_FILE;
                                        goto onPackDone;
                                    }
                                    onPackPack->outputExists = true;
                                    onPackPack->outputComplete = true;
                                    onPackPack->pendingReboot = true;
                                    result = PLUGIN_onln_ScanSelected(api);
                                    if (R_FAILED(result))
                                    {
                                        onPackResult = result;
                                        goto onPackDone;
                                    }
                                    PLUGIN_onln_LinkRemoteToSelected();
                                    PLUGIN_onln_DrawPackMessage(api, onPackPack, g_packUpdated, 0);
                                    {
                                        onPackResult = 0;
                                        goto onPackDone;
                                    }
                                }
                                result = PLUGIN_onln_UpdatePackComponents(api, onPackPack, false);
                                if (R_SUCCEEDED(result))
                                    PLUGIN_onln_DrawPackMessage(api, onPackPack, g_packUpdated, 0);
                                {
                                    onPackResult = result;
                                    goto onPackDone;
                                }
                            }
                            if (state == ONLN_PACK_STATE_WHITE || state == ONLN_PACK_STATE_RED)
                            {
                                result = PLUGIN_onln_UpdatePackComponents(api, onPackPack, true);
                                if (R_SUCCEEDED(result))
                                    PLUGIN_onln_DrawPackMessage(api, onPackPack, g_packUpdated, 0);
                                {
                                    onPackResult = result;
                                    goto onPackDone;
                                }
                            }
                            if (state != ONLN_PACK_STATE_BLUE)
                            {
                                onPackResult = 0;
                                goto onPackDone;
                            }
                            {
                                bool hasAbsent = false;
                                bool hasDisabled = false;
                                bool hasEnabled = false;
                                Result missingPresenceResult;
                                {
                                    const OnlnPack *missingPresencePack = onPackPack;
                                    OnlnPackPresenceContext scan;
                                    u32 visited = 0;
                                    Result result;
                                    if (!api || !missingPresencePack)
                                    {
                                        missingPresenceResult = ONLN_PACK_BAD_FORMAT;
                                        goto missingPresenceDone;
                                    }
                                    scan.api = api;
                                    scan.pack = missingPresencePack;
                                    for (u32 i = 0; i < ONLN_PACK_MAX_COMPONENTS; i++)
                                    {
                                        ((volatile bool *) scan.wanted)[i] = false;
                                        ((volatile bool *) scan.foundDisabled)[i] = false;
                                        ((volatile bool *) scan.foundEnabled)[i] = false;
                                    }

                                    for (u32 i = 0; i < missingPresencePack->componentCount; i++)
                                        if (!PLUGIN_onln_FindSelected(missingPresencePack->components[i].magic, missingPresencePack->components[i].plgid))
                                        scan.wanted[i] = true;

                                    result = api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_PackPresenceVisitor, &scan, &visited);
                                    if (R_FAILED(result))
                                    {
                                        missingPresenceResult = result;
                                        goto missingPresenceDone;
                                    }
                                    hasAbsent = false;
                                    hasDisabled = false;
                                    hasEnabled = false;
                                    for (u32 i = 0; i < missingPresencePack->componentCount; i++)
                                    {
                                        if (!scan.wanted[i])
                                            continue;
                                        if (!scan.foundDisabled[i] && !scan.foundEnabled[i])
                                            hasAbsent = true;
                                        if (scan.foundDisabled[i] && !scan.foundEnabled[i])
                                            hasDisabled = true;
                                        if (scan.foundEnabled[i])
                                            hasEnabled = true;
                                    }

                                    missingPresenceResult = 0;
                                    missingPresenceDone:;

                                }
                                result = missingPresenceResult;
                                if (R_FAILED(result))
                                {
                                    onPackResult = result;
                                    goto onPackDone;
                                }
                                if (!hasAbsent)
                                {
                                    if (hasDisabled)
                                    {
                                        PLUGIN_onln_DrawPackDisabled(api, onPackPack);
                                        onPackResult = ONLN_PACK_DISABLED;
                                        goto onPackDone;
                                    }
                                    if (hasEnabled)
                                    {
                                        {
                                            const OnlnPack *outsideErrorPack = onPackPack;
                                            api->drawLock();
                                            api->drawClear();
                                            PLUGIN_onln_DrawPackFrame(api);
                                            if (outsideErrorPack)
                                                api->drawString(20, 40, ONLN_PACK_MISSING_COLOR, outsideErrorPack->title);
                                            api->drawString(20, 62, COLOR_RED, g_packAlreadyOutside);
                                            api->drawString(20, 84, COLOR_WHITE, g_packAlreadyOutside1);
                                            api->drawString(20, 102, COLOR_WHITE, g_packAlreadyOutside2);
                                            api->drawString(20, 128, COLOR_GRAY, g_updatePressBack);
                                            api->drawFlush();
                                            api->drawUnlock();
                                        }
                                        {
                                            onPackResult = ONLN_PACK_BAD_FORMAT;
                                            goto onPackDone;
                                        }
                                    }
                                }
                                else
                                {
                                    bool fitsSelectedResult2;
                                    {
                                        const OnlnPack *fitsSelectedPack = onPackPack;
                                        u32 loaderMissing = 0;
                                        u32 rosalinaMissing = 0;
                                        if (!fitsSelectedPack)
                                        {
                                            fitsSelectedResult2 = false;
                                            goto fitsSelectedDone2;
                                        }
                                        for (u32 i = 0; i < fitsSelectedPack->componentCount; i++)
                                        {
                                            const OnlnStackId *id = &fitsSelectedPack->components[i];
                                            if (PLUGIN_onln_FindSelected(id->magic, id->plgid))
                                                continue;
                                            if (id->magic == ONLN_LOADER_MAGIC)
                                                loaderMissing++;
                                            else
                                                rosalinaMissing++;
                                        }

                                        fitsSelectedResult2 = (g_updateLoaderCount + loaderMissing) <= ONLN_MAX_SELECTED && (g_updateRosalinaCount + rosalinaMissing) <= ONLN_MAX_SELECTED;
                                        fitsSelectedDone2:;

                                    }
                                    bool packCapacityCondition = !fitsSelectedResult2 != 0;
                                    if (packCapacityCondition)
                                    {
                                        bool packCapacityResult;
                                        {
                                            const OnlnPack *packCapacityPack = onPackPack;
                                            api->drawLock();
                                            api->drawClear();
                                            PLUGIN_onln_DrawPackFrame(api);
                                            if (packCapacityPack)
                                                api->drawString(20, 40, ONLN_PACK_MISSING_COLOR, packCapacityPack->title);
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
                                                {
                                                    packCapacityResult = false;
                                                    goto packCapacityDone;
                                                }
                                                pressed = api->waitInputWithTimeout(50);
                                                if (pressed & KEY_A)
                                                {
                                                    packCapacityResult = true;
                                                    goto packCapacityDone;
                                                }
                                                if (pressed & KEY_B)
                                                {
                                                    packCapacityResult = false;
                                                    goto packCapacityDone;
                                                }
                                            }

                                            packCapacityDone:;

                                        }
                                        packCapacityCondition = !packCapacityResult != 0;
                                    }
                                    if (packCapacityCondition)
                                    {
                                        {
                                            onPackResult = 0;
                                            goto onPackDone;
                                        }
                                    }
                                }
                            }
                            if (onPackPack->outputComplete)
                            {
                                result = PLUGIN_onln_UpdatePackComponents(api, onPackPack, false);
                                if (R_FAILED(result))
                                {
                                    onPackResult = result;
                                    goto onPackDone;
                                }
                            }
                            PLUGIN_onln_DrawPackMessage(api, onPackPack, g_packBuilding, 0);
                            result = PLUGIN_onln_BuildPack(api, onPackPack, NULL);
                            if (R_FAILED(result))
                            {
                                onPackResult = result;
                                goto onPackDone;
                            }
                            if (!api->addSysplugin || !api->addSysplugin(onPackPack->output))
                            {
                                onPackResult = ONLN_UPDATE_BAD_FILE;
                                goto onPackDone;
                            }
                            onPackPack->outputExists = true;
                            onPackPack->outputComplete = true;
                            onPackPack->pendingReboot = true;
                            result = PLUGIN_onln_ScanSelected(api);
                            if (R_FAILED(result))
                            {
                                onPackResult = result;
                                goto onPackDone;
                            }
                            PLUGIN_onln_LinkRemoteToSelected();
                            PLUGIN_onln_DrawPackMessage(api, onPackPack, g_stackInstalled, 0);
                            onPackResult = 0;
                            onPackDone:;

                        }
                        result = onPackResult;
                        if (R_FAILED(result))
                        {
                            if (result != ONLN_PACK_DISABLED &&
                                (result != ONLN_PACK_BAD_FORMAT || PLUGIN_onln_PackState(pack) != ONLN_PACK_STATE_BLUE))
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

            packListDone2:;

        }
        runPacksFinish:
        PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);

    }
}
