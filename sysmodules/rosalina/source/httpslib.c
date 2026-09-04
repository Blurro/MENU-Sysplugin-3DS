#include <3ds.h>
#include "minisoc.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define MENU_HTTPS_HOST_API_VERSION 4u
#define MENU_HTTPS_API_VERSION 4u
#define HTTPS_DOWNLOAD_CHUNK_SIZE 0x1000u
#define HTTPS_LOW 0x10000000u
#define HTTPS_HIGH 0x14000000u
#define HTTPS_TLS_HEAP_SIZE 0x18000u
#define HTTPS_TLS_DNS_BUFFER_SIZE 512u
#define HTTPS_TLS_TCP_PORT 443u
#define HTTPS_TLS_DNS_PORT 53u
#define HTTPS_TLS_DNS_TIMEOUT_MS 3000

#define PLUGIN_htps_OnlineSetFailure(stage, result) ((void)0)

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

#define HTPS_ONLINE_API_VERSION 5u
#define HTPS_TRANSIENT_MAGIC 0x26584E33u
#define HTPS_ONLINE_ID 0x6E6C6E6Fu
#define HTPS_TRANSIENT_HEADER_SIZE 0x2Cu
#define HTPS_TRANSIENT_MAX_FILE_SIZE 0x20000u
#define HTPS_TRANSIENT_MAX_RUNTIME_SIZE 0x30000u
#define HTPS_FETCH_CONFIG_MAX 0x800u
#define HTPS_FETCH_MAX_SOURCES 64u
#define HTPS_FETCH_BASE_MAX 1024u
#define HTPS_VISIBLE_ITEMS 16u
#define HTPS_FS_MAX_PATH 271u
#define HTPS_FS_BAD_ARG ((Result)0xD8A0A070u)
#define HTPS_FS_TOO_LARGE ((Result)0xD8A0A071u)
#define HTPS_DIR_NAME_CAP 256u
#define HTPS_DIR_NAME_COMPLETE 1u

typedef struct
{
    char name[HTPS_DIR_NAME_CAP];
    u64 fileSize;
    u32 attributes;
    u32 flags;
} HtpsOnlineDirEntry;

typedef Result (*HtpsOnlineDirVisitor)(const HtpsOnlineDirEntry *, void *, bool *);

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
    Result (*downloadToFile)(const char *, const char *, u32);
    Result (*downloadToMemory)(const char *, void *, u32, u32 *);
    Result (*getFileSize)(const char *, u32 *);
    Result (*readFile)(const char *, u32, void *, u32, u32 *);
    Result (*writeFile)(const char *, u32, const void *, u32, u32 *);
    Result (*setFileSize)(const char *, u32);
    Result (*deleteFile)(const char *);
    Result (*renameFile)(const char *, const char *);
    Result (*fileExists)(const char *, bool *);
    Result (*enumerateDirectory)(const char *, HtpsOnlineDirVisitor, void *, u32 *);
    const char *sourceUrlPrefix;
} MENUOnlineApi;

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
} HtpsTransientHeader;

typedef struct { u32 base, codeSize, totalSize; } HtpsTransientImage;
typedef struct { char *title; char *url; } HtpsFetchSource;

typedef struct
{
    u32 size;
    u32 used;
} HttpsTlsHeapBlock;

PLUGIN_DATA(htps) static const MENUHttpsHostApi *g_httpsHost = NULL;
PLUGIN_RODATA(htps) static const char g_httpsOnlineEmptyPath[] = "";
PLUGIN_RODATA(htps) static const char g_httpsTlsSslService[] = "ssl:C";
PLUGIN_RODATA(htps) static const char g_httpsTlsPsService[] = "ps:ps";
PLUGIN_RODATA(htps) static const char g_httpsTlsHttpsPrefix[] = "https://";
PLUGIN_RODATA(htps) static const char g_httpsTlsRootPath[] = "/";
PLUGIN_RODATA(htps) static const char g_httpsTlsHttpGet[] = "GET ";
PLUGIN_RODATA(htps) static const char g_httpsTlsHttpVersionHost[] = " HTTP/1.1\r\nHost: ";
PLUGIN_RODATA(htps) static const char g_httpsTlsHttpTail[] = "\r\nUser-Agent: Nexus3DS-SysPlugin/1\r\nConnection: close\r\n\r\n";
PLUGIN_RODATA(htps) static const char g_httpsTlsContentLength[] = "Content-Length";
PLUGIN_RODATA(htps) static const u32 g_httpsTlsDnsServers[] = { 0x08080808u, 0x01010101u };
PLUGIN_BSS(htps) static char g_httpsTlsHost[96];
PLUGIN_BSS(htps) static u8 g_httpsTlsDnsBuffer[HTTPS_TLS_DNS_BUFFER_SIZE];
PLUGIN_BSS(htps) static u32 g_httpsTlsHeapBase;
PLUGIN_BSS(htps) static u32 g_httpsTlsHeapSize;

#define HTTPS_TABLE (g_httpsHost->hostTable)
#define HTTPS_HOST__FSUSER_OpenArchive ((Result(*)(FS_Archive*,FS_ArchiveID,FS_Path))HTTPS_TABLE[1])
#define HTTPS_HOST__FSUSER_CloseArchive ((Result(*)(FS_Archive))HTTPS_TABLE[2])
#define HTTPS_HOST__FSUSER_OpenDirectory ((Result(*)(Handle*,FS_Archive,FS_Path))HTTPS_TABLE[3])
#define HTTPS_HOST__FSDIR_Read ((Result(*)(Handle,u32*,u32,FS_DirectoryEntry*))HTTPS_TABLE[4])
#define HTTPS_HOST__FSDIR_Close ((Result(*)(Handle))HTTPS_TABLE[5])
#define HTTPS_HOST__FSUSER_OpenFile ((Result(*)(Handle*,FS_Archive,FS_Path,u32,u32))HTTPS_TABLE[6])
#define HTTPS_HOST__FSFILE_Read ((Result(*)(Handle,u32*,u64,void*,u32))HTTPS_TABLE[7])
#define HTTPS_HOST__FSFILE_Write ((Result(*)(Handle,u32*,u64,const void*,u32,u32))HTTPS_TABLE[8])
#define HTTPS_HOST__FSFILE_SetSize ((Result(*)(Handle,u64))HTTPS_TABLE[9])
#define HTTPS_HOST__FSFILE_Close ((Result(*)(Handle))HTTPS_TABLE[10])
#define HTTPS_HOST__fsMakePath ((FS_Path(*)(FS_PathType,const void*))HTTPS_TABLE[11])
#define HTTPS_HOST__Draw_Lock ((void(*)(void))HTTPS_TABLE[12])
#define HTTPS_HOST__Draw_Unlock ((void(*)(void))HTTPS_TABLE[13])
#define HTTPS_HOST__Draw_Clear ((void(*)(void))HTTPS_TABLE[14])
#define HTTPS_HOST__Draw_String ((u32(*)(u32,u32,u32,const char*))HTTPS_TABLE[15])
#define HTTPS_HOST__Draw_Flush ((void(*)(void))HTTPS_TABLE[17])
#define HTTPS_HOST__menuShouldExit ((volatile bool*)HTTPS_TABLE[19])
#define HTTPS_HOST__svcQueryMemory ((Result(*)(MemInfo*,PageInfo*,u32))HTTPS_TABLE[24])
#define HTTPS_HOST__svcFlushEntireDataCache ((void(*)(void))HTTPS_TABLE[25])
#define HTTPS_HOST__svcInvalidateEntireInstructionCache ((void(*)(void))HTTPS_TABLE[26])
#define HTTPS_HOST__FSFILE_GetSize ((Result(*)(Handle,u64*))HTTPS_TABLE[27])
#define HTTPS_HOST__FSUSER_DeleteFile ((Result(*)(FS_Archive,FS_Path))HTTPS_TABLE[28])
#define HTTPS_HOST__svcControlMemoryUnsafe ((Result(*)(u32*,u32,u32,MemOp,MemPerm))HTTPS_TABLE[29])
#define HTTPS_HOST__waitInputWithTimeout ((u32(*)(s32))HTTPS_TABLE[30])
#define HTTPS_HOST__srvGetServiceHandle ((Result(*)(Handle*,const char*))HTTPS_TABLE[31])
#define HTTPS_HOST__miniSocInit ((Result(*)(void))HTTPS_TABLE[32])
#define HTTPS_HOST__miniSocExit ((Result(*)(void))HTTPS_TABLE[33])
#define HTTPS_HOST__socSocket ((int(*)(int,int,int))HTTPS_TABLE[34])
#define HTTPS_HOST__socConnect ((int(*)(int,const struct sockaddr*,socklen_t))HTTPS_TABLE[35])
#define HTTPS_HOST__socPoll ((int(*)(struct pollfd*,nfds_t,int))HTTPS_TABLE[36])
#define HTTPS_HOST__socSendto ((ssize_t(*)(int,const void*,size_t,int,const struct sockaddr*,socklen_t))HTTPS_TABLE[37])
#define HTTPS_HOST__socRecvfrom ((ssize_t(*)(int,void*,size_t,int,struct sockaddr*,socklen_t*))HTTPS_TABLE[38])
#define HTTPS_HOST__socClose ((int(*)(int))HTTPS_TABLE[39])
#define HTTPS_HOST__FSUSER_RenameFile ((Result(*)(FS_Archive,FS_Path,FS_Archive,FS_Path))HTTPS_TABLE[40])
#define HTTPS_HOST__FSUSER_CreateDirectory ((Result(*)(FS_Archive,FS_Path,u32))HTTPS_TABLE[41])
#define HTTPS_HOST__protectMemory (g_httpsHost->protectMemory)

extern Result PLUGIN_htps_OnlineSvcSendSyncRequest(Handle handle);
extern Result PLUGIN_htps_OnlineSvcCloseHandle(Handle handle);
extern int PLUGIN_htps_TlsConnect(
    void **out,
    const char *host,
    void *bio,
    int (*rng)(void *, unsigned char *, size_t),
    void *rngCtx,
    int (*sendFn)(void *, const unsigned char *, size_t),
    int (*recvFn)(void *, unsigned char *, size_t)
);
extern int PLUGIN_htps_TlsWrite(void *tls, const void *buffer, size_t size);
extern int PLUGIN_htps_TlsRead(void *tls, void *buffer, size_t size);
extern void PLUGIN_htps_TlsClose(void *tls);

