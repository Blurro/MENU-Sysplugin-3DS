#include <3ds.h>
#include "memory.h"
#include "csvc.h"
#include "minisoc.h"
#include "draw.h"
#include "menu.h"
#include "sysplugin_menu.h"
#include "menus.h"
#include "menus/miscellaneous.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define MENU_PLUGIN_MAGIC       0x24584E33u
#define LOADER_PLUGIN_MAGIC     0x25584E33u
#define MENU_PLUGIN_ID          0x554E454Du
#define MENU_HEADER_SIZE        0x30u
#define MENU_MAX_HOST_ITEMS     24u
#define MENU_VISIBLE_ITEMS      16u
#define MENU_ITEM_SPACING_X     6u
#define MENU_ITEM_SPACING_Y     11u
#define MENU_ITEM_TOP_Y         45u
#define MENU_TOP_DOTS_Y         34u
#define MENU_FRAME_COLOR        RGB565(2, 19, 31)
#define MENU_DATA_COPY_CHUNK    0x400u
#define MENU_MANAGE_VISIBLE_ROWS 14u
#define MENU_MANAGE_MAX_FILES    96u
#define MENU_MANAGE_NAME_BYTES   0x1800u
#define MENU_MANAGE_CHANGE_BYTES 0x1000u
#define MENU_MANAGE_TEMP_BYTES   (MENU_MANAGE_NAME_BYTES + MENU_MANAGE_CHANGE_BYTES)
#define MENU_MANAGE_SCRATCH_SIZE 0x6000u
#define MENU_MANAGE_PROMPT_Y     221u
#define MENU_MANAGE_ACTIVE_COLOR         RGB565(20, 63, 21)
#define MENU_MANAGE_DISABLED_ENTRY_COLOR RGB565(31, 36, 18)
#define MENU_MANAGE_DISABLED_LABEL_COLOR RGB8_to_565(255, 188, 80)
#define MENU_MANAGE_CURSOR_COLOR         MENU_FRAME_COLOR
#define MENU_MANAGE_DETAIL_BOTTOM_Y      209u
#define MENU_MANAGE_SECTION_ITEM_Y_OFFSET 5u
#define MENU_MANAGE_MAX_COMPONENTS 64u
#define MENU_MANAGE_MAX_MISSING    64u


typedef struct __attribute__((packed))
{
    u32 magic;
    u32 pluginId;
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
} PluginMenu3nxHeader;


typedef struct
{
    u32 expectedEnvLo;
    u32 expectedEnvHi;
} PluginMenuSeenState;

typedef struct __attribute__((packed))
{
    u32 count;
} PluginMenuDataHeader;

typedef struct __attribute__((packed))
{
    u32 pluginId;
    u32 offset;
    u32 size;
} PluginMenuDataEntry;

#define MENU_TRANSIENT_MAGIC 0x26584E33u
#define MENU_HTTPS_ID 0x73707468u
#define MENU_TRANSIENT_HEADER_SIZE 0x2Cu
#define MENU_HTTPS_HOST_API_VERSION 4u
#define MENU_HTTPS_API_VERSION 4u
#define MENU_TRANSIENT_LOW 0x10000000u
#define MENU_TRANSIENT_HIGH 0x14000000u
#define MENU_TRANSIENT_MAX_FILE_SIZE 0x20000u
#define MENU_TRANSIENT_MAX_RUNTIME_SIZE 0x30000u
#define MENU_3NX_VERSION_MAGIC 0x56584E33u
#define MENU_3ON_VERSION_MAGIC 0x4F584E33u
#define MENU_HTTPS_COMPRESSED_MAGIC MENU_3ON_VERSION_MAGIC
#define MENU_SYSPLUGIN_MAX_PLUGINS 31u
#define MENU_ONLINE_BORDER_COLOR RGB8_to_565(65, 105, 225)
#define MENU_ONLINE_TITLE_COLOR COLOR_CYAN

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
} MENUTransientHeader;

typedef struct
{
    u32 magic;
    u32 version;
    u32 compressedSize;
} PluginMenuCompressed3onHeader;
#pragma pack(pop)

typedef struct
{
    Handle file;
    u32 offset;
    u32 remaining;
    u32 buffered;
    u32 cursor;
    u8 *buffer;
    u32 bufferSize;
} PluginMenuCompressedReader;

typedef struct
{
    u32 version;
    void **hostTable;
    Result (*protectMemory)(u32, u32, MemPerm);
} MENUHttpsHostApi;

typedef struct
{
    u32 version;
    Result (*downloadToFile)(const char *url, const char *path, u32 maxSize);
    Result (*downloadToMemory)(const char *url, void *buffer, u32 bufferSize, u32 *actualSize);
    void (*openOnlineMenu)(void);
    void (*openOnlineSource)(const char *url);
} MENUHttpsApi;



typedef struct
{
    u32 base;
    u32 codeSize;
    u32 totalSize;
    bool codeProtected;
} MENUTransientImage;


typedef struct
{
    char *title;
    char *url;
} PluginMenuFetchSource;

typedef struct
{
    u32 priority;
    u16 nameOffset;
    u8 currentDisabled;
    u8 bootActive;
    u8 selected;
    u8 group;
} PluginMenuManageFile;

typedef struct
{
    u32 magic;
    u32 pluginId;
    u16 nameOffset;
    u16 reserved;
    u32 fileOffset;
} PluginMenuManageWinner;

typedef struct
{
    u32 loaderCount;
    u32 rosalinaCount;
    PluginMenuManageWinner loader[MENU_SYSPLUGIN_MAX_PLUGINS];
    PluginMenuManageWinner rosalina[MENU_SYSPLUGIN_MAX_PLUGINS];
} PluginMenuManageSelection;

typedef struct
{
    u32 componentId;
    u32 providerId;
} PluginMenuManageMissing;

typedef struct
{
    u32 componentCount;
    u32 componentIds[MENU_MANAGE_MAX_COMPONENTS];
    u8 componentModules[MENU_MANAGE_MAX_COMPONENTS];
    u32 missingCount;
    PluginMenuManageMissing missing[MENU_MANAGE_MAX_MISSING];
    u32 selectedComponentCount;
    u32 selectedMenuCount;
} PluginMenuManageActionInfo;

typedef struct __attribute__((packed))
{
    u64 key;
    u32 value;
} PluginMenuManageExportRecord;

typedef struct __attribute__((packed))
{
    u32 providerId;
    u32 count;
} PluginMenuManageRepairGroup;

typedef struct __attribute__((packed))
{
    u64 key;
    u32 cacheOffset;
    u32 addend;
} PluginMenuManageRepairRecord;

typedef struct
{
    FS_DirectoryEntry entry;
    char name[256];
    char path[272];
    char bestName[256];
    char altPath[272];
} PluginMenuScanScratch;

#define MENU_FETCH_CONFIG_MAX 0x800u
#define MENU_FETCH_MAX_SOURCES 64u
#define MENU_FETCH_BASE_MAX 1024u
#define MENU_FETCH_TITLE_MAX 46u

extern bool PLUGIN_MENU_InstallDrawStringHook(void);

PLUGIN_DATA(MENU) void *pluginTable_MENU[] = {
    (void*)svcSleepThread,
    (void*)FSUSER_OpenArchive,
    (void*)FSUSER_CloseArchive,
    (void*)FSUSER_OpenDirectory,
    (void*)FSDIR_Read,
    (void*)FSDIR_Close,
    (void*)FSUSER_OpenFile,
    (void*)FSFILE_Read,
    (void*)FSFILE_Write,
    (void*)FSFILE_SetSize,
    (void*)FSFILE_Close,
    (void*)fsMakePath,
    (void*)Draw_Lock,
    (void*)Draw_Unlock,
    (void*)Draw_ClearFramebuffer,
    (void*)Draw_DrawString,
    (void*)Draw_DrawCharacter,
    (void*)Draw_FlushFramebuffer,
    (void*)waitInput,
    (void*)&menuShouldExit,
    (void*)&rosalinaMenu,
    (void*)&miscellaneousMenu,
    (void*)svcMapProcessMemoryEx,
    (void*)svcUnmapProcessMemoryEx,
    (void*)svcQueryMemory,
    (void*)svcFlushEntireDataCache,
    (void*)svcInvalidateEntireInstructionCache,
    (void*)FSFILE_GetSize,
    (void*)FSUSER_DeleteFile,
    (void*)svcControlMemoryUnsafe,
    (void*)waitInputWithTimeout,
    (void*)srvGetServiceHandle,
    (void*)miniSocInit,
    (void*)miniSocExit,
    (void*)socSocket,
    (void*)socConnect,
    (void*)socPoll,
    (void*)socSendto,
    (void*)socRecvfrom,
    (void*)socClose,
    (void*)FSUSER_RenameFile,
    (void*)FSUSER_CreateDirectory,
};

#define MENU_HOST__svcSleepThread            ((void(*)(s64))pluginTable_MENU[0])
#define MENU_HOST__FSUSER_OpenArchive        ((Result(*)(FS_Archive*,FS_ArchiveID,FS_Path))pluginTable_MENU[1])
#define MENU_HOST__FSUSER_CloseArchive       ((Result(*)(FS_Archive))pluginTable_MENU[2])
#define MENU_HOST__FSUSER_OpenDirectory      ((Result(*)(Handle*,FS_Archive,FS_Path))pluginTable_MENU[3])
#define MENU_HOST__FSDIR_Read                ((Result(*)(Handle,u32*,u32,FS_DirectoryEntry*))pluginTable_MENU[4])
#define MENU_HOST__FSDIR_Close               ((Result(*)(Handle))pluginTable_MENU[5])
#define MENU_HOST__FSUSER_OpenFile           ((Result(*)(Handle*,FS_Archive,FS_Path,u32,u32))pluginTable_MENU[6])
#define MENU_HOST__FSFILE_Read               ((Result(*)(Handle,u32*,u64,void*,u32))pluginTable_MENU[7])
#define MENU_HOST__FSFILE_Write              ((Result(*)(Handle,u32*,u64,const void*,u32,u32))pluginTable_MENU[8])
#define MENU_HOST__FSFILE_SetSize            ((Result(*)(Handle,u64))pluginTable_MENU[9])
#define MENU_HOST__FSFILE_Close              ((Result(*)(Handle))pluginTable_MENU[10])
#define MENU_HOST__fsMakePath                ((FS_Path(*)(FS_PathType,const void*))pluginTable_MENU[11])
#define MENU_HOST__Draw_Lock                 ((void(*)(void))pluginTable_MENU[12])
#define MENU_HOST__Draw_Unlock               ((void(*)(void))pluginTable_MENU[13])
#define MENU_HOST__Draw_ClearFramebuffer     ((void(*)(void))pluginTable_MENU[14])
#define MENU_HOST__Draw_DrawString           ((u32(*)(u32,u32,u32,const char*))pluginTable_MENU[15])
#define MENU_HOST__Draw_DrawCharacter        ((void(*)(u32,u32,u32,char))pluginTable_MENU[16])
#define MENU_HOST__Draw_FlushFramebuffer     ((void(*)(void))pluginTable_MENU[17])
#define MENU_HOST__waitInput                 ((u32(*)(void))pluginTable_MENU[18])
#define MENU_HOST__menuShouldExit            (*(volatile bool*)pluginTable_MENU[19])
#define MENU_HOST__rosalinaMenu              ((Menu*)pluginTable_MENU[20])
#define MENU_HOST__miscellaneousMenu         ((Menu*)pluginTable_MENU[21])
#define MENU_HOST__svcQueryMemory             ((Result(*)(MemInfo*,PageInfo*,u32))pluginTable_MENU[24])
#define MENU_HOST__svcFlushEntireDataCache    ((void(*)(void))pluginTable_MENU[25])
#define MENU_HOST__svcInvalidateEntireInstructionCache ((void(*)(void))pluginTable_MENU[26])
#define MENU_HOST__FSFILE_GetSize            ((Result(*)(Handle,u64*))pluginTable_MENU[27])
#define MENU_HOST__FSUSER_DeleteFile         ((Result(*)(FS_Archive,FS_Path))pluginTable_MENU[28])
#define MENU_HOST__svcControlMemoryUnsafe     ((Result(*)(u32*,u32,u32,MemOp,MemPerm))pluginTable_MENU[29])
#define MENU_HOST__waitInputWithTimeout       ((u32(*)(s32))pluginTable_MENU[30])
#define MENU_HOST__srvGetServiceHandle        ((Result(*)(Handle*,const char*))pluginTable_MENU[31])
#define MENU_HOST__miniSocInit                ((Result(*)(void))pluginTable_MENU[32])
#define MENU_HOST__miniSocExit                ((Result(*)(void))pluginTable_MENU[33])
#define MENU_HOST__socSocket                  ((int(*)(int,int,int))pluginTable_MENU[34])
#define MENU_HOST__socConnect                 ((int(*)(int,const struct sockaddr*,socklen_t))pluginTable_MENU[35])
#define MENU_HOST__socPoll                    ((int(*)(struct pollfd*,nfds_t,int))pluginTable_MENU[36])
#define MENU_HOST__socSendto                  ((ssize_t(*)(int,const void*,size_t,int,const struct sockaddr*,socklen_t))pluginTable_MENU[37])
#define MENU_HOST__socRecvfrom                ((ssize_t(*)(int,void*,size_t,int,struct sockaddr*,socklen_t*))pluginTable_MENU[38])
#define MENU_HOST__socClose                   ((int(*)(int))pluginTable_MENU[39])
#define MENU_HOST__FSUSER_RenameFile          ((Result(*)(FS_Archive,FS_Path,FS_Archive,FS_Path))pluginTable_MENU[40])
#define MENU_HOST__FSUSER_CreateDirectory     ((Result(*)(FS_Archive,FS_Path,u32))pluginTable_MENU[41])

PLUGIN_RODATA(MENU) const char g_MENUEntryTitle[] = "Sysplugin Menu";
PLUGIN_RODATA(MENU) const char g_MENUUnreadText[] = "(!)";
PLUGIN_RODATA(MENU) static const char g_MENUPluginsPath[] = "/luma/plugins";
PLUGIN_RODATA(MENU) static const char g_MENUStatePath[] = "/luma/modmenu.dat";
PLUGIN_RODATA(MENU) static const char g_MENUTempPath[] = "/luma/modmenu.tmp";
PLUGIN_RODATA(MENU) static const char g_MENUEmptyPath[] = "";
PLUGIN_RODATA(MENU) static const char g_MENUManageItemTitle[] = "Manage Sysplugins...";
PLUGIN_RODATA(MENU) static const char g_MENUManageTitle[] = "Manage Sysplugins";
PLUGIN_RODATA(MENU) static const char g_MENUManageEmptyText[] = "No sysplugins found.";
PLUGIN_RODATA(MENU) static const char g_MENUManageTempListPath[] = "/luma/plugins/menutemplist.txt";
PLUGIN_RODATA(MENU) static const char g_MENUManageActive[] = "Active";
PLUGIN_RODATA(MENU) static const char g_MENUManageDisabled[] = "Disabled";
PLUGIN_RODATA(MENU) static const char g_MENUManageActionsPrompt[] = "X: actions...";
PLUGIN_RODATA(MENU) static const char g_MENUManageDisable[] = "Disable Sysplugin";
PLUGIN_RODATA(MENU) static const char g_MENUManageEnable[] = "Enable Sysplugin";
PLUGIN_RODATA(MENU) static const char g_MENUManageDelete[] = "Delete Sysplugin";
PLUGIN_RODATA(MENU) static const char g_MENUManageContains[] = "Contains";
PLUGIN_RODATA(MENU) static const char g_MENUManageWarning[] = "Warning";
PLUGIN_RODATA(MENU) static const char g_MENUManageMissingPrefix[] = "component ";
PLUGIN_RODATA(MENU) static const char g_MENUManageMissingMiddle[] = " is missing ";
PLUGIN_RODATA(MENU) static const char g_MENUManageDanger1[] = "Disabling or deleting this MENU";
PLUGIN_RODATA(MENU) static const char g_MENUManageDanger2[] = "Sysplugin may cause all other Sysplugins";
PLUGIN_RODATA(MENU) static const char g_MENUManageDanger3[] = "that use it to no longer load!";
PLUGIN_RODATA(MENU) static const char g_MENUManageScanning[] = "Scanning...";
PLUGIN_RODATA(MENU) static const char g_MENUManageConfirmTitle[] = "Are you sure?";
PLUGIN_RODATA(MENU) static const char g_MENUManageNo[] = "No";
PLUGIN_RODATA(MENU) static const char g_MENUManageYes[] = "Yes";
PLUGIN_RODATA(MENU) static const char g_MENUManageFailed[] = "Operation failed.";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineItemTitle[] = "Open Online Menu...";
PLUGIN_RODATA(MENU) static const char g_MENUFetchPath[] = "/luma/plugins/sysplgfetch.txt";
PLUGIN_RODATA(MENU) static const char g_MENUFetchTempPath[] = "/luma/plugins/sysplgfetch.tmp";
PLUGIN_RODATA(MENU) static const char g_MENUFetchNewline[] = "\n";
PLUGIN_RODATA(MENU) static const char g_MENUDots[] = "...";
PLUGIN_RODATA(MENU) static const char g_MENUPlus[] = "+";
PLUGIN_RODATA(MENU) static const char g_MENUPipe[] = "|";
PLUGIN_RODATA(MENU) static const char g_MENUSelectedLeft[] = ">>";
PLUGIN_RODATA(MENU) static const char g_MENUSelectedRight[] = "<<        ";
PLUGIN_RODATA(MENU) static const char g_MENUUnselected[] = " *";
PLUGIN_RODATA(MENU) static const char g_MENUManageCursor[] = ">";
PLUGIN_RODATA(MENU) static const char g_MENUClearRow[] =
    "                                                  ";

PLUGIN_RODATA(MENU) static const char g_MENUHttpsPath[] = "/luma/plugins/httpslib.3on";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineErrorTitle[] = "Online Menu";
PLUGIN_RODATA(MENU) static const char g_MENUOpeningHttpsText[] = "Opening httpslib.3on...";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineErrorText[] = "Could not load Online Menu";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineBackText[] = "press B to go back";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageOpenHttps[] = "open httpslib";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageInitHttps[] = "init httpslib";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageOpenArchive[] = "open SD";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageOpenFile[] = "open MENU file";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageValidate[] = "validate";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageFindMemory[] = "find memory";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageAllocate[] = "allocate";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageRead[] = "read payload";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageRelocate[] = "relocate";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageProtect[] = "protect RX";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageHttpsCreate[] = "create httpslib";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageHttpsResize[] = "resize httpslib";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageHttpsRead[] = "read embedded";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageHttpsDecode[] = "decode httpslib";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineStageHttpsWrite[] = "write httpslib";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineResultPrefix[] = "result 0x";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineHexDigits[] = "0123456789ABCDEF";
PLUGIN_RODATA(MENU) static const char g_MENUOnlinePlus[] = "+";
PLUGIN_RODATA(MENU) static const char g_MENUOnlinePipe[] = "|";
PLUGIN_RODATA(MENU) static const char g_MENUOnlineRail[] = "----------------------";
PLUGIN_DATA(MENU) static const char *g_MENUOnlineLastStage = g_MENUOnlineStageValidate;
PLUGIN_DATA(MENU) static Result g_MENUOnlineLastResult = 0;
PLUGIN_DATA(MENU) static void (*g_MENUHttpsOpenOnline)(void) = NULL;
PLUGIN_DATA(MENU) static void (*g_MENUHttpsOpenOnlineSource)(const char *url) = NULL;
PLUGIN_BSS(MENU) static MENUHttpsHostApi g_MENUHttpsHostApi;
PLUGIN_BSS(MENU) static char g_MENUOnlineResultHex[9];


