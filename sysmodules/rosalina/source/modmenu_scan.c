#ifdef MENU_MODMENU_SOURCE
// .3nx lookup and MENU's seen-state
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
    bool allocScratchResult;
    {
        u32 base = 0;
        if (g_MENUScanScratch)
        {
            allocScratchResult = true;
            goto allocScratchDone;
        }
        if (!PLUGIN_MENU_TempAlloc(0x1000u, &base))
        {
            allocScratchResult = false;
            goto allocScratchDone;
        }
        g_MENUScanScratch = (PluginMenuScanScratch *) base;
        allocScratchResult = true;
        allocScratchDone:;

    }
    if (!allocScratchResult)
        goto done;
    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        goto done;
    }
    // first find the duplicate winner K11 would pick
    if (R_FAILED(MENU_HOST__FSUSER_OpenDirectory(&directory, archive, MENU_HOST__fsMakePath(PATH_ASCII, g_MENUPluginsPath))))
    {
        goto close_archive;
    }
    for (;;)
    {
        u32 entriesRead = 0;
        if (R_FAILED(MENU_HOST__FSDIR_Read(directory, &entriesRead, 1, &g_MENUScanEntry)) || !entriesRead)
        {
            break;
        }
        u32 length = 0;
        while ((length + 1u) < sizeof(g_MENUScanName) && g_MENUScanEntry.name[length])
        {
            g_MENUScanName[length] = (char) g_MENUScanEntry.name[length];
            length++;
        }

        g_MENUScanName[length] = 0;
        u32 priority;
        if (!PLUGIN_MENU_ParsePriority(g_MENUScanName, length, &priority) || !PLUGIN_MENU_MakePluginPath(g_MENUScanName))
        {
            continue;
        }
        Handle file = 0;
        if (R_FAILED(MENU_HOST__FSUSER_OpenFile(&file, archive, MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath), FS_OPEN_READ, 0)))
        {
            continue;
        }
        u32 offset = 0;
        for (;;)
        {
            PluginMenu3nxHeader header;
            u32 metadataOffset;
            u32 nextOffset;
            if (!PLUGIN_MENU_ReadHeader(file, offset, &header, &metadataOffset, &nextOffset) || (header.magic != MENU_PLUGIN_MAGIC && header.magic != LOADER_PLUGIN_MAGIC))
            {
                break;
            }
            if (header.magic == MENU_PLUGIN_MAGIC && header.pluginId == pluginId && PLUGIN_MENU_IsEarlier(found, priority, g_MENUScanName, offset, bestPriority, bestOffset))
            {
                found = true;
                bestPriority = priority;
                bestOffset = offset;
                bestMetadataOffset = metadataOffset;
                bestMetadataSize = header.metadataSize;
                {
                    char *copyStringDst = g_MENUBestName;
                    const char *copyStringSrc = g_MENUScanName;
                    u32 copyStringSize = sizeof(g_MENUBestName);
                    volatile char *out = copyStringDst;
                    const volatile char *in = copyStringSrc;
                    u32 i = 0;
                    while ((i + 1u) < copyStringSize && in[i])
                    {
                        out[i] = in[i];
                        i++;
                    }

                    out[i] = 0;
                }
            }
            offset = nextOffset;
        }

        MENU_HOST__FSFILE_Close(file);
    }

    MENU_HOST__FSDIR_Close(directory);
    directory = 0;
    if (!found)
        goto close_archive;
    // then make sure it lands inside K11's first 31 IDs
    {
        u32 earlierIds[MENU_SYSPLUGIN_MAX_PLUGINS];
        u32 earlierCount = 0;
        if (R_FAILED(MENU_HOST__FSUSER_OpenDirectory(&directory, archive, MENU_HOST__fsMakePath(PATH_ASCII, g_MENUPluginsPath))))
        {
            goto close_archive;
        }
        for (;;)
        {
            u32 entriesRead = 0;
            if (R_FAILED(MENU_HOST__FSDIR_Read(directory, &entriesRead, 1, &g_MENUScanEntry)) || !entriesRead)
            {
                break;
            }
            u32 length = 0;
            while ((length + 1u) < sizeof(g_MENUScanName) && g_MENUScanEntry.name[length])
            {
                g_MENUScanName[length] = (char) g_MENUScanEntry.name[length];
                length++;
            }

            g_MENUScanName[length] = 0;
            u32 priority;
            if (!PLUGIN_MENU_ParsePriority(g_MENUScanName, length, &priority) || !PLUGIN_MENU_MakePluginPath(g_MENUScanName))
            {
                continue;
            }
            Handle file = 0;
            if (R_FAILED(MENU_HOST__FSUSER_OpenFile(&file, archive, MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath), FS_OPEN_READ, 0)))
            {
                continue;
            }
            u32 offset = 0;
            for (;;)
            {
                PluginMenu3nxHeader header;
                u32 metadataOffset;
                u32 nextOffset;
                if (!PLUGIN_MENU_ReadHeader(file, offset, &header, &metadataOffset, &nextOffset) || (header.magic != MENU_PLUGIN_MAGIC && header.magic != LOADER_PLUGIN_MAGIC))
                {
                    break;
                }
                if (header.magic == MENU_PLUGIN_MAGIC && header.pluginId != pluginId && PLUGIN_MENU_IsEarlier(true, priority, g_MENUScanName, offset, bestPriority, bestOffset))
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
                        if (earlierCount >= (MENU_SYSPLUGIN_MAX_PLUGINS - 1u))
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
    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(&context->file, archive, MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath), FS_OPEN_READ, 0)))
    {
        context->file = 0;
        goto close_archive;
    }
    // re-read after the scans in case the file changed underneath us
    {
        PluginMenu3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        if (!PLUGIN_MENU_ReadHeader(context->file, bestOffset, &header, &metadataOffset, &nextOffset) || header.magic != MENU_PLUGIN_MAGIC || header.pluginId != pluginId || metadataOffset != bestMetadataOffset || header.metadataSize != bestMetadataSize)
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
    {
        if (g_MENUScanScratch)
            PLUGIN_MENU_TempFree((u32) g_MENUScanScratch, 0x1000u);
        g_MENUScanScratch = NULL;
    }

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

#endif
