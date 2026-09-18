# Nexus3DS Sysplugin Menu

The core menu and shared API for [Nexus3DS](https://github.com/2b-zipper/Nexus3DS/tree/dev) Sysplugins!

This adds the **Sysplugin Menu** provider to both Loader and Rosalina. Rosalina keeps the visible Sysplugin Menu, persistence, metadata and Online Menu APIs, while Loader owns shared application-patching hook points so other Loader `.3nx` plugins can register title/HOME callbacks instead of patching Loader independently.

For example plugins using it, see [my Sysplugins](https://github.com/Blurro/Blurros-Sysplugins).
<br>For developers, the simplest plugin to check out is 'Hello World', and 'PowerPrevent'!

## Building

Build by overlaying [Stock Nexus3DS](https://github.com/2b-zipper/Nexus3DS/tree/dev) -> [3NX Dev Kit](https://github.com/Blurro/3NX-Plugin-DevKit) -> This repository, then run:

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
* Loader-side title/HOME patch dispatcher for other Sysplugins
* One-way Loader -> Rosalina MENU bridge over `plg:ldr`
* Online Menu API compatibility gate that updates MENU before using management callbacks when the required API is unavailable

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

### Sysplugin management

```c
bool PLUGIN_MENU_AddSysplugin(const char *name);
bool PLUGIN_MENU_DisableSysplugin(const char *name);
bool PLUGIN_MENU_EnableSysplugin(const char *name);
bool PLUGIN_MENU_DeleteSysplugin(const char *name);
```

These functions are the single writer path for `menutemplist.txt` after boot. `AddSysplugin` is called after a new or replacement `.3nx` has been successfully installed on SD; it records the current-boot pending-reboot state without pretending the plugin is already active. Disable/enable perform the `.3nx` <-> `.3nx.d` rename and update the boot-state records consistently, while delete removes the file and its pending/change records. The built-in Manage Sysplugins page uses these same functions, and Online Menu receives them through its host API instead of editing `menutemplist.txt` itself.

External Rosalina plugins that need these operations include the devkit-owned `sysplugin_menu.h` and add `MENU` to `allowed_refs`.

### Temporary memory

```c
bool PLUGIN_MENU_FindFreeRange(u32 size, u32 *outBase);
bool PLUGIN_MENU_TempAlloc(u32 size, u32 *outBase);
void PLUGIN_MENU_TempFree(u32 base, u32 size);
bool PLUGIN_MENU_MapPage(Handle sourceProcess, u32 sourceAddress, u32 *mappedBase, u32 *mappedAddress);
void PLUGIN_MENU_UnmapPage(u32 mappedBase);
```

`FindFreeRange` only searches Rosalina's virtual address space for a suitable free range.

`TempAlloc` uses that free-range search to create ordinary temporary memory with `ControlMemoryUnsafe`, and `TempFree` releases that allocation. Use these for temporary buffers without permanently inflating plugin BSS.

`MapPage` and `UnmapPage` are a separate guarded-alias API. `MapPage` handles the full mapping setup itself: it finds space for three pages, allocates a guard page before and after the alias, and maps the requested source page into the middle. Callers do not need to find the alias address or allocate guard pages themselves.

`UnmapPage` removes the middle alias and then frees both guards.

The guards are required because compatible adjacent `MapProcessMemoryEx` mappings can merge into one kernel region. Unmapping only one page from such a merged region, using UnmapProcessMemoryEx, has a bug where it will remove following pages too.

</details>

---

<details>
<summary><b>Loader MENU API</b></summary>

`ModMenu.0.3nx` now stacks one `MENU` entry in Loader and one in Rosalina. Loader consumers add `MENU` to their Loader `allowed_refs` and include the Loader-side `sysplugin_menu.h`.

The Loader API deliberately keeps persistence, unpacking and Online Menu work on Rosalina. It exposes the five memory helpers, four patch-registration calls, and one queued bridge send call:

```c
bool PLUGIN_MENU_FindFreeRange(u32 size, u32 *outBase);
bool PLUGIN_MENU_TempAlloc(u32 size, u32 *outBase);
void PLUGIN_MENU_TempFree(u32 base, u32 size);
bool PLUGIN_MENU_MapPage(Handle sourceProcess, u32 sourceAddress, u32 *mappedBase, u32 *mappedAddress);
void PLUGIN_MENU_UnmapPage(u32 mappedBase);

bool PLUGIN_MENU_RegisterTitlePatch(PluginMenuLoaderTitlePatch *registration);
bool PLUGIN_MENU_UnregisterTitlePatch(PluginMenuLoaderTitlePatch *registration);
bool PLUGIN_MENU_RegisterHomePatch(PluginMenuLoaderHomePatch *registration);
bool PLUGIN_MENU_UnregisterHomePatch(PluginMenuLoaderHomePatch *registration);
bool PLUGIN_MENU_BridgeSend(u32 targetPluginId, u32 command, const void *payload, u32 payloadSize);
```

One title registration can list every regional title ID it supports. `prepare` runs after Loader has built the `CodeSetHeader` but before `svcCreateCodeSet`; `processCreated` runs after `svcCreateProcess` succeeds; `loaderFinished` runs after Loader has closed its normal `plg:ldr` session. The target process has not started executing at any of those stages. Returning `false` from `prepare` suppresses that registration's later callbacks for the current launch without aborting the title.

HOME Menu has a separate registry and dispatches from Loader's existing HOME-specific patch point. Multiple plugins can register for the same title or HOME; callbacks run in registration order. Registration objects, title-ID arrays and callbacks belong to the consumer plugin and must remain valid while registered.

### Loader -> Rosalina bridge

Loader plugins use `PLUGIN_MENU_BridgeSend` to queue a small copied message for a Rosalina plugin ID. MENU owns the `plg:ldr` transport, waits for Rosalina to become available, and retries delivery without making the consumer manage service sessions or startup timing. Payloads are capped at `SYSPLUGIN_MENU_BRIDGE_MAX_PAYLOAD` (`0xC0` bytes). `BridgeSend` returning `true` means MENU accepted the message into its queue, not that Rosalina has already received it.

Rosalina receivers include the devkit-owned `sysplugin_menu.h` and register locally:

```c
bool PLUGIN_MENU_RegisterBridgeReceiver(PluginMenuBridgeRegistration *registration);
bool PLUGIN_MENU_UnregisterBridgeReceiver(PluginMenuBridgeRegistration *registration);
```

The registration supplies the destination plugin ID and a callback receiving `(command, payload, payloadSize)`. Returning `false` asks MENU Loader to retry later; returning `true` consumes the message. Registrations never cross processes and callback pointers are never sent over IPC.

MENU also guards stock Loader's own single-session `plg:ldr` use. Bridge traffic pauses while stock plugin loading owns the service, and stock `plgldrInit()` retries the transient port-full result if a bridge session is still being reaped.

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
