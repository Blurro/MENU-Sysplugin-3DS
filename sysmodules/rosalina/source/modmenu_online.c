#ifdef MENU_MODMENU_SOURCE
// transient https loader and source list plumbing
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

    // don't edit sources while an unpack is using them
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

#endif