PLUGIN_DATA(MENU) bool g_MENUUnread = true;
PLUGIN_BSS(MENU) bool g_MENUInsideMenu;
PLUGIN_BSS(MENU) static bool g_MENUHasExpectedEnv;
PLUGIN_BSS(MENU) u64 PLUGIN_MENU_expectedEnv;
PLUGIN_BSS(MENU) static volatile s32 g_MENURegistryLock;
PLUGIN_BSS(MENU) static volatile s32 g_MENUDataLock;
PLUGIN_BSS(MENU) static volatile s32 g_MENUFetchLock;
PLUGIN_BSS(MENU) static PluginMenuRegistration *g_MENUFirstItem;
PLUGIN_BSS(MENU) static PluginMenuRegistration *g_MENULastItem;
PLUGIN_BSS(MENU) static u32 g_MENUItemCount;
PLUGIN_BSS(MENU) static u32 g_MENURegistryGeneration;
PLUGIN_BSS(MENU) static PluginMenuRegistration g_MENUManageItem;
PLUGIN_BSS(MENU) static PluginMenuRegistration g_MENUOnlineItem;
PLUGIN_BSS(MENU) static char *g_MENUFetchEditConfig;
PLUGIN_BSS(MENU) static PluginMenuFetchSource *g_MENUFetchEditSources;
PLUGIN_BSS(MENU) static u32 g_MENUFetchScratchBase;
PLUGIN_BSS(MENU) static PluginMenuScanScratch *g_MENUScanScratch;
#define g_MENUScanEntry (g_MENUScanScratch->entry)
#define g_MENUScanName  (g_MENUScanScratch->name)
#define g_MENUScanPath  (g_MENUScanScratch->path)
#define g_MENUBestName  (g_MENUScanScratch->bestName)
#define g_MENUScanAltPath (g_MENUScanScratch->altPath)
PLUGIN_BSS(MENU) static volatile s32 g_MENUPluginFileLock;
PLUGIN_BSS(MENU) static bool g_MENUHttpsReady;
PLUGIN_BSS(MENU) static bool g_MENURootInserted;
PLUGIN_BSS(MENU) static u32 g_MENURootIndex;
PLUGIN_BSS(MENU) static u32 g_MENURootOriginalCount;
PLUGIN_BSS(MENU) static PluginMenuManageFile *g_MENUManageFiles;
PLUGIN_BSS(MENU) static char *g_MENUManageNames;
PLUGIN_BSS(MENU) static char *g_MENUManageChanges;
PLUGIN_BSS(MENU) static PluginMenuManageSelection *g_MENUManageSelection;
PLUGIN_BSS(MENU) static PluginMenuManageActionInfo *g_MENUManageActionInfo;
PLUGIN_BSS(MENU) static u32 g_MENUManageScratchBase;
PLUGIN_BSS(MENU) static u32 g_MENUManageFileCount;
PLUGIN_BSS(MENU) static u32 g_MENUManageActiveCount;
PLUGIN_BSS(MENU) static u32 g_MENUManageNameUsed;
PLUGIN_BSS(MENU) static u32 g_MENUManageChangeSize;
PLUGIN_BSS(MENU) static bool g_MENUManageCapturingBoot;

PLUGIN_CODE(MENU) bool PLUGIN_MENU_FindFreeRange(u32 size, u32 *outBase);
PLUGIN_CODE(MENU) bool PLUGIN_MENU_TempAlloc(u32 size, u32 *outBase);
PLUGIN_CODE(MENU) void PLUGIN_MENU_TempFree(u32 base, u32 size);
PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ScanAllocScratch(void);
PLUGIN_CODE(MENU) static void PLUGIN_MENU_ScanFreeScratch(void);

PLUGIN_CODE(MENU) static void PLUGIN_MENU_LockWord(volatile s32 *word)
{
    s32 *lock = (s32*)word;

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

PLUGIN_CODE(MENU) static void PLUGIN_MENU_UnlockWord(volatile s32 *word)
{
    __dmb();
    *word = 0;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_LockRegistry(void)
{
    PLUGIN_MENU_LockWord(&g_MENURegistryLock);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_UnlockRegistry(void)
{
    PLUGIN_MENU_UnlockWord(&g_MENURegistryLock);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_LockData(void)
{
    PLUGIN_MENU_LockWord(&g_MENUDataLock);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_UnlockData(void)
{
    PLUGIN_MENU_UnlockWord(&g_MENUDataLock);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_LockFetch(void)
{
    PLUGIN_MENU_LockWord(&g_MENUFetchLock);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_UnlockFetch(void)
{
    PLUGIN_MENU_UnlockWord(&g_MENUFetchLock);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_AppendInternal(
    PluginMenuRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void),
    u32 color
)
{
    if (!item || !title || !callback)
        return false;

    PLUGIN_MENU_LockRegistry();
    if (g_MENUItemCount == 0xFFFFFFFFu)
    {
        PLUGIN_MENU_UnlockRegistry();
        return false;
    }

    item->pluginId = pluginId;
    item->title = title;
    item->callback = callback;
    item->color = color;
    item->next = NULL;

    if (g_MENULastItem)
        g_MENULastItem->next = item;
    else
        g_MENUFirstItem = item;

    g_MENULastItem = item;
    g_MENUItemCount++;
    g_MENURegistryGeneration++;
    PLUGIN_MENU_UnlockRegistry();
    return true;
}

// The caller owns this record and must keep it mapped until RemoveItem returns.
PLUGIN_CODE(MENU) bool PLUGIN_MENU_AddItem(
    PluginMenuRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void),
    u32 color
)
{
    if (!item || !pluginId || !title || !callback)
        return false;

    PLUGIN_MENU_LockRegistry();

    for (PluginMenuRegistration *current = g_MENUFirstItem; current; current = current->next)
    {
        if (current == item)
        {
            current->pluginId = pluginId;
            current->title = title;
            current->callback = callback;
            current->color = color;
            g_MENURegistryGeneration++;
            PLUGIN_MENU_UnlockRegistry();
            return true;
        }
    }

    if (g_MENUItemCount == 0xFFFFFFFFu)
    {
        PLUGIN_MENU_UnlockRegistry();
        return false;
    }

    item->pluginId = pluginId;
    item->title = title;
    item->callback = callback;
    item->color = color;
    item->next = NULL;

    if (g_MENULastItem)
        g_MENULastItem->next = item;
    else
        g_MENUFirstItem = item;

    g_MENULastItem = item;
    g_MENUItemCount++;
    g_MENURegistryGeneration++;
    PLUGIN_MENU_UnlockRegistry();
    return true;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_RemoveItem(PluginMenuRegistration *item)
{
    if (!item)
        return false;

    if (item == &g_MENUManageItem)
        return false;

    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *previous = NULL;
    PluginMenuRegistration *current = g_MENUFirstItem;

    while (current && current != item)
    {
        previous = current;
        current = current->next;
    }

    if (!current)
    {
        PLUGIN_MENU_UnlockRegistry();
        return false;
    }

    if (previous)
        previous->next = current->next;
    else
        g_MENUFirstItem = current->next;

    if (g_MENULastItem == current)
        g_MENULastItem = previous;

    current->pluginId = 0;
    current->title = NULL;
    current->callback = NULL;
    current->color = 0;
    current->next = NULL;
    g_MENUItemCount--;
    g_MENURegistryGeneration++;
    PLUGIN_MENU_UnlockRegistry();
    return true;
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_StringLength(const char *text)
{
    const volatile char *p = text;
    u32 length = 0;

    while (p[length])
        length++;

    return length;
}

PLUGIN_CODE(MENU) static s32 PLUGIN_MENU_StringCompare(const char *a, const char *b)
{
    const volatile char *left = a;
    const volatile char *right = b;

    while (*left && *left == *right)
    {
        left++;
        right++;
    }

    return (s32)(u8)*left - (s32)(u8)*right;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_CopyString(char *dst, const char *src, u32 size)
{
    volatile char *out = dst;
    const volatile char *in = src;
    u32 i = 0;

    while (i + 1u < size && in[i])
    {
        out[i] = in[i];
        i++;
    }

    out[i] = 0;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_Add32(u32 a, u32 b, u32 *out)
{
    u32 value = a + b;

    if (value < a)
        return false;

    *out = value;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadExact(
    Handle file,
    u64 offset,
    void *buffer,
    u32 size
)
{
    u32 read = 0;
    return !size ||
        (R_SUCCEEDED(MENU_HOST__FSFILE_Read(file, &read, offset, buffer, size)) && read == size);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_WriteExact(
    Handle file,
    u64 offset,
    const void *buffer,
    u32 size
)
{
    u32 written = 0;
    return !size ||
        (R_SUCCEEDED(MENU_HOST__FSFILE_Write(file, &written, offset, buffer, size, FS_WRITE_FLUSH)) &&
         written == size);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadBufferRange(
    Handle file,
    u64 offset,
    void *buffer,
    u32 size
)
{
    u8 *out = (u8*)buffer;

    while (size)
    {
        u32 chunk = size < MENU_DATA_COPY_CHUNK ? size : MENU_DATA_COPY_CHUNK;
        if (!PLUGIN_MENU_ReadExact(file, offset, out, chunk))
            return false;

        offset += chunk;
        out += chunk;
        size -= chunk;
    }

    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_WriteBufferRange(
    Handle file,
    u64 offset,
    const void *buffer,
    u32 size
)
{
    const u8 *in = (const u8*)buffer;

    while (size)
    {
        u32 chunk = size < MENU_DATA_COPY_CHUNK ? size : MENU_DATA_COPY_CHUNK;
        if (!PLUGIN_MENU_WriteExact(file, offset, in, chunk))
            return false;

        offset += chunk;
        in += chunk;
        size -= chunk;
    }

    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadDataEntry(
    Handle file,
    u32 index,
    PluginMenuDataEntry *entry
)
{
    u32 offset;
    u32 scaled;

    if (index > 0xFFFFFFFFu / sizeof(*entry))
        return false;

    scaled = index * sizeof(*entry);
    if (!PLUGIN_MENU_Add32(sizeof(PluginMenuDataHeader), scaled, &offset))
        return false;

    return PLUGIN_MENU_ReadExact(file, offset, entry, sizeof(*entry));
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ValidateDataFile(
    Handle file,
    PluginMenuDataHeader *headerOut,
    u32 *fileSizeOut
)
{
    u64 fileSize64;
    PluginMenuDataHeader header;
    u32 tableBytes;
    u32 expectedOffset;
    u32 previousId = 0;

    if (R_FAILED(MENU_HOST__FSFILE_GetSize(file, &fileSize64)) ||
        fileSize64 > 0xFFFFFFFFu ||
        fileSize64 < sizeof(header) ||
        !PLUGIN_MENU_ReadExact(file, 0, &header, sizeof(header)) ||
        header.count > (0xFFFFFFFFu - sizeof(header)) / sizeof(PluginMenuDataEntry))
    {
        return false;
    }

    tableBytes = header.count * sizeof(PluginMenuDataEntry);
    if (!PLUGIN_MENU_Add32(sizeof(header), tableBytes, &expectedOffset) ||
        expectedOffset > (u32)fileSize64)
    {
        return false;
    }

    for (u32 i = 0; i < header.count; i++)
    {
        PluginMenuDataEntry entry;
        u32 blockEnd;
        u32 blockId;

        if (!PLUGIN_MENU_ReadDataEntry(file, i, &entry) ||
            !entry.pluginId ||
            (i && entry.pluginId <= previousId) ||
            entry.offset != expectedOffset ||
            !PLUGIN_MENU_Add32(entry.offset, sizeof(u32), &blockEnd) ||
            !PLUGIN_MENU_Add32(blockEnd, entry.size, &blockEnd) ||
            blockEnd > (u32)fileSize64 ||
            !PLUGIN_MENU_ReadExact(file, entry.offset, &blockId, sizeof(blockId)) ||
            blockId != entry.pluginId)
        {
            return false;
        }

        previousId = entry.pluginId;
        expectedOffset = blockEnd;
    }

    if (expectedOffset != (u32)fileSize64)
        return false;

    if (headerOut)
        *headerOut = header;
    if (fileSizeOut)
        *fileSizeOut = (u32)fileSize64;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_FindDataEntry(
    Handle file,
    const PluginMenuDataHeader *header,
    u32 pluginId,
    PluginMenuDataEntry *entryOut
)
{
    for (u32 i = 0; i < header->count; i++)
    {
        PluginMenuDataEntry entry;
        if (!PLUGIN_MENU_ReadDataEntry(file, i, &entry))
            return false;

        if (entry.pluginId == pluginId)
        {
            if (entryOut)
                *entryOut = entry;
            return true;
        }

        if (entry.pluginId > pluginId)
            break;
    }

    return false;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_GetDataSize(u32 pluginId, u32 *sizeOut)
{
    if (!pluginId || !sizeOut)
        return false;

    bool found = false;
    FS_Archive archive;
    Handle file;

    PLUGIN_MENU_LockData();

    if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenFile(
                &file,
                archive,
                MENU_HOST__fsMakePath(PATH_ASCII, g_MENUStatePath),
                FS_OPEN_READ,
                0)))
        {
            PluginMenuDataHeader header;
            PluginMenuDataEntry entry;
            if (PLUGIN_MENU_ValidateDataFile(file, &header, NULL) &&
                PLUGIN_MENU_FindDataEntry(file, &header, pluginId, &entry))
            {
                *sizeOut = entry.size;
                found = true;
            }

            MENU_HOST__FSFILE_Close(file);
        }

        MENU_HOST__FSUSER_CloseArchive(archive);
    }

    PLUGIN_MENU_UnlockData();
    return found;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_LoadData(u32 pluginId, void *data, u32 size)
{
    if (!pluginId || (size && !data))
        return false;

    bool loaded = false;
    FS_Archive archive;
    Handle file;

    PLUGIN_MENU_LockData();

    if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenFile(
                &file,
                archive,
                MENU_HOST__fsMakePath(PATH_ASCII, g_MENUStatePath),
                FS_OPEN_READ,
                0)))
        {
            PluginMenuDataHeader header;
            PluginMenuDataEntry entry;
            if (PLUGIN_MENU_ValidateDataFile(file, &header, NULL) &&
                PLUGIN_MENU_FindDataEntry(file, &header, pluginId, &entry) &&
                entry.size == size)
            {
                loaded = PLUGIN_MENU_ReadBufferRange(
                    file,
                    (u64)entry.offset + sizeof(u32),
                    data,
                    size
                );
            }

            MENU_HOST__FSFILE_Close(file);
        }

        MENU_HOST__FSUSER_CloseArchive(archive);
    }

    PLUGIN_MENU_UnlockData();
    return loaded;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_CopyDataRange(
    Handle src,
    u64 srcOffset,
    Handle dst,
    u64 dstOffset,
    u32 size,
    u8 *scratch
)
{
    while (size)
    {
        u32 chunk = size < MENU_DATA_COPY_CHUNK ? size : MENU_DATA_COPY_CHUNK;
        if (!scratch || !PLUGIN_MENU_ReadExact(src, srcOffset, scratch, chunk) ||
            !PLUGIN_MENU_WriteExact(dst, dstOffset, scratch, chunk))
        {
            return false;
        }

        srcOffset += chunk;
        dstOffset += chunk;
        size -= chunk;
    }

    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_WriteDataRecord(
    Handle newFile,
    Handle oldFile,
    bool copyOld,
    const PluginMenuDataEntry *oldEntry,
    u32 tableIndex,
    u32 *dataOffset,
    u32 pluginId,
    const void *data,
    u32 size,
    u8 *scratch
)
{
    PluginMenuDataEntry entry;
    u32 tableOffset;
    u32 scaled;
    u32 payloadOffset;
    u32 end;

    if (tableIndex > 0xFFFFFFFFu / sizeof(entry))
        return false;

    scaled = tableIndex * sizeof(entry);
    if (!PLUGIN_MENU_Add32(sizeof(PluginMenuDataHeader), scaled, &tableOffset) ||
        !PLUGIN_MENU_Add32(*dataOffset, sizeof(u32), &payloadOffset) ||
        !PLUGIN_MENU_Add32(payloadOffset, size, &end))
    {
        return false;
    }

    entry.pluginId = pluginId;
    entry.offset = *dataOffset;
    entry.size = size;

    if (!PLUGIN_MENU_WriteExact(newFile, tableOffset, &entry, sizeof(entry)) ||
        !PLUGIN_MENU_WriteExact(newFile, entry.offset, &pluginId, sizeof(pluginId)))
    {
        return false;
    }

    if (copyOld)
    {
        if (!oldEntry || oldEntry->size != size ||
            !PLUGIN_MENU_CopyDataRange(
                oldFile,
                (u64)oldEntry->offset + sizeof(u32),
                newFile,
                payloadOffset,
                size,
                scratch))
        {
            return false;
        }
    }
    else if (!PLUGIN_MENU_WriteBufferRange(newFile, payloadOffset, data, size))
    {
        return false;
    }

    *dataOffset = end;
    return true;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_SaveData(u32 pluginId, const void *data, u32 size)
{
    u32 scratchBase = 0;
    u8 *scratch = NULL;
    if (!pluginId || (size && !data))
        return false;
    if (!PLUGIN_MENU_TempAlloc(0x1000u, &scratchBase))
        return false;
    scratch = (u8 *)scratchBase;

    bool success = false;
    bool oldOpen = false;
    FS_Archive archive;
    Handle oldFile = 0;
    Handle tempFile = 0;
    Handle stateFile = 0;
    PluginMenuDataHeader oldHeader;
    u32 oldCount = 0;
    u32 preservedBytes = 0;
    bool replacing = false;

    PLUGIN_MENU_LockData();

    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        goto done;
    }

    if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenFile(
            &oldFile,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUStatePath),
            FS_OPEN_READ,
            0)))
    {
        oldOpen = true;
        if (PLUGIN_MENU_ValidateDataFile(oldFile, &oldHeader, NULL))
        {
            oldCount = oldHeader.count;
            for (u32 i = 0; i < oldCount; i++)
            {
                PluginMenuDataEntry entry;
                u32 blockSize;

                if (!PLUGIN_MENU_ReadDataEntry(oldFile, i, &entry) ||
                    !PLUGIN_MENU_Add32(sizeof(u32), entry.size, &blockSize))
                {
                    oldCount = 0;
                    preservedBytes = 0;
                    replacing = false;
                    break;
                }

                if (entry.pluginId == pluginId)
                    replacing = true;
                else if (!PLUGIN_MENU_Add32(preservedBytes, blockSize, &preservedBytes))
                {
                    oldCount = 0;
                    preservedBytes = 0;
                    replacing = false;
                    break;
                }
            }
        }
    }

    u32 newCount = oldCount + (replacing ? 0u : 1u);
    u32 tableBytes;
    u32 tableEnd;
    u32 targetBytes;
    u32 finalSize;

    if (newCount < oldCount ||
        newCount > (0xFFFFFFFFu - sizeof(PluginMenuDataHeader)) / sizeof(PluginMenuDataEntry) ||
        !PLUGIN_MENU_Add32(sizeof(u32), size, &targetBytes))
    {
        goto close_archive;
    }

    tableBytes = newCount * sizeof(PluginMenuDataEntry);
    if (!PLUGIN_MENU_Add32(sizeof(PluginMenuDataHeader), tableBytes, &tableEnd) ||
        !PLUGIN_MENU_Add32(tableEnd, preservedBytes, &finalSize) ||
        !PLUGIN_MENU_Add32(finalSize, targetBytes, &finalSize))
    {
        goto close_archive;
    }

    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &tempFile,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUTempPath),
            FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
            0)) ||
        R_FAILED(MENU_HOST__FSFILE_SetSize(tempFile, finalSize)))
    {
        goto close_archive;
    }

    PluginMenuDataHeader newHeader;
    newHeader.count = newCount;

    if (!PLUGIN_MENU_WriteExact(tempFile, 0, &newHeader, sizeof(newHeader)))
        goto close_temp;

    u32 tableIndex = 0;
    u32 dataOffset = tableEnd;
    bool targetWritten = false;

    for (u32 i = 0; i < oldCount; i++)
    {
        PluginMenuDataEntry entry;
        if (!PLUGIN_MENU_ReadDataEntry(oldFile, i, &entry))
            goto close_temp;

        if (!targetWritten && pluginId < entry.pluginId)
        {
            if (!PLUGIN_MENU_WriteDataRecord(
                    tempFile,
                    oldFile,
                    false,
                    NULL,
                    tableIndex++,
                    &dataOffset,
                    pluginId,
                    data,
                    size,
                    scratch))
            {
                goto close_temp;
            }
            targetWritten = true;
        }

        if (entry.pluginId == pluginId)
        {
            if (!PLUGIN_MENU_WriteDataRecord(
                    tempFile,
                    oldFile,
                    false,
                    NULL,
                    tableIndex++,
                    &dataOffset,
                    pluginId,
                    data,
                    size,
                    scratch))
            {
                goto close_temp;
            }
            targetWritten = true;
        }
        else if (!PLUGIN_MENU_WriteDataRecord(
                    tempFile,
                    oldFile,
                    true,
                    &entry,
                    tableIndex++,
                    &dataOffset,
                    entry.pluginId,
                    NULL,
                    entry.size,
                    scratch))
        {
            goto close_temp;
        }
    }

    if (!targetWritten &&
        !PLUGIN_MENU_WriteDataRecord(
            tempFile,
            oldFile,
            false,
            NULL,
            tableIndex++,
            &dataOffset,
            pluginId,
            data,
            size,
            scratch))
    {
        goto close_temp;
    }

    if (tableIndex != newCount || dataOffset != finalSize)
        goto close_temp;

    if (oldOpen)
    {
        MENU_HOST__FSFILE_Close(oldFile);
        oldOpen = false;
    }

    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &stateFile,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUStatePath),
            FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
            0)) ||
        R_FAILED(MENU_HOST__FSFILE_SetSize(stateFile, finalSize)) ||
        !PLUGIN_MENU_CopyDataRange(tempFile, 0, stateFile, 0, finalSize, scratch))
    {
        goto close_state;
    }

    success = true;

close_state:
    if (stateFile)
        MENU_HOST__FSFILE_Close(stateFile);
close_temp:
    if (tempFile)
        MENU_HOST__FSFILE_Close(tempFile);
    (void)MENU_HOST__FSUSER_DeleteFile(
        archive,
        MENU_HOST__fsMakePath(PATH_ASCII, g_MENUTempPath)
    );
close_archive:
    if (oldOpen)
        MENU_HOST__FSFILE_Close(oldFile);
    MENU_HOST__FSUSER_CloseArchive(archive);
done:
    PLUGIN_MENU_UnlockData();
    PLUGIN_MENU_TempFree(scratchBase, 0x1000u);
    return success;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadHeader(
    Handle file,
    u32 offset,
    PluginMenu3nxHeader *header,
    u32 *metadataOffset,
    u32 *nextOffset
)
{
    u32 end;

    if (!metadataOffset || !nextOffset ||
        !PLUGIN_MENU_ReadExact(file, offset, header, sizeof(*header)) ||
        !PLUGIN_MENU_Add32(offset, MENU_HEADER_SIZE, &end) ||
        !PLUGIN_MENU_Add32(end, header->fastRelocSize, &end) ||
        !PLUGIN_MENU_Add32(end, header->codeSize, &end) ||
        !PLUGIN_MENU_Add32(end, header->dataSize, &end) ||
        !PLUGIN_MENU_Add32(end, header->repairSize, &end) ||
        end > 0xFFFFFFF0u)
    {
        return false;
    }

    *metadataOffset = (end + 0xFu) & ~0xFu;
    return PLUGIN_MENU_Add32(*metadataOffset, header->metadataSize, nextOffset) &&
           *nextOffset > offset;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ParsePriority(
    char *name,
    u32 length,
    u32 *priority
)
{
    if (length < 7u ||
        name[length - 4u] != '.' ||
        name[length - 3u] != '3' ||
        name[length - 2u] != 'n' ||
        name[length - 1u] != 'x')
    {
        return false;
    }

    char *extension = &name[length - 4u];
    char *priorityDot = extension - 1;

    while (priorityDot > name && *priorityDot != '.')
        priorityDot--;

    if (*priorityDot != '.' || priorityDot + 1 == extension)
        return false;

    u32 value = 0;
    for (char *character = priorityDot + 1; character < extension; character++)
    {
        if (*character < '0' || *character > '9')
            return false;

        u32 digit = (u32)(*character - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u)
            return false;

        value = value * 10u + digit;
    }

    *priority = value;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_MakePluginPathTo(
    char *path,
    u32 pathSize,
    const char *name
)
{
    u32 prefixLength = PLUGIN_MENU_StringLength(g_MENUPluginsPath);
    u32 nameLength = PLUGIN_MENU_StringLength(name);

    if (!path || prefixLength + nameLength + 2u > pathSize)
        return false;

    for (u32 i = 0; i < prefixLength; i++)
        path[i] = g_MENUPluginsPath[i];

    path[prefixLength] = '/';
    for (u32 i = 0; i < nameLength; i++)
        path[prefixLength + 1u + i] = name[i];
    path[prefixLength + 1u + nameLength] = 0;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_MakePluginPath(const char *name)
{
    return PLUGIN_MENU_MakePluginPathTo(g_MENUScanPath, sizeof(g_MENUScanPath), name);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_IsEarlier(
    bool found,
    u32 priority,
    const char *name,
    u32 offset,
    u32 bestPriority,
    u32 bestOffset
)
{
    if (!found || priority != bestPriority)
        return !found || priority < bestPriority;

    s32 comparison = PLUGIN_MENU_StringCompare(name, g_MENUBestName);
    if (comparison)
        return comparison < 0;

    return offset < bestOffset;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_OpenPluginFile(
    u32 pluginId,
    PluginMenuFileContext *context
)
{
    FS_Archive archive = 0;
    Handle directory = 0;
    bool found = false;
    bool selected = true;
    bool success = false;
    u32 bestPriority = 0;
    u32 bestOffset = 0;
    u32 bestMetadataOffset = 0;
    u32 bestMetadataSize = 0;

    if (!pluginId || !context)
        return false;

    context->archive = 0;
    context->file = 0;
    context->entryOffset = 0;
    context->metadataOffset = 0;
    context->metadataSize = 0;

    PLUGIN_MENU_LockWord(&g_MENUPluginFileLock);
    if (!PLUGIN_MENU_ScanAllocScratch())
        goto done;

    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        goto done;
    }

    // Pass 1: find this ID's K11 duplicate winner using the exact
    // priority -> filename -> stack-offset order.
    if (R_FAILED(MENU_HOST__FSUSER_OpenDirectory(
            &directory,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUPluginsPath))))
    {
        goto close_archive;
    }

    for (;;)
    {
        u32 entriesRead = 0;
        if (R_FAILED(MENU_HOST__FSDIR_Read(directory, &entriesRead, 1, &g_MENUScanEntry)) ||
            !entriesRead)
        {
            break;
        }

        u32 length = 0;
        while (length + 1u < sizeof(g_MENUScanName) && g_MENUScanEntry.name[length])
        {
            g_MENUScanName[length] = (char)g_MENUScanEntry.name[length];
            length++;
        }
        g_MENUScanName[length] = 0;

        u32 priority;
        if (!PLUGIN_MENU_ParsePriority(g_MENUScanName, length, &priority) ||
            !PLUGIN_MENU_MakePluginPath(g_MENUScanName))
        {
            continue;
        }

        Handle file = 0;
        if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
                &file,
                archive,
                MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath),
                FS_OPEN_READ,
                0)))
        {
            continue;
        }

        u32 offset = 0;
        for (;;)
        {
            PluginMenu3nxHeader header;
            u32 metadataOffset;
            u32 nextOffset;

            if (!PLUGIN_MENU_ReadHeader(file, offset, &header, &metadataOffset, &nextOffset) ||
                (header.magic != MENU_PLUGIN_MAGIC && header.magic != LOADER_PLUGIN_MAGIC))
            {
                break;
            }

            if (header.magic == MENU_PLUGIN_MAGIC &&
                header.pluginId == pluginId &&
                PLUGIN_MENU_IsEarlier(
                    found,
                    priority,
                    g_MENUScanName,
                    offset,
                    bestPriority,
                    bestOffset))
            {
                found = true;
                bestPriority = priority;
                bestOffset = offset;
                bestMetadataOffset = metadataOffset;
                bestMetadataSize = header.metadataSize;
                PLUGIN_MENU_CopyString(g_MENUBestName, g_MENUScanName, sizeof(g_MENUBestName));
            }

            offset = nextOffset;
        }

        MENU_HOST__FSFILE_Close(file);
    }

    MENU_HOST__FSDIR_Close(directory);
    directory = 0;

    if (!found)
        goto close_archive;

    // Pass 2: K11 keeps only the first 31 distinct Rosalina IDs after
    // duplicate resolution. An ID is ahead of our target iff any candidate
    // for that ID sorts ahead of our target's winner, because that ID's own
    // winner can only sort even earlier.
    {
        u32 earlierIds[MENU_SYSPLUGIN_MAX_PLUGINS];
        u32 earlierCount = 0;

        if (R_FAILED(MENU_HOST__FSUSER_OpenDirectory(
                &directory,
                archive,
                MENU_HOST__fsMakePath(PATH_ASCII, g_MENUPluginsPath))))
        {
            goto close_archive;
        }

        for (;;)
        {
            u32 entriesRead = 0;
            if (R_FAILED(MENU_HOST__FSDIR_Read(directory, &entriesRead, 1, &g_MENUScanEntry)) ||
                !entriesRead)
            {
                break;
            }

            u32 length = 0;
            while (length + 1u < sizeof(g_MENUScanName) && g_MENUScanEntry.name[length])
            {
                g_MENUScanName[length] = (char)g_MENUScanEntry.name[length];
                length++;
            }
            g_MENUScanName[length] = 0;

            u32 priority;
            if (!PLUGIN_MENU_ParsePriority(g_MENUScanName, length, &priority) ||
                !PLUGIN_MENU_MakePluginPath(g_MENUScanName))
            {
                continue;
            }

            Handle file = 0;
            if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
                    &file,
                    archive,
                    MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath),
                    FS_OPEN_READ,
                    0)))
            {
                continue;
            }

            u32 offset = 0;
            for (;;)
            {
                PluginMenu3nxHeader header;
                u32 metadataOffset;
                u32 nextOffset;

                if (!PLUGIN_MENU_ReadHeader(file, offset, &header, &metadataOffset, &nextOffset) ||
                    (header.magic != MENU_PLUGIN_MAGIC && header.magic != LOADER_PLUGIN_MAGIC))
                {
                    break;
                }

                if (header.magic == MENU_PLUGIN_MAGIC &&
                    header.pluginId != pluginId &&
                    PLUGIN_MENU_IsEarlier(
                        true,
                        priority,
                        g_MENUScanName,
                        offset,
                        bestPriority,
                        bestOffset))
                {
                    bool duplicate = false;
                    for (u32 i = 0; i < earlierCount; i++)
                    {
                        if (earlierIds[i] == header.pluginId)
                        {
                            duplicate = true;
                            break;
                        }
                    }

                    if (!duplicate)
                    {
                        if (earlierCount >= MENU_SYSPLUGIN_MAX_PLUGINS - 1u)
                        {
                            selected = false;
                            break;
                        }
                        earlierIds[earlierCount++] = header.pluginId;
                    }
                }

                offset = nextOffset;
            }

            MENU_HOST__FSFILE_Close(file);
            if (!selected)
                break;
        }

        MENU_HOST__FSDIR_Close(directory);
        directory = 0;
    }

    if (!selected || !PLUGIN_MENU_MakePluginPath(g_MENUBestName))
        goto close_archive;

    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &context->file,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath),
            FS_OPEN_READ,
            0)))
    {
        context->file = 0;
        goto close_archive;
    }

    // Re-read the selected entry from the handle we are returning. This also
    // guards against the file being replaced between the directory scans and
    // the final open.
    {
        PluginMenu3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        if (!PLUGIN_MENU_ReadHeader(
                context->file,
                bestOffset,
                &header,
                &metadataOffset,
                &nextOffset) ||
            header.magic != MENU_PLUGIN_MAGIC ||
            header.pluginId != pluginId ||
            metadataOffset != bestMetadataOffset ||
            header.metadataSize != bestMetadataSize)
        {
            MENU_HOST__FSFILE_Close(context->file);
            context->file = 0;
            goto close_archive;
        }
    }

    context->archive = archive;
    context->entryOffset = bestOffset;
    context->metadataOffset = bestMetadataOffset;
    context->metadataSize = bestMetadataSize;
    archive = 0;
    success = true;
    goto done;