__asm__(
    ".arm\n"
    ".section .plugin_htps, \"ax\", %progbits\n"
    ".balign 4\n"
    ".global PLUGIN_htps_OnlineSvcSendSyncRequest\n"
    ".type PLUGIN_htps_OnlineSvcSendSyncRequest, %function\n"
    "PLUGIN_htps_OnlineSvcSendSyncRequest:\n"
    "svc 0x32\n"
    "bx lr\n"
    ".global PLUGIN_htps_OnlineSvcCloseHandle\n"
    ".type PLUGIN_htps_OnlineSvcCloseHandle, %function\n"
    "PLUGIN_htps_OnlineSvcCloseHandle:\n"
    "svc 0x23\n"
    "bx lr\n"
);

PLUGIN_CODE(htps) void *PLUGIN_htps_TlsMemcpy(void *dst, const void *src, size_t size)
{
    volatile u8 *d = (volatile u8 *)dst;
    volatile const u8 *s = (volatile const u8 *)src;
    while (size--)
        *d++ = *s++;
    return dst;
}

PLUGIN_CODE(htps) void *PLUGIN_htps_TlsMemmove(void *dst, const void *src, size_t size)
{
    volatile u8 *d = (volatile u8 *)dst;
    volatile const u8 *s = (volatile const u8 *)src;

    if (d < s)
    {
        while (size--)
            *d++ = *s++;
    }
    else if (d > s)
    {
        d += size;
        s += size;
        while (size--)
            *--d = *--s;
    }
    return dst;
}

PLUGIN_CODE(htps) void *PLUGIN_htps_TlsMemset(void *dst, int value, size_t size)
{
    volatile u8 *d = (volatile u8 *)dst;
    while (size--)
        *d++ = (u8)value;
    return dst;
}

PLUGIN_CODE(htps) int PLUGIN_htps_TlsMemcmp(const void *a, const void *b, size_t size)
{
    volatile const u8 *x = (volatile const u8 *)a;
    volatile const u8 *y = (volatile const u8 *)b;
    while (size--)
    {
        if (*x != *y)
            return *x < *y ? -1 : 1;
        x++;
        y++;
    }
    return 0;
}

PLUGIN_CODE(htps) size_t PLUGIN_htps_TlsStrlen(const char *text)
{
    size_t size = 0;
    while (*(volatile const char *)&text[size])
        size++;
    return size;
}

PLUGIN_CODE(htps) int PLUGIN_htps_TlsStrcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (u8)*a - (u8)*b;
}

PLUGIN_CODE(htps) int PLUGIN_htps_TlsStrncmp(const char *a, const char *b, size_t size)
{
    while (size && *a && *a == *b)
    {
        a++;
        b++;
        size--;
    }
    return size ? (u8)*a - (u8)*b : 0;
}

PLUGIN_CODE(htps) char *PLUGIN_htps_TlsStrchr(const char *text, int value)
{
    char target = (char)value;
    for (;;)
    {
        if (*text == target)
            return (char *)text;
        if (!*text)
            return NULL;
        text++;
    }
}

PLUGIN_CODE(htps) static u16 PLUGIN_htps_TlsNet16(u16 value)
{
    return (u16)((value << 8) | (value >> 8));
}

PLUGIN_CODE(htps) static Result PLUGIN_htps_TlsRandomFromService(
    const char *service,
    u32 command,
    unsigned char *output,
    size_t size
)
{
    Handle handle = 0;
    u32 *cmdbuf;
    Result result;

    if (!output || !size || size > 0x0FFFFFFFu)
        return (Result)0xD8A0A061u;

    result = HTTPS_HOST__srvGetServiceHandle(&handle, service);
    if (R_FAILED(result))
        return result;

    cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = command;
    cmdbuf[1] = (u32)size;
    cmdbuf[2] = ((u32)size << 4) | 12u;
    cmdbuf[3] = (u32)output;
    result = PLUGIN_htps_OnlineSvcSendSyncRequest(handle);
    if (R_SUCCEEDED(result))
        result = (Result)cmdbuf[1];
    (void)PLUGIN_htps_OnlineSvcCloseHandle(handle);
    return result;
}

PLUGIN_CODE(htps) static int PLUGIN_htps_TlsRandom(
    void *ctx,
    unsigned char *output,
    size_t size
)
{
    Result result;
    (void)ctx;

    result = PLUGIN_htps_TlsRandomFromService(
        g_httpsTlsSslService,
        0x00110042u,
        output,
        size);
    if (R_FAILED(result))
        result = PLUGIN_htps_TlsRandomFromService(
            g_httpsTlsPsService,
            0x000D0042u,
            output,
            size);
    return R_SUCCEEDED(result) ? 0 : -1;
}

PLUGIN_CODE(htps) static int PLUGIN_htps_TlsBioSend(
    void *ctx,
    const unsigned char *buffer,
    size_t size
)
{
    int socket = (int)(u32)ctx;
    ssize_t sent = HTTPS_HOST__socSendto(socket, buffer, size, 0, NULL, 0);
    return sent < 0 ? -1 : (int)sent;
}

PLUGIN_CODE(htps) static int PLUGIN_htps_TlsBioRecv(
    void *ctx,
    unsigned char *buffer,
    size_t size
)
{
    int socket = (int)(u32)ctx;
    ssize_t received = HTTPS_HOST__socRecvfrom(socket, buffer, size, 0, NULL, NULL);
    return received < 0 ? -1 : (int)received;
}

