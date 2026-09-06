#ifdef MENU_MODMENU_SOURCE
// shared menu.dat helpers
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

#endif
