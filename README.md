# Nexus3DS Sysplugin Menu

The core menu and shared API for [Nexus3DS](https://github.com/2b-zipper/Nexus3DS/tree/dev) Sysplugins!

This adds the **Sysplugin Menu** to Rosalina, letting other `.3nx` plugins register their own pages, save persistent data and integrate with the Online Menu without having to build everything directly into `boot.firm`.

For example plugins using it, see [my Sysplugins](https://github.com/Blurro/Blurros-Sysplugins).
<br>The simplest plugin to check out is PowerPrevent!

## Building

Build by overlaying Stock Nexus3DS -> 3NX Dev Kit -> This repository, then run:

```text
./pre_makeplugin.sh
./makeplugin.sh
```

Then place the built `ModMenu.0.3nx` in:

```text
/luma/plugins/
```

Make sure **Load external FIRMs and modules** is enabled in the SELECT boot settings.

### Features

* Central Sysplugin Menu for other plugins to add their own pages
* Persistent per-plugin save data
* Manage installed Sysplugins
* Online Sysplugin sources and updates (add any source to `sysplgfetch.txt`)
* Plugin metadata access and lz10 extraction helpers
* Temporary memory helpers for plugins
* Downloadable `.3on` pages for extending the Online Menu

---

<details>
<summary><b>MENU API for .3nx plugins</b></summary>

Rosalina Sysplugins can use the MENU API by adding `MENU` to `allowed_refs` in `makeplugin.sh` and including:

```c
#include "sysplugin_menu.h"
```

### Menu registration

```c
bool PLUGIN_MENU_AddItem(PluginMenuRegistration *item, u32 pluginId, const char *title, void (*callback)(void), u32 color);
bool PLUGIN_MENU_RemoveItem(PluginMenuRegistration *item);
```

Adds/removes pages from the Sysplugin Menu. The `PluginMenuRegistration` belongs to the calling plugin and must remain valid while registered.

### Persistent plugin data

```c
bool PLUGIN_MENU_GetDataSize(u32 pluginId, u32 *sizeOut);
bool PLUGIN_MENU_LoadData(u32 pluginId, void *data, u32 size);
bool PLUGIN_MENU_SaveData(u32 pluginId, const void *data, u32 size);
```

Provides persistent storage keyed by the plugin's 4-character ID.

### Plugin file / metadata access

```c
bool PLUGIN_MENU_OpenPluginFile(u32 pluginId, PluginMenuFileContext *context);
Result PLUGIN_MENU_UnpackLz10File(const PluginMenuFileContext *source, u32 compressedOffset, u32 compressedSize, const char *outputPath);
void PLUGIN_MENU_ClosePluginFile(PluginMenuFileContext *context);
```

`OpenPluginFile` locates the selected `.3nx` containing a plugin ID and gives access to its entry/metadata offsets. This lets larger assets live in `.3nx` metadata instead of storing them in permanent memory, or requiring packaging alongside the `.3nx`

### Online Menu sources

```c
bool PLUGIN_MENU_AddOnlineEntry(const char *title, const char *url);
bool PLUGIN_MENU_RemoveOnlineEntry(const char *title);
void PLUGIN_MENU_OpenOnlineSource(const char *url);
```

Plugins can add their own Online Menu sources or open one directly.

### Temporary memory

```c
bool PLUGIN_MENU_FindFreeRange(u32 size, u32 *outBase);
bool PLUGIN_MENU_TempAlloc(u32 size, u32 *outBase);
void PLUGIN_MENU_TempFree(u32 base, u32 size);
```

Useful for temporary buffers without permanently inflating plugin BSS. `TempAlloc` allocates the range, while `FindFreeRange` only finds a suitable free location.

</details>

---

<details>
<summary><b>HTTPSlib / Online .3on API</b></summary>

MENU keeps its HTTPS implementation in compressed `.3on` metadata and only loads it when the Online Menu needs it.

The HTTPS library exposes this API back to MENU:

```c
Result downloadToFile(const char *url, const char *path, u32 maxSize);
Result downloadToMemory(const char *url, void *buffer, u32 bufferSize, u32 *actualSize);
void openOnlineMenu(void);
void openOnlineSource(const char *url);
```

Normal `.3nx` plugins should generally use the public MENU Online functions instead of depending on HTTPSlib directly.

### Downloadable .3on pages

Online sources can point to downloadable `.3on` pages. These receive `MENUOnlineApi` containing:

```c
void drawLock(void);
void drawUnlock(void);
void drawClear(void);
u32 drawString(u32 x, u32 y, u32 color, const char *text);
void drawFlush(void);

u32 waitInputWithTimeout(s32 timeout);
volatile bool *menuShouldExit;

Result downloadToFile(const char *url, const char *path, u32 maxSize);
Result downloadToMemory(const char *url, void *buffer, u32 bufferSize, u32 *actualSize);

Result getFileSize(const char *path, u32 *size);
Result readFile(const char *path, u32 offset, void *buffer, u32 size, u32 *actualRead);
Result writeFile(const char *path, u32 offset, const void *buffer, u32 size, u32 *actualWritten);
Result setFileSize(const char *path, u32 size);
Result deleteFile(const char *path);
Result renameFile(const char *oldPath, const char *newPath);
Result fileExists(const char *path, bool *exists);
Result enumerateDirectory(const char *path, MENUOnlineDirVisitor visitor, void *context, u32 *entriesVisited);

const char *sourceUrlPrefix;
```

`sourceUrlPrefix` is the directory URL the current `.3on` came from, so assets can sit beside it without hardcoding the complete web path.

For example, a page downloaded from:

```text
https://example.com/files/page.3on
```

receives:

```text
https://example.com/files/
```

as its source prefix.

</details>

---