PLUGIN_CODE(htps) static u16 PLUGIN_htps_TlsRead16(const u8 *data)
{
    return (u16)(((u16)data[0] << 8) | data[1]);
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsSkipDnsName(
    const u8 *buffer,
    u32 size,
    u32 *position
)
{
    u32 pos = *position;
    u32 labels = 0;

    while (pos < size && labels++ < 128u)
    {
        u8 length = buffer[pos++];
        if (!length)
        {
            *position = pos;
            return true;
        }
        if ((length & 0xC0u) == 0xC0u)
        {
            if (pos >= size)
                return false;
            *position = pos + 1u;
            return true;
        }
        if ((length & 0xC0u) || length > 63u || length > size - pos)
            return false;
        pos += length;
    }
    return false;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsBuildDnsQuery(
    const char *host,
    u8 *buffer,
    u32 size,
    u32 *querySize
)
{
    u32 pos = 12u;
    const char *cursor = host;

    if (size < 18u || PLUGIN_htps_TlsRandom(NULL, buffer, 2u) != 0)
        return false;

    for (u32 i = 2; i < 12u; i++)
        buffer[i] = 0;
    buffer[2] = 1u;
    buffer[5] = 1u;

    while (*cursor)
    {
        const char *label = cursor;
        u32 length = 0;

        while (*cursor && *cursor != '.')
        {
            length++;
            cursor++;
        }
        if (!length || length > 63u || pos + length + 1u >= size)
            return false;
        buffer[pos++] = (u8)length;
        for (u32 i = 0; i < length; i++)
            buffer[pos++] = (u8)label[i];
        if (*cursor == '.')
            cursor++;
    }

    if (pos + 5u > size)
        return false;
    buffer[pos++] = 0;
    buffer[pos++] = 0;
    buffer[pos++] = 1;
    buffer[pos++] = 0;
    buffer[pos++] = 1;
    *querySize = pos;
    return true;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsParseDnsResponse(
    const u8 *buffer,
    u32 size,
    u8 id0,
    u8 id1,
    u32 *ip
)
{
    u32 pos = 12u;
    u16 questions;
    u16 answers;

    if (size < 12u || buffer[0] != id0 || buffer[1] != id1 ||
        !(buffer[2] & 0x80u) || (buffer[3] & 0x0Fu))
        return false;

    questions = PLUGIN_htps_TlsRead16(buffer + 4u);
    answers = PLUGIN_htps_TlsRead16(buffer + 6u);

    for (u32 i = 0; i < questions; i++)
    {
        if (!PLUGIN_htps_TlsSkipDnsName(buffer, size, &pos) || pos + 4u > size)
            return false;
        pos += 4u;
    }

    for (u32 i = 0; i < answers; i++)
    {
        u16 type;
        u16 classId;
        u16 dataSize;

        if (!PLUGIN_htps_TlsSkipDnsName(buffer, size, &pos) || pos + 10u > size)
            return false;
        type = PLUGIN_htps_TlsRead16(buffer + pos);
        classId = PLUGIN_htps_TlsRead16(buffer + pos + 2u);
        dataSize = PLUGIN_htps_TlsRead16(buffer + pos + 8u);
        pos += 10u;
        if (dataSize > size - pos)
            return false;

        if (type == 1u && classId == 1u && dataSize == 4u)
        {
            u8 *dst = (u8 *)ip;
            dst[0] = buffer[pos];
            dst[1] = buffer[pos + 1u];
            dst[2] = buffer[pos + 2u];
            dst[3] = buffer[pos + 3u];
            return true;
        }
        pos += dataSize;
    }
    return false;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsResolve(const char *host, u32 *ip)
{
    u32 querySize = 0;

    if (!PLUGIN_htps_TlsBuildDnsQuery(
            host,
            g_httpsTlsDnsBuffer,
            sizeof(g_httpsTlsDnsBuffer),
            &querySize))
        return false;

    for (u32 server = 0; server < 2u; server++)
    {
        int socket = HTTPS_HOST__socSocket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in address;
        struct pollfd pollfd;
        ssize_t received;
        u8 id0 = g_httpsTlsDnsBuffer[0];
        u8 id1 = g_httpsTlsDnsBuffer[1];
        bool ok = false;

        if (socket < 0)
            continue;

        PLUGIN_htps_TlsMemset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = PLUGIN_htps_TlsNet16(HTTPS_TLS_DNS_PORT);
        address.sin_addr.s_addr = g_httpsTlsDnsServers[server];

        if (HTTPS_HOST__socSendto(
                socket,
                g_httpsTlsDnsBuffer,
                querySize,
                0,
                (const struct sockaddr *)&address,
                sizeof(address)) < 0)
            goto dns_done;

        pollfd.fd = socket;
        pollfd.events = POLLIN;
        pollfd.revents = 0;
        if (HTTPS_HOST__socPoll(&pollfd, 1, HTTPS_TLS_DNS_TIMEOUT_MS) <= 0 ||
            !(pollfd.revents & POLLIN))
            goto dns_done;

        received = HTTPS_HOST__socRecvfrom(
            socket,
            g_httpsTlsDnsBuffer,
            sizeof(g_httpsTlsDnsBuffer),
            0,
            NULL,
            NULL);
        if (received > 0)
            ok = PLUGIN_htps_TlsParseDnsResponse(
                g_httpsTlsDnsBuffer,
                (u32)received,
                id0,
                id1,
                ip);

dns_done:
        (void)HTTPS_HOST__socClose(socket);
        if (ok)
            return true;
    }
    return false;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsParseUrl(
    const char *url,
    const char **path
)
{
    const char *prefix = g_httpsTlsHttpsPrefix;
    u32 hostSize = 0;

    while (*prefix)
    {
        if (*url++ != *prefix++)
            return false;
    }

    while (*url && *url != '/')
    {
        if (hostSize + 1u >= sizeof(g_httpsTlsHost) || *url == ':')
            return false;
        g_httpsTlsHost[hostSize++] = *url++;
    }
    if (!hostSize)
        return false;
    g_httpsTlsHost[hostSize] = 0;
    *path = *url ? url : g_httpsTlsRootPath;
    return true;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsAppend(
    u8 *buffer,
    u32 size,
    u32 *position,
    const char *text
)
{
    u32 pos = *position;
    while (*text)
    {
        if (pos >= size)
            return false;
        buffer[pos++] = (u8)*text++;
    }
    *position = pos;
    return true;
}

PLUGIN_CODE(htps) static char PLUGIN_htps_TlsLower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsHeaderNameEqual(
    const u8 *line,
    u32 lineSize,
    const char *name
)
{
    u32 i = 0;
    while (name[i])
    {
        if (i >= lineSize ||
            PLUGIN_htps_TlsLower((char)line[i]) != PLUGIN_htps_TlsLower(name[i]))
            return false;
        i++;
    }
    return i == lineSize;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsParseHttpHeaders(
    const u8 *buffer,
    u32 headerSize,
    u32 *contentSize
)
{
    u32 pos = 0;
    u32 lineEnd = 0;
    bool foundLength = false;
    u32 lengthValue = 0;

    while (lineEnd + 1u < headerSize &&
           !(buffer[lineEnd] == '\r' && buffer[lineEnd + 1u] == '\n'))
        lineEnd++;
    if (lineEnd + 1u >= headerSize || lineEnd < 12u ||
        buffer[0] != 'H' || buffer[1] != 'T' || buffer[2] != 'T' || buffer[3] != 'P')
        return false;

    while (pos < lineEnd && buffer[pos] != ' ')
        pos++;
    if (pos + 4u > lineEnd || buffer[pos + 1u] != '2' ||
        buffer[pos + 2u] != '0' || buffer[pos + 3u] != '0')
        return false;

    pos = lineEnd + 2u;
    while (pos + 1u < headerSize)
    {
        u32 start = pos;
        u32 end = pos;
        u32 colon;

        while (end + 1u < headerSize &&
               !(buffer[end] == '\r' && buffer[end + 1u] == '\n'))
            end++;
        if (end + 1u >= headerSize)
            return false;
        if (end == start)
            break;

        colon = start;
        while (colon < end && buffer[colon] != ':')
            colon++;
        if (colon < end &&
            PLUGIN_htps_TlsHeaderNameEqual(
                buffer + start,
                colon - start,
                g_httpsTlsContentLength))
        {
            u32 value = 0;
            u32 at = colon + 1u;
            bool digit = false;
            while (at < end && (buffer[at] == ' ' || buffer[at] == '\t'))
                at++;
            while (at < end && buffer[at] >= '0' && buffer[at] <= '9')
            {
                u32 next = value * 10u + (u32)(buffer[at] - '0');
                if (next < value)
                    return false;
                value = next;
                digit = true;
                at++;
            }
            if (!digit)
                return false;
            lengthValue = value;
            foundLength = true;
        }
        pos = end + 2u;
    }

    if (!foundLength)
        return false;
    *contentSize = lengthValue;
    return true;
}


PLUGIN_CODE(htps) static bool PLUGIN_htps_OnlineAdd32(u32 a, u32 b, u32 *out)
{
    u32 value = a + b;
    if (value < a)
        return false;
    *out = value;
    return true;
}


PLUGIN_CODE(htps) static bool PLUGIN_htps_OnlineFindFreeRange(u32 size, u32 *outBase)
{
    MemInfo info;
    PageInfo page;
    u32 scan = HTTPS_LOW;

    while (scan < HTTPS_HIGH)
    {
        u32 end;
        u32 base;

        if (R_FAILED(HTTPS_HOST__svcQueryMemory(&info, &page, scan)))
            return false;

        end = info.base_addr + info.size;
        if (end <= scan)
            return false;

        if (info.state == MEMSTATE_FREE)
        {
            base = (info.base_addr + 0xFFFu) & ~0xFFFu;
            if (base < HTTPS_LOW)
                base = HTTPS_LOW;

            if (base < HTTPS_HIGH &&
                size <= HTTPS_HIGH - base &&
                size <= end - base)
            {
                *outBase = base;
                return true;
            }
        }

        scan = end;
    }

    return false;
}


PLUGIN_CODE(htps) static Result PLUGIN_htps_TlsHeapInit(void)
{
    u32 base = 0;
    u32 allocated = 0;
    HttpsTlsHeapBlock *block;
    Result result;

    if (g_httpsTlsHeapBase)
        return 0;
    if (!PLUGIN_htps_OnlineFindFreeRange(HTTPS_TLS_HEAP_SIZE, &base))
        return (Result)0xD8A0A047u;

    result = HTTPS_HOST__svcControlMemoryUnsafe(
        &allocated,
        base,
        HTTPS_TLS_HEAP_SIZE,
        MEMOP_ALLOC | MEMOP_REGION_SYSTEM,
        MEMPERM_READWRITE);
    if (R_FAILED(result) || !allocated)
        return R_FAILED(result) ? result : (Result)0xD8A0A047u;

    g_httpsTlsHeapBase = allocated;
    g_httpsTlsHeapSize = HTTPS_TLS_HEAP_SIZE;
    block = (HttpsTlsHeapBlock *)allocated;
    block->size = HTTPS_TLS_HEAP_SIZE - sizeof(*block);
    block->used = 0;
    return 0;
}

PLUGIN_CODE(htps) static void PLUGIN_htps_TlsHeapDestroy(void)
{
    u32 out;
    if (!g_httpsTlsHeapBase)
        return;
    (void)HTTPS_HOST__svcControlMemoryUnsafe(
        &out,
        g_httpsTlsHeapBase,
        g_httpsTlsHeapSize,
        MEMOP_FREE | MEMOP_REGION_SYSTEM,
        MEMPERM_DONTCARE);
    g_httpsTlsHeapBase = 0;
    g_httpsTlsHeapSize = 0;
}

PLUGIN_CODE(htps) void *PLUGIN_htps_TlsCalloc(size_t count, size_t itemSize)
{
    u32 bytes;
    u32 at;
    u32 end;

    if (!g_httpsTlsHeapBase || !count || !itemSize ||
        count > 0xFFFFFFFFu / itemSize)
        return NULL;
    bytes = (u32)(count * itemSize);
    if (bytes > 0xFFFFFFF8u)
        return NULL;
    bytes = (bytes + 7u) & ~7u;
    at = g_httpsTlsHeapBase;
    end = g_httpsTlsHeapBase + g_httpsTlsHeapSize;

    while (at + sizeof(HttpsTlsHeapBlock) <= end)
    {
        HttpsTlsHeapBlock *block = (HttpsTlsHeapBlock *)at;
        u32 payload = at + sizeof(*block);
        u32 next;

        if (block->size > end - payload)
            return NULL;
        next = payload + block->size;
        if (!block->used && block->size >= bytes)
        {
            u32 remaining = block->size - bytes;
            if (remaining >= sizeof(*block) + 8u)
            {
                HttpsTlsHeapBlock *split = (HttpsTlsHeapBlock *)(payload + bytes);
                split->size = remaining - sizeof(*split);
                split->used = 0;
                block->size = bytes;
            }
            block->used = 1;
            PLUGIN_htps_TlsMemset((void *)payload, 0, block->size);
            return (void *)payload;
        }
        if (next <= at || next == end)
            break;
        at = next;
    }
    return NULL;
}

PLUGIN_CODE(htps) void PLUGIN_htps_TlsFree(void *pointer)
{
    u32 target = (u32)pointer;
    u32 at;
    u32 end;

    if (!pointer || !g_httpsTlsHeapBase)
        return;
    at = g_httpsTlsHeapBase;
    end = g_httpsTlsHeapBase + g_httpsTlsHeapSize;

    while (at + sizeof(HttpsTlsHeapBlock) <= end)
    {
        HttpsTlsHeapBlock *block = (HttpsTlsHeapBlock *)at;
        u32 payload = at + sizeof(*block);
        u32 next;

        if (block->size > end - payload)
            return;
        next = payload + block->size;
        if (payload == target)
        {
            block->used = 0;
            break;
        }
        if (next <= at || next == end)
            return;
        at = next;
    }

    at = g_httpsTlsHeapBase;
    while (at + sizeof(HttpsTlsHeapBlock) <= end)
    {
        HttpsTlsHeapBlock *block = (HttpsTlsHeapBlock *)at;
        u32 payload = at + sizeof(*block);
        u32 next = payload + block->size;
        if (block->size > end - payload || next >= end)
            break;
        {
            HttpsTlsHeapBlock *following = (HttpsTlsHeapBlock *)next;
            if (!block->used && !following->used)
            {
                block->size += sizeof(*following) + following->size;
                continue;
            }
        }
        at = next;
    }
}


PLUGIN_CODE(htps) static Result PLUGIN_htps_OnlineDownload(
    const char *url,
    const char *path,
    void *output,
    u32 maxSize,
    u32 *actualSize
)
{
    const char *urlPath = NULL;
    FS_Archive archive = 0;
    Handle file = 0;
    void *tls = NULL;
    int tcpSocket = -1;
    u32 ip = 0;
    u32 bufferBase = 0;
    u32 allocated = 0;
    u32 headerBytes = 0;
    u32 headerEnd = 0;
    u32 contentSize = 0;
    u32 totalWritten = 0;
    bool toFile = path != NULL;
    bool archiveOpen = false;
    bool fileOpen = false;
    bool socReady = false;
    bool heapReady = false;
    bool bufferReady = false;
    bool ok = false;
    Result result = 0;

    if (actualSize)
        *actualSize = 0;

    if (!url || !*url || !maxSize ||
        (toFile && !*path) || (!toFile && !output))
    {
        result = (Result)0xD8A0A04Bu;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsParseUrl, result);
        goto done;
    }

    if (!PLUGIN_htps_TlsParseUrl(url, &urlPath))
    {
        result = (Result)0xD8A0A04Bu;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsParseUrl, result);
        goto done;
    }

    result = PLUGIN_htps_TlsHeapInit();
    if (R_FAILED(result))
    {
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsAllocate, result);
        goto done;
    }
    heapReady = true;

    result = HTTPS_HOST__miniSocInit();
    if (R_FAILED(result))
    {
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsInitSoc, result);
        goto done;
    }
    socReady = true;

    if (!PLUGIN_htps_TlsResolve(g_httpsTlsHost, &ip))
    {
        result = (Result)0xD8A0A062u;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsDns, result);
        goto done;
    }

    tcpSocket = HTTPS_HOST__socSocket(AF_INET, SOCK_STREAM, 0);
    if (tcpSocket < 0)
    {
        result = (Result)0xD8A0A063u;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsConnect, result);
        goto done;
    }

    {
        struct sockaddr_in address;
        PLUGIN_htps_TlsMemset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = PLUGIN_htps_TlsNet16(HTTPS_TLS_TCP_PORT);
        address.sin_addr.s_addr = ip;
        if (HTTPS_HOST__socConnect(
                tcpSocket,
                (const struct sockaddr *)&address,
                sizeof(address)) < 0)
        {
            result = (Result)0xD8A0A064u;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsConnect, result);
            goto done;
        }
    }

    {
        int tlsResult = PLUGIN_htps_TlsConnect(
            &tls,
            g_httpsTlsHost,
            (void *)(u32)tcpSocket,
            PLUGIN_htps_TlsRandom,
            NULL,
            PLUGIN_htps_TlsBioSend,
            PLUGIN_htps_TlsBioRecv);
        if (tlsResult != 0)
        {
            result = (Result)tlsResult;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHandshake, result);
            goto done;
        }
    }

    if (!PLUGIN_htps_OnlineFindFreeRange(HTTPS_DOWNLOAD_CHUNK_SIZE, &bufferBase))
    {
        result = (Result)0xD8A0A047u;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsAllocate, result);
        goto done;
    }

    result = HTTPS_HOST__svcControlMemoryUnsafe(
        &allocated,
        bufferBase,
        HTTPS_DOWNLOAD_CHUNK_SIZE,
        MEMOP_ALLOC | MEMOP_REGION_SYSTEM,
        MEMPERM_READWRITE);
    if (R_FAILED(result) || !allocated)
    {
        if (R_SUCCEEDED(result))
            result = (Result)0xD8A0A047u;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsAllocate, result);
        goto done;
    }
    bufferBase = allocated;
    bufferReady = true;

    {
        u8 *buffer = (u8 *)bufferBase;
        u32 requestSize = 0;

        if (!PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, g_httpsTlsHttpGet) ||
            !PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, urlPath) ||
            !PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, g_httpsTlsHttpVersionHost) ||
            !PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, g_httpsTlsHost) ||
            !PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, g_httpsTlsHttpTail))
        {
            result = (Result)0xD8A0A065u;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHttp, result);
            goto done;
        }

        {
            int tlsResult = PLUGIN_htps_TlsWrite(tls, buffer, requestSize);
            if (tlsResult != 0)
            {
                result = (Result)tlsResult;
                PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHttp, result);
                goto done;
            }
        }

        while (!headerEnd && headerBytes < HTTPS_DOWNLOAD_CHUNK_SIZE)
        {
            int received = PLUGIN_htps_TlsRead(
                tls,
                buffer + headerBytes,
                HTTPS_DOWNLOAD_CHUNK_SIZE - headerBytes);
            if (received <= 0)
            {
                result = received < 0 ? (Result)received : (Result)0xD8A0A066u;
                PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHttp, result);
                goto done;
            }
            headerBytes += (u32)received;
            for (u32 i = 3u; i < headerBytes; i++)
            {
                if (buffer[i - 3u] == '\r' && buffer[i - 2u] == '\n' &&
                    buffer[i - 1u] == '\r' && buffer[i] == '\n')
                {
                    headerEnd = i + 1u;
                    break;
                }
            }
        }

        if (!headerEnd ||
            !PLUGIN_htps_TlsParseHttpHeaders(buffer, headerEnd, &contentSize) ||
            !contentSize || contentSize > maxSize)
        {
            result = (Result)0xD8A0A04Du;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHttp, result);
            goto done;
        }
    }

    if (toFile)
    {
        result = HTTPS_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            HTTPS_HOST__fsMakePath(PATH_EMPTY, g_httpsOnlineEmptyPath));
        if (R_FAILED(result))
        {
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
            goto done;
        }
        archiveOpen = true;

        (void)HTTPS_HOST__FSUSER_DeleteFile(
            archive,
            HTTPS_HOST__fsMakePath(PATH_ASCII, path));

        result = HTTPS_HOST__FSUSER_OpenFile(
            &file,
            archive,
            HTTPS_HOST__fsMakePath(PATH_ASCII, path),
            FS_OPEN_CREATE | FS_OPEN_WRITE,
            0);
        if (R_FAILED(result))
        {
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
            goto done;
        }
        fileOpen = true;

        result = HTTPS_HOST__FSFILE_SetSize(file, contentSize);
        if (R_FAILED(result))
        {
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
            goto done;
        }
    }

    {
        u8 *buffer = (u8 *)bufferBase;
        u32 bodyBytes = headerBytes - headerEnd;

        if (bodyBytes > contentSize)
        {
            result = (Result)0xD8A0A04Cu;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsReceive, result);
            goto done;
        }

        if (bodyBytes)
        {
            if (toFile)
            {
                u32 written = 0;
                result = HTTPS_HOST__FSFILE_Write(
                    file,
                    &written,
                    0,
                    buffer + headerEnd,
                    bodyBytes,
                    bodyBytes == contentSize ? FS_WRITE_FLUSH : 0);
                if (R_FAILED(result) || written != bodyBytes)
                {
                    result = R_FAILED(result) ? result : (Result)0xD8A0A04Au;
                    PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
                    goto done;
                }
            }
            else
            {
                PLUGIN_htps_TlsMemcpy(output, buffer + headerEnd, bodyBytes);
            }
            totalWritten = bodyBytes;
        }

        while (totalWritten < contentSize)
        {
            u32 remaining = contentSize - totalWritten;
            u32 request = remaining > HTTPS_DOWNLOAD_CHUNK_SIZE ?
                          HTTPS_DOWNLOAD_CHUNK_SIZE : remaining;
            int received = PLUGIN_htps_TlsRead(tls, buffer, request);

            if (received <= 0 || (u32)received > request)
            {
                result = received < 0 ? (Result)received : (Result)0xD8A0A049u;
                PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsReceive, result);
                goto done;
            }

            if (toFile)
            {
                u32 written = 0;
                result = HTTPS_HOST__FSFILE_Write(
                    file,
                    &written,
                    totalWritten,
                    buffer,
                    (u32)received,
                    totalWritten + (u32)received == contentSize ? FS_WRITE_FLUSH : 0);
                if (R_FAILED(result) || written != (u32)received)
                {
                    result = R_FAILED(result) ? result : (Result)0xD8A0A04Au;
                    PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
                    goto done;
                }
            }
            else
            {
                PLUGIN_htps_TlsMemcpy((u8 *)output + totalWritten, buffer, (u32)received);
            }
            totalWritten += (u32)received;
        }
    }

    ok = totalWritten == contentSize;
    if (ok && actualSize)
        *actualSize = contentSize;
    result = ok ? 0 : (Result)0xD8A0A049u;

