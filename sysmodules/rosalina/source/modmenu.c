#define MENU_MODMENU_SOURCE 1
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

// registered pages live in one linked list
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

// caller keeps this alive until RemoveItem
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

#include "modmenu_data.c"
#include "modmenu_scan.c"
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

#include "modmenu_online.c"
#include "modmenu_manage.c"
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

// put one entry in Rosalina's root menu
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

    // install last so failed setup leaves no callback behind
    if (!PLUGIN_MENU_InstallDrawStringHook())
    {
        PLUGIN_MENU_RemoveRootItem();
        PLUGIN_MENU_ResetRegistry();
        return false;
    }

    return true;
}
#undef MENU_MODMENU_SOURCE
