#ifdef MENU_MODMENU_SOURCE
// installed sysplugin list + actions
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

    // same order K11 uses: priority, filename, stack offset
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
                item->group = 2u; // active now, disabled next boot
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

#endif