done:
    if (fileOpen)
        HTTPS_HOST__FSFILE_Close(file);
    if (archiveOpen)
    {
        if (!ok)
            (void)HTTPS_HOST__FSUSER_DeleteFile(
                archive,
                HTTPS_HOST__fsMakePath(PATH_ASCII, path));
        HTTPS_HOST__FSUSER_CloseArchive(archive);
    }
    if (tls)
        PLUGIN_htps_TlsClose(tls);
    if (tcpSocket >= 0)
        (void)HTTPS_HOST__socClose(tcpSocket);
    if (socReady)
        (void)HTTPS_HOST__miniSocExit();
    if (bufferReady)
    {
        u32 out;
        (void)HTTPS_HOST__svcControlMemoryUnsafe(
            &out,
            bufferBase,
            HTTPS_DOWNLOAD_CHUNK_SIZE,
            MEMOP_FREE | MEMOP_REGION_SYSTEM,
            MEMPERM_DONTCARE);
    }
    if (heapReady)
        PLUGIN_htps_TlsHeapDestroy();
    return ok ? 0 : result;
}

PLUGIN_CODE(htps) static Result PLUGIN_htps_CreateDirectory(const char *p);
PLUGIN_RODATA(htps) static const char g_htpsCreateDirectoryCommand[] = "3nx:mkdir";