close_archive:
    if (directory)
        MENU_HOST__FSDIR_Close(directory);
    if (archive)
        MENU_HOST__FSUSER_CloseArchive(archive);
done:
    PLUGIN_MENU_ScanFreeScratch();
    PLUGIN_MENU_UnlockWord(&g_MENUPluginFileLock);
    return success;
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_ClosePluginFile(PluginMenuFileContext *context)
{
    if (!context)
        return;

    if (context->file)
        MENU_HOST__FSFILE_Close(context->file);
    if (context->archive)
        MENU_HOST__FSUSER_CloseArchive(context->archive);

    context->archive = 0;
    context->file = 0;
    context->entryOffset = 0;
    context->metadataOffset = 0;
    context->metadataSize = 0;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_LoadSeenState(
    const PluginMenuFileContext *selfFile
)
{
    bool foundEnvironment = false;

    g_MENUUnread = true;
    g_MENUHasExpectedEnv = false;
    PLUGIN_MENU_expectedEnv = 0;

    if (selfFile && selfFile->file)
    {
        PluginMenu3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;

        if (PLUGIN_MENU_ReadHeader(
                selfFile->file,
                selfFile->entryOffset,
                &header,
                &metadataOffset,
                &nextOffset) &&
            header.magic == MENU_PLUGIN_MAGIC &&
            header.pluginId == MENU_PLUGIN_ID &&
            (header.expectedEnvLo || header.expectedEnvHi))
        {
            u32 expectedEnvLo = header.expectedEnvLo & ~1u;
            PLUGIN_MENU_expectedEnv = ((u64)header.expectedEnvHi << 32) | expectedEnvLo;
            g_MENUHasExpectedEnv = true;
            foundEnvironment = true;
        }
    }

    if (foundEnvironment)
    {
        PluginMenuSeenState state;
        if (PLUGIN_MENU_LoadData(MENU_PLUGIN_ID, &state, sizeof(state)) &&
            state.expectedEnvLo == (u32)PLUGIN_MENU_expectedEnv &&
            state.expectedEnvHi == (u32)(PLUGIN_MENU_expectedEnv >> 32))
        {
            g_MENUUnread = false;
        }
    }
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_SaveSeenState(void)
{
    if (!g_MENUHasExpectedEnv)
        return false;

    PluginMenuSeenState state;
    state.expectedEnvLo = (u32)PLUGIN_MENU_expectedEnv;
    state.expectedEnvHi = (u32)(PLUGIN_MENU_expectedEnv >> 32);
    return PLUGIN_MENU_SaveData(MENU_PLUGIN_ID, &state, sizeof(state));
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_GetRegistryState(
    u32 *count,
    u32 *generation
)
{
    PLUGIN_MENU_LockRegistry();
    *count = g_MENUItemCount;
    *generation = g_MENURegistryGeneration;
    PLUGIN_MENU_UnlockRegistry();
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_SnapshotItems(
    u32 first,
    const char **titles,
    u32 *colors
)
{
    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *item = g_MENUFirstItem;
    for (u32 i = 0; item && i < first; i++)
        item = item->next;

    u32 count = 0;
    while (item && count < MENU_VISIBLE_ITEMS)
    {
        titles[count] = item->title;
        colors[count] = item->color;
        count++;
        item = item->next;
    }

    PLUGIN_MENU_UnlockRegistry();
    return count;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_GetPresentation(
    u32 index,
    const char **title,
    u32 *color
)
{
    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *item = g_MENUFirstItem;
    for (u32 i = 0; item && i < index; i++)
        item = item->next;

    if (item)
    {
        *title = item->title;
        *color = item->color;
    }

    PLUGIN_MENU_UnlockRegistry();
    return item != NULL;
}

PLUGIN_CODE(MENU) static void (*PLUGIN_MENU_GetCallback(u32 index))(void)
{
    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *item = g_MENUFirstItem;
    for (u32 i = 0; item && i < index; i++)
        item = item->next;

    void (*callback)(void) = item ? item->callback : NULL;
    PLUGIN_MENU_UnlockRegistry();
    return callback;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_DrawFrame(const char *title)
{
    MENU_HOST__Draw_DrawString(10, 8, MENU_FRAME_COLOR, g_MENUPlus);
    for (u32 i = 0; i < 35u; i++)
        MENU_HOST__Draw_DrawCharacter(16u + i * MENU_ITEM_SPACING_X, 8, MENU_FRAME_COLOR, '-');
    MENU_HOST__Draw_DrawString(222, 8, MENU_FRAME_COLOR, g_MENUPlus);
    MENU_HOST__Draw_DrawString(10, 16, MENU_FRAME_COLOR, g_MENUPipe);
    MENU_HOST__Draw_DrawString(222, 16, MENU_FRAME_COLOR, g_MENUPipe);
    MENU_HOST__Draw_DrawString(10, 24, MENU_FRAME_COLOR, g_MENUPlus);
    for (u32 i = 0; i < 35u; i++)
        MENU_HOST__Draw_DrawCharacter(16u + i * MENU_ITEM_SPACING_X, 24, MENU_FRAME_COLOR, '-');
    MENU_HOST__Draw_DrawString(222, 24, MENU_FRAME_COLOR, g_MENUPlus);
    MENU_HOST__Draw_DrawString(20, 16, COLOR_WHITE, title);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_DrawItem(
    u32 y,
    bool selected,
    const char *title,
    u32 color
)
{
    if (selected)
    {
        MENU_HOST__Draw_DrawString(15, y, COLOR_ORANGE, g_MENUSelectedLeft);
        MENU_HOST__Draw_DrawString(35, y, COLOR_CYAN, title);
        MENU_HOST__Draw_DrawString(250, y, COLOR_ORANGE, g_MENUSelectedRight);
    }
    else
    {
        MENU_HOST__Draw_DrawString(15, y, COLOR_GRAY, g_MENUUnselected);
        MENU_HOST__Draw_DrawString(35, y, color, title);
    }
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_Draw(u32 first, u32 selected, u32 total)
{
    const char *titles[MENU_VISIBLE_ITEMS];
    u32 colors[MENU_VISIBLE_ITEMS];
    u32 shown = PLUGIN_MENU_SnapshotItems(first, titles, colors);

    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    PLUGIN_MENU_DrawFrame(g_MENUEntryTitle);

    if (first)
        MENU_HOST__Draw_DrawString(35, MENU_TOP_DOTS_Y, COLOR_GRAY, g_MENUDots);

    for (u32 i = 0; i < shown; i++)
    {
        PLUGIN_MENU_DrawItem(
            MENU_ITEM_TOP_Y + i * MENU_ITEM_SPACING_Y,
            first + i == selected,
            titles[i],
            colors[i]
        );
    }

    if (first + shown < total)
    {
        MENU_HOST__Draw_DrawString(
            35,
            MENU_ITEM_TOP_Y + MENU_VISIBLE_ITEMS * MENU_ITEM_SPACING_Y,
            COLOR_GRAY,
            g_MENUDots
        );
    }

    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_RedrawSelection(
    u32 first,
    u32 oldSelected,
    u32 selected
)
{
    const char *oldTitle;
    const char *newTitle;
    u32 oldColor;
    u32 newColor;

    if (!PLUGIN_MENU_GetPresentation(oldSelected, &oldTitle, &oldColor) ||
        !PLUGIN_MENU_GetPresentation(selected, &newTitle, &newColor))
    {
        return;
    }

    u32 oldY = MENU_ITEM_TOP_Y + (oldSelected - first) * MENU_ITEM_SPACING_Y;
    u32 newY = MENU_ITEM_TOP_Y + (selected - first) * MENU_ITEM_SPACING_Y;

    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_MENUClearRow);
    MENU_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_MENUClearRow);
    PLUGIN_MENU_DrawItem(oldY, false, oldTitle, oldColor);
    PLUGIN_MENU_DrawItem(newY, true, newTitle, newColor);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ClearForCallback(void)
{
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

extern Result PLUGIN_MENU_OnlineSvcGetProcessId(u32 *processId, Handle process);
extern Result PLUGIN_MENU_OnlineSvcOpenProcess(Handle *process, u32 processId);
extern Result PLUGIN_MENU_OnlineSvcControlProcessMemory(
    Handle process,
    u32 addr0,
    u32 addr1,
    u32 size,
    u32 operation,
    u32 permission
);
extern Result PLUGIN_MENU_OnlineSvcCloseHandle(Handle handle);

__asm__(
    ".arm\n"
    ".section .plugin_MENU, \"ax\", %progbits\n"
    ".balign 4\n"
    ".global PLUGIN_MENU_OnlineSvcGetProcessId\n"
    ".type PLUGIN_MENU_OnlineSvcGetProcessId, %function\n"
    "PLUGIN_MENU_OnlineSvcGetProcessId:\n"
    "push {r0}\n"
    "mov r0, r1\n"
    "svc 0x35\n"
    "pop {r2}\n"
    "str r1, [r2]\n"
    "bx lr\n"
    ".global PLUGIN_MENU_OnlineSvcOpenProcess\n"
    ".type PLUGIN_MENU_OnlineSvcOpenProcess, %function\n"
    "PLUGIN_MENU_OnlineSvcOpenProcess:\n"
    "push {r0}\n"
    "mov r0, r1\n"
    "svc 0x33\n"
    "pop {r2}\n"
    "str r1, [r2]\n"
    "bx lr\n"
    ".global PLUGIN_MENU_OnlineSvcControlProcessMemory\n"
    ".type PLUGIN_MENU_OnlineSvcControlProcessMemory, %function\n"
    "PLUGIN_MENU_OnlineSvcControlProcessMemory:\n"
    "push {r4, r5}\n"
    "ldr r4, [sp, #8]\n"
    "ldr r5, [sp, #12]\n"
    "svc 0x70\n"
    "pop {r4, r5}\n"
    "bx lr\n"
    ".global PLUGIN_MENU_OnlineSvcCloseHandle\n"
    ".type PLUGIN_MENU_OnlineSvcCloseHandle, %function\n"
    "PLUGIN_MENU_OnlineSvcCloseHandle:\n"
    "svc 0x23\n"
    "bx lr\n"
);

PLUGIN_CODE(MENU) static Result PLUGIN_MENU_OnlineProtect(u32 address, u32 size, MemPerm permission)
{
    u32 processId = 0;
    Handle process = 0;
    Result result = PLUGIN_MENU_OnlineSvcGetProcessId(&processId, CUR_PROCESS_HANDLE);

    if (R_FAILED(result))
        return result;

    result = PLUGIN_MENU_OnlineSvcOpenProcess(&process, processId);
    if (R_FAILED(result))
        return result;

    result = PLUGIN_MENU_OnlineSvcControlProcessMemory(
        process,
        address,
        0,
        size,
        MEMOP_PROT,
        permission
    );
    (void)PLUGIN_MENU_OnlineSvcCloseHandle(process);
    return result;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_OnlineAdd32(u32 a, u32 b, u32 *out)
{
    u32 value = a + b;
    if (value < a)
        return false;
    *out = value;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_OnlineMul32(u32 a, u32 b, u32 *out)
{
    if (a && b > 0xFFFFFFFFu / a)
        return false;
    *out = a * b;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_OnlineAlignPage(u32 value, u32 *out)
{
    if (value > 0xFFFFF000u)
        return false;
    *out = (value + 0xFFFu) & ~0xFFFu;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_OnlineAlign16(u32 value, u32 *out)
{
    if (value > 0xFFFFFFF0u)
        return false;
    *out = (value + 0xFu) & ~0xFu;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_OnlineReadExact(
    Handle file,
    u32 offset,
    void *buffer,
    u32 size
)
{
    u32 read = 0;
    return R_SUCCEEDED(MENU_HOST__FSFILE_Read(file, &read, offset, buffer, size)) && read == size;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_OnlineResolveTransientLayout(
    Handle file,
    u32 fileSize,
    const MENUTransientHeader *header,
    u32 *headerSize
)
{
    u32 end;
    u32 metadataOffset;
    u32 total;
    u32 metadataSize = 0;

    if (!header || !headerSize)
        return false;

    if (fileSize >= 0x30u &&
        PLUGIN_MENU_OnlineReadExact(file, 0x2Cu, &metadataSize, sizeof(metadataSize)) &&
        metadataSize >= 8u &&
        (metadataSize & 0xFu) == 0 &&
        PLUGIN_MENU_OnlineAdd32(0x30u, header->fastRelocSize, &end) &&
        PLUGIN_MENU_OnlineAdd32(end, header->codeSize, &end) &&
        PLUGIN_MENU_OnlineAdd32(end, header->dataSize, &end) &&
        PLUGIN_MENU_OnlineAdd32(end, header->repairSize, &end) &&
        PLUGIN_MENU_OnlineAlign16(end, &metadataOffset) &&
        PLUGIN_MENU_OnlineAdd32(metadataOffset, metadataSize, &total) &&
        total == fileSize)
    {
        *headerSize = 0x30u;
        return true;
    }

    if (PLUGIN_MENU_OnlineAdd32(MENU_TRANSIENT_HEADER_SIZE, header->fastRelocSize, &end) &&
        PLUGIN_MENU_OnlineAdd32(end, header->codeSize, &end) &&
        PLUGIN_MENU_OnlineAdd32(end, header->dataSize, &end) &&
        PLUGIN_MENU_OnlineAdd32(end, header->repairSize, &end) &&
        PLUGIN_MENU_OnlineAlign16(end, &total) &&
        total == fileSize)
    {
        *headerSize = MENU_TRANSIENT_HEADER_SIZE;
        return true;
    }

    return false;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_FindFreeRange(u32 size, u32 *outBase)
{
    u32 scan = MENU_TRANSIENT_LOW;

    while (scan < MENU_TRANSIENT_HIGH)
    {
        MemInfo info;
        PageInfo page;
        u32 next;

        if (R_FAILED(MENU_HOST__svcQueryMemory(&info, &page, scan)))
            return false;

        if (info.state == MEMSTATE_FREE)
        {
            u32 base = info.base_addr;
            u32 end;
            if (base < MENU_TRANSIENT_LOW)
                base = MENU_TRANSIENT_LOW;
            if (base <= 0xFFFFFFFFu - size &&
                PLUGIN_MENU_OnlineAdd32(base, size, &end) &&
                end <= MENU_TRANSIENT_HIGH &&
                end <= info.base_addr + info.size)
            {
                *outBase = base;
                return true;
            }
        }

        next = info.base_addr + info.size;
        if (next <= scan)
            return false;
        scan = next;
    }
    return false;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_TempAlloc(u32 size, u32 *outBase)
{
    u32 base;
    u32 allocated = 0;

    if (!outBase || !size || (size & 0xFFFu) || !PLUGIN_MENU_FindFreeRange(size, &base))
        return false;

    Result result = MENU_HOST__svcControlMemoryUnsafe(
        &allocated,
        base,
        size,
        MEMOP_ALLOC | MEMOP_REGION_SYSTEM,
        MEMPERM_READWRITE
    );
    if (R_FAILED(result) || !allocated)
        return false;

    *outBase = allocated;
    return true;
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_TempFree(u32 base, u32 size)
{
    u32 out;
    if (!base || !size)
        return;
    (void)MENU_HOST__svcControlMemoryUnsafe(
        &out,
        base,
        size,
        MEMOP_FREE | MEMOP_REGION_SYSTEM,
        MEMPERM_DONTCARE
    );
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_FetchAllocScratch(void)
{
    u32 base = 0;
    if (g_MENUFetchScratchBase)
        return true;
    if (!PLUGIN_MENU_TempAlloc(0x1000u, &base))
        return false;
    g_MENUFetchScratchBase = base;
    g_MENUFetchEditConfig = (char *)base;
    g_MENUFetchEditSources = (PluginMenuFetchSource *)(base + MENU_FETCH_CONFIG_MAX);
    return true;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_FetchFreeScratch(void)
{
    if (g_MENUFetchScratchBase)
        PLUGIN_MENU_TempFree(g_MENUFetchScratchBase, 0x1000u);
    g_MENUFetchScratchBase = 0;
    g_MENUFetchEditConfig = NULL;
    g_MENUFetchEditSources = NULL;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ScanAllocScratch(void)
{
    u32 base = 0;
    if (g_MENUScanScratch)
        return true;
    if (!PLUGIN_MENU_TempAlloc(0x1000u, &base))
        return false;
    g_MENUScanScratch = (PluginMenuScanScratch *)base;
    return true;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ScanFreeScratch(void)
{
    if (g_MENUScanScratch)
        PLUGIN_MENU_TempFree((u32)g_MENUScanScratch, 0x1000u);
    g_MENUScanScratch = NULL;
}


PLUGIN_CODE(MENU) static bool PLUGIN_MENU_OnlineValidateRepair(
    Handle file,
    u32 repairOffset,
    u32 repairSize
)
{
    u32 exportCount;
    u32 exportBytes;

    if (repairSize < 4u || !PLUGIN_MENU_OnlineReadExact(file, repairOffset, &exportCount, 4u))
        return false;
    if (!PLUGIN_MENU_OnlineMul32(exportCount, 12u, &exportBytes))
        return false;
    return exportBytes == repairSize - 4u;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_OnlineApplyRelocs(
    Handle file,
    u32 relocOffset,
    u32 relocSize,
    u32 expectedId,
    u32 runtimeBase,
    u32 runtimeSize
)
{
    u32 cursor = 0;

    while (cursor < relocSize)
    {
        u32 group[2];
        u32 pairBytes;

        if (relocSize - cursor < 8u ||
            !PLUGIN_MENU_OnlineReadExact(file, relocOffset + cursor, group, 8u) ||
            group[0] != expectedId ||
            !PLUGIN_MENU_OnlineMul32(group[1], 8u, &pairBytes) ||
            pairBytes > relocSize - cursor - 8u)
        {
            return false;
        }

        cursor += 8u;
        for (u32 i = 0; i < group[1]; i++)
        {
            u32 pair[2];

            if (!PLUGIN_MENU_OnlineReadExact(file, relocOffset + cursor, pair, 8u) ||
                (pair[0] & 3u) ||
                pair[0] > runtimeSize - 4u ||
                pair[1] >= runtimeSize)
            {
                return false;
            }

            *(volatile u32 *)(runtimeBase + pair[0]) = runtimeBase + pair[1];
            cursor += 8u;
        }
    }

    return cursor == relocSize;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_OnlineFree(MENUTransientImage *image)
{
    if (!image || !image->base)
        return;

    if (image->codeProtected && image->codeSize)
        (void)PLUGIN_MENU_OnlineProtect(image->base, image->codeSize, MEMPERM_READWRITE);

    PLUGIN_MENU_TempFree(image->base, image->totalSize);
    image->base = 0;
    image->codeSize = 0;
    image->totalSize = 0;
    image->codeProtected = false;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_OnlineSetFailure(const char *stage, Result result)
{
    u32 value = (u32)result;

    g_MENUOnlineLastStage = stage;
    g_MENUOnlineLastResult = result;
    for (u32 i = 0; i < 8; i++)
        g_MENUOnlineResultHex[i] = g_MENUOnlineHexDigits[(value >> ((7u - i) * 4u)) & 0xFu];
    g_MENUOnlineResultHex[8] = 0;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_LoadTransient(
    const char *path,
    u32 expectedId,
    const char *openStage,
    MENUTransientImage *image
)
{
    FS_Archive archive = 0;
    Handle file = 0;
    MENUTransientHeader header;
    u64 fileSize64 = 0;
    u32 fileEnd;
    u32 payloadHeaderSize;
    u32 codeOffset;
    u32 dataOffset;
    u32 repairOffset;
    u32 codeSize;
    u32 dataAndBssSize;
    u32 dataSize;
    u32 totalSize;
    u32 base = 0;
    u32 dataAddress;
    bool archiveOpen = false;
    bool fileOpen = false;
    bool memoryAllocated = false;

    if (!image)
        return false;
    image->base = 0;
    image->codeSize = 0;
    image->totalSize = 0;
    image->codeProtected = false;

    {
        Result result = MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath));
        if (R_FAILED(result))
        {
            PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageOpenArchive, result);
            goto fail;
        }
    }
    archiveOpen = true;

    {
        Result result = MENU_HOST__FSUSER_OpenFile(
            &file,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, path),
            FS_OPEN_READ,
            0);
        if (R_FAILED(result))
        {
            PLUGIN_MENU_OnlineSetFailure(openStage, result);
            goto fail;
        }
    }
    fileOpen = true;

    if (R_FAILED(MENU_HOST__FSFILE_GetSize(file, &fileSize64)) ||
        fileSize64 < MENU_TRANSIENT_HEADER_SIZE ||
        fileSize64 > MENU_TRANSIENT_MAX_FILE_SIZE ||
        !PLUGIN_MENU_OnlineReadExact(file, 0, &header, sizeof(header)) ||
        header.magic != MENU_TRANSIENT_MAGIC ||
        header.plgid != expectedId ||
        !header.codeSize ||
        !PLUGIN_MENU_OnlineResolveTransientLayout(file, (u32)fileSize64, &header, &payloadHeaderSize) ||
        !PLUGIN_MENU_OnlineAdd32(payloadHeaderSize, header.fastRelocSize, &codeOffset) ||
        !PLUGIN_MENU_OnlineAdd32(codeOffset, header.codeSize, &dataOffset) ||
        !PLUGIN_MENU_OnlineAdd32(dataOffset, header.dataSize, &repairOffset) ||
        !PLUGIN_MENU_OnlineAdd32(repairOffset, header.repairSize, &fileEnd) ||
        fileEnd > (u32)fileSize64 ||
        !PLUGIN_MENU_OnlineAlignPage(header.codeSize, &codeSize) ||
        !PLUGIN_MENU_OnlineAdd32(header.dataSize, header.bssSize, &dataAndBssSize) ||
        !PLUGIN_MENU_OnlineAlignPage(dataAndBssSize, &dataSize) ||
        !PLUGIN_MENU_OnlineAdd32(codeSize, dataSize, &totalSize) ||
        !totalSize ||
        totalSize > MENU_TRANSIENT_MAX_RUNTIME_SIZE ||
        !PLUGIN_MENU_OnlineValidateRepair(file, repairOffset, header.repairSize))
    {
        PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageValidate, (Result)0xD8A0A046u);
        goto fail;
    }

    if (!PLUGIN_MENU_TempAlloc(totalSize, &base))
    {
        PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageAllocate, (Result)0xD8A0A047u);
        goto fail;
    }
    memoryAllocated = true;
    dataAddress = base + codeSize;

    if (!PLUGIN_MENU_OnlineReadExact(file, codeOffset, (void *)base, header.codeSize) ||
        (header.dataSize && !PLUGIN_MENU_OnlineReadExact(file, dataOffset, (void *)dataAddress, header.dataSize)))
    {
        PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageRead, (Result)0xD8A0A048u);
        goto fail;
    }

    for (u32 i = 0; i < header.bssSize; i++)
        *(volatile u8 *)(dataAddress + header.dataSize + i) = 0;

    if (!PLUGIN_MENU_OnlineApplyRelocs(
            file,
            payloadHeaderSize,
            header.fastRelocSize,
            expectedId,
            base,
            totalSize))
    {
        PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageRelocate, (Result)0xD8A0A049u);
        goto fail;
    }

    MENU_HOST__FSFILE_Close(file);
    file = 0;
    fileOpen = false;
    MENU_HOST__FSUSER_CloseArchive(archive);
    archive = 0;
    archiveOpen = false;

    MENU_HOST__svcFlushEntireDataCache();
    {
        Result result = PLUGIN_MENU_OnlineProtect(base, codeSize, MEMPERM_READEXECUTE);
        if (R_FAILED(result))
        {
            PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageProtect, result);
            goto fail;
        }
    }
    MENU_HOST__svcInvalidateEntireInstructionCache();

    image->base = base;
    image->codeSize = codeSize;
    image->totalSize = totalSize;
    image->codeProtected = true;
    return true;

fail:
    if (fileOpen)
        MENU_HOST__FSFILE_Close(file);
    if (archiveOpen)
        MENU_HOST__FSUSER_CloseArchive(archive);
    if (memoryAllocated)
    {
        MENUTransientImage temporary = { base, codeSize, totalSize, false };
        PLUGIN_MENU_OnlineFree(&temporary);
    }
    return false;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_GetHttpsVersion(FS_Archive archive, u32 *version)
{
    Handle file = 0;
    u32 header[12];
    u32 offset, end;
    u32 versionWords[2];
    u64 fileSize = 0;
    bool ok = false;
    if (!version || R_FAILED(MENU_HOST__FSUSER_OpenFile(&file, archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUHttpsPath), FS_OPEN_READ, 0)))
        return false;
    if (R_SUCCEEDED(MENU_HOST__FSFILE_GetSize(file, &fileSize)) && fileSize >= 0x30u &&
        fileSize <= MENU_TRANSIENT_MAX_FILE_SIZE &&
        PLUGIN_MENU_OnlineReadExact(file, 0, header, sizeof(header)) &&
        header[0] == MENU_TRANSIENT_MAGIC && header[1] == MENU_HTTPS_ID && header[2] &&
        header[11] >= 8u && !(header[11] & 0xFu) &&
        PLUGIN_MENU_OnlineAdd32(0x30u, header[5], &end) &&
        PLUGIN_MENU_OnlineAdd32(end, header[2], &end) &&
        PLUGIN_MENU_OnlineAdd32(end, header[3], &end) &&
        PLUGIN_MENU_OnlineAdd32(end, header[6], &end) &&
        PLUGIN_MENU_OnlineAlign16(end, &offset) &&
        PLUGIN_MENU_OnlineAdd32(offset, header[11], &end) && end == (u32)fileSize &&
        PLUGIN_MENU_OnlineReadExact(file, offset, versionWords, sizeof(versionWords)) &&
        versionWords[0] == MENU_3ON_VERSION_MAGIC)
    { *version = versionWords[1]; ok = true; }
    MENU_HOST__FSFILE_Close(file);
    return ok;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_CompressedGetByte(
    PluginMenuCompressedReader *reader,
    u8 *value
)
{
    if (!reader || !value)
        return false;

    if (reader->cursor >= reader->buffered)
    {
        if (!reader->remaining)
            return false;

        u32 chunk = reader->remaining;
        if (!reader->buffer || !reader->bufferSize)
            return false;
        if (chunk > reader->bufferSize)
            chunk = reader->bufferSize;

        u32 read = 0;
        if (R_FAILED(MENU_HOST__FSFILE_Read(
                reader->file,
                &read,
                reader->offset,
                reader->buffer,
                chunk)) ||
            read != chunk)
        {
            return false;
        }

        reader->offset += chunk;
        reader->remaining -= chunk;
        reader->buffered = chunk;
        reader->cursor = 0;
    }

    *value = reader->buffer[reader->cursor++];
    return true;
}

PLUGIN_CODE(MENU) Result PLUGIN_MENU_UnpackLz10File(
    const PluginMenuFileContext *source,
    u32 compressedOffset,
    u32 compressedSize,
    const char *outputPath
)
{
    u32 scratchBase = 0;
    u8 *history = NULL;
    PluginMenuCompressedReader reader;
    Handle outputFile = 0;
    u8 lzHeader[4];
    u8 *flush = NULL;
    u32 flushSize = 0;
    u32 outputOffset = 0;
    u32 uncompressedSize;
    u32 compressedEnd;
    u64 sourceSize = 0;
    Result result = 0;

    if (!source || !source->file || !source->archive || !outputPath || !outputPath[0] ||
        compressedSize < 4u ||
        !PLUGIN_MENU_Add32(compressedOffset, compressedSize, &compressedEnd) ||
        R_FAILED(MENU_HOST__FSFILE_GetSize(source->file, &sourceSize)) ||
        compressedEnd > sourceSize)
    {
        return (Result)0xD8A0A062u;
    }

    if (!PLUGIN_MENU_TempAlloc(0x2000u, &scratchBase))
        return (Result)0xD8A0A067u;
    history = (u8 *)scratchBase;
    reader.buffer = history + 0x1000u;
    reader.bufferSize = 0x400u;
    flush = history + 0x1400u;

    // Serialize file/source-registry operations while an unpack is active.
    PLUGIN_MENU_LockFetch();

    reader.file = source->file;
    reader.offset = compressedOffset;
    reader.remaining = compressedSize;
    reader.buffered = 0;
    reader.cursor = 0;

    for (u32 i = 0; i < 4u; i++)
    {
        if (!PLUGIN_MENU_CompressedGetByte(&reader, &lzHeader[i]))
        {
            result = (Result)0xD8A0A063u;
            goto done;
        }
    }

    if (lzHeader[0] != 0x10u)
    {
        result = (Result)0xD8A0A064u;
        goto done;
    }

    uncompressedSize = (u32)lzHeader[1] |
                       ((u32)lzHeader[2] << 8) |
                       ((u32)lzHeader[3] << 16);
    if (!uncompressedSize)
    {
        result = (Result)0xD8A0A065u;
        goto done;
    }

    (void)MENU_HOST__FSUSER_DeleteFile(
        source->archive,
        MENU_HOST__fsMakePath(PATH_ASCII, outputPath)
    );

    result = MENU_HOST__FSUSER_OpenFile(
        &outputFile,
        source->archive,
        MENU_HOST__fsMakePath(PATH_ASCII, outputPath),
        FS_OPEN_WRITE | FS_OPEN_CREATE,
        0
    );
    if (R_FAILED(result))
        goto done;

    result = MENU_HOST__FSFILE_SetSize(outputFile, uncompressedSize);
    if (R_FAILED(result))
        goto done;

#define HIST_GET(pos) (history[(pos) & 0xFFFu])
#define HIST_SET(pos, val) do { history[(pos) & 0xFFFu] = (u8)(val); } while (0)
#define FLUSH_OUT() do { \
    if (flushSize) { \
        u32 _written = 0; \
        Result _result = MENU_HOST__FSFILE_Write( \
            outputFile, &_written, outputOffset - flushSize, \
            flush, flushSize, FS_WRITE_FLUSH); \
        if (R_FAILED(_result) || _written != flushSize) { \
            result = R_FAILED(_result) ? _result : (Result)0xD8A0A066u; \
            goto unpack_done; \
        } \
        flushSize = 0; \
    } \
} while (0)
#define EMIT(value) do { \
    u8 _value = (u8)(value); \
    HIST_SET(outputOffset, _value); \
    flush[flushSize++] = _value; \
    outputOffset++; \
    if (flushSize == 0xC00u) \
        FLUSH_OUT(); \
} while (0)

    while (outputOffset < uncompressedSize)
    {
        u8 flags;
        if (!PLUGIN_MENU_CompressedGetByte(&reader, &flags))
        {
            result = (Result)0xD8A0A063u;
            goto unpack_done;
        }

        for (u32 bit = 0; bit < 8u && outputOffset < uncompressedSize; bit++)
        {
            if (flags & (0x80u >> bit))
            {
                u8 a;
                u8 b;
                if (!PLUGIN_MENU_CompressedGetByte(&reader, &a) ||
                    !PLUGIN_MENU_CompressedGetByte(&reader, &b))
                {
                    result = (Result)0xD8A0A063u;
                    goto unpack_done;
                }

                u32 length = ((u32)a >> 4) + 3u;
                u32 distance = ((((u32)a & 0xFu) << 8) | b) + 1u;
                if (distance > outputOffset || length > uncompressedSize - outputOffset)
                {
                    result = (Result)0xD8A0A064u;
                    goto unpack_done;
                }

                for (u32 i = 0; i < length; i++)
                    EMIT(HIST_GET(outputOffset - distance));
            }
            else
            {
                u8 value;
                if (!PLUGIN_MENU_CompressedGetByte(&reader, &value))
                {
                    result = (Result)0xD8A0A063u;
                    goto unpack_done;
                }
                EMIT(value);
            }
        }
    }

    FLUSH_OUT();
    result = 0;

unpack_done:
#undef EMIT
#undef FLUSH_OUT
#undef HIST_SET
#undef HIST_GET

done:
    if (outputFile)
        MENU_HOST__FSFILE_Close(outputFile);
    if (R_FAILED(result))
    {
        (void)MENU_HOST__FSUSER_DeleteFile(
            source->archive,
            MENU_HOST__fsMakePath(PATH_ASCII, outputPath)
        );
    }

    PLUGIN_MENU_UnlockFetch();
    PLUGIN_MENU_TempFree(scratchBase, 0x2000u);
    return result;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_EnsureHttpslib(
    const PluginMenuFileContext *selfFile
)
{
    u32 selfVersion[2];
    PluginMenuCompressed3onHeader compressed;
    u32 installedVersion = 0;
    u32 compressedOffset;
    u32 compressedEnd;

    if (!selfFile || !selfFile->file ||
        selfFile->metadataSize < sizeof(selfVersion) + sizeof(compressed))
    {
        PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageValidate, (Result)0xD8A0A060u);
        return false;
    }

    if (!PLUGIN_MENU_OnlineReadExact(
            selfFile->file,
            selfFile->metadataOffset,
            selfVersion,
            sizeof(selfVersion)) ||
        selfVersion[0] != MENU_3NX_VERSION_MAGIC ||
        !PLUGIN_MENU_OnlineAdd32(selfFile->metadataOffset, sizeof(selfVersion), &compressedOffset) ||
        !PLUGIN_MENU_OnlineReadExact(
            selfFile->file,
            compressedOffset,
            &compressed,
            sizeof(compressed)) ||
        compressed.magic != MENU_HTTPS_COMPRESSED_MAGIC ||
        !compressed.compressedSize ||
        !PLUGIN_MENU_OnlineAdd32(compressedOffset, sizeof(compressed), &compressedOffset) ||
        !PLUGIN_MENU_OnlineAdd32(compressedOffset, compressed.compressedSize, &compressedEnd) ||
        compressedEnd > selfFile->metadataOffset + selfFile->metadataSize)
    {
        PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageValidate, (Result)0xD8A0A061u);
        return false;
    }

    if (PLUGIN_MENU_GetHttpsVersion(selfFile->archive, &installedVersion) &&
        installedVersion == compressed.version)
    {
        return true;
    }

    Result result = PLUGIN_MENU_UnpackLz10File(
        selfFile,
        compressedOffset,
        compressed.compressedSize,
        g_MENUHttpsPath
    );

    if (R_FAILED(result))
    {
        if (result == (Result)0xD8A0A063u)
            PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageHttpsRead, result);
        else if (result == (Result)0xD8A0A064u || result == (Result)0xD8A0A065u ||
                 result == (Result)0xD8A0A062u)
            PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageHttpsDecode, result);
        else
            PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageHttpsWrite, result);
        return false;
    }

    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_LoadHttpsLibrary(MENUTransientImage *image)
{
    MENUHttpsApi api;
    if(!PLUGIN_MENU_LoadTransient(g_MENUHttpsPath,MENU_HTTPS_ID,g_MENUOnlineStageOpenHttps,image))return false;
    g_MENUHttpsHostApi.version=MENU_HTTPS_HOST_API_VERSION;g_MENUHttpsHostApi.hostTable=pluginTable_MENU;g_MENUHttpsHostApi.protectMemory=PLUGIN_MENU_OnlineProtect;
    api.version=0;api.downloadToFile=NULL;api.downloadToMemory=NULL;api.openOnlineMenu=NULL;api.openOnlineSource=NULL;
    bool ok=((bool(*)(const MENUHttpsHostApi*,MENUHttpsApi*))image->base)(&g_MENUHttpsHostApi,&api);
    if(!ok||api.version!=MENU_HTTPS_API_VERSION||!api.downloadToFile||!api.downloadToMemory||!api.openOnlineMenu||!api.openOnlineSource){PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageInitHttps,(Result)0xD8A0A069u);PLUGIN_MENU_OnlineFree(image);return false;}
    g_MENUHttpsOpenOnline=api.openOnlineMenu;g_MENUHttpsOpenOnlineSource=api.openOnlineSource;return true;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_UnloadHttpsLibrary(MENUTransientImage *image)
{ g_MENUHttpsOpenOnline=NULL; g_MENUHttpsOpenOnlineSource=NULL; PLUGIN_MENU_OnlineFree(image); }

PLUGIN_CODE(MENU) static void PLUGIN_MENU_DrawOpeningHttps(void)
{
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    MENU_HOST__Draw_DrawString(10, 10, COLOR_WHITE, g_MENUOpeningHttpsText);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_DrawOnlineFrame(void)
{
    MENU_HOST__Draw_DrawString(10, 8, MENU_ONLINE_BORDER_COLOR, g_MENUOnlinePlus);
    MENU_HOST__Draw_DrawString(16, 8, MENU_ONLINE_BORDER_COLOR, g_MENUOnlineRail);
    MENU_HOST__Draw_DrawString(148, 8, MENU_ONLINE_BORDER_COLOR, g_MENUOnlinePlus);
    MENU_HOST__Draw_DrawString(10, 16, MENU_ONLINE_BORDER_COLOR, g_MENUOnlinePipe);
    MENU_HOST__Draw_DrawString(148, 16, MENU_ONLINE_BORDER_COLOR, g_MENUOnlinePipe);
    MENU_HOST__Draw_DrawString(10, 24, MENU_ONLINE_BORDER_COLOR, g_MENUOnlinePlus);
    MENU_HOST__Draw_DrawString(16, 24, MENU_ONLINE_BORDER_COLOR, g_MENUOnlineRail);
    MENU_HOST__Draw_DrawString(148, 24, MENU_ONLINE_BORDER_COLOR, g_MENUOnlinePlus);
    MENU_HOST__Draw_DrawString(20, 16, MENU_ONLINE_TITLE_COLOR, g_MENUOnlineErrorTitle);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_DrawOnlineError(void)
{
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    PLUGIN_MENU_DrawOnlineFrame();
    MENU_HOST__Draw_DrawString(20, 45, COLOR_RED, g_MENUOnlineErrorText);
    MENU_HOST__Draw_DrawString(20, 65, COLOR_WHITE, g_MENUOnlineLastStage);
    MENU_HOST__Draw_DrawString(20, 85, COLOR_WHITE, g_MENUOnlineResultPrefix);
    MENU_HOST__Draw_DrawString(80, 85, COLOR_WHITE, g_MENUOnlineResultHex);
    MENU_HOST__Draw_DrawString(20, 120, COLOR_GRAY, g_MENUOnlineBackText);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();

    do
    {
        if (MENU_HOST__waitInputWithTimeout(50) & KEY_B)
            return;
    } while (!MENU_HOST__menuShouldExit);
}

PLUGIN_CODE(MENU) static char *PLUGIN_MENU_NextFetchLine(char **cursor)
{
    char *line;
    char *end;

    if (!cursor || !*cursor)
        return NULL;

    for (;;)
    {
        while (**cursor == '\r' || **cursor == '\n')
            (*cursor)++;
        if (!**cursor)
            return NULL;

        line = *cursor;
        while (**cursor && **cursor != '\r' && **cursor != '\n')
            (*cursor)++;
        end = *cursor;
        if (**cursor)
            *(*cursor)++ = 0;

        while (end > line && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line)
            return line;
    }
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ParseFetchSources(
    char *config,
    u32 configSize,
    PluginMenuFetchSource *sources,
    u32 *countOut
)
{
    u32 count = 0;
    char *cursor;

    if (!config || !sources || configSize >= MENU_FETCH_CONFIG_MAX)
        return false;

    config[configSize] = 0;
    cursor = config;

    while (count < MENU_FETCH_MAX_SOURCES)
    {
        char *title = PLUGIN_MENU_NextFetchLine(&cursor);
        char *url;

        if (!title)
            break;

        url = PLUGIN_MENU_NextFetchLine(&cursor);
        if (!url)
            return false;

        sources[count].title = title;
        sources[count].url = url;
        count++;
    }

    if (PLUGIN_MENU_NextFetchLine(&cursor))
        return false;

    if (countOut)
        *countOut = count;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_IsValidFetchField(
    const char *text,
    u32 maxLength
)
{
    u32 length = 0;

    if (!text || !*text)
        return false;

    while (text[length])
    {
        if (text[length] == '\r' || text[length] == '\n' || length >= maxLength)
            return false;
        length++;
    }

    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadFetchEditSources(
    FS_Archive archive,
    bool allowMissing,
    bool *existsOut,
    u32 *countOut
)
{
    Handle file = 0;
    u64 size64 = 0;
    u32 count = 0;

    if (existsOut)
        *existsOut = false;
    if (countOut)
        *countOut = 0;

    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &file,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUFetchPath),
            FS_OPEN_READ,
            0)))
    {
        return allowMissing;
    }

    if (existsOut)
        *existsOut = true;

    bool ok =
        R_SUCCEEDED(MENU_HOST__FSFILE_GetSize(file, &size64)) &&
        size64 < MENU_FETCH_CONFIG_MAX &&
        (!size64 || PLUGIN_MENU_ReadExact(
            file,
            0,
            g_MENUFetchEditConfig,
            (u32)size64)) &&
        PLUGIN_MENU_ParseFetchSources(
            g_MENUFetchEditConfig,
            (u32)size64,
            g_MENUFetchEditSources,
            &count);

    MENU_HOST__FSFILE_Close(file);

    if (ok && countOut)
        *countOut = count;
    return ok;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_FetchSourceMatches(
    const PluginMenuFetchSource *source,
    const char *title,
    const char *url
)
{
    return (title && !PLUGIN_MENU_StringCompare(source->title, title)) ||
           (url && !PLUGIN_MENU_StringCompare(source->url, url));
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_AddFetchSize(
    u32 *size,
    const char *text
)
{
    u32 length = PLUGIN_MENU_StringLength(text);

    if (length >= MENU_FETCH_CONFIG_MAX - 1u ||
        *size >= MENU_FETCH_CONFIG_MAX - (length + 1u))
    {
        return false;
    }

    *size += length + 1u;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_WriteFetchLine(
    Handle file,
    u32 *offset,
    const char *text
)
{
    u32 length = PLUGIN_MENU_StringLength(text);

    if (!PLUGIN_MENU_WriteExact(file, *offset, text, length))
        return false;
    *offset += length;

    if (!PLUGIN_MENU_WriteExact(file, *offset, g_MENUFetchNewline, 1u))
        return false;
    (*offset)++;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_CopyFetchFile(
    Handle source,
    Handle destination,
    u32 size
)
{
    u32 offset = 0;

    while (offset < size)
    {
        u32 chunk = size - offset;
        if (chunk > MENU_FETCH_CONFIG_MAX)
            chunk = MENU_FETCH_CONFIG_MAX;

        if (!PLUGIN_MENU_ReadExact(source, offset, g_MENUFetchEditConfig, chunk) ||
            !PLUGIN_MENU_WriteExact(destination, offset, g_MENUFetchEditConfig, chunk))
        {
            return false;
        }

        offset += chunk;
    }

    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_WriteFetchEditSources(
    FS_Archive archive,
    const char *replacementTitle,
    const char *replacementUrl,
    bool replaceMatches,
    const char *removeTitle,
    u32 oldCount
)
{
    Handle tempFile = 0;
    Handle targetFile = 0;
    u32 finalSize = 0;
    u32 finalCount = 0;
    bool replacementWritten = false;
    bool success = false;

    for (u32 i = 0; i < oldCount; i++)
    {
        PluginMenuFetchSource *source = &g_MENUFetchEditSources[i];
        bool matched = false;

        if (replaceMatches)
            matched = PLUGIN_MENU_FetchSourceMatches(
                source,
                replacementTitle,
                replacementUrl);
        else if (removeTitle)
            matched = !PLUGIN_MENU_StringCompare(source->title, removeTitle);

        if (matched)
        {
            if (replaceMatches && !replacementWritten)
            {
                if (!PLUGIN_MENU_AddFetchSize(&finalSize, replacementTitle) ||
                    !PLUGIN_MENU_AddFetchSize(&finalSize, replacementUrl))
                {
                    return false;
                }
                finalCount++;
                replacementWritten = true;
            }
            continue;
        }

        if (!PLUGIN_MENU_AddFetchSize(&finalSize, source->title) ||
            !PLUGIN_MENU_AddFetchSize(&finalSize, source->url))
        {
            return false;
        }
        finalCount++;
    }

    if (replaceMatches && !replacementWritten)
    {
        if (finalCount >= MENU_FETCH_MAX_SOURCES ||
            !PLUGIN_MENU_AddFetchSize(&finalSize, replacementTitle) ||
            !PLUGIN_MENU_AddFetchSize(&finalSize, replacementUrl))
        {
            return false;
        }
        finalCount++;
    }

    if (finalCount > MENU_FETCH_MAX_SOURCES ||
        finalSize >= MENU_FETCH_CONFIG_MAX)
    {
        return false;
    }

    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &tempFile,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUFetchTempPath),
            FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
            0)) ||
        R_FAILED(MENU_HOST__FSFILE_SetSize(tempFile, finalSize)))
    {
        goto done;
    }

    u32 writeOffset = 0;
    replacementWritten = false;

    for (u32 i = 0; i < oldCount; i++)
    {
        PluginMenuFetchSource *source = &g_MENUFetchEditSources[i];
        bool matched = false;

        if (replaceMatches)
            matched = PLUGIN_MENU_FetchSourceMatches(
                source,
                replacementTitle,
                replacementUrl);
        else if (removeTitle)
            matched = !PLUGIN_MENU_StringCompare(source->title, removeTitle);

        if (matched)
        {
            if (replaceMatches && !replacementWritten)
            {
                if (!PLUGIN_MENU_WriteFetchLine(
                        tempFile,
                        &writeOffset,
                        replacementTitle) ||
                    !PLUGIN_MENU_WriteFetchLine(
                        tempFile,
                        &writeOffset,
                        replacementUrl))
                {
                    goto done;
                }
                replacementWritten = true;
            }
            continue;
        }

        if (!PLUGIN_MENU_WriteFetchLine(tempFile, &writeOffset, source->title) ||
            !PLUGIN_MENU_WriteFetchLine(tempFile, &writeOffset, source->url))
        {
            goto done;
        }
    }

    if (replaceMatches && !replacementWritten)
    {
        if (!PLUGIN_MENU_WriteFetchLine(
                tempFile,
                &writeOffset,
                replacementTitle) ||
            !PLUGIN_MENU_WriteFetchLine(
                tempFile,
                &writeOffset,
                replacementUrl))
        {
            goto done;
        }
    }

    if (writeOffset != finalSize)
        goto done;

    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &targetFile,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUFetchPath),
            FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
            0)) ||
        R_FAILED(MENU_HOST__FSFILE_SetSize(targetFile, finalSize)) ||
        !PLUGIN_MENU_CopyFetchFile(tempFile, targetFile, finalSize))
    {
        goto done;
    }

    success = true;

done:
    if (targetFile)
        MENU_HOST__FSFILE_Close(targetFile);
    if (tempFile)
        MENU_HOST__FSFILE_Close(tempFile);
    (void)MENU_HOST__FSUSER_DeleteFile(
        archive,
        MENU_HOST__fsMakePath(PATH_ASCII, g_MENUFetchTempPath)
    );
    return success;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_AddOnlineEntry(
    const char *title,
    const char *url
)
{
    FS_Archive archive = 0;
    bool archiveOpen = false;
    bool exists = false;
    bool success = false;
    u32 count = 0;

    if (!PLUGIN_MENU_IsValidFetchField(title, MENU_FETCH_TITLE_MAX) ||
        !PLUGIN_MENU_IsValidFetchField(url, MENU_FETCH_BASE_MAX - 1u))
    {
        return false;
    }

    PLUGIN_MENU_LockFetch();
    if (!PLUGIN_MENU_FetchAllocScratch())
    {
        PLUGIN_MENU_UnlockFetch();
        return false;
    }

    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        goto done;
    }
    archiveOpen = true;

    if (!PLUGIN_MENU_ReadFetchEditSources(
            archive,
            true,
            &exists,
            &count))
    {
        goto done;
    }

    for (u32 i = 0; i < count; i++)
    {
        if (!PLUGIN_MENU_StringCompare(g_MENUFetchEditSources[i].title, title) &&
            !PLUGIN_MENU_StringCompare(g_MENUFetchEditSources[i].url, url))
        {
            success = true;
            goto done;
        }
    }

    success = PLUGIN_MENU_WriteFetchEditSources(
        archive,
        title,
        url,
        true,
        NULL,
        count
    );

done:
    if (archiveOpen)
        MENU_HOST__FSUSER_CloseArchive(archive);
    PLUGIN_MENU_FetchFreeScratch();
    PLUGIN_MENU_UnlockFetch();
    return success;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_RemoveOnlineEntry(const char *title)
{
    FS_Archive archive = 0;
    bool archiveOpen = false;
    bool exists = false;
    bool success = false;
    bool found = false;
    u32 count = 0;

    if (!PLUGIN_MENU_IsValidFetchField(title, MENU_FETCH_TITLE_MAX))
        return false;

    PLUGIN_MENU_LockFetch();
    if (!PLUGIN_MENU_FetchAllocScratch())
    {
        PLUGIN_MENU_UnlockFetch();
        return false;
    }

    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        goto done;
    }
    archiveOpen = true;

    if (!PLUGIN_MENU_ReadFetchEditSources(
            archive,
            true,
            &exists,
            &count))
    {
        goto done;
    }

    if (!exists)
    {
        success = true;
        goto done;
    }

    for (u32 i = 0; i < count; i++)
    {
        if (!PLUGIN_MENU_StringCompare(g_MENUFetchEditSources[i].title, title))
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        success = true;
        goto done;
    }

    success = PLUGIN_MENU_WriteFetchEditSources(
        archive,
        NULL,
        NULL,
        false,
        title,
        count
    );

done:
    if (archiveOpen)
        MENU_HOST__FSUSER_CloseArchive(archive);
    PLUGIN_MENU_FetchFreeScratch();
    PLUGIN_MENU_UnlockFetch();
    return success;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_OpenOnlineMenu(void)
{
    MENUTransientImage image;
    PLUGIN_MENU_DrawOpeningHttps();

    if (!g_MENUHttpsReady)
    {
        PluginMenuFileContext selfFile;
        if (PLUGIN_MENU_OpenPluginFile(MENU_PLUGIN_ID, &selfFile))
        {
            g_MENUHttpsReady = PLUGIN_MENU_EnsureHttpslib(&selfFile);
            PLUGIN_MENU_ClosePluginFile(&selfFile);
        }
        else
        {
            PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageOpenFile, (Result)0xD8A0A060u);
        }

        if (!g_MENUHttpsReady)
        {
            PLUGIN_MENU_DrawOnlineError();
            return;
        }
    }

    if (!PLUGIN_MENU_LoadHttpsLibrary(&image))
    {
        PLUGIN_MENU_DrawOnlineError();
        return;
    }

    g_MENUHttpsOpenOnline();
    PLUGIN_MENU_UnloadHttpsLibrary(&image);
}

PLUGIN_CODE(MENU) void PLUGIN_MENU_OpenOnlineSource(const char *url)
{
    MENUTransientImage image;
    if (!url || !*url) return;
    PLUGIN_MENU_DrawOpeningHttps();

    if (!g_MENUHttpsReady)
    {
        PluginMenuFileContext selfFile;
        if (PLUGIN_MENU_OpenPluginFile(MENU_PLUGIN_ID, &selfFile))
        {
            g_MENUHttpsReady = PLUGIN_MENU_EnsureHttpslib(&selfFile);
            PLUGIN_MENU_ClosePluginFile(&selfFile);
        }
        else
        {
            PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageOpenFile, (Result)0xD8A0A060u);
        }

        if (!g_MENUHttpsReady)
        {
            PLUGIN_MENU_DrawOnlineError();
            return;
        }
    }

    if (!PLUGIN_MENU_LoadHttpsLibrary(&image))
    {
        PLUGIN_MENU_DrawOnlineError();
        return;
    }

    g_MENUHttpsOpenOnlineSource(url);
    PLUGIN_MENU_UnloadHttpsLibrary(&image);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageAllocScratch(void)
{
    u32 base = 0;
    u32 cursor;
    if (!PLUGIN_MENU_TempAlloc(MENU_MANAGE_SCRATCH_SIZE, &base))
        return false;

    g_MENUManageScratchBase = base;
    g_MENUManageFiles = (PluginMenuManageFile *)base;
    g_MENUManageNames = (char *)(base + sizeof(PluginMenuManageFile) * MENU_MANAGE_MAX_FILES);
    g_MENUManageChanges = g_MENUManageNames + MENU_MANAGE_NAME_BYTES;
    cursor = (u32)(g_MENUManageChanges + MENU_MANAGE_TEMP_BYTES);
    g_MENUScanScratch = (PluginMenuScanScratch *)cursor;
    cursor += sizeof(PluginMenuScanScratch);
    g_MENUManageSelection = (PluginMenuManageSelection *)cursor;
    cursor += sizeof(PluginMenuManageSelection);
    g_MENUManageActionInfo = (PluginMenuManageActionInfo *)cursor;
    cursor += sizeof(PluginMenuManageActionInfo);
    if (cursor > base + MENU_MANAGE_SCRATCH_SIZE)
    {
        PLUGIN_MENU_TempFree(base, MENU_MANAGE_SCRATCH_SIZE);
        g_MENUManageScratchBase = 0;
        g_MENUManageFiles = NULL;
        g_MENUManageNames = NULL;
        g_MENUManageChanges = NULL;
        g_MENUScanScratch = NULL;
        g_MENUManageSelection = NULL;
        g_MENUManageActionInfo = NULL;
        return false;
    }
    g_MENUManageFileCount = 0;
    g_MENUManageActiveCount = 0;
    g_MENUManageNameUsed = 0;
    g_MENUManageChangeSize = 0;
    return true;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageFreeScratch(void)
{
    if (g_MENUManageScratchBase)
        PLUGIN_MENU_TempFree(g_MENUManageScratchBase, MENU_MANAGE_SCRATCH_SIZE);
    g_MENUManageScratchBase = 0;
    g_MENUManageFiles = NULL;
    g_MENUManageNames = NULL;
    g_MENUManageChanges = NULL;
    g_MENUScanScratch = NULL;
    g_MENUManageSelection = NULL;
    g_MENUManageActionInfo = NULL;
    g_MENUManageFileCount = 0;
    g_MENUManageActiveCount = 0;
    g_MENUManageNameUsed = 0;
    g_MENUManageChangeSize = 0;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageLoadChanges(void)
{
    FS_Archive archive = 0;
    Handle file = 0;
    u64 size = 0;
    g_MENUManageChangeSize = 0;

    if (!g_MENUManageChanges ||
        R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
        return;

    if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenFile(
            &file,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUManageTempListPath),
            FS_OPEN_READ,
            0)) &&
        R_SUCCEEDED(MENU_HOST__FSFILE_GetSize(file, &size)) &&
        size <= MENU_MANAGE_TEMP_BYTES &&
        (!size || PLUGIN_MENU_ReadExact(file, 0, g_MENUManageChanges, (u32)size)))
    {
        g_MENUManageChangeSize = (u32)size;
    }

    if (file)
        MENU_HOST__FSFILE_Close(file);
    MENU_HOST__FSUSER_CloseArchive(archive);
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_ManageCanonicalLength(
    const char *name,
    bool disabled
)
{
    u32 length = PLUGIN_MENU_StringLength(name);
    return disabled && length >= 2u ? length - 2u : length;
}

PLUGIN_CODE(MENU) static char PLUGIN_MENU_ManageFindChange(
    const char *name,
    bool disabled,
    u32 *lineStart,
    u32 *lineEnd
)
{
    u32 wanted = PLUGIN_MENU_ManageCanonicalLength(name, disabled);
    u32 pos = 0;

    while (pos < g_MENUManageChangeSize)
    {
        u32 start = pos;
        while (pos < g_MENUManageChangeSize && g_MENUManageChanges[pos] != '\n')
            pos++;
        u32 textEnd = pos;
        u32 end = pos < g_MENUManageChangeSize ? pos + 1u : pos;

        if (textEnd >= start + 2u &&
            (g_MENUManageChanges[start] == 'D' || g_MENUManageChanges[start] == 'E') &&
            g_MENUManageChanges[start + 1u] == '|')
        {
            u32 textStart = start + 2u;
            u32 textLength = textEnd - textStart;
            bool match = textLength == wanted;

            for (u32 i = 0; match && i < wanted; i++)
                if (g_MENUManageChanges[textStart + i] != name[i])
                    match = false;

            if (match)
            {
                if (lineStart) *lineStart = start;
                if (lineEnd) *lineEnd = end;
                return g_MENUManageChanges[start];
            }
        }
        pos = end;
    }
    return 0;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageWasInBootSnapshot(
    const char *name,
    bool disabled
)
{
    u32 wanted = PLUGIN_MENU_ManageCanonicalLength(name, disabled);
    u32 pos = 0;

    while (pos < g_MENUManageChangeSize)
    {
        u32 start = pos;
        while (pos < g_MENUManageChangeSize && g_MENUManageChanges[pos] != '\n')
            pos++;
        u32 textEnd = pos;
        u32 end = pos < g_MENUManageChangeSize ? pos + 1u : pos;
        bool stateLine = textEnd >= start + 2u &&
            (g_MENUManageChanges[start] == 'D' || g_MENUManageChanges[start] == 'E') &&
            g_MENUManageChanges[start + 1u] == '|';
        bool match = !stateLine && textEnd - start == wanted;

        for (u32 i = 0; match && i < wanted; i++)
            if (g_MENUManageChanges[start + i] != name[i])
                match = false;
        if (match)
            return true;
        pos = end;
    }
    return false;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageWasEnabledAtBoot(
    const char *name,
    bool disabled
)
{
    char change = PLUGIN_MENU_ManageFindChange(name, disabled, NULL, NULL);
    if (disabled)
        return change == 'E';
    if (change == 'D')
        return false;
    return g_MENUManageCapturingBoot ||
           PLUGIN_MENU_ManageWasInBootSnapshot(name, disabled);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageSaveChanges(void)
{
    FS_Archive archive = 0;
    Handle file = 0;
    bool success = false;

    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
        return false;

    (void)MENU_HOST__FSUSER_DeleteFile(
        archive,
        MENU_HOST__fsMakePath(PATH_ASCII, g_MENUManageTempListPath));

    if (!g_MENUManageChangeSize)
    {
        success = true;
        goto done;
    }

    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &file,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUManageTempListPath),
            FS_OPEN_WRITE | FS_OPEN_CREATE,
            0)) ||
        R_FAILED(MENU_HOST__FSFILE_SetSize(file, g_MENUManageChangeSize)) ||
        !PLUGIN_MENU_WriteExact(file, 0, g_MENUManageChanges, g_MENUManageChangeSize))
    {
        goto done;
    }
    success = true;

done:
    if (file)
        MENU_HOST__FSFILE_Close(file);
    MENU_HOST__FSUSER_CloseArchive(archive);
    return success;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageSetChange(
    const char *name,
    bool disabled,
    char state
)
{
    u32 start = 0, end = 0;
    char old = PLUGIN_MENU_ManageFindChange(name, disabled, &start, &end);

    if (old)
    {
        u32 remove = end - start;
        for (u32 i = end; i < g_MENUManageChangeSize; i++)
            g_MENUManageChanges[i - remove] = g_MENUManageChanges[i];
        g_MENUManageChangeSize -= remove;
    }
    if (state)
    {
        u32 length = PLUGIN_MENU_ManageCanonicalLength(name, disabled);
        u32 need = length + 3u;
        if (g_MENUManageChangeSize + need > MENU_MANAGE_TEMP_BYTES)
            return false;

        u32 pos = g_MENUManageChangeSize;
        g_MENUManageChanges[pos++] = state;
        g_MENUManageChanges[pos++] = '|';
        for (u32 i = 0; i < length; i++)
            g_MENUManageChanges[pos++] = name[i];
        g_MENUManageChanges[pos++] = '\n';
        g_MENUManageChangeSize = pos;
    }

    return PLUGIN_MENU_ManageSaveChanges();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageResetTempList(void)
{
    FS_Archive archive = 0;
    g_MENUManageChangeSize = 0;

    if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        (void)MENU_HOST__FSUSER_DeleteFile(
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUManageTempListPath));
        MENU_HOST__FSUSER_CloseArchive(archive);
    }
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageParseName(
    const char *name,
    u32 *priority,
    bool *disabled
)
{
    u32 length = PLUGIN_MENU_StringLength(name);
    u32 extension;

    if (length >= 9u &&
        name[length - 6u] == '.' && name[length - 5u] == '3' &&
        name[length - 4u] == 'n' && name[length - 3u] == 'x' &&
        name[length - 2u] == '.' && name[length - 1u] == 'd')
    {
        extension = length - 6u;
        *disabled = true;
    }
    else if (length >= 7u &&
             name[length - 4u] == '.' && name[length - 3u] == '3' &&
             name[length - 2u] == 'n' && name[length - 1u] == 'x')
    {
        extension = length - 4u;
        *disabled = false;
    }
    else
    {
        return false;
    }

    u32 dot = extension ? extension - 1u : 0u;
    while (dot && name[dot] != '.')
        dot--;
    if (!dot || dot + 1u == extension)
        return false;

    u32 value = 0;
    for (u32 i = dot + 1u; i < extension; i++)
    {
        if (name[i] < '0' || name[i] > '9')
            return false;
        u32 digit = (u32)(name[i] - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    *priority = value;
    return true;
}

PLUGIN_CODE(MENU) static const char *PLUGIN_MENU_ManageName(const PluginMenuManageFile *file)
{
    return &g_MENUManageNames[file->nameOffset];
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageFileEarlier(
    const PluginMenuManageFile *a,
    const PluginMenuManageFile *b,
    bool grouped
)
{
    if (grouped && a->group != b->group)
        return a->group < b->group;
    if (a->priority != b->priority)
        return a->priority < b->priority;
    return PLUGIN_MENU_StringCompare(PLUGIN_MENU_ManageName(a), PLUGIN_MENU_ManageName(b)) < 0;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageSort(bool grouped)
{
    for (u32 i = 1; i < g_MENUManageFileCount; i++)
    {
        PluginMenuManageFile value = g_MENUManageFiles[i];
        u32 j = i;
        while (j && PLUGIN_MENU_ManageFileEarlier(&value, &g_MENUManageFiles[j - 1u], grouped))
        {
            g_MENUManageFiles[j] = g_MENUManageFiles[j - 1u];
            j--;
        }
        g_MENUManageFiles[j] = value;
    }
}

PLUGIN_CODE(MENU) static PluginMenuManageWinner *PLUGIN_MENU_ManageFindWinner(
    u32 magic,
    u32 pluginId
)
{
    PluginMenuManageWinner *winners;
    u32 count;

    if (!g_MENUManageSelection)
        return NULL;
    if (magic == LOADER_PLUGIN_MAGIC)
    {
        winners = g_MENUManageSelection->loader;
        count = g_MENUManageSelection->loaderCount;
    }
    else if (magic == MENU_PLUGIN_MAGIC)
    {
        winners = g_MENUManageSelection->rosalina;
        count = g_MENUManageSelection->rosalinaCount;
    }
    else
    {
        return NULL;
    }

    for (u32 i = 0; i < count; i++)
        if (winners[i].pluginId == pluginId)
            return &winners[i];
    return NULL;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageWinnerMatches(
    u32 magic,
    u32 pluginId,
    u16 nameOffset,
    u32 fileOffset
)
{
    PluginMenuManageWinner *winner = PLUGIN_MENU_ManageFindWinner(magic, pluginId);
    return winner && winner->nameOffset == nameOffset && winner->fileOffset == fileOffset;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageBuildSelection(FS_Archive archive)
{
    if (!g_MENUManageSelection)
        return;
    g_MENUManageSelection->loaderCount = 0;
    g_MENUManageSelection->rosalinaCount = 0;

    for (u32 i = 0; i < g_MENUManageFileCount; i++)
    {
        PluginMenuManageFile *item = &g_MENUManageFiles[i];
        item->selected = 0;
        if (!item->bootActive || !PLUGIN_MENU_MakePluginPath(PLUGIN_MENU_ManageName(item)))
            continue;

        Handle file = 0;
        if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
                &file,
                archive,
                MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath),
                FS_OPEN_READ,
                0)))
            continue;

        u32 offset = 0;
        for (;;)
        {
            PluginMenu3nxHeader header;
            u32 metadataOffset;
            u32 nextOffset;
            if (!PLUGIN_MENU_ReadHeader(file, offset, &header, &metadataOffset, &nextOffset) ||
                (header.magic != MENU_PLUGIN_MAGIC && header.magic != LOADER_PLUGIN_MAGIC))
                break;

            PluginMenuManageWinner *winners = header.magic == LOADER_PLUGIN_MAGIC ?
                g_MENUManageSelection->loader : g_MENUManageSelection->rosalina;
            u32 *count = header.magic == LOADER_PLUGIN_MAGIC ?
                &g_MENUManageSelection->loaderCount : &g_MENUManageSelection->rosalinaCount;
            if (*count < MENU_SYSPLUGIN_MAX_PLUGINS &&
                !PLUGIN_MENU_ManageFindWinner(header.magic, header.pluginId))
            {
                PluginMenuManageWinner *winner = &winners[*count];
                winner->magic = header.magic;
                winner->pluginId = header.pluginId;
                winner->nameOffset = item->nameOffset;
                winner->reserved = 0;
                winner->fileOffset = offset;
                (*count)++;
                item->selected = 1;
            }
            offset = nextOffset;
        }
        MENU_HOST__FSFILE_Close(file);
    }
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageBuildList(void)
{
    FS_Archive archive = 0;
    Handle directory = 0;
    bool scanComplete = false;
    g_MENUManageFileCount = 0;
    g_MENUManageActiveCount = 0;
    g_MENUManageNameUsed = 0;

    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))) ||
        R_FAILED(MENU_HOST__FSUSER_OpenDirectory(
            &directory,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUPluginsPath))))
        goto done;

    while (g_MENUManageFileCount < MENU_MANAGE_MAX_FILES)
    {
        u32 read = 0;
        if (R_FAILED(MENU_HOST__FSDIR_Read(directory, &read, 1, &g_MENUScanEntry)))
            goto done;
        if (!read)
        {
            scanComplete = true;
            break;
        }
        if (g_MENUScanEntry.attributes & FS_ATTRIBUTE_DIRECTORY)
            continue;

        u32 length = 0;
        while (length + 1u < sizeof(g_MENUScanName) && g_MENUScanEntry.name[length])
        {
            g_MENUScanName[length] = (char)g_MENUScanEntry.name[length];
            length++;
        }
        g_MENUScanName[length] = 0;

        u32 priority;
        bool disabled;
        if (!PLUGIN_MENU_ManageParseName(g_MENUScanName, &priority, &disabled) ||
            g_MENUManageNameUsed + length + 1u > MENU_MANAGE_NAME_BYTES)
            continue;

        PluginMenuManageFile *item = &g_MENUManageFiles[g_MENUManageFileCount++];
        item->priority = priority;
        item->nameOffset = (u16)g_MENUManageNameUsed;
        item->currentDisabled = disabled;
        item->selected = 0;
        item->group = 0;
        volatile char *nameOut = &g_MENUManageNames[g_MENUManageNameUsed];
        const volatile char *nameIn = g_MENUScanName;
        for (u32 i = 0; i <= length; i++)
            nameOut[i] = nameIn[i];
        g_MENUManageNameUsed += length + 1u;

        item->bootActive = PLUGIN_MENU_ManageWasEnabledAtBoot(g_MENUScanName, disabled);
    }

    if (g_MENUManageFileCount == MENU_MANAGE_MAX_FILES)
        scanComplete = true;

    MENU_HOST__FSDIR_Close(directory);
    directory = 0;

    if (!scanComplete)
        goto done;

    // K11 order is priority -> filename -> stack offset. Sorting the files first
    // means the first occurrence of each module+ID is its duplicate winner.
    PLUGIN_MENU_ManageSort(false);
    PLUGIN_MENU_ManageBuildSelection(archive);

    for (u32 i = 0; i < g_MENUManageFileCount; i++)
    {
        PluginMenuManageFile *item = &g_MENUManageFiles[i];
        const char *name = PLUGIN_MENU_ManageName(item);
        char change = PLUGIN_MENU_ManageFindChange(name, item->currentDisabled, NULL, NULL);

        if (item->bootActive)
        {
            if (item->currentDisabled && change == 'E')
                item->group = 2u; // active this boot, disabled for the next boot
            else
                item->group = item->selected ? 0u : 1u;
        }
        else
        {
            item->group = item->currentDisabled ? 4u : 3u;
        }
    }

    PLUGIN_MENU_ManageSort(true);
    while (g_MENUManageActiveCount < g_MENUManageFileCount &&
           g_MENUManageFiles[g_MENUManageActiveCount].group < 3u)
        g_MENUManageActiveCount++;

done:
    if (directory)
        MENU_HOST__FSDIR_Close(directory);
    if (archive)
        MENU_HOST__FSUSER_CloseArchive(archive);
    return scanComplete;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageSaveBootSnapshot(void)
{
    u32 size = 0;

    for (u32 i = 0; i < g_MENUManageFileCount; i++)
    {
        PluginMenuManageFile *item = &g_MENUManageFiles[i];
        const char *name;
        u32 length;
        if (item->currentDisabled)
            continue;
        name = PLUGIN_MENU_ManageName(item);
        length = PLUGIN_MENU_StringLength(name);
        if (size + length + 1u > MENU_MANAGE_TEMP_BYTES)
            return false;
        for (u32 j = 0; j < length; j++)
            g_MENUManageChanges[size++] = name[j];
        g_MENUManageChanges[size++] = '\n';
    }

    g_MENUManageChangeSize = size;
    return PLUGIN_MENU_ManageSaveChanges();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageCaptureBootSnapshot(void)
{
    if (!PLUGIN_MENU_ManageAllocScratch())
        return;

    g_MENUManageCapturingBoot = true;
    g_MENUManageChangeSize = 0;
    if (PLUGIN_MENU_ManageBuildList())
        (void)PLUGIN_MENU_ManageSaveBootSnapshot();
    g_MENUManageCapturingBoot = false;
    PLUGIN_MENU_ManageFreeScratch();
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_ManageRowForFile(u32 fileIndex)
{
    return fileIndex < g_MENUManageActiveCount ? fileIndex + 1u : fileIndex + 3u;
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_ManageTotalRows(void)
{
    return g_MENUManageFileCount + 1u +
           (g_MENUManageActiveCount < g_MENUManageFileCount ? 2u : 0u);
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_ManageAdjustFirst(u32 first, u32 selectedRow)
{
    u32 total = PLUGIN_MENU_ManageTotalRows();
    if (total <= MENU_MANAGE_VISIBLE_ROWS)
        return 0;
    u32 maxFirst = total - MENU_MANAGE_VISIBLE_ROWS;
    if (selectedRow > 0u && selectedRow <= first)
        first = selectedRow - 1u;
    if (selectedRow >= first + MENU_MANAGE_VISIBLE_ROWS)
        first = selectedRow - MENU_MANAGE_VISIBLE_ROWS + 1u;
    if (first > maxFirst)
        first = maxFirst;
    if (selectedRow > 0u && first >= selectedRow)
        first = selectedRow - 1u;
    return first;
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_ManageColor(const PluginMenuManageFile *item)
{
    if (item->group == 0u || item->group == 3u)
        return COLOR_WHITE;
    if (item->group == 1u)
        return COLOR_GRAY;
    return MENU_MANAGE_DISABLED_ENTRY_COLOR;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageDrawFile(
    const PluginMenuManageFile *item,
    u32 y,
    bool selected
)
{
    if (selected)
        MENU_HOST__Draw_DrawString(12, y, MENU_MANAGE_CURSOR_COLOR, g_MENUManageCursor);
    MENU_HOST__Draw_DrawString(24, y, PLUGIN_MENU_ManageColor(item), PLUGIN_MENU_ManageName(item));
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageDraw(u32 first, u32 selectedFile)
{
    u32 total = PLUGIN_MENU_ManageTotalRows();
    u32 selectedRow = g_MENUManageFileCount ? PLUGIN_MENU_ManageRowForFile(selectedFile) : 0xFFFFFFFFu;
    bool hasDisabled = g_MENUManageActiveCount < g_MENUManageFileCount;

    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    PLUGIN_MENU_DrawFrame(g_MENUManageTitle);

    if (first)
        MENU_HOST__Draw_DrawString(24, MENU_TOP_DOTS_Y, COLOR_GRAY, g_MENUDots);

    for (u32 i = 0; i < MENU_MANAGE_VISIBLE_ROWS && first + i < total; i++)
    {
        u32 row = first + i;
        u32 y = MENU_ITEM_TOP_Y + i * MENU_ITEM_SPACING_Y;
        if (row == 0u)
            MENU_HOST__Draw_DrawString(20, y, MENU_MANAGE_ACTIVE_COLOR, g_MENUManageActive);
        else if (hasDisabled && row == g_MENUManageActiveCount + 1u)
            ;
        else if (hasDisabled && row == g_MENUManageActiveCount + 2u)
            MENU_HOST__Draw_DrawString(20, y, MENU_MANAGE_DISABLED_LABEL_COLOR, g_MENUManageDisabled);
        else
        {
            u32 fileIndex = row <= g_MENUManageActiveCount ? row - 1u : row - 3u;
            if (fileIndex < g_MENUManageFileCount)
                PLUGIN_MENU_ManageDrawFile(&g_MENUManageFiles[fileIndex], y + MENU_MANAGE_SECTION_ITEM_Y_OFFSET, row == selectedRow);
        }
    }

    if (first + MENU_MANAGE_VISIBLE_ROWS < total)
        MENU_HOST__Draw_DrawString(24, MENU_ITEM_TOP_Y + MENU_MANAGE_VISIBLE_ROWS * MENU_ITEM_SPACING_Y,
                                  COLOR_GRAY, g_MENUDots);
    if (!g_MENUManageFileCount)
        MENU_HOST__Draw_DrawString(35, 67, COLOR_GRAY, g_MENUManageEmptyText);
    else
        MENU_HOST__Draw_DrawString(24, MENU_MANAGE_PROMPT_Y, COLOR_GRAY, g_MENUManageActionsPrompt);

    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageRedrawSelection(
    u32 first,
    u32 oldSelected,
    u32 selected
)
{
    u32 oldRow = PLUGIN_MENU_ManageRowForFile(oldSelected);
    u32 newRow = PLUGIN_MENU_ManageRowForFile(selected);
    u32 oldY = MENU_ITEM_TOP_Y + (oldRow - first) * MENU_ITEM_SPACING_Y + MENU_MANAGE_SECTION_ITEM_Y_OFFSET;
    u32 newY = MENU_ITEM_TOP_Y + (newRow - first) * MENU_ITEM_SPACING_Y + MENU_MANAGE_SECTION_ITEM_Y_OFFSET;

    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_MENUClearRow);
    MENU_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_MENUClearRow);
    PLUGIN_MENU_ManageDrawFile(&g_MENUManageFiles[oldSelected], oldY, false);
    PLUGIN_MENU_ManageDrawFile(&g_MENUManageFiles[selected], newY, true);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageDrawChoice(u32 y, bool selected, const char *text)
{
    if (selected)
        MENU_HOST__Draw_DrawString(12, y, MENU_MANAGE_CURSOR_COLOR, g_MENUManageCursor);
    MENU_HOST__Draw_DrawString(24, y, COLOR_WHITE, text);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageRedrawChoice(
    u32 oldY,
    const char *oldText,
    u32 newY,
    const char *newText
)
{
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_MENUClearRow);
    MENU_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_MENUClearRow);
    PLUGIN_MENU_ManageDrawChoice(oldY, false, oldText);
    PLUGIN_MENU_ManageDrawChoice(newY, true, newText);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageConfirm(void)
{
    u32 selected = 0;
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    PLUGIN_MENU_DrawFrame(g_MENUManageConfirmTitle);
    PLUGIN_MENU_ManageDrawChoice(55u, true, g_MENUManageNo);
    PLUGIN_MENU_ManageDrawChoice(70u, false, g_MENUManageYes);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();

    for (;;)
    {
        u32 pressed = MENU_HOST__waitInput();
        if (MENU_HOST__menuShouldExit || (pressed & KEY_B))
            return false;
        if (pressed & (KEY_UP | KEY_DOWN))
        {
            u32 oldSelected = selected;
            selected ^= 1u;
            PLUGIN_MENU_ManageRedrawChoice(
                oldSelected ? 70u : 55u,
                oldSelected ? g_MENUManageYes : g_MENUManageNo,
                selected ? 70u : 55u,
                selected ? g_MENUManageYes : g_MENUManageNo);
        }
        else if (pressed & KEY_A)
            return selected == 1u;
    }
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageDrawFailure(void)
{
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    PLUGIN_MENU_DrawFrame(g_MENUManageTitle);
    MENU_HOST__Draw_DrawString(35, 55, COLOR_RED, g_MENUManageFailed);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
    while (!MENU_HOST__menuShouldExit)
        if (MENU_HOST__waitInput() & KEY_B)
            break;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageDrawScanning(void)
{
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    MENU_HOST__Draw_DrawString(10, 10, COLOR_WHITE, g_MENUManageScanning);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageIdText(char out[5], u32 id)
{
    out[0] = (char)(id & 0xFFu);
    out[1] = (char)((id >> 8) & 0xFFu);
    out[2] = (char)((id >> 16) & 0xFFu);
    out[3] = (char)((id >> 24) & 0xFFu);
    out[4] = 0;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageAddMissing(u32 componentId, u32 providerId)
{
    if (!g_MENUManageActionInfo)
        return false;
    for (u32 i = 0; i < g_MENUManageActionInfo->missingCount; i++)
    {
        PluginMenuManageMissing *missing = &g_MENUManageActionInfo->missing[i];
        if (missing->componentId == componentId && missing->providerId == providerId)
            return true;
    }
    if (g_MENUManageActionInfo->missingCount >= MENU_MANAGE_MAX_MISSING)
        return false;
    PluginMenuManageMissing *missing =
        &g_MENUManageActionInfo->missing[g_MENUManageActionInfo->missingCount++];
    missing->componentId = componentId;
    missing->providerId = providerId;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageScanRepairProviders(
    Handle file,
    u32 fileOffset,
    const PluginMenu3nxHeader *header
)
{
    u32 repairStart;
    u32 repairEnd;
    u32 cursor;
    u32 exportStart;
    u32 exportCount;
    u32 exportBytes;

    if (!header->repairSize)
        return true;

    if (!PLUGIN_MENU_Add32(fileOffset, MENU_HEADER_SIZE, &repairStart) ||
        !PLUGIN_MENU_Add32(repairStart, header->fastRelocSize, &repairStart) ||
        !PLUGIN_MENU_Add32(repairStart, header->codeSize, &repairStart) ||
        !PLUGIN_MENU_Add32(repairStart, header->dataSize, &repairStart) ||
        !PLUGIN_MENU_Add32(repairStart, header->repairSize, &repairEnd) ||
        header->repairSize < sizeof(u32) ||
        !PLUGIN_MENU_ReadExact(file, repairStart, &exportCount, sizeof(exportCount)) ||
        exportCount > (header->repairSize - sizeof(u32)) / sizeof(PluginMenuManageExportRecord))
    {
        return false;
    }

    exportBytes = exportCount * sizeof(PluginMenuManageExportRecord);
    if (!PLUGIN_MENU_Add32(repairStart, sizeof(u32), &exportStart) ||
        !PLUGIN_MENU_Add32(exportStart, exportBytes, &cursor) || cursor > repairEnd)
        return false;

    while (cursor < repairEnd)
    {
        PluginMenuManageRepairGroup group;
        u32 recordsBytes;
        if (repairEnd - cursor < sizeof(group) ||
            !PLUGIN_MENU_ReadExact(file, cursor, &group, sizeof(group)))
        {
            return false;
        }
        cursor += sizeof(group);
        if (group.count > (repairEnd - cursor) / sizeof(PluginMenuManageRepairRecord))
            return false;
        recordsBytes = group.count * sizeof(PluginMenuManageRepairRecord);

        if (group.count && group.providerId &&
            !PLUGIN_MENU_ManageFindWinner(header->magic, group.providerId) &&
            !PLUGIN_MENU_ManageAddMissing(header->pluginId, group.providerId))
        {
            return false;
        }
        cursor += recordsBytes;
    }
    return cursor == repairEnd;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageScanActionInfo(u32 fileIndex)
{
    FS_Archive archive = 0;
    Handle file = 0;
    PluginMenuManageFile *item;
    bool success = false;

    if (fileIndex >= g_MENUManageFileCount || !g_MENUManageActionInfo)
        return false;
    item = &g_MENUManageFiles[fileIndex];
    g_MENUManageActionInfo->componentCount = 0;
    g_MENUManageActionInfo->missingCount = 0;
    g_MENUManageActionInfo->selectedComponentCount = 0;
    g_MENUManageActionInfo->selectedMenuCount = 0;

    if (!PLUGIN_MENU_MakePluginPath(PLUGIN_MENU_ManageName(item)) ||
        R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))) ||
        R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &file,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath),
            FS_OPEN_READ,
            0)))
    {
        goto done;
    }

    for (u32 offset = 0;;)
    {
        PluginMenu3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        if (!PLUGIN_MENU_ReadHeader(file, offset, &header, &metadataOffset, &nextOffset) ||
            (header.magic != MENU_PLUGIN_MAGIC && header.magic != LOADER_PLUGIN_MAGIC))
            break;

        if (g_MENUManageActionInfo->componentCount >= MENU_MANAGE_MAX_COMPONENTS)
            goto done;
        u32 componentIndex = g_MENUManageActionInfo->componentCount++;
        g_MENUManageActionInfo->componentIds[componentIndex] = header.pluginId;
        g_MENUManageActionInfo->componentModules[componentIndex] =
            header.magic == LOADER_PLUGIN_MAGIC ? 'L' : 'R';

        if (PLUGIN_MENU_ManageWinnerMatches(
                header.magic,
                header.pluginId,
                item->nameOffset,
                offset))
        {
            g_MENUManageActionInfo->selectedComponentCount++;
            if (header.magic == MENU_PLUGIN_MAGIC && header.pluginId == MENU_PLUGIN_ID)
                g_MENUManageActionInfo->selectedMenuCount++;
        }

        if (!PLUGIN_MENU_ManageScanRepairProviders(file, offset, &header))
            goto done;
        offset = nextOffset;
    }

    success = g_MENUManageActionInfo->componentCount != 0u;

done:
    if (file)
        MENU_HOST__FSFILE_Close(file);
    if (archive)
        MENU_HOST__FSUSER_CloseArchive(archive);
    return success;
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_ManageDrawContains(u32 y)
{
    char list[MENU_MANAGE_MAX_COMPONENTS * 10u + 1u];
    u32 length = 0;

    list[0] = 0;
    for (u32 i = 0; i < g_MENUManageActionInfo->componentCount; i++)
    {
        char id[5];
        PLUGIN_MENU_ManageIdText(id, g_MENUManageActionInfo->componentIds[i]);
        if (i)
        {
            list[length++] = ',';
            list[length++] = ' ';
        }
        for (u32 j = 0; j < 4u; j++)
            list[length++] = id[j];
        list[length++] = ' ';
        list[length++] = '(';
        list[length++] = (char)g_MENUManageActionInfo->componentModules[i];
        list[length++] = ')';
    }
    list[length] = 0;

    MENU_HOST__Draw_DrawString(20, y, COLOR_GRAY, g_MENUManageContains);
    y += 16u;

    u32 pos = 0;
    while (pos < length)
    {
        u32 take = length - pos;
        if (take > 32u)
        {
            take = 32u;
            while (take > 1u &&
                   (list[pos + take] != ' ' || list[pos + take - 1u] != ','))
                take--;
            if (take <= 1u)
                take = 32u;
        }
        char saved = list[pos + take];
        list[pos + take] = 0;
        MENU_HOST__Draw_DrawString(24, y, COLOR_WHITE, &list[pos]);
        list[pos + take] = saved;
        pos += take;
        while (pos < length && list[pos] == ' ')
            pos++;
        y += MENU_ITEM_SPACING_Y;
    }
    return y;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageDrawWarnings(void)
{
    bool menuDanger = g_MENUManageActionInfo->selectedComponentCount == 1u &&
                      g_MENUManageActionInfo->selectedMenuCount == 1u;
    u32 messageLines = g_MENUManageActionInfo->missingCount + (menuDanger ? 3u : 0u);
    if (!messageLines)
        return;

    u32 y = MENU_MANAGE_DETAIL_BOTTOM_Y - 16u - (messageLines - 1u) * MENU_ITEM_SPACING_Y;
    MENU_HOST__Draw_DrawString(20, y, MENU_MANAGE_DISABLED_LABEL_COLOR, g_MENUManageWarning);
    y += 16u;

    for (u32 i = 0; i < g_MENUManageActionInfo->missingCount; i++)
    {
        char line[32];
        char component[5];
        char provider[5];
        u32 pos = 0;
        PLUGIN_MENU_ManageIdText(component, g_MENUManageActionInfo->missing[i].componentId);
        PLUGIN_MENU_ManageIdText(provider, g_MENUManageActionInfo->missing[i].providerId);
        for (u32 j = 0; g_MENUManageMissingPrefix[j] && pos + 1u < sizeof(line); j++)
            line[pos++] = g_MENUManageMissingPrefix[j];
        for (u32 j = 0; j < 4u && pos + 1u < sizeof(line); j++)
            line[pos++] = component[j];
        for (u32 j = 0; g_MENUManageMissingMiddle[j] && pos + 1u < sizeof(line); j++)
            line[pos++] = g_MENUManageMissingMiddle[j];
        for (u32 j = 0; j < 4u && pos + 1u < sizeof(line); j++)
            line[pos++] = provider[j];
        line[pos] = 0;
        MENU_HOST__Draw_DrawString(24, y, COLOR_WHITE, line);
        y += MENU_ITEM_SPACING_Y;
    }

    if (menuDanger)
    {
        MENU_HOST__Draw_DrawString(24, y, COLOR_WHITE, g_MENUManageDanger1);
        y += MENU_ITEM_SPACING_Y;
        MENU_HOST__Draw_DrawString(24, y, COLOR_WHITE, g_MENUManageDanger2);
        y += MENU_ITEM_SPACING_Y;
        MENU_HOST__Draw_DrawString(24, y, COLOR_WHITE, g_MENUManageDanger3);
    }
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ManageDrawActionsPage(
    const PluginMenuManageFile *item,
    const char *firstAction,
    u32 selected
)
{
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    PLUGIN_MENU_DrawFrame(g_MENUManageTitle);
    u32 titleColor = PLUGIN_MENU_ManageColor(item);
    if (titleColor == COLOR_WHITE)
        titleColor = MENU_MANAGE_ACTIVE_COLOR;
    MENU_HOST__Draw_DrawString(20, 42, titleColor, PLUGIN_MENU_ManageName(item));
    PLUGIN_MENU_ManageDrawChoice(60u, selected == 0u, firstAction);
    PLUGIN_MENU_ManageDrawChoice(75u, selected == 1u, g_MENUManageDelete);
    (void)PLUGIN_MENU_ManageDrawContains(96u);
    PLUGIN_MENU_ManageDrawWarnings();
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageRename(PluginMenuManageFile *item)
{
    const char *name = PLUGIN_MENU_ManageName(item);
    u32 length = PLUGIN_MENU_StringLength(name);
    bool disabling = !item->currentDisabled;

    if (length + (disabling ? 3u : 1u) > sizeof(g_MENUBestName))
        return false;

    if (disabling)
    {
        for (u32 i = 0; i < length; i++) g_MENUBestName[i] = name[i];
        g_MENUBestName[length] = '.';
        g_MENUBestName[length + 1u] = 'd';
        g_MENUBestName[length + 2u] = 0;
    }
    else
    {
        if (length < 2u || name[length - 2u] != '.' || name[length - 1u] != 'd')
            return false;
        for (u32 i = 0; i < length - 2u; i++) g_MENUBestName[i] = name[i];
        g_MENUBestName[length - 2u] = 0;
    }

    u32 changeStart = 0, changeEnd = 0;
    char existing = PLUGIN_MENU_ManageFindChange(
        name, item->currentDisabled, &changeStart, &changeEnd);
    bool wasEnabledAtBoot = existing == 'E' ||
        (existing != 'D' &&
         PLUGIN_MENU_ManageWasInBootSnapshot(name, item->currentDisabled));
    char nextState = disabling ? (wasEnabledAtBoot ? 'E' : 0) :
                                 (wasEnabledAtBoot ? 0 : 'D');
    u32 remove = existing ? changeEnd - changeStart : 0u;
    u32 add = nextState ?
        PLUGIN_MENU_ManageCanonicalLength(name, item->currentDisabled) + 3u : 0u;
    if (g_MENUManageChangeSize - remove + add > MENU_MANAGE_TEMP_BYTES)
        return false;

    if (!PLUGIN_MENU_MakePluginPathTo(g_MENUScanPath, sizeof(g_MENUScanPath), name) ||
        !PLUGIN_MENU_MakePluginPathTo(g_MENUScanAltPath, sizeof(g_MENUScanAltPath), g_MENUBestName))
        return false;

    FS_Archive archive = 0;
    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
        return false;

    Result rc = MENU_HOST__FSUSER_RenameFile(
        archive,
        MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath),
        archive,
        MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanAltPath));
    MENU_HOST__FSUSER_CloseArchive(archive);
    if (R_FAILED(rc))
        return false;

    return PLUGIN_MENU_ManageSetChange(name, item->currentDisabled, nextState);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageDeleteFile(PluginMenuManageFile *item)
{
    const char *name = PLUGIN_MENU_ManageName(item);
    if (!PLUGIN_MENU_MakePluginPath(name))
        return false;

    FS_Archive archive = 0;
    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
        return false;
    Result rc = MENU_HOST__FSUSER_DeleteFile(
        archive,
        MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath));
    MENU_HOST__FSUSER_CloseArchive(archive);
    if (R_FAILED(rc))
        return false;

    if (PLUGIN_MENU_ManageFindChange(name, item->currentDisabled, NULL, NULL))
        return PLUGIN_MENU_ManageSetChange(name, item->currentDisabled, 0);
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ManageActions(u32 fileIndex)
{
    if (fileIndex >= g_MENUManageFileCount)
        return false;

    PluginMenuManageFile *item = &g_MENUManageFiles[fileIndex];
    const char *firstAction = item->currentDisabled ? g_MENUManageEnable : g_MENUManageDisable;
    u32 selected = 0;
    bool redraw = true;

    PLUGIN_MENU_ManageDrawScanning();
    if (!PLUGIN_MENU_ManageScanActionInfo(fileIndex))
    {
        PLUGIN_MENU_ManageDrawFailure();
        return false;
    }

    for (;;)
    {
        if (redraw)
        {
            PLUGIN_MENU_ManageDrawActionsPage(item, firstAction, selected);
            redraw = false;
        }

        u32 pressed = MENU_HOST__waitInput();
        if (MENU_HOST__menuShouldExit || (pressed & KEY_B))
            return false;
        if (pressed & (KEY_UP | KEY_DOWN))
        {
            u32 oldSelected = selected;
            selected ^= 1u;
            PLUGIN_MENU_ManageRedrawChoice(
                oldSelected ? 75u : 60u,
                oldSelected ? g_MENUManageDelete : firstAction,
                selected ? 75u : 60u,
                selected ? g_MENUManageDelete : firstAction);
        }
        else if (pressed & KEY_A)
        {
            if (!PLUGIN_MENU_ManageConfirm())
            {
                redraw = true;
                continue;
            }
            bool success = selected == 0u ?
                PLUGIN_MENU_ManageRename(item) : PLUGIN_MENU_ManageDeleteFile(item);
            if (!success)
            {
                PLUGIN_MENU_ManageDrawFailure();
                return false;
            }
            return true;
        }
    }
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_Manage(void)
{
    u32 selected = 0;
    u32 first = 0;
    bool redraw = true;

    PLUGIN_MENU_ManageDrawScanning();
    if (!PLUGIN_MENU_ManageAllocScratch())
    {
        PLUGIN_MENU_ManageDrawFailure();
        return;
    }

    PLUGIN_MENU_ManageLoadChanges();
    PLUGIN_MENU_ManageBuildList();

    for (;;)
    {
        if (MENU_HOST__menuShouldExit)
            break;
        if (selected >= g_MENUManageFileCount && g_MENUManageFileCount)
            selected = g_MENUManageFileCount - 1u;

        if (g_MENUManageFileCount)
            first = PLUGIN_MENU_ManageAdjustFirst(first, PLUGIN_MENU_ManageRowForFile(selected));
        else
            first = 0;
        if (redraw)
        {
            PLUGIN_MENU_ManageDraw(first, selected);
            redraw = false;
        }

        u32 pressed = MENU_HOST__waitInput();
        if (MENU_HOST__menuShouldExit || (pressed & KEY_B))
            break;
        if (!g_MENUManageFileCount)
            continue;

        if (pressed & KEY_DOWN)
        {
            u32 oldSelected = selected;
            u32 oldFirst = first;
            selected = selected + 1u < g_MENUManageFileCount ? selected + 1u : 0u;
            first = PLUGIN_MENU_ManageAdjustFirst(first, PLUGIN_MENU_ManageRowForFile(selected));
            if (first != oldFirst)
                redraw = true;
            else if (selected != oldSelected)
                PLUGIN_MENU_ManageRedrawSelection(first, oldSelected, selected);
        }
        else if (pressed & KEY_UP)
        {
            u32 oldSelected = selected;
            u32 oldFirst = first;
            selected = selected ? selected - 1u : g_MENUManageFileCount - 1u;
            first = PLUGIN_MENU_ManageAdjustFirst(first, PLUGIN_MENU_ManageRowForFile(selected));
            if (first != oldFirst)
                redraw = true;
            else if (selected != oldSelected)
                PLUGIN_MENU_ManageRedrawSelection(first, oldSelected, selected);
        }
        else if (pressed & KEY_X)
        {
            bool changed = PLUGIN_MENU_ManageActions(selected);
            if (changed)
            {
                PLUGIN_MENU_ManageDrawScanning();
                PLUGIN_MENU_ManageBuildList();
                if (selected >= g_MENUManageFileCount && g_MENUManageFileCount)
                    selected = g_MENUManageFileCount - 1u;
                first = 0;
            }
            redraw = true;
        }
    }

    PLUGIN_MENU_ManageFreeScratch();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_Open(void)
{
    if (g_MENUUnread)
    {
        g_MENUUnread = false;
        (void)PLUGIN_MENU_SaveSeenState();
    }

    u32 selected = 0;
    u32 first = 0;
    u32 drawnCount = 0;
    u32 drawnGeneration = 0;
    bool fullRedraw = true;
    g_MENUInsideMenu = true;

    for (;;)
    {
        u32 count;
        u32 generation;
        PLUGIN_MENU_GetRegistryState(&count, &generation);
        if (!count || MENU_HOST__menuShouldExit)
            break;

        if (selected >= count)
            selected = count - 1u;

        if (count <= MENU_VISIBLE_ITEMS)
            first = 0;
        else
        {
            if (first > selected)
                first = selected;
            if (selected >= first + MENU_VISIBLE_ITEMS)
                first = selected - MENU_VISIBLE_ITEMS + 1u;
            if (first > count - MENU_VISIBLE_ITEMS)
                first = count - MENU_VISIBLE_ITEMS;
        }

        if (count != drawnCount || generation != drawnGeneration)
            fullRedraw = true;

        if (fullRedraw)
        {
            PLUGIN_MENU_Draw(first, selected, count);
            drawnCount = count;
            drawnGeneration = generation;
            fullRedraw = false;
        }

        u32 pressed = MENU_HOST__waitInput();

        if (MENU_HOST__menuShouldExit)
            break;

        if (pressed & KEY_A)
        {
            void (*callback)(void) = PLUGIN_MENU_GetCallback(selected);
            if (callback)
            {
                PLUGIN_MENU_ClearForCallback();
                callback();
                fullRedraw = true;
            }
        }
        else if (pressed & KEY_B)
        {
            break;
        }
        else if (pressed & KEY_DOWN)
        {
            u32 oldSelected = selected;
            u32 oldFirst = first;

            if (selected + 1u >= count)
            {
                selected = 0;
                first = 0;
            }
            else
            {
                selected++;
                if (selected >= first + MENU_VISIBLE_ITEMS)
                    first++;
            }

            if (first != oldFirst)
                fullRedraw = true;
            else if (selected != oldSelected)
                PLUGIN_MENU_RedrawSelection(first, oldSelected, selected);
        }
        else if (pressed & KEY_UP)
        {
            u32 oldSelected = selected;
            u32 oldFirst = first;

            if (!selected)
            {
                selected = count - 1u;
                first = count > MENU_VISIBLE_ITEMS ? count - MENU_VISIBLE_ITEMS : 0;
            }
            else
            {
                selected--;
                if (selected < first)
                    first--;
            }

            if (first != oldFirst)
                fullRedraw = true;
            else if (selected != oldSelected)
                PLUGIN_MENU_RedrawSelection(first, oldSelected, selected);
        }
    }

    g_MENUInsideMenu = false;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_CopyMenuItem(MenuItem *dst, const MenuItem *src)
{
    dst->title = src->title;
    dst->action_type = src->action_type;
    if (src->action_type == METHOD)
        dst->method = src->method;
    else
        dst->menu = src->menu;
    dst->visibility = src->visibility;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InsertRootItem(void)
{
    Menu *root = MENU_HOST__rosalinaMenu;
    u32 count = 0;

    while (count < MENU_MAX_HOST_ITEMS && root->items[count].action_type != MENU_END)
        count++;

    if (count >= MENU_MAX_HOST_ITEMS - 1u)
        return false;

    u32 index = MENU_MAX_HOST_ITEMS;
    for (u32 i = 0; i < count; i++)
    {
        if (root->items[i].action_type == MENU &&
            root->items[i].menu == MENU_HOST__miscellaneousMenu)
        {
            index = i;
            break;
        }
    }

    if (index == MENU_MAX_HOST_ITEMS)
        return false;

    for (u32 i = count + 1u; i > index; i--)
        PLUGIN_MENU_CopyMenuItem(&root->items[i], &root->items[i - 1u]);

    root->items[index].title = g_MENUEntryTitle;
    root->items[index].action_type = METHOD;
    root->items[index].method = PLUGIN_MENU_Open;
    root->items[index].visibility = NULL;

    g_MENURootInserted = true;
    g_MENURootIndex = index;
    g_MENURootOriginalCount = count;
    return true;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_RemoveRootItem(void)
{
    if (!g_MENURootInserted)
        return;

    Menu *root = MENU_HOST__rosalinaMenu;
    if (root->items[g_MENURootIndex].action_type == METHOD &&
        root->items[g_MENURootIndex].method == PLUGIN_MENU_Open)
    {
        for (u32 i = g_MENURootIndex; i <= g_MENURootOriginalCount; i++)
            PLUGIN_MENU_CopyMenuItem(&root->items[i], &root->items[i + 1u]);
    }

    g_MENURootInserted = false;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_SetupItems(void)
{
    if (!PLUGIN_MENU_AppendInternal(
            &g_MENUManageItem,
            MENU_PLUGIN_ID,
            g_MENUManageItemTitle,
            PLUGIN_MENU_Manage,
            COLOR_WHITE))
    {
        return false;
    }

    return PLUGIN_MENU_AppendInternal(
        &g_MENUOnlineItem,
        MENU_PLUGIN_ID,
        g_MENUOnlineItemTitle,
        PLUGIN_MENU_OpenOnlineMenu,
        COLOR_WHITE
    );
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ResetRegistry(void)
{
    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *item = g_MENUFirstItem;
    while (item)
    {
        PluginMenuRegistration *next = item->next;
        item->pluginId = 0;
        item->title = NULL;
        item->callback = NULL;
        item->color = 0;
        item->next = NULL;
        item = next;
    }

    g_MENUFirstItem = NULL;
    g_MENULastItem = NULL;
    g_MENUItemCount = 0;
    g_MENURegistryGeneration++;
    PLUGIN_MENU_UnlockRegistry();
}

PLUGIN_MAIN(MENU) bool PLUGIN_MENU_Main(void)
{
    PLUGIN_MENU_ManageResetTempList();
    PLUGIN_MENU_ManageCaptureBootSnapshot();

    if (!PLUGIN_MENU_SetupItems())
    {
        PLUGIN_MENU_ResetRegistry();
        return false;
    }

    PluginMenuFileContext selfFile;
    bool selfOpen = PLUGIN_MENU_OpenPluginFile(MENU_PLUGIN_ID, &selfFile);

    PLUGIN_MENU_LoadSeenState(selfOpen ? &selfFile : NULL);
    if (selfOpen)
        g_MENUHttpsReady = PLUGIN_MENU_EnsureHttpslib(&selfFile);
    else
    {
        g_MENUHttpsReady = false;
        PLUGIN_MENU_OnlineSetFailure(g_MENUOnlineStageOpenFile, (Result)0xD8A0A060u);
    }

    if (selfOpen)
        PLUGIN_MENU_ClosePluginFile(&selfFile);

    if (!PLUGIN_MENU_InsertRootItem())
    {
        PLUGIN_MENU_ResetRegistry();
        return false;
    }

    // Install last so every false return leaves no pointer into MENU.
    if (!PLUGIN_MENU_InstallDrawStringHook())
    {
        PLUGIN_MENU_RemoveRootItem();
        PLUGIN_MENU_ResetRegistry();
        return false;
    }

    return true;
}