PLUGIN_CODE(htps) static Result PLUGIN_htps_OnlineDownloadToFile(
    const char *url,
    const char *path,
    u32 maxSize
)
{
    if (url && path && maxSize == 0u &&
        PLUGIN_htps_TlsStrcmp(url, g_htpsCreateDirectoryCommand) == 0)
        return PLUGIN_htps_CreateDirectory(path);
    return PLUGIN_htps_OnlineDownload(url, path, NULL, maxSize, NULL);
}

PLUGIN_CODE(htps) static Result PLUGIN_htps_OnlineDownloadToMemory(
    const char *url,
    void *buffer,
    u32 bufferSize,
    u32 *actualSize
)
{
    return PLUGIN_htps_OnlineDownload(url, NULL, buffer, bufferSize, actualSize);
}



PLUGIN_RODATA(htps) static const char g_htpsFetchPath[] = "/luma/plugins/sysplgfetch.txt";
PLUGIN_RODATA(htps) static const char g_htpsOnlinePath[] = "/luma/plugins/onlinetemporary.3on";
PLUGIN_RODATA(htps) static const char g_htpsChooserTitle[] = "Open Online Menu";
PLUGIN_RODATA(htps) static const char g_htpsOnlineTitle[] = "Online Menu";
PLUGIN_RODATA(htps) static const char g_htpsFetchError[] = "Could not read online sources";
PLUGIN_RODATA(htps) static const char g_htpsOnlineError[] = "Could not load Online Menu";
PLUGIN_RODATA(htps) static const char g_htpsWaiting[] = "Waiting...";
PLUGIN_RODATA(htps) static const char g_htpsBack[] = "B: go back";
PLUGIN_RODATA(htps) static const char g_htpsConfirmWarning[] = "Warning:";
PLUGIN_RODATA(htps) static const char g_htpsConfirmBody1[] = "Sysplugins contain native executable code";
PLUGIN_RODATA(htps) static const char g_htpsConfirmBody2[] = "with extensive system access. A malicious";
PLUGIN_RODATA(htps) static const char g_htpsConfirmBody3[] = "source could damage your system or replace";
PLUGIN_RODATA(htps) static const char g_htpsConfirmBody4[] = "critical files.";
PLUGIN_RODATA(htps) static const char g_htpsConfirmTrust[] = "Only add sources you trust.";
PLUGIN_RODATA(htps) static const char g_htpsConfirmA[] = "Press X to connect";
PLUGIN_RODATA(htps) static const char g_htpsConfirmB[] = "Press B to go back";
PLUGIN_RODATA(htps) static const char g_htpsConfirmSource[] = "Source:";
PLUGIN_RODATA(htps) static const char g_htpsDots[] = "...";
PLUGIN_RODATA(htps) static const char g_htpsSelect[] = ">>";
PLUGIN_RODATA(htps) static const char g_htpsSelectedRight[] = "<<        ";
PLUGIN_RODATA(htps) static const char g_htpsUnselected[] = " *";
PLUGIN_RODATA(htps) static const char g_htpsClearRow[] = "                                                  ";
PLUGIN_RODATA(htps) static const char g_htpsChooserRail[] = "-----------------------------------";
PLUGIN_RODATA(htps) static const char g_htpsOnlineRail[] = "----------------------";
PLUGIN_RODATA(htps) static const char g_htpsPlus[] = "+";
PLUGIN_RODATA(htps) static const char g_htpsPipe[] = "|";
PLUGIN_RODATA(htps) static const char g_htpsStageDownload[] = "HTTPS download";
PLUGIN_RODATA(htps) static const char g_htpsStageOpen[] = "open online 3on";
PLUGIN_RODATA(htps) static const char g_htpsStageValidate[] = "validate online 3on";
PLUGIN_RODATA(htps) static const char g_htpsStageMemory[] = "map online 3on";
PLUGIN_RODATA(htps) static const char g_htpsStageReloc[] = "relocate online 3on";
PLUGIN_RODATA(htps) static const char g_htpsStageProtect[] = "protect online 3on";
PLUGIN_RODATA(htps) static const char g_htpsHex[] = "0123456789ABCDEF";
PLUGIN_BSS(htps) static char g_htpsFetchConfig[HTPS_FETCH_CONFIG_MAX];
PLUGIN_BSS(htps) static HtpsFetchSource g_htpsFetchSources[HTPS_FETCH_MAX_SOURCES];
PLUGIN_BSS(htps) static char g_htpsFetchBase[HTPS_FETCH_BASE_MAX];
PLUGIN_BSS(htps) static char g_htpsOnlineUrl[HTPS_FETCH_BASE_MAX];
PLUGIN_BSS(htps) static const char *g_htpsLastStage;
PLUGIN_BSS(htps) static Result g_htpsLastResult;

PLUGIN_CODE(htps) static void PLUGIN_htps_SetOnlineFailure(const char *stage, Result result)
{ g_htpsLastStage=stage; g_htpsLastResult=result; }

PLUGIN_CODE(htps) static bool PLUGIN_htps_ReadExact(Handle file,u32 offset,void *buffer,u32 size)
{ u32 read=0; return R_SUCCEEDED(HTTPS_HOST__FSFILE_Read(file,&read,offset,buffer,size))&&read==size; }
PLUGIN_CODE(htps) static bool PLUGIN_htps_Add32(u32 a,u32 b,u32 *out){u32 v=a+b;if(v<a)return false;*out=v;return true;}
PLUGIN_CODE(htps) static bool PLUGIN_htps_AlignPage(u32 value,u32 *out){if(value>0xFFFFF000u)return false;*out=(value+0xFFFu)&~0xFFFu;return true;}

PLUGIN_CODE(htps) static bool PLUGIN_htps_LoadOnlineTransient(HtpsTransientImage *image)
{
    FS_Archive archive=0; Handle file=0; HtpsTransientHeader h; u64 size64=0;
    u32 codeOffset,dataOffset,end,codeSize,dataSize,totalSize,base=0,allocated=0; bool memory=false; Result r=0;
    if (!image) return false;
    image->base=image->codeSize=image->totalSize=0;
    r=HTTPS_HOST__FSUSER_OpenArchive(&archive,ARCHIVE_SDMC,HTTPS_HOST__fsMakePath(PATH_EMPTY,g_httpsOnlineEmptyPath));
    if(R_FAILED(r)){PLUGIN_htps_SetOnlineFailure(g_htpsStageOpen,r);return false;}
    r=HTTPS_HOST__FSUSER_OpenFile(&file,archive,HTTPS_HOST__fsMakePath(PATH_ASCII,g_htpsOnlinePath),FS_OPEN_READ,0);
    if(R_FAILED(r)){PLUGIN_htps_SetOnlineFailure(g_htpsStageOpen,r);goto fail;}
    if(R_FAILED(HTTPS_HOST__FSFILE_GetSize(file,&size64))||size64<HTPS_TRANSIENT_HEADER_SIZE||size64>HTPS_TRANSIENT_MAX_FILE_SIZE||
       !PLUGIN_htps_ReadExact(file,0,&h,sizeof(h))||h.magic!=HTPS_TRANSIENT_MAGIC||h.plgid!=HTPS_ONLINE_ID||!h.codeSize||
       !PLUGIN_htps_Add32(HTPS_TRANSIENT_HEADER_SIZE,h.fastRelocSize,&codeOffset)||!PLUGIN_htps_Add32(codeOffset,h.codeSize,&dataOffset)||
       !PLUGIN_htps_Add32(dataOffset,h.dataSize,&end)||!PLUGIN_htps_Add32(end,h.repairSize,&end)||end>(u32)size64||
       !PLUGIN_htps_AlignPage(h.codeSize,&codeSize)||!PLUGIN_htps_Add32(h.dataSize,h.bssSize,&dataSize)||!PLUGIN_htps_AlignPage(dataSize,&dataSize)||
       !PLUGIN_htps_Add32(codeSize,dataSize,&totalSize)||!totalSize||totalSize>HTPS_TRANSIENT_MAX_RUNTIME_SIZE)
    {PLUGIN_htps_SetOnlineFailure(g_htpsStageValidate,(Result)0xD8A0A046u);goto fail;}
    for(u32 scan=HTTPS_LOW;scan<HTTPS_HIGH;){MemInfo info;PageInfo page;if(R_FAILED(HTTPS_HOST__svcQueryMemory(&info,&page,scan)))goto no_memory;
      if(info.state==MEMSTATE_FREE){u32 b=info.base_addr<HTTPS_LOW?HTTPS_LOW:info.base_addr;if(b<=0xFFFFFFFFu-totalSize&&b+totalSize<=HTTPS_HIGH&&b+totalSize<=info.base_addr+info.size){base=b;break;}}
      u32 next=info.base_addr+info.size;if(next<=scan)break;scan=next;}
    if(!base)goto no_memory;
    r=HTTPS_HOST__svcControlMemoryUnsafe(&allocated,base,totalSize,MEMOP_ALLOC|MEMOP_REGION_SYSTEM,MEMPERM_READWRITE);if(R_FAILED(r)||!allocated)goto no_memory;
    base=allocated;memory=true;
    if(!PLUGIN_htps_ReadExact(file,codeOffset,(void*)base,h.codeSize)||(h.dataSize&&!PLUGIN_htps_ReadExact(file,dataOffset,(void*)(base+codeSize),h.dataSize)))
    {PLUGIN_htps_SetOnlineFailure(g_htpsStageValidate,(Result)0xD8A0A048u);goto fail;}
    for(u32 i=0;i<h.bssSize;i++)*(volatile u8*)(base+codeSize+h.dataSize+i)=0;
    {u32 group[2];if(h.fastRelocSize<8u||!PLUGIN_htps_ReadExact(file,HTPS_TRANSIENT_HEADER_SIZE,group,8u)||group[0]!=HTPS_ONLINE_ID||group[1]!=(h.fastRelocSize-8u)/8u||8u+group[1]*8u!=h.fastRelocSize)
      {PLUGIN_htps_SetOnlineFailure(g_htpsStageReloc,(Result)0xD8A0A049u);goto fail;}
      u32 pos=HTPS_TRANSIENT_HEADER_SIZE+8u;for(u32 i=0;i<group[1];i++,pos+=8u){u32 pair[2];if(!PLUGIN_htps_ReadExact(file,pos,pair,8u)||(pair[0]&3u)||pair[0]>totalSize-4u||pair[1]>=totalSize)
        {PLUGIN_htps_SetOnlineFailure(g_htpsStageReloc,(Result)0xD8A0A049u);goto fail;}*(volatile u32*)(base+pair[0])=base+pair[1];}}
    HTTPS_HOST__FSFILE_Close(file);file=0;HTTPS_HOST__FSUSER_CloseArchive(archive);archive=0;HTTPS_HOST__svcFlushEntireDataCache();
    r=HTTPS_HOST__protectMemory(base,codeSize,MEMPERM_READEXECUTE);if(R_FAILED(r)){PLUGIN_htps_SetOnlineFailure(g_htpsStageProtect,r);goto fail;}
    HTTPS_HOST__svcInvalidateEntireInstructionCache();image->base=base;image->codeSize=codeSize;image->totalSize=totalSize;return true;
no_memory: PLUGIN_htps_SetOnlineFailure(g_htpsStageMemory,R_FAILED(r)?r:(Result)0xD8A0A047u);
fail: if(file)HTTPS_HOST__FSFILE_Close(file);if(archive)HTTPS_HOST__FSUSER_CloseArchive(archive);if(memory){u32 out;(void)HTTPS_HOST__svcControlMemoryUnsafe(&out,base,totalSize,MEMOP_FREE|MEMOP_REGION_SYSTEM,MEMPERM_DONTCARE);}return false;
}
PLUGIN_CODE(htps) static void PLUGIN_htps_FreeOnlineTransient(HtpsTransientImage *image)
{if(!image||!image->base)return;(void)HTTPS_HOST__protectMemory(image->base,image->codeSize,MEMPERM_READWRITE);u32 out;(void)HTTPS_HOST__svcControlMemoryUnsafe(&out,image->base,image->totalSize,MEMOP_FREE|MEMOP_REGION_SYSTEM,MEMPERM_DONTCARE);image->base=0;}

PLUGIN_CODE(htps) static char *PLUGIN_htps_NextFetchLine(char **cursor)
{if(!cursor||!*cursor)return NULL;for(;;){while(**cursor=='\r'||**cursor=='\n')(*cursor)++;if(!**cursor)return NULL;char*line=*cursor;while(**cursor&&**cursor!='\r'&&**cursor!='\n')(*cursor)++;if(**cursor)*(*cursor)++=0;while(*line==' '||*line=='\t')line++;char*end=line;while(*end)end++;while(end>line&&(end[-1]==' '||end[-1]=='\t'))*--end=0;if(*line)return line;}}
PLUGIN_CODE(htps) static bool PLUGIN_htps_LoadFetchSources(u32 *countOut)
{FS_Archive a=0;Handle f=0;u64 n=0;u32 count=0;bool ok=false;if(!countOut)return false;if(R_FAILED(HTTPS_HOST__FSUSER_OpenArchive(&a,ARCHIVE_SDMC,HTTPS_HOST__fsMakePath(PATH_EMPTY,g_httpsOnlineEmptyPath))))return false;
 if(R_FAILED(HTTPS_HOST__FSUSER_OpenFile(&f,a,HTTPS_HOST__fsMakePath(PATH_ASCII,g_htpsFetchPath),FS_OPEN_READ,0)))goto done;
 if(R_FAILED(HTTPS_HOST__FSFILE_GetSize(f,&n))||n>=sizeof(g_htpsFetchConfig)||(n&&!PLUGIN_htps_ReadExact(f,0,g_htpsFetchConfig,(u32)n))) goto done;
 g_htpsFetchConfig[(u32)n]=0;
 char*cur=g_htpsFetchConfig;while(count<HTPS_FETCH_MAX_SOURCES){char*t=PLUGIN_htps_NextFetchLine(&cur);if(!t)break;char*u=PLUGIN_htps_NextFetchLine(&cur);if(!u)goto done;g_htpsFetchSources[count].title=t;g_htpsFetchSources[count].url=u;count++;}
 if(PLUGIN_htps_NextFetchLine(&cur)) goto done;
 ok=count!=0; if(ok)*countOut=count;
done:if(f)HTTPS_HOST__FSFILE_Close(f);if(a)HTTPS_HOST__FSUSER_CloseArchive(a);return ok;}
PLUGIN_CODE(htps) static bool PLUGIN_htps_SetSourceUrl(const char *url)
{
    u32 n=0,lastSlash=0xFFFFFFFFu;
    if(!url)return false;
    while(url[n])
    {
        if(n+1u>=sizeof(g_htpsOnlineUrl))return false;
        g_htpsOnlineUrl[n]=url[n];
        if(url[n]=='/')lastSlash=n;
        n++;
    }
    if(!n||lastSlash==0xFFFFFFFFu||lastSlash+1u>=n||lastSlash+2u>sizeof(g_htpsFetchBase))return false;
    g_htpsOnlineUrl[n]=0;
    for(u32 i=0;i<=lastSlash;i++)((volatile char *)g_htpsFetchBase)[i]=((volatile const char *)g_htpsOnlineUrl)[i];
    ((volatile char *)g_htpsFetchBase)[lastSlash+1u]=0;
    return true;
}
PLUGIN_CODE(htps) static void PLUGIN_htps_DrawConfirmSource(const char *url)
{
    char line[48];
    u32 pos = 0, y = 170;

    HTTPS_HOST__Draw_Lock();
    HTTPS_HOST__Draw_Clear();
    HTTPS_HOST__Draw_String(20, 15, 0xF800u, g_htpsConfirmWarning);
    HTTPS_HOST__Draw_String(20, 35, 0xFFFFu, g_htpsConfirmBody1);
    HTTPS_HOST__Draw_String(20, 46, 0xFFFFu, g_htpsConfirmBody2);
    HTTPS_HOST__Draw_String(20, 57, 0xFFFFu, g_htpsConfirmBody3);
    HTTPS_HOST__Draw_String(20, 68, 0xFFFFu, g_htpsConfirmBody4);
    HTTPS_HOST__Draw_String(20, 90, 0xFFFFu, g_htpsConfirmTrust);
    HTTPS_HOST__Draw_String(20, 115, 0xFC00u, g_htpsConfirmA);
    HTTPS_HOST__Draw_String(20, 130, 0x7BEFu, g_htpsConfirmB);
    HTTPS_HOST__Draw_String(20, 155, 0xFFFFu, g_htpsConfirmSource);

    while (url && url[pos] && y <= 225u)
    {
        u32 n = 0;
        while (n < 47u && url[pos + n])
        {
            line[n] = url[pos + n];
            n++;
        }
        line[n] = 0;
        HTTPS_HOST__Draw_String(20, y, 0x87FFu, line);
        pos += n;
        y += 11u;
    }

    HTTPS_HOST__Draw_Flush();
    HTTPS_HOST__Draw_Unlock();
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_ConfirmSource(void)
{
    PLUGIN_htps_DrawConfirmSource(g_htpsFetchBase);
    while (!*HTTPS_HOST__menuShouldExit)
    {
        u32 pressed = HTTPS_HOST__waitInputWithTimeout(-1);
        if (pressed & KEY_X) return true;
        if (pressed & KEY_B) return false;
    }
    return false;
}

PLUGIN_CODE(htps) static void PLUGIN_htps_DrawChooserFrame(void)
{
    HTTPS_HOST__Draw_String(10, 8, 0x127Fu, g_htpsPlus);
    HTTPS_HOST__Draw_String(16, 8, 0x127Fu, g_htpsChooserRail);
    HTTPS_HOST__Draw_String(222, 8, 0x127Fu, g_htpsPlus);
    HTTPS_HOST__Draw_String(10, 16, 0x127Fu, g_htpsPipe);
    HTTPS_HOST__Draw_String(222, 16, 0x127Fu, g_htpsPipe);
    HTTPS_HOST__Draw_String(10, 24, 0x127Fu, g_htpsPlus);
    HTTPS_HOST__Draw_String(16, 24, 0x127Fu, g_htpsChooserRail);
    HTTPS_HOST__Draw_String(222, 24, 0x127Fu, g_htpsPlus);
    HTTPS_HOST__Draw_String(20, 16, 0xFFFFu, g_htpsChooserTitle);
}

PLUGIN_CODE(htps) static void PLUGIN_htps_DrawChooserItem(u32 y, bool selected, const char *title)
{
    if (selected)
    {
        HTTPS_HOST__Draw_String(15, y, 0xFC00u, g_htpsSelect);
        HTTPS_HOST__Draw_String(35, y, 0x07FFu, title);
        HTTPS_HOST__Draw_String(250, y, 0xFC00u, g_htpsSelectedRight);
    }
    else
    {
        HTTPS_HOST__Draw_String(15, y, 0x7BEFu, g_htpsUnselected);
        HTTPS_HOST__Draw_String(35, y, 0xFFFFu, title);
    }
}

PLUGIN_CODE(htps) static void PLUGIN_htps_DrawChooser(u32 first, u32 selected, u32 count)
{
    u32 shown = count - first;
    if (shown > HTPS_VISIBLE_ITEMS) shown = HTPS_VISIBLE_ITEMS;

    HTTPS_HOST__Draw_Lock();
    HTTPS_HOST__Draw_Clear();
    PLUGIN_htps_DrawChooserFrame();
    if (first) HTTPS_HOST__Draw_String(35, 34, 0x7BEFu, g_htpsDots);
    for (u32 i = 0; i < shown; i++)
        PLUGIN_htps_DrawChooserItem(45u + i * 11u, first + i == selected, g_htpsFetchSources[first + i].title);
    if (first + shown < count)
        HTTPS_HOST__Draw_String(35, 45u + HTPS_VISIBLE_ITEMS * 11u, 0x7BEFu, g_htpsDots);
    HTTPS_HOST__Draw_Flush();
    HTTPS_HOST__Draw_Unlock();
}

PLUGIN_CODE(htps) static void PLUGIN_htps_RedrawChooserSelection(u32 first, u32 oldSelected, u32 selected)
{
    u32 oldY = 45u + (oldSelected - first) * 11u;
    u32 newY = 45u + (selected - first) * 11u;
    HTTPS_HOST__Draw_Lock();
    HTTPS_HOST__Draw_String(10, oldY, 0x0000u, g_htpsClearRow);
    HTTPS_HOST__Draw_String(10, newY, 0x0000u, g_htpsClearRow);
    PLUGIN_htps_DrawChooserItem(oldY, false, g_htpsFetchSources[oldSelected].title);
    PLUGIN_htps_DrawChooserItem(newY, true, g_htpsFetchSources[selected].title);
    HTTPS_HOST__Draw_Flush();
    HTTPS_HOST__Draw_Unlock();
}

PLUGIN_CODE(htps) static void PLUGIN_htps_DrawFetchError(void)
{
    HTTPS_HOST__Draw_Lock();
    HTTPS_HOST__Draw_Clear();
    PLUGIN_htps_DrawChooserFrame();
    HTTPS_HOST__Draw_String(35, 45, 0xF800u, g_htpsFetchError);
    HTTPS_HOST__Draw_Flush();
    HTTPS_HOST__Draw_Unlock();
    while (!*HTTPS_HOST__menuShouldExit)
        if (HTTPS_HOST__waitInputWithTimeout(-1) & KEY_B) break;
}

PLUGIN_CODE(htps) static void PLUGIN_htps_DrawOnlineFrame(void)
{
    HTTPS_HOST__Draw_String(10, 8, 0x435Cu, g_htpsPlus);
    HTTPS_HOST__Draw_String(16, 8, 0x435Cu, g_htpsOnlineRail);
    HTTPS_HOST__Draw_String(148, 8, 0x435Cu, g_htpsPlus);
    HTTPS_HOST__Draw_String(10, 16, 0x435Cu, g_htpsPipe);
    HTTPS_HOST__Draw_String(148, 16, 0x435Cu, g_htpsPipe);
    HTTPS_HOST__Draw_String(10, 24, 0x435Cu, g_htpsPlus);
    HTTPS_HOST__Draw_String(16, 24, 0x435Cu, g_htpsOnlineRail);
    HTTPS_HOST__Draw_String(148, 24, 0x435Cu, g_htpsPlus);
    HTTPS_HOST__Draw_String(20, 16, 0x07FFu, g_htpsOnlineTitle);
}

PLUGIN_CODE(htps) static void PLUGIN_htps_DrawOnlineStatus(const char *text, bool error)
{
    HTTPS_HOST__Draw_Lock();
    HTTPS_HOST__Draw_Clear();
    PLUGIN_htps_DrawOnlineFrame();
    HTTPS_HOST__Draw_String(20, error ? 45u : 55u, error ? 0xF800u : 0xFFFFu, text);
    if (error && g_htpsLastStage)
    {
        char hex[11];
        hex[0] = '0'; hex[1] = 'x';
        PLUGIN_htps_TlsMemset(hex + 2, '0', 8u); hex[10] = 0;
        u32 v = (u32)g_htpsLastResult;
        for (u32 i = 0; i < 8u; i++) hex[2u + i] = g_htpsHex[(v >> ((7u - i) * 4u)) & 0xFu];
        HTTPS_HOST__Draw_String(20, 65, 0xFFFFu, g_htpsLastStage);
        HTTPS_HOST__Draw_String(20, 85, 0xFFFFu, hex);
        HTTPS_HOST__Draw_String(20, 120, 0x7BEFu, g_htpsBack);
    }
    HTTPS_HOST__Draw_Flush();
    HTTPS_HOST__Draw_Unlock();
    if (error)
        while (!*HTTPS_HOST__menuShouldExit)
            if (HTTPS_HOST__waitInputWithTimeout(-1) & KEY_B) break;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_ValidFsPath(const char *path){if(!path||path[0]!='/')return false;for(u32 i=1;i<=HTPS_FS_MAX_PATH;i++)if(!path[i])return true;return false;}
PLUGIN_CODE(htps) static Result PLUGIN_htps_OpenSd(FS_Archive *a){if(!a)return HTPS_FS_BAD_ARG;*a=0;return HTTPS_HOST__FSUSER_OpenArchive(a,ARCHIVE_SDMC,HTTPS_HOST__fsMakePath(PATH_EMPTY,g_httpsOnlineEmptyPath));}
PLUGIN_CODE(htps) static Result PLUGIN_htps_GetFileSize(const char *p,u32 *size){FS_Archive a=0;Handle f=0;u64 n=0;Result r;if(!size||!PLUGIN_htps_ValidFsPath(p))return HTPS_FS_BAD_ARG;*size=0;r=PLUGIN_htps_OpenSd(&a);if(R_FAILED(r))return r;r=HTTPS_HOST__FSUSER_OpenFile(&f,a,HTTPS_HOST__fsMakePath(PATH_ASCII,p),FS_OPEN_READ,0);if(R_SUCCEEDED(r)){r=HTTPS_HOST__FSFILE_GetSize(f,&n);HTTPS_HOST__FSFILE_Close(f);if(R_SUCCEEDED(r)){if(n>0xFFFFFFFFu)r=HTPS_FS_TOO_LARGE;else *size=(u32)n;}}HTTPS_HOST__FSUSER_CloseArchive(a);return r;}
PLUGIN_CODE(htps) static Result PLUGIN_htps_ReadFile(const char*p,u32 off,void*b,u32 sz,u32*actual){FS_Archive a=0;Handle f=0;Result r;if(!actual||(!b&&sz)||!PLUGIN_htps_ValidFsPath(p)||sz>0xFFFFFFFFu-off)return HTPS_FS_BAD_ARG;*actual=0;if(!sz)return 0;r=PLUGIN_htps_OpenSd(&a);if(R_FAILED(r))return r;r=HTTPS_HOST__FSUSER_OpenFile(&f,a,HTTPS_HOST__fsMakePath(PATH_ASCII,p),FS_OPEN_READ,0);if(R_SUCCEEDED(r)){r=HTTPS_HOST__FSFILE_Read(f,actual,off,b,sz);HTTPS_HOST__FSFILE_Close(f);}HTTPS_HOST__FSUSER_CloseArchive(a);return r;}
PLUGIN_CODE(htps) static Result PLUGIN_htps_WriteFile(const char*p,u32 off,const void*b,u32 sz,u32*actual){FS_Archive a=0;Handle f=0;Result r;if(!actual||(!b&&sz)||!PLUGIN_htps_ValidFsPath(p)||sz>0xFFFFFFFFu-off)return HTPS_FS_BAD_ARG;*actual=0;if(!sz)return 0;r=PLUGIN_htps_OpenSd(&a);if(R_FAILED(r))return r;r=HTTPS_HOST__FSUSER_OpenFile(&f,a,HTTPS_HOST__fsMakePath(PATH_ASCII,p),FS_OPEN_WRITE|FS_OPEN_CREATE,0);if(R_SUCCEEDED(r)){r=HTTPS_HOST__FSFILE_Write(f,actual,off,b,sz,FS_WRITE_FLUSH);HTTPS_HOST__FSFILE_Close(f);}HTTPS_HOST__FSUSER_CloseArchive(a);return r;}
PLUGIN_CODE(htps) static Result PLUGIN_htps_SetFileSize(const char*p,u32 sz){FS_Archive a=0;Handle f=0;Result r;if(!PLUGIN_htps_ValidFsPath(p))return HTPS_FS_BAD_ARG;r=PLUGIN_htps_OpenSd(&a);if(R_FAILED(r))return r;r=HTTPS_HOST__FSUSER_OpenFile(&f,a,HTTPS_HOST__fsMakePath(PATH_ASCII,p),FS_OPEN_WRITE|FS_OPEN_CREATE,0);if(R_SUCCEEDED(r)){r=HTTPS_HOST__FSFILE_SetSize(f,sz);HTTPS_HOST__FSFILE_Close(f);}HTTPS_HOST__FSUSER_CloseArchive(a);return r;}
PLUGIN_CODE(htps) static Result PLUGIN_htps_DeleteFile(const char*p){FS_Archive a=0;Result r;if(!PLUGIN_htps_ValidFsPath(p))return HTPS_FS_BAD_ARG;r=PLUGIN_htps_OpenSd(&a);if(R_FAILED(r))return r;r=HTTPS_HOST__FSUSER_DeleteFile(a,HTTPS_HOST__fsMakePath(PATH_ASCII,p));HTTPS_HOST__FSUSER_CloseArchive(a);return r;}
PLUGIN_CODE(htps) static Result PLUGIN_htps_RenameFile(const char*o,const char*n){FS_Archive a=0;Result r;if(!PLUGIN_htps_ValidFsPath(o)||!PLUGIN_htps_ValidFsPath(n))return HTPS_FS_BAD_ARG;r=PLUGIN_htps_OpenSd(&a);if(R_FAILED(r))return r;r=HTTPS_HOST__FSUSER_RenameFile(a,HTTPS_HOST__fsMakePath(PATH_ASCII,o),a,HTTPS_HOST__fsMakePath(PATH_ASCII,n));HTTPS_HOST__FSUSER_CloseArchive(a);return r;}
PLUGIN_CODE(htps) static Result PLUGIN_htps_CreateDirectory(const char*p){FS_Archive a=0;Handle d=0;Result r;if(!PLUGIN_htps_ValidFsPath(p))return HTPS_FS_BAD_ARG;r=PLUGIN_htps_OpenSd(&a);if(R_FAILED(r))return r;r=HTTPS_HOST__FSUSER_CreateDirectory(a,HTTPS_HOST__fsMakePath(PATH_ASCII,p),0);if(R_FAILED(r)){Result openResult=HTTPS_HOST__FSUSER_OpenDirectory(&d,a,HTTPS_HOST__fsMakePath(PATH_ASCII,p));if(R_SUCCEEDED(openResult)){HTTPS_HOST__FSDIR_Close(d);r=0;}}{Result c=HTTPS_HOST__FSUSER_CloseArchive(a);if(R_SUCCEEDED(r)&&R_FAILED(c))r=c;}return r;}
PLUGIN_CODE(htps) static Result PLUGIN_htps_FileExists(const char*p,bool*exists){FS_Archive a=0;Handle f=0;Result r;if(!exists||!PLUGIN_htps_ValidFsPath(p))return HTPS_FS_BAD_ARG;*exists=false;r=PLUGIN_htps_OpenSd(&a);if(R_FAILED(r))return r;r=HTTPS_HOST__FSUSER_OpenFile(&f,a,HTTPS_HOST__fsMakePath(PATH_ASCII,p),FS_OPEN_READ,0);if(R_SUCCEEDED(r)){*exists=true;HTTPS_HOST__FSFILE_Close(f);r=0;}else if(R_DESCRIPTION(r)==RD_NOT_FOUND)r=0;HTTPS_HOST__FSUSER_CloseArchive(a);return r;}
PLUGIN_CODE(htps) static bool PLUGIN_htps_AppendUtf8(char*out,u32 cap,u32*len,u32 cp){u8 b[4];u32 n;if(cp<=0x7F){b[0]=(u8)cp;n=1;}else if(cp<=0x7FF){b[0]=0xC0|(cp>>6);b[1]=0x80|(cp&0x3F);n=2;}else if(cp<=0xFFFF){b[0]=0xE0|(cp>>12);b[1]=0x80|((cp>>6)&0x3F);b[2]=0x80|(cp&0x3F);n=3;}else{b[0]=0xF0|(cp>>18);b[1]=0x80|((cp>>12)&0x3F);b[2]=0x80|((cp>>6)&0x3F);b[3]=0x80|(cp&0x3F);n=4;}if(!out||!len||!cap||*len+n>=cap)return false;for(u32 i=0;i<n;i++)out[(*len)++]=(char)b[i];out[*len]=0;return true;}
PLUGIN_CODE(htps) static bool PLUGIN_htps_DirNameToUtf8(char*out,u32 cap,const u16*in){u32 len=0;if(!out||!cap||!in)return false;out[0]=0;for(u32 i=0;i<0x106u;i++){u32 cp=in[i];if(!cp)return true;if(cp>=0xD800u&&cp<=0xDBFFu){u32 low=i+1u<0x106u?in[i+1u]:0;if(low<0xDC00u||low>0xDFFFu)cp=0xFFFDu;else{cp=0x10000u+((cp-0xD800u)<<10)+(low-0xDC00u);i++;}}else if(cp>=0xDC00u&&cp<=0xDFFFu)cp=0xFFFDu;if(!PLUGIN_htps_AppendUtf8(out,cap,&len,cp))return false;}return false;}
PLUGIN_CODE(htps) static Result PLUGIN_htps_EnumerateDirectory(const char*p,HtpsOnlineDirVisitor visitor,void*ctx,u32*visited){FS_Archive a=0;Handle d=0;FS_DirectoryEntry raw;HtpsOnlineDirEntry e;Result r;bool open=false;if(!visitor||!PLUGIN_htps_ValidFsPath(p))return HTPS_FS_BAD_ARG;if(visited)*visited=0;r=PLUGIN_htps_OpenSd(&a);if(R_FAILED(r))return r;r=HTTPS_HOST__FSUSER_OpenDirectory(&d,a,HTTPS_HOST__fsMakePath(PATH_ASCII,p));if(R_FAILED(r))goto done;open=true;for(;;){u32 count=0;bool stop=false;r=HTTPS_HOST__FSDIR_Read(d,&count,1,&raw);if(R_FAILED(r)||!count)break;e.fileSize=raw.fileSize;e.attributes=raw.attributes;e.flags=0;if(PLUGIN_htps_DirNameToUtf8(e.name,sizeof(e.name),raw.name))e.flags|=HTPS_DIR_NAME_COMPLETE;if(visited)(*visited)++;r=visitor(&e,ctx,&stop);if(R_FAILED(r)||stop)break;}done:if(open){Result c=HTTPS_HOST__FSDIR_Close(d);if(R_SUCCEEDED(r)&&R_FAILED(c))r=c;}if(a){Result c=HTTPS_HOST__FSUSER_CloseArchive(a);if(R_SUCCEEDED(r)&&R_FAILED(c))r=c;}return r;}

PLUGIN_CODE(htps) static bool PLUGIN_htps_RunOnlineSource(void)
{HtpsTransientImage image;PLUGIN_htps_DrawOnlineStatus(g_htpsWaiting,false);Result r=PLUGIN_htps_OnlineDownloadToFile(g_htpsOnlineUrl,g_htpsOnlinePath,HTPS_TRANSIENT_MAX_FILE_SIZE);if(R_FAILED(r)){PLUGIN_htps_SetOnlineFailure(g_htpsStageDownload,r);goto fail;}if(!PLUGIN_htps_LoadOnlineTransient(&image))goto fail_delete;MENUOnlineApi api;api.version=HTPS_ONLINE_API_VERSION;api.drawLock=HTTPS_HOST__Draw_Lock;api.drawUnlock=HTTPS_HOST__Draw_Unlock;api.drawClear=HTTPS_HOST__Draw_Clear;api.drawString=HTTPS_HOST__Draw_String;api.drawFlush=HTTPS_HOST__Draw_Flush;api.waitInputWithTimeout=HTTPS_HOST__waitInputWithTimeout;api.menuShouldExit=HTTPS_HOST__menuShouldExit;api.downloadToFile=PLUGIN_htps_OnlineDownloadToFile;api.downloadToMemory=PLUGIN_htps_OnlineDownloadToMemory;api.getFileSize=PLUGIN_htps_GetFileSize;api.readFile=PLUGIN_htps_ReadFile;api.writeFile=PLUGIN_htps_WriteFile;api.setFileSize=PLUGIN_htps_SetFileSize;api.deleteFile=PLUGIN_htps_DeleteFile;api.renameFile=PLUGIN_htps_RenameFile;api.fileExists=PLUGIN_htps_FileExists;api.enumerateDirectory=PLUGIN_htps_EnumerateDirectory;api.sourceUrlPrefix=g_htpsFetchBase;((void(*)(const MENUOnlineApi*))image.base)(&api);PLUGIN_htps_FreeOnlineTransient(&image);
fail_delete:{FS_Archive a=0;if(R_SUCCEEDED(HTTPS_HOST__FSUSER_OpenArchive(&a,ARCHIVE_SDMC,HTTPS_HOST__fsMakePath(PATH_EMPTY,g_httpsOnlineEmptyPath)))){(void)HTTPS_HOST__FSUSER_DeleteFile(a,HTTPS_HOST__fsMakePath(PATH_ASCII,g_htpsOnlinePath));HTTPS_HOST__FSUSER_CloseArchive(a);}}return true;
fail:PLUGIN_htps_DrawOnlineStatus(g_htpsOnlineError,true);return false;}
PLUGIN_CODE(htps) static void PLUGIN_htps_OpenOnlineSource(const char *url)
{
    if (!PLUGIN_htps_SetSourceUrl(url))
        return;
    if (PLUGIN_htps_ConfirmSource())
        (void)PLUGIN_htps_RunOnlineSource();
}

PLUGIN_CODE(htps) static void PLUGIN_htps_OpenOnlineMenu(void)
{
    u32 count = 0, selected = 0, first = 0;
    bool redraw = true;

    if (!PLUGIN_htps_LoadFetchSources(&count) || !count)
    {
        PLUGIN_htps_DrawFetchError();
        return;
    }

    while (!*HTTPS_HOST__menuShouldExit)
    {
        if (count <= HTPS_VISIBLE_ITEMS) first = 0;
        else
        {
            if (first > selected) first = selected;
            if (selected >= first + HTPS_VISIBLE_ITEMS) first = selected - HTPS_VISIBLE_ITEMS + 1u;
            if (first > count - HTPS_VISIBLE_ITEMS) first = count - HTPS_VISIBLE_ITEMS;
        }

        if (redraw)
        {
            PLUGIN_htps_DrawChooser(first, selected, count);
            redraw = false;
        }

        u32 pressed = HTTPS_HOST__waitInputWithTimeout(-1);
        if (pressed & KEY_B) return;
        if (pressed & KEY_DOWN)
        {
            u32 oldSelected = selected, oldFirst = first;
            if (selected + 1u >= count) { selected = 0; first = 0; }
            else { selected++; if (selected >= first + HTPS_VISIBLE_ITEMS) first++; }
            if (first != oldFirst) redraw = true;
            else if (selected != oldSelected) PLUGIN_htps_RedrawChooserSelection(first, oldSelected, selected);
        }
        else if (pressed & KEY_UP)
        {
            u32 oldSelected = selected, oldFirst = first;
            if (!selected) { selected = count - 1u; first = count > HTPS_VISIBLE_ITEMS ? count - HTPS_VISIBLE_ITEMS : 0u; }
            else { selected--; if (selected < first) first--; }
            if (first != oldFirst) redraw = true;
            else if (selected != oldSelected) PLUGIN_htps_RedrawChooserSelection(first, oldSelected, selected);
        }
        else if (pressed & KEY_A)
        {
            if (!PLUGIN_htps_SetSourceUrl(g_htpsFetchSources[selected].url))
                PLUGIN_htps_DrawFetchError();
            else if (PLUGIN_htps_ConfirmSource())
                (void)PLUGIN_htps_RunOnlineSource();
            if (*HTTPS_HOST__menuShouldExit) return;
            redraw = true;
        }
    }
}


PLUGIN_MAIN(htps) bool PLUGIN_htps_Main(const MENUHttpsHostApi *host, MENUHttpsApi *api)
{
    if (!host || !api || host->version != MENU_HTTPS_HOST_API_VERSION || !host->hostTable || !host->protectMemory)
        return false;
    for (u32 i = 1; i <= 41u; i++) if (!host->hostTable[i]) return false;
    g_httpsHost = host;
    api->version = MENU_HTTPS_API_VERSION;
    api->downloadToFile = PLUGIN_htps_OnlineDownloadToFile;
    api->downloadToMemory = PLUGIN_htps_OnlineDownloadToMemory;
    api->openOnlineMenu = PLUGIN_htps_OpenOnlineMenu;
    api->openOnlineSource = PLUGIN_htps_OpenOnlineSource;
    return true;
}