#include "app_state.h"

#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ONLINE_MANIFEST_URL "https://raw.githubusercontent.com/aidam1980/Plugins/main/plugins.json"
#define ONLINE_RAW_BASE     "https://raw.githubusercontent.com/aidam1980/Plugins/main/"
#define ONLINE_NET_POOL_SIZE (1024 * 1024)
#define ONLINE_HTTP_POOL_SIZE (1024 * 1024)
#define ONLINE_SSL_POOL_SIZE (128 * 1024)
#define ONLINE_MANIFEST_MAX (192 * 1024)
#define ONLINE_PLUGIN_MAX (64 * 1024 * 1024)
#define ONLINE_IMAGE_MAX (4 * 1024 * 1024)
#define ONLINE_READ_CHUNK 16384
// The catalog list is re-downloaded at most this often (SQUARE forces a refresh).
#define ONLINE_MANIFEST_CACHE_SECONDS (15 * 60)

static int sOnlineNetPool = -1;
static int sOnlineSslContext = -1;
static int sOnlineHttpContext = -1;
static int sOnlineTrustedRootCount = 0;
static bool sOnlineHttpReady = false;
static char sOnlineManifest[ONLINE_MANIFEST_MAX + 1];
// Catalog artwork is retained separately from installed plugin artwork.  The
// cache lets the library view render without re-downloading the same PNG on
// every visit to the Internet catalog.
static const char *ONLINE_MANIFEST_CACHE = "/data/PluginManager/online_manifest.json";
static const char *ONLINE_IMAGE_CACHE_DIR = "/data/PluginManager/plugins_manager_catalog_cache";
static PngImage sOnlineImages[MAX_GET_ONLINE_ITEMS] = {};
static bool sOnlineImageTried[MAX_GET_ONLINE_ITEMS] = {};
static PngImage sOnlineQrSiteImages[MAX_GET_ONLINE_ITEMS] = {};
static PngImage sOnlineQrDiscordImages[MAX_GET_ONLINE_ITEMS] = {};
static bool sOnlineQrSiteTried[MAX_GET_ONLINE_ITEMS] = {};
static bool sOnlineQrDiscordTried[MAX_GET_ONLINE_ITEMS] = {};
static bool sOnlineDetailsView = false;
struct OnlineCategoryArtwork
{
    char name[96];
    char image[512];
    PngImage decoded;
    bool tried;
};
static OnlineCategoryArtwork sOnlineCategories[MAX_GET_ONLINE_ITEMS] = {};
static int sOnlineCategoryCount = 0;
struct OnlineBrowserTile
{
    bool isFolder;
    int entryIndex;
    char label[96];
    char path[256];
};
static OnlineBrowserTile sOnlineBrowserTiles[MAX_GET_ONLINE_ITEMS * 2] = {};
static int sOnlineBrowserTileCount = 0;
static int sOnlineSelectedEntry = -1;
static char sOnlineCurrentFolder[256] = "";

// raw.githubusercontent.com currently serves a Let's Encrypt chain ending in
// ISRG Root X1.  A newly-created libSceHttp context does not inherit the
// browser's system CA store, so register the reviewed public root explicitly.
//
// Source: Internet Security Research Group, ISRG Root X1
// SHA-256: 96:BCEC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:
//         CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6
// Valid through 2035-06-04.  CRLF PEM formatting is intentional: the PS4
// HTTPS API consumes PEM records directly.  This is a trust anchor only;
// hostname, validity-period, and certificate-chain verification stay enabled.
static char sOnlineIsrgRootX1[] =
    "-----BEGIN CERTIFICATE-----\r\n"
    "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\r\n"
    "-----END CERTIFICATE-----\r\n";

// GitHub also serves a Sectigo E36/E46 chain from some CDN edges.  These are
// public roots for that alternate chain, not server certificates.
// Sectigo E46 SHA-256: C9:0F:26:F0:FB:1B:40:18:B2:22:27:51:9B:5C:A2:B5:
//                      3E:2C:A5:B3:BE:5C:F1:8E:FE:1B:EF:47:38:0C:53:83
static char sOnlineSectigoRootE46[] =
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIICOjCCAcGgAwIBAgIQQvLM2htpN0RfFf51KBC49DAKBggqhkjOPQQDAzBfMQsw\r\n"
    "CQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1T\r\n"
    "ZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcN\r\n"
    "MjEwMzIyMDAwMDAwWhcNNDYwMzIxMjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYG\r\n"
    "A1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBT\r\n"
    "ZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQA\r\n"
    "IgNiAAR2+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccC\r\n"
    "WvkEN/U0NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+\r\n"
    "6xnOQ6OjQjBAMB0GA1UdDgQWBBTRItpMWfFLXyY4qp3W7usNw/upYTAOBgNVHQ8B\r\n"
    "Af8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNnADBkAjAn7qRa\r\n"
    "qCG76UeXlImldCBteU/IvZNeWBj7LRoAasm4PdCkT0RHlAFWovgzJQxC36oCMB3q\r\n"
    "4S6ILuH5px0CMk7yn2xVdOOurvulGu7t0vzCAxHrRVxgED1cf5kDW21USAGKcw==\r\n"
    "-----END CERTIFICATE-----\r\n";

// E46 is cross-signed by this root on the chain GitHub currently uses.
// USERTrust ECC SHA-256: 4F:F4:60:D5:4B:9C:86:DA:BF:BC:FC:57:12:E0:40:0D:
//                        2B:ED:3F:BC:4D:4F:BD:AA:86:E0:6A:DC:D2:A9:AD:7A
static char sOnlineUserTrustEccRoot[] =
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL\r\n"
    "MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\r\n"
    "eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\r\n"
    "JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx\r\n"
    "MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT\r\n"
    "Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg\r\n"
    "VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm\r\n"
    "aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo\r\n"
    "I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng\r\n"
    "o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G\r\n"
    "A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD\r\n"
    "VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB\r\n"
    "zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW\r\n"
    "RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=\r\n"
    "-----END CERTIFICATE-----\r\n";

// Let’s Encrypt’s Generation-Y chain is:
// leaf -> YR1 -> Root YR -> ISRG Root X1.  Older libSceSsl builds fail to
// construct the last bridge even with X1 loaded.  This is the official Root
// YR certificate cross-signed by ISRG Root X1 (not a leaf certificate).
// SHA-256: 07:26:39:D0:B1:40:D5:BF:FA:E1:6A:D9:C3:F6:CC:60:86:04:06:21:
//         F5:1E:E6:1A:6D:46:A8:91:5C:07:CF:76; expires 2032-09-02.
static char sOnlineLetsEncryptRootYrByX1[] =
    "-----BEGIN CERTIFICATE-----\r\n"
    "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\r\n"
    "-----END CERTIFICATE-----\r\n";

static SslPem sOnlineTrustedRoots[] = {
    {sOnlineIsrgRootX1, sizeof(sOnlineIsrgRootX1) - 1},
    {sOnlineSectigoRootE46, sizeof(sOnlineSectigoRootE46) - 1},
    {sOnlineUserTrustEccRoot, sizeof(sOnlineUserTrustEccRoot) - 1},
    {sOnlineLetsEncryptRootYrByX1, sizeof(sOnlineLetsEncryptRootYrByX1) - 1},
};

static bool OnlineLoadTrustedRoots()
{
    // sceHttpsLoadCert retains the SslPem records, so both the PEM and record
    // array above have static storage for the entire HTTP-context lifetime.
    void *rootList[] = {
        &sOnlineTrustedRoots[0],
        &sOnlineTrustedRoots[1],
        &sOnlineTrustedRoots[2],
        &sOnlineTrustedRoots[3],
    };
    int ret = sceHttpsLoadCert(sOnlineHttpContext, 4, rootList, NULL, NULL);
    printf("[online] sceHttpsLoadCert(4 GitHub CA roots): 0x%08X\n", ret);
    if (ret < 0)
    {
        snprintf(gStatus, sizeof(gStatus), "SSL root CA load failed: 0x%08X", ret);
        return false;
    }
    sOnlineTrustedRootCount = 4;
    return true;
}

static bool OnlineHttpInit()
{
    if (sOnlineHttpReady) return true;

    // These are internal PS4 system modules exposed by OpenOrbis.
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);

    int ret = sceNetCtlInit();
    if (ret != 0)
    {
        snprintf(gStatus, sizeof(gStatus), "Network control init failed: 0x%08X", ret);
        return false;
    }

    ret = sceNetInit();
    if (ret != 0)
    {
        sceNetCtlTerm();
        snprintf(gStatus, sizeof(gStatus), "Network init failed: 0x%08X", ret);
        return false;
    }

    sOnlineNetPool = sceNetPoolCreate("plugins_manager_http", ONLINE_NET_POOL_SIZE, 0);
    if (sOnlineNetPool < 0)
    {
        sceNetTerm();
        sceNetCtlTerm();
        snprintf(gStatus, sizeof(gStatus), "Network pool failed: 0x%08X", sOnlineNetPool);
        return false;
    }

    sOnlineSslContext = sceSslInit(ONLINE_SSL_POOL_SIZE);
    if (sOnlineSslContext < 0)
    {
        sceNetPoolDestroy(sOnlineNetPool);
        sOnlineNetPool = -1;
        sceNetTerm();
        sceNetCtlTerm();
        snprintf(gStatus, sizeof(gStatus), "SSL init failed: 0x%08X", sOnlineSslContext);
        return false;
    }

    sOnlineHttpContext = sceHttpInit(sOnlineNetPool, sOnlineSslContext, ONLINE_HTTP_POOL_SIZE);
    if (sOnlineHttpContext < 0)
    {
        sceSslTerm();
        sOnlineSslContext = -1;
        sceNetPoolDestroy(sOnlineNetPool);
        sOnlineNetPool = -1;
        sceNetTerm();
        sceNetCtlTerm();
        snprintf(gStatus, sizeof(gStatus), "HTTP init failed: 0x%08X", sOnlineHttpContext);
        return false;
    }

    if (!OnlineLoadTrustedRoots())
    {
        sceHttpTerm(sOnlineHttpContext);
        sOnlineHttpContext = -1;
        sceSslTerm();
        sOnlineSslContext = -1;
        sceNetPoolDestroy(sOnlineNetPool);
        sOnlineNetPool = -1;
        sceNetTerm();
        sceNetCtlTerm();
        return false;
    }

    sOnlineHttpReady = true;
    printf("[online] HTTPS initialized with 4 GitHub CA roots; verification remains enabled\n");
    return true;
}

// One HTTPS connection is kept alive and reused for consecutive requests. Previously every
// image paid for a brand new TCP + TLS handshake, which was the slowest part of the catalog.
static int sOnlineTemplateId = -1;
static int sOnlineConnectionId = -1;
static char sOnlineConnectionOrigin[160] = {};

static void OnlineDropConnection()
{
    if (sOnlineConnectionId >= 0) sceHttpDeleteConnection(sOnlineConnectionId);
    if (sOnlineTemplateId >= 0) sceHttpDeleteTemplate(sOnlineTemplateId);
    sOnlineConnectionId = -1;
    sOnlineTemplateId = -1;
    sOnlineConnectionOrigin[0] = '\0';
}

// "https://host/a/b" -> "https://host"
static void OnlineUrlOrigin(const char *url, char *origin, size_t originSize)
{
    const char *scheme = strstr(url, "://");
    const char *hostStart = scheme ? scheme + 3 : url;
    const char *slash = strchr(hostStart, '/');
    size_t length = slash ? (size_t)(slash - url) : strlen(url);
    if (length >= originSize) length = originSize - 1;
    memcpy(origin, url, length);
    origin[length] = '\0';
}

static int OnlineHttpGetLastErrno(int requestId)
{
    int httpErrno = 0;
    int ret = sceHttpGetLastErrno(requestId, &httpErrno);
    return (ret == 0) ? httpErrno : ret;
}

// Fetches either into a bounded memory buffer or into an already-open file.
// HTTPS certificate verification is left enabled; no insecure TLS flags are used.
static bool OnlineHttpGet(const char *url, char *memory, size_t memoryCapacity,
                          FILE *output, size_t maximumBytes, size_t *actualBytes)
{
    int templateId = -1, connectionId = -1, requestId = -1;
    int ret = -1;
    bool ok = false;
    size_t total = 0;
    int statusCode = 0;
    int contentLengthType = ORBIS_HTTP_CONTENTLEN_NOT_FOUND;
    size_t contentLength = 0;
    unsigned char chunk[ONLINE_READ_CHUNK];
    char origin[160];
    char statusBefore[sizeof(gStatus)];
    bool reusedConnection = false;

    if (actualBytes) *actualBytes = 0;
    if (!url)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP error: URL is null");
        return false;
    }
    if (!memory && !output)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP error: no output destination");
        return false;
    }
    if (maximumBytes == 0)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP error: zero byte limit");
        return false;
    }
    if (!OnlineHttpInit()) return false;

    OnlineUrlOrigin(url, origin, sizeof(origin));
    if (sOnlineConnectionId >= 0 && strcmp(sOnlineConnectionOrigin, origin) == 0)
    {
        reusedConnection = true; // same server as the previous request: skip the handshake
        CopyText(statusBefore, sizeof(statusBefore), gStatus);
    }
    else
    {
    OnlineDropConnection();
    templateId = sceHttpCreateTemplate(sOnlineHttpContext, "PS4PluginManager/1.02",
                                       ORBIS_HTTP_VERSION_1_1, 1);
    if (templateId < 0)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP create template failed: 0x%08X", templateId);
        goto cleanup;
    }

    ret = sceHttpSetConnectTimeOut(templateId, 15000000);
    if (ret < 0)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP connect timeout setup failed: 0x%08X", ret);
        goto cleanup;
    }
    ret = sceHttpSetResolveTimeOut(templateId, 15000000);
    if (ret < 0)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP DNS timeout setup failed: 0x%08X", ret);
        goto cleanup;
    }
    ret = sceHttpSetSendTimeOut(templateId, 30000000);
    if (ret < 0)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP send timeout setup failed: 0x%08X", ret);
        goto cleanup;
    }

    connectionId = sceHttpCreateConnectionWithURL(templateId, url, true);
    if (connectionId < 0)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP create connection failed: 0x%08X", connectionId);
        goto cleanup;
    }

    // From here on the connection belongs to the shared slot and survives this call.
    sOnlineTemplateId = templateId;
    sOnlineConnectionId = connectionId;
    CopyText(sOnlineConnectionOrigin, sizeof(sOnlineConnectionOrigin), origin);
    templateId = -1;
    connectionId = -1;
    }

    requestId = sceHttpCreateRequestWithURL(sOnlineConnectionId, ORBIS_METHOD_GET, url, 0);
    if (requestId < 0)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP create GET request failed: 0x%08X", requestId);
        goto cleanup;
    }

    ret = sceHttpSendRequest(requestId, NULL, 0);
    if (ret != 0)
    {
        int httpErrno = OnlineHttpGetLastErrno(requestId);
        snprintf(gStatus, sizeof(gStatus),
                 "HTTPS send failed 0x%08X / errno 0x%08X (CA roots: %d)",
                 ret, httpErrno, sOnlineTrustedRootCount);
        goto cleanup;
    }

    ret = sceHttpGetStatusCode(requestId, &statusCode);
    if (ret < 0)
    {
        int httpErrno = OnlineHttpGetLastErrno(requestId);
        snprintf(gStatus, sizeof(gStatus), "HTTP status failed 0x%08X / errno 0x%08X",
                 ret, httpErrno);
        goto cleanup;
    }
    printf("[online-http] URL='%s' HTTP status=%d\n", url, statusCode);
    if (statusCode != 200)
    {
        snprintf(gStatus, sizeof(gStatus), "GitHub returned HTTP status %d", statusCode);
        goto cleanup;
    }

    ret = sceHttpGetResponseContentLength(requestId, &contentLengthType, &contentLength);
    if (ret == 0 && contentLengthType == ORBIS_HTTP_CONTENTLEN_EXIST && contentLength > maximumBytes)
    {
        snprintf(gStatus, sizeof(gStatus), "Download too large: %lu bytes (limit %lu)",
                 (unsigned long)contentLength, (unsigned long)maximumBytes);
        goto cleanup;
    }

    for (;;)
    {
        size_t remaining = maximumBytes - total;
        uint32_t want = (uint32_t)(remaining > sizeof(chunk) ? sizeof(chunk) : remaining);
        if (want == 0)
        {
            // One extra byte distinguishes a valid exact-size response from truncation.
            unsigned char extra;
            ret = sceHttpReadData(requestId, &extra, 1);
            if (ret > 0)
                snprintf(gStatus, sizeof(gStatus), "Download exceeds the safe size limit");
            else if (ret == 0)
                ok = true;
            else
            {
                int httpErrno = OnlineHttpGetLastErrno(requestId);
                snprintf(gStatus, sizeof(gStatus), "HTTP final read failed 0x%08X / errno 0x%08X",
                         ret, httpErrno);
            }
            break;
        }

        ret = sceHttpReadData(requestId, chunk, want);
        if (ret < 0)
        {
            int httpErrno = OnlineHttpGetLastErrno(requestId);
            snprintf(gStatus, sizeof(gStatus), "HTTP read failed 0x%08X / errno 0x%08X (%lu bytes)",
                     ret, httpErrno, (unsigned long)total);
            break;
        }
        if (ret == 0)
        {
            ok = true;
            break;
        }
        if ((size_t)ret > remaining)
        {
            snprintf(gStatus, sizeof(gStatus), "HTTP returned more data than the receive buffer allows");
            break;
        }

        if (memory)
        {
            memcpy(memory + total, chunk, (size_t)ret);
        }
        else if (fwrite(chunk, 1, (size_t)ret, output) != (size_t)ret)
        {
            snprintf(gStatus, sizeof(gStatus), "Could not write the downloaded file");
            break;
        }
        total += (size_t)ret;
    }

    if (ok && total == 0)
    {
        snprintf(gStatus, sizeof(gStatus), "HTTP request succeeded but returned an empty body");
        ok = false;
    }
    if (ok && contentLengthType == ORBIS_HTTP_CONTENTLEN_EXIST && total != contentLength)
    {
        snprintf(gStatus, sizeof(gStatus), "Incomplete HTTP body: got %lu of %lu bytes",
                 (unsigned long)total, (unsigned long)contentLength);
        ok = false;
    }

cleanup:
    if (requestId >= 0) sceHttpDeleteRequest(requestId);
    if (connectionId >= 0) sceHttpDeleteConnection(connectionId);
    if (templateId >= 0) sceHttpDeleteTemplate(templateId);
    // Never keep a connection that just failed or was not read to the end.
    if (!(ok && total > 0)) OnlineDropConnection();

    if (memory && memoryCapacity > 0)
    {
        size_t termAt = total < memoryCapacity ? total : memoryCapacity - 1;
        memory[termAt] = '\0';
    }
    if (actualBytes) *actualBytes = total;
    printf("[online-http] URL='%s' received=%lu result=%s reason='%s'\n", url,
           (unsigned long)total, (ok && total > 0) ? "ok" : "failed", gStatus);
    if (!(ok && total > 0) && reusedConnection && total == 0)
    {
        // The server probably closed the idle connection: retry once on a fresh one.
        printf("[online-http] reused connection failed before any data; retrying fresh\n");
        CopyText(gStatus, sizeof(gStatus), statusBefore);
        return OnlineHttpGet(url, memory, memoryCapacity, output, maximumBytes, actualBytes);
    }
    return ok && total > 0;
}

static const char *JsonSkipWhitespace(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

static bool JsonReadString(const char **cursor, const char *end, char *out, size_t outSize)
{
    if (!cursor || !*cursor || *cursor >= end || **cursor != '"' || !out || outSize == 0)
        return false;

    const char *p = *cursor + 1;
    size_t used = 0;
    while (p < end && *p != '"')
    {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\' && p < end)
        {
            char escaped = *p++;
            switch (escaped)
            {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u':
                    // Catalog text is currently ASCII; preserve a placeholder for escaped Unicode.
                    if (p + 4 <= end) p += 4;
                    c = '?';
                    break;
                default: c = (unsigned char)escaped; break;
            }
        }
        if (used + 1 < outSize) out[used++] = (char)c;
    }
    if (p >= end || *p != '"') return false;
    out[used] = '\0';
    *cursor = p + 1;
    return true;
}

static const char *JsonSkipValue(const char *p, const char *end)
{
    p = JsonSkipWhitespace(p, end);
    if (p >= end) return p;
    if (*p == '"')
    {
        char ignored[2];
        if (!JsonReadString(&p, end, ignored, sizeof(ignored))) return end;
        return p;
    }
    if (*p == '{' || *p == '[')
    {
        char open = *p;
        char close = open == '{' ? '}' : ']';
        int depth = 0;
        bool inString = false, escaped = false;
        for (; p < end; ++p)
        {
            char c = *p;
            if (inString)
            {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == open) ++depth;
            else if (c == close && --depth == 0) return p + 1;
        }
        return end;
    }
    while (p < end && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\r' && *p != '\n' && *p != '\t') ++p;
    return p;
}

static const char *JsonFindObjectEnd(const char *start, const char *end)
{
    if (!start || start >= end || *start != '{') return end;
    int depth = 0;
    bool inString = false, escaped = false;
    for (const char *p = start; p < end; ++p)
    {
        char c = *p;
        if (inString)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return p + 1;
    }
    return end;
}

static bool JsonGetStringField(const char *objectStart, const char *objectEnd,
                               const char *wanted, char *out, size_t outSize)
{
    if (!objectStart || !objectEnd || !wanted || !out || outSize == 0) return false;
    const char *p = objectStart + 1;
    while (p < objectEnd)
    {
        p = JsonSkipWhitespace(p, objectEnd);
        if (p >= objectEnd || *p == '}') break;
        char key[80] = {};
        if (!JsonReadString(&p, objectEnd, key, sizeof(key))) return false;
        p = JsonSkipWhitespace(p, objectEnd);
        if (p >= objectEnd || *p != ':') return false;
        p = JsonSkipWhitespace(p + 1, objectEnd);
        if (strcmp(key, wanted) == 0)
        {
            if (*p != '"') return false;
            return JsonReadString(&p, objectEnd, out, outSize);
        }
        p = JsonSkipValue(p, objectEnd);
        p = JsonSkipWhitespace(p, objectEnd);
        if (p < objectEnd && *p == ',') ++p;
    }
    return false;
}

static bool JsonGetBoolField(const char *objectStart, const char *objectEnd, const char *wanted)
{
    if (!objectStart || !objectEnd || !wanted) return false;
    const char *p = objectStart + 1;
    while (p < objectEnd)
    {
        p = JsonSkipWhitespace(p, objectEnd);
        if (p >= objectEnd || *p == '}') break;
        char key[80] = {};
        if (!JsonReadString(&p, objectEnd, key, sizeof(key))) return false;
        p = JsonSkipWhitespace(p, objectEnd);
        if (p >= objectEnd || *p != ':') return false;
        p = JsonSkipWhitespace(p + 1, objectEnd);
        if (strcmp(key, wanted) == 0) return (objectEnd - p >= 4 && strncmp(p, "true", 4) == 0);
        p = JsonSkipValue(p, objectEnd);
        p = JsonSkipWhitespace(p, objectEnd);
        if (p < objectEnd && *p == ',') ++p;
    }
    return false;
}

static bool JsonGetNestedStringField(const char *objectStart, const char *objectEnd,
                                     const char *objectName, const char *wanted,
                                     char *out, size_t outSize)
{
    const char *p = objectStart + 1;
    while (p < objectEnd)
    {
        p = JsonSkipWhitespace(p, objectEnd);
        if (p >= objectEnd || *p == '}') break;
        char key[80] = {};
        if (!JsonReadString(&p, objectEnd, key, sizeof(key))) return false;
        p = JsonSkipWhitespace(p, objectEnd);
        if (p >= objectEnd || *p != ':') return false;
        p = JsonSkipWhitespace(p + 1, objectEnd);
        if (strcmp(key, objectName) == 0)
        {
            if (*p != '{') return false;
            const char *nestedEnd = JsonFindObjectEnd(p, objectEnd);
            return nestedEnd > p && JsonGetStringField(p, nestedEnd, wanted, out, outSize);
        }
        p = JsonSkipValue(p, objectEnd);
        p = JsonSkipWhitespace(p, objectEnd);
        if (p < objectEnd && *p == ',') ++p;
    }
    return false;
}

static bool IsSafeCatalogFile(const char *path)
{
    if (!path || !path[0] || path[0] == '/' || strstr(path, "..") || strchr(path, '\\')) return false;
    if (!(strncmp(path, "GTA V/", 6) == 0 || strncmp(path, "MODS/", 5) == 0 ||
          strncmp(path, "Online/", 7) == 0 || strncmp(path, "GoldHEN/", 8) == 0)) return false;
    const char *dot = strrchr(path, '.');
    return dot && (strcasecmp(dot, ".prx") == 0 || strcasecmp(dot, ".sprx") == 0);
}

static bool IsSafeCatalogImage(const char *path)
{
    return path && strncmp(path, "pngs/", 5) == 0 && path[0] != '/' &&
           !strstr(path, "..") && !strchr(path, '\\');
}

// Validate before extracting anything.  The old scanner walked brace pairs and
// consequently accepted a broken document (and then omitted arbitrary fields).
// Keep this small validator here rather than accepting a permissive subset of
// JSON from the network.
static const char *JsonValidateValue(const char *p, const char *end);
static const char *sJsonErrorAt = NULL;

static const char *JsonInvalid(const char *at)
{
    if (!sJsonErrorAt) sJsonErrorAt = at;
    return NULL;
}

static const char *JsonValidateString(const char *p, const char *end)
{
    if (p >= end || *p++ != '"') return JsonInvalid(p);
    while (p < end)
    {
        unsigned char c = (unsigned char)*p++;
        if (c == '"') return p;
        if (c < 0x20) return JsonInvalid(p - 1);
        if (c != '\\') continue;
        if (p >= end) return JsonInvalid(p);
        char e = *p++;
        if (e == 'u')
        {
            for (int i = 0; i < 4; ++i)
                if (p >= end || !isxdigit((unsigned char)*p++)) return JsonInvalid(p);
        }
        else if (!strchr("\"\\/bfnrt", e)) return JsonInvalid(p - 1);
    }
    return JsonInvalid(p);
}

static const char *JsonValidateArray(const char *p, const char *end)
{
    p = JsonSkipWhitespace(p + 1, end);
    if (p < end && *p == ']') return p + 1;
    for (;;)
    {
        p = JsonValidateValue(p, end);
        if (!p) return NULL;
        p = JsonSkipWhitespace(p, end);
        if (p < end && *p == ']') return p + 1;
        if (p >= end || *p++ != ',') return JsonInvalid(p);
        p = JsonSkipWhitespace(p, end);
    }
}

static const char *JsonValidateObject(const char *p, const char *end)
{
    p = JsonSkipWhitespace(p + 1, end);
    if (p < end && *p == '}') return p + 1;
    for (;;)
    {
        p = JsonValidateString(p, end);
        if (!p) return NULL;
        p = JsonSkipWhitespace(p, end);
        if (p >= end || *p++ != ':') return JsonInvalid(p);
        p = JsonSkipWhitespace(p, end);
        p = JsonValidateValue(p, end);
        if (!p) return NULL;
        p = JsonSkipWhitespace(p, end);
        if (p < end && *p == '}') return p + 1;
        if (p >= end || *p++ != ',') return JsonInvalid(p);
        p = JsonSkipWhitespace(p, end);
    }
}

static const char *JsonValidateValue(const char *p, const char *end)
{
    p = JsonSkipWhitespace(p, end);
    if (p >= end) return JsonInvalid(p);
    if (*p == '"') return JsonValidateString(p, end);
    if (*p == '{') return JsonValidateObject(p, end);
    if (*p == '[') return JsonValidateArray(p, end);
    if (end - p >= 4 && strncmp(p, "true", 4) == 0) return p + 4;
    if (end - p >= 5 && strncmp(p, "false", 5) == 0) return p + 5;
    if (end - p >= 4 && strncmp(p, "null", 4) == 0) return p + 4;
    const char *start = p;
    if (*p == '-') ++p;
    if (p >= end) return JsonInvalid(p);
    if (*p == '0') ++p;
    else { if (!isdigit((unsigned char)*p)) return JsonInvalid(p); while (p < end && isdigit((unsigned char)*p)) ++p; }
    if (p < end && *p == '.') { ++p; if (p >= end || !isdigit((unsigned char)*p)) return JsonInvalid(p); while (p < end && isdigit((unsigned char)*p)) ++p; }
    if (p < end && (*p == 'e' || *p == 'E')) { ++p; if (p < end && (*p == '+' || *p == '-')) ++p; if (p >= end || !isdigit((unsigned char)*p)) return JsonInvalid(p); while (p < end && isdigit((unsigned char)*p)) ++p; }
    if (p != start && !(p == start + 1 && *start == '-')) return p;
    return JsonInvalid(start);
}

static bool ValidateOnlineManifest(const char *json, size_t size)
{
    const char *begin = json, *end = json + size;
    sJsonErrorAt = NULL;
    const char *p = JsonValidateValue(begin, end);
    p = p ? JsonSkipWhitespace(p, end) : NULL;
    if (p == end) return true;
    const char *at = p ? p : (sJsonErrorAt ? sJsonErrorAt : begin);
    size_t offset = (size_t)(at - begin), line = 1, column = 1;
    for (size_t i = 0; i < offset; ++i)
        if (begin[i] == '\n') { ++line; column = 1; } else ++column;
    printf("[online-manifest] strict JSON error at line %lu, column %lu\n", (unsigned long)line, (unsigned long)column);
    snprintf(gStatus, sizeof(gStatus), "Catalog JSON error at line %lu, column %lu", (unsigned long)line, (unsigned long)column);
    return false;
}

static void AddOnlineCategoryFromObject(const char *start, const char *end)
{
    char image[512] = {}, name[96] = {}, file[2] = {};
    JsonGetStringField(start, end, "file", file, sizeof(file));
    JsonGetStringField(start, end, "image", image, sizeof(image));
    if (file[0] || strncmp(image, "pngs/Categories/", 16) != 0 || !IsSafeCatalogImage(image)) return;
    JsonGetStringField(start, end, "name", name, sizeof(name));
    if (!name[0]) JsonGetStringField(start, end, "title", name, sizeof(name));
    if (!name[0]) JsonGetStringField(start, end, "category", name, sizeof(name));
    if (!name[0])
    {
        const char *base = strrchr(image, '/'); base = base ? base + 1 : image;
        CopyText(name, sizeof(name), base);
        char *dot = strrchr(name, '.'); if (dot) *dot = '\0';
    }
    for (int i = 0; i < sOnlineCategoryCount; ++i)
        if (strcasecmp(sOnlineCategories[i].name, name) == 0) return;
    if (sOnlineCategoryCount >= MAX_GET_ONLINE_ITEMS) { printf("[online-manifest] category capacity exceeded\n"); return; }
    OnlineCategoryArtwork &category = sOnlineCategories[sOnlineCategoryCount++];
    CopyText(category.name, sizeof(category.name), name);
    CopyText(category.image, sizeof(category.image), image);
}

static void AddOnlineEntryFromObject(const char *start, const char *end)
{
    if (gOnlineCount >= MAX_GET_ONLINE_ITEMS) return;

    GetOnlineEntry entry = {};
    if (!JsonGetStringField(start, end, "file", entry.file, sizeof(entry.file))) return;
    if (!IsSafeCatalogFile(entry.file)) return;

    JsonGetStringField(start, end, "title", entry.title, sizeof(entry.title));
    JsonGetStringField(start, end, "author", entry.author, sizeof(entry.author));
    JsonGetStringField(start, end, "description", entry.description, sizeof(entry.description));
    JsonGetStringField(start, end, "image", entry.image, sizeof(entry.image));
    JsonGetStringField(start, end, "website", entry.websiteUrl, sizeof(entry.websiteUrl));
    if (!entry.websiteUrl[0]) JsonGetStringField(start, end, "url", entry.websiteUrl, sizeof(entry.websiteUrl));
    // The OrbisNet website is encoded in the repository QR artwork; keep a
    // readable URL beside the QR even when older manifests omit website/url.
    if (!entry.websiteUrl[0] && strcmp(entry.title, "OrbisNet") == 0)
        CopyText(entry.websiteUrl, sizeof(entry.websiteUrl), "https://orbisnet.app");
    JsonGetNestedStringField(start, end, "qr", "site", entry.qrSite, sizeof(entry.qrSite));
    JsonGetNestedStringField(start, end, "qr", "discord", entry.qrDiscord, sizeof(entry.qrDiscord));
    entry.registrationRequired = JsonGetBoolField(start, end, "registration_required");
    if (!IsSafeCatalogImage(entry.image)) entry.image[0] = '\0';
    if (!IsSafeCatalogImage(entry.qrSite)) entry.qrSite[0] = '\0';
    if (!IsSafeCatalogImage(entry.qrDiscord)) entry.qrDiscord[0] = '\0';

    const char *slash = strchr(entry.file, '/');
    if (slash)
    {
        size_t categoryLength = (size_t)(slash - entry.file);
        if (categoryLength >= sizeof(entry.category)) categoryLength = sizeof(entry.category) - 1;
        memcpy(entry.category, entry.file, categoryLength);
        entry.category[categoryLength] = '\0';
    }
    if (!entry.title[0])
    {
        const char *base = strrchr(entry.file, '/');
        CopyText(entry.title, sizeof(entry.title), base ? base + 1 : entry.file);
        char *dot = strrchr(entry.title, '.');
        if (dot) *dot = '\0';
    }
    if (!entry.author[0]) CopyText(entry.author, sizeof(entry.author), "Community");
    gOnlineEntries[gOnlineCount++] = entry;
}

static int ParseOnlineManifest(const char *json, size_t size)
{
    gOnlineCount = 0;
    memset(gOnlineEntries, 0, sizeof(gOnlineEntries));
    for (int i = 0; i < sOnlineCategoryCount; ++i) FreePngImage(&sOnlineCategories[i].decoded);
    memset(sOnlineCategories, 0, sizeof(sOnlineCategories));
    sOnlineCategoryCount = 0;
    if (!json || size == 0 || !ValidateOnlineManifest(json, size)) return 0;
    const char *p = json;
    const char *end = json + size;
    // Walk the complete document even after the entry capacity is reached so
    // category metadata (including late GoldHEN declarations) is never lost.
    while (p < end)
    {
        if (*p == '{')
        {
            const char *objectEnd = JsonFindObjectEnd(p, end);
            if (objectEnd <= p || objectEnd > end) break;
            AddOnlineCategoryFromObject(p, objectEnd);
            AddOnlineEntryFromObject(p, objectEnd);
            ++p;
        }
        else ++p;
    }
    gOnlineCursor = 0;
    gOnlineScroll = 0;
    return gOnlineCount;
}

static bool AddOnlineBrowserFolder(const char *label, const char *path)
{
    for (int i = 0; i < sOnlineBrowserTileCount; ++i)
        if (sOnlineBrowserTiles[i].isFolder && strcmp(sOnlineBrowserTiles[i].path, path) == 0)
            return true;
    if (sOnlineBrowserTileCount >= (int)(sizeof(sOnlineBrowserTiles) / sizeof(sOnlineBrowserTiles[0])))
        return false;
    OnlineBrowserTile &tile = sOnlineBrowserTiles[sOnlineBrowserTileCount++];
    tile = {};
    tile.isFolder = true;
    tile.entryIndex = -1;
    CopyText(tile.label, sizeof(tile.label), label);
    CopyText(tile.path, sizeof(tile.path), path);
    return true;
}

static bool AddOnlineBrowserPlugin(int entryIndex)
{
    if (sOnlineBrowserTileCount >= (int)(sizeof(sOnlineBrowserTiles) / sizeof(sOnlineBrowserTiles[0])))
        return false;
    OnlineBrowserTile &tile = sOnlineBrowserTiles[sOnlineBrowserTileCount++];
    tile = {};
    tile.isFolder = false;
    tile.entryIndex = entryIndex;
    CopyText(tile.label, sizeof(tile.label), gOnlineEntries[entryIndex].title);
    return true;
}

static void RebuildOnlineBrowserTiles()
{
    sOnlineBrowserTileCount = 0;
    memset(sOnlineBrowserTiles, 0, sizeof(sOnlineBrowserTiles));
    const size_t folderLength = strlen(sOnlineCurrentFolder);
    for (int i = 0; i < gOnlineCount; ++i)
    {
        const char *file = gOnlineEntries[i].file;
        const char *remaining = file;
        if (folderLength)
        {
            if (strncmp(file, sOnlineCurrentFolder, folderLength) != 0 || file[folderLength] != '/')
                continue;
            remaining = file + folderLength + 1;
        }
        const char *slash = strchr(remaining, '/');
        if (!slash)
        {
            AddOnlineBrowserPlugin(i);
            continue;
        }
        char label[96], childPath[256];
        size_t length = (size_t)(slash - remaining);
        if (length >= sizeof(label)) length = sizeof(label) - 1;
        memcpy(label, remaining, length);
        label[length] = '\0';
        if (folderLength) snprintf(childPath, sizeof(childPath), "%s/%s", sOnlineCurrentFolder, label);
        else CopyText(childPath, sizeof(childPath), label);
        AddOnlineBrowserFolder(label, childPath);
    }
    gOnlineCursor = 0;
    gOnlineScroll = 0;
}

static void OpenOnlineBrowserFolder(const char *path)
{
    CopyText(sOnlineCurrentFolder, sizeof(sOnlineCurrentFolder), path);
    RebuildOnlineBrowserTiles();
}

static bool LeaveOnlineBrowserFolder()
{
    if (!sOnlineCurrentFolder[0]) return false;
    char *slash = strrchr(sOnlineCurrentFolder, '/');
    if (slash) *slash = '\0';
    else sOnlineCurrentFolder[0] = '\0';
    RebuildOnlineBrowserTiles();
    return true;
}

static const char *OnlineBrowserFolderLabel()
{
    if (!sOnlineCurrentFolder[0]) return "INTERNET";
    const char *slash = strrchr(sOnlineCurrentFolder, '/');
    return slash ? slash + 1 : sOnlineCurrentFolder;
}

static const PngImage *FindOnlineCategoryArtwork(const char *name)
{
    for (int i = 0; i < sOnlineCategoryCount; ++i)
        if (strcasecmp(sOnlineCategories[i].name, name) == 0 && sOnlineCategories[i].decoded.rgba)
            return &sOnlineCategories[i].decoded;

    // A category cover may be JPEG (the GTA V cover currently is) or missing.
    // Reuse the first valid plugin PNG in that folder rather than showing a blank tile.
    for (int i = 0; i < gOnlineCount; ++i)
        if (strcasecmp(gOnlineEntries[i].category, name) == 0 && sOnlineImages[i].rgba)
            return &sOnlineImages[i];
    return NULL;
}

static bool BuildRawUrl(const char *relativePath, char *url, size_t urlSize)
{
    if (!relativePath || !url || urlSize < strlen(ONLINE_RAW_BASE) + 8) return false;
    size_t used = (size_t)snprintf(url, urlSize, "%s", ONLINE_RAW_BASE);
    if (used >= urlSize) return false;
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)relativePath; *p; ++p)
    {
        unsigned char c = *p;
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe)
        {
            if (used + 1 >= urlSize) return false;
            url[used++] = (char)c;
        }
        else
        {
            if (used + 3 >= urlSize) return false;
            url[used++] = '%';
            url[used++] = hex[c >> 4];
            url[used++] = hex[c & 15];
        }
    }
    url[used] = '\0';
    printf("[online-url] manifest path='%s' encoded URL='%s'\n", relativePath, url);
    return true;
}

static int DrawWrappedCatalogText(Scene2D *scene, const char *text, FT_Face font,
                                  int x, int y, int maxChars, int lineHeight,
                                  Color background, Color color)
{
    if (!scene || !text || maxChars < 1) return 0;
    int lines = 0;
    const char *p = text;
    while (*p)
    {
        char line[256] = {};
        int used = 0, lastSpace = -1;
        while (*p && *p != '\n' && used < maxChars)
        {
            line[used] = *p;
            if (*p == ' ') lastSpace = used;
            ++used; ++p;
        }
        if (*p && *p != '\n' && lastSpace >= 0)
        {
            p -= used - lastSpace - 1;
            used = lastSpace;
        }
        while (used > 0 && line[used - 1] == ' ') --used;
        line[used] = '\0';
        scene->DrawText(line, font, x, y + lines * lineHeight, background, color);
        ++lines;
        while (*p == ' ') ++p;
        if (*p == '\n') ++p;
    }
    return lines;
}

static void FreeOnlineCatalogImages()
{
    for (int i = 0; i < MAX_GET_ONLINE_ITEMS; ++i)
    {
        if (sOnlineImages[i].rgba) free(sOnlineImages[i].rgba);
        if (sOnlineQrSiteImages[i].rgba) free(sOnlineQrSiteImages[i].rgba);
        if (sOnlineQrDiscordImages[i].rgba) free(sOnlineQrDiscordImages[i].rgba);
        sOnlineImages[i] = {};
        sOnlineQrSiteImages[i] = {};
        sOnlineQrDiscordImages[i] = {};
        sOnlineImageTried[i] = false;
        sOnlineQrSiteTried[i] = false;
        sOnlineQrDiscordTried[i] = false;
    }
}

static unsigned int OnlineImageCacheKey(const char *path)
{
    // Stable FNV-1a key: paths can contain spaces and nested directories, so
    // they must not be used directly as cache file names.
    unsigned int hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)path; p && *p; ++p)
    {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

static bool LoadOnlineCachedPng(const char *kind, const char *path, PngImage *image, bool *tried)
{
    if (!path || !path[0] || !image || !tried) return false;
    if (*tried) return image->rgba != NULL;
    *tried = true;

    // Keep catalog artwork cache outside GoldHEN/plugins. Create the parent
    // first because mkdir() on a nested path fails when /data/PluginManager
    // does not exist yet.
    mkdir("/data/PluginManager", 0777);
    mkdir(ONLINE_IMAGE_CACHE_DIR, 0777); // EEXIST is expected after first use.
    char cachePath[640], temporary[680], url[768];
    unsigned int key = OnlineImageCacheKey(path);
    snprintf(cachePath, sizeof(cachePath), "%s/%08X.png", ONLINE_IMAGE_CACHE_DIR, key);
    struct stat cacheStat = {};
    bool cached = stat(cachePath, &cacheStat) == 0 && cacheStat.st_size > 0;
    if (!cached && FileExists(cachePath)) unlink(cachePath);
    printf("[online-image:%s] manifest path='%s' cache='%s' cached bytes=%ld\n",
           kind, path, cachePath, cached ? (long)cacheStat.st_size : 0L);

download_image:
    if (!cached)
    {
        if (!BuildRawUrl(path, url, sizeof(url)))
        {
            printf("[online-image:%s] failure: cannot construct raw GitHub URL\n", kind);
            return false;
        }
        snprintf(temporary, sizeof(temporary), "%s.download", cachePath);
        FILE *fp = fopen(temporary, "wb");
        if (!fp) { printf("[online-image:%s] failure: cannot create cache file\n", kind); return false; }
        size_t received = 0;
        bool ok = OnlineHttpGet(url, NULL, 0, fp, ONLINE_IMAGE_MAX, &received);
        if (fflush(fp) != 0) ok = false;
        if (fclose(fp) != 0) ok = false;
        if (!ok || received == 0 || rename(temporary, cachePath) != 0)
        {
            printf("[online-image:%s] failure: download=%s bytes=%lu\n", kind, ok ? "ok" : "failed", (unsigned long)received);
            unlink(temporary);
            return false;
        }
        if (stat(cachePath, &cacheStat) != 0 || cacheStat.st_size <= 0)
        {
            printf("[online-image:%s] failure: saved cache is empty\n", kind);
            unlink(cachePath);
            return false;
        }
        printf("[online-image:%s] downloaded bytes=%lu saved bytes=%ld\n", kind,
               (unsigned long)received, (long)cacheStat.st_size);
    }
    if (!LoadPngRGBA(cachePath, image))
    {
        // A corrupt/stale cache entry must not be reused forever.
        printf("[online-image:%s] decode failed: '%s' is not a supported PNG\n", kind, cachePath);
        unlink(cachePath);
        *image = {};
        // A stale cache must not suppress the real request.  Retry exactly
        // once in this call, then leave the slot failed until refresh.
        if (cached)
        {
            cached = false;
            printf("[online-image:%s] retrying download after invalid cached PNG\n", kind);
            goto download_image;
        }
        return false;
    }
    printf("[online-image:%s] decode OK: %dx%d\n", kind, image->width, image->height);
    return image->rgba != NULL;
}

static bool LoadOnlineCatalogImage(int index)
{
    if (index < 0 || index >= gOnlineCount) return false;
    printf("[online-image:plugin artwork] plugin='%s' manifest='%s'\n",
           gOnlineEntries[index].title, gOnlineEntries[index].image);
    return LoadOnlineCachedPng("plugin artwork", gOnlineEntries[index].image, &sOnlineImages[index], &sOnlineImageTried[index]);
}

static bool LoadOnlineQrImage(int index, bool website)
{
    if (index < 0 || index >= gOnlineCount) return false;
    const GetOnlineEntry &entry = gOnlineEntries[index];
    printf("[online-image:%s] plugin='%s' manifest='%s'\n",
           website ? "website QR" : "Discord QR", entry.title,
           website ? entry.qrSite : entry.qrDiscord);
    return website
        ? LoadOnlineCachedPng("website QR", entry.qrSite, &sOnlineQrSiteImages[index], &sOnlineQrSiteTried[index])
        : LoadOnlineCachedPng("Discord QR", entry.qrDiscord, &sOnlineQrDiscordImages[index], &sOnlineQrDiscordTried[index]);
}

// ---------------------------------------------------------------------------
// Catalog loading.  Opening the Internet view used to block for several seconds with a frozen
// screen: it downloaded the list and then every artwork + QR image of every plugin before the
// first frame.  Now the view opens at once with a loading screen, the list comes from a saved
// copy when it is recent, and images are loaded one per frame, only for what is on screen.
// ---------------------------------------------------------------------------
enum { ONLINE_STAGE_READY = 0, ONLINE_STAGE_LOADING = 1, ONLINE_STAGE_FAILED = 2 };
static int sOnlineLoadStage = ONLINE_STAGE_READY;
static bool sOnlineLoadingDrawn = false;   // the loading screen reached the display
static bool sOnlineForceNetwork = false;
static bool sOnlineLoadedFromCache = false;
static int sOnlineSpinnerTick = 0;

static bool LoadFreshCachedManifest(size_t *size)
{
    struct stat st = {};
    if (stat(ONLINE_MANIFEST_CACHE, &st) != 0 || st.st_size <= 0 ||
        (size_t)st.st_size >= sizeof(sOnlineManifest))
        return false;
    const time_t now = time(NULL);
    if (now < st.st_mtime || now - st.st_mtime > ONLINE_MANIFEST_CACHE_SECONDS) return false;
    FILE *fp = fopen(ONLINE_MANIFEST_CACHE, "rb");
    if (!fp) return false;
    const size_t got = fread(sOnlineManifest, 1, sizeof(sOnlineManifest) - 1, fp);
    fclose(fp);
    if (got != (size_t)st.st_size) return false;
    sOnlineManifest[got] = '\0';
    *size = got;
    return true;
}

static void SaveManifestCache(const char *json, size_t size)
{
    mkdir("/data/PluginManager", 0777);
    char temporary[256];
    snprintf(temporary, sizeof(temporary), "%s.tmp", ONLINE_MANIFEST_CACHE);
    FILE *fp = fopen(temporary, "wb");
    if (!fp) return;
    bool ok = fwrite(json, 1, size, fp) == size;
    if (fclose(fp) != 0) ok = false;
    if (!ok || rename(temporary, ONLINE_MANIFEST_CACHE) != 0) unlink(temporary);
}

static void BeginOnlineLoad(bool forceNetwork)
{
    gGetView = 4;
    gOnlineCount = 0;
    gOnlineCursor = 0;
    gOnlineScroll = 0;
    gOnlineConfirmOverwrite = false;
    gOnlinePendingIndex = -1;
    sOnlineDetailsView = false;
    sOnlineSelectedEntry = -1;
    sOnlineCurrentFolder[0] = '\0';
    sOnlineBrowserTileCount = 0;
    FreeOnlineCatalogImages();
    sOnlineLoadStage = ONLINE_STAGE_LOADING;
    sOnlineLoadingDrawn = false;
    sOnlineForceNetwork = forceNetwork;
    sOnlineLoadedFromCache = false;
    snprintf(gStatus, sizeof(gStatus), forceNetwork ? "Refreshing the Plugins online catalog..."
                                                    : "Opening the Plugins online catalog...");
}

// Runs once, on the frame after the loading screen was shown.
static void RunOnlineLoad()
{
    size_t received = 0;
    int count = 0;
    if (!sOnlineForceNetwork && LoadFreshCachedManifest(&received))
    {
        count = ParseOnlineManifest(sOnlineManifest, received);
        if (count > 0) sOnlineLoadedFromCache = true;
    }
    if (count <= 0)
    {
        snprintf(gStatus, sizeof(gStatus), "Connecting to the Plugins online catalog...");
        if (!OnlineHttpGet(ONLINE_MANIFEST_URL, sOnlineManifest, sizeof(sOnlineManifest),
                           NULL, ONLINE_MANIFEST_MAX, &received))
        {
            if (!gStatus[0] || strstr(gStatus, "Connecting") == gStatus)
                snprintf(gStatus, sizeof(gStatus), "Could not load catalog. Check your Internet connection.");
            sOnlineLoadStage = ONLINE_STAGE_FAILED;
            return;
        }
        count = ParseOnlineManifest(sOnlineManifest, received);
        if (count > 0) SaveManifestCache(sOnlineManifest, received);
    }
    if (count > 0)
    {
        RebuildOnlineBrowserTiles();
        sOnlineLoadStage = ONLINE_STAGE_READY;
        snprintf(gStatus, sizeof(gStatus), "%d plugin(s) loaded%s", count,
                 sOnlineLoadedFromCache ? " (saved copy, SQUARE to refresh)" : " from GitHub");
    }
    else
    {
        if (!gStatus[0] || strstr(gStatus, "Connecting") == gStatus || strstr(gStatus, "Opening") == gStatus)
            snprintf(gStatus, sizeof(gStatus), "Catalog loaded but no valid PRX/SPRX entries were found");
        sOnlineLoadStage = ONLINE_STAGE_FAILED;
    }
}

bool OpenGetOnlineCatalog()
{
    BeginOnlineLoad(false);
    return true;
}

// ---- Progressive image loading: at most one image per frame, only what is visible ----
static bool LoadEntryArtworkIfNeeded(int entry)
{
    if (entry < 0 || entry >= gOnlineCount) return false;
    if (!gOnlineEntries[entry].image[0] || sOnlineImageTried[entry]) return false;
    LoadOnlineCatalogImage(entry);
    return true;
}

static bool LoadEntryQrIfNeeded(int entry)
{
    if (entry < 0 || entry >= gOnlineCount) return false;
    const GetOnlineEntry &e = gOnlineEntries[entry];
    if (e.qrSite[0] && !sOnlineQrSiteTried[entry]) { LoadOnlineQrImage(entry, true); return true; }
    if (e.qrDiscord[0] && !sOnlineQrDiscordTried[entry]) { LoadOnlineQrImage(entry, false); return true; }
    return false;
}

// Folder tiles show the category cover, or the first plugin picture of that folder.
// dryRun = only report whether something is still waiting to be loaded.
static bool FolderArtworkPending(const char *name, bool dryRun)
{
    int category = -1;
    for (int i = 0; i < sOnlineCategoryCount; ++i)
        if (strcasecmp(sOnlineCategories[i].name, name) == 0) { category = i; break; }
    if (category >= 0)
    {
        OnlineCategoryArtwork &cover = sOnlineCategories[category];
        if (cover.decoded.rgba) return false;
        if (cover.image[0] && !cover.tried)
        {
            if (!dryRun)
            {
                printf("[online-image:category artwork] category='%s' manifest='%s'\n", cover.name, cover.image);
                LoadOnlineCachedPng("category artwork", cover.image, &cover.decoded, &cover.tried);
            }
            return true;
        }
    }
    int firstUntried = -1;
    for (int i = 0; i < gOnlineCount; ++i)
    {
        if (strcasecmp(gOnlineEntries[i].category, name) != 0 || !gOnlineEntries[i].image[0]) continue;
        if (sOnlineImages[i].rgba) return false;
        if (!sOnlineImageTried[i] && firstUntried < 0) firstUntried = i;
    }
    if (firstUntried < 0) return false;
    if (!dryRun) LoadOnlineCatalogImage(firstUntried);
    return true;
}

static bool LoadTileArtworkIfNeeded(int tileIndex)
{
    if (tileIndex < 0 || tileIndex >= sOnlineBrowserTileCount) return false;
    const OnlineBrowserTile &tile = sOnlineBrowserTiles[tileIndex];
    if (tile.isFolder) return FolderArtworkPending(tile.label, false);
    return LoadEntryArtworkIfNeeded(tile.entryIndex);
}

static void LoadNextOnlineImage()
{
    if (gOnlineCount <= 0 || gOnlineConfirmOverwrite) return;
    if (sOnlineDetailsView)
    {
        if (LoadEntryArtworkIfNeeded(sOnlineSelectedEntry)) return;
        LoadEntryQrIfNeeded(sOnlineSelectedEntry);
        return;
    }
    // The highlighted item first, then the rest of what is visible.
    if (LoadTileArtworkIfNeeded(gOnlineCursor)) return;
    if (gOnlineCursor >= 0 && gOnlineCursor < sOnlineBrowserTileCount &&
        !sOnlineBrowserTiles[gOnlineCursor].isFolder &&
        LoadEntryQrIfNeeded(sOnlineBrowserTiles[gOnlineCursor].entryIndex))
        return;
    int first = gOnlineScroll, count = sOnlineCurrentFolder[0] ? 10 : GAME_PAGE_SIZE;
    for (int i = first; i < first + count && i < sOnlineBrowserTileCount; ++i)
        if (LoadTileArtworkIfNeeded(i)) return;
}

static bool DownloadOnlineFile(const char *relativePath, const char *target, size_t maxBytes)
{
    char url[768], temporary[640];
    if (!BuildRawUrl(relativePath, url, sizeof(url)))
    {
        snprintf(gStatus, sizeof(gStatus), "Invalid download path in catalog");
        return false;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.pm_download", target) >= (int)sizeof(temporary))
    {
        snprintf(gStatus, sizeof(gStatus), "Target path is too long");
        return false;
    }

    FILE *fp = fopen(temporary, "wb");
    if (!fp)
    {
        snprintf(gStatus, sizeof(gStatus), "Cannot create temporary download file");
        return false;
    }

    size_t downloaded = 0;
    bool ok = OnlineHttpGet(url, NULL, 0, fp, maxBytes, &downloaded);
    if (fflush(fp) != 0) ok = false;
    if (fclose(fp) != 0) ok = false;
    if (!ok || downloaded == 0)
    {
        unlink(temporary);
        if (strstr(gStatus, "Downloading ") == gStatus || !gStatus[0])
            snprintf(gStatus, sizeof(gStatus), "Download failed or returned an empty file");
        return false;
    }

    // Replace atomically after the new file has arrived completely.
    char backup[680];
    snprintf(backup, sizeof(backup), "%s.pm_backup", target);
    bool hadTarget = FileExists(target);
    if (hadTarget)
    {
        unlink(backup);
        if (rename(target, backup) != 0)
        {
            unlink(temporary);
            snprintf(gStatus, sizeof(gStatus), "Could not preserve the existing file");
            return false;
        }
    }
    if (rename(temporary, target) != 0)
    {
        if (hadTarget) rename(backup, target);
        unlink(temporary);
        snprintf(gStatus, sizeof(gStatus), "Could not install downloaded file");
        return false;
    }
    if (hadTarget) unlink(backup);
    return true;
}

static bool IsOnlineEntryDownloaded(int index)
{
    if (index < 0 || index >= gOnlineCount) return false;
    const GetOnlineEntry &entry = gOnlineEntries[index];
    const char *base = strrchr(entry.file, '/');
    base = base ? base + 1 : entry.file;
    char target[600];
    if (snprintf(target, sizeof(target), "%s/%s", PLUGIN_ROOT, base) >= (int)sizeof(target))
        return false;
    return FileExists(target);
}

static void InstallOnlineEntry(int index, bool overwrite)
{
    if (index < 0 || index >= gOnlineCount) return;
    if (!DirectoryExists(PLUGIN_ROOT))
    {
        snprintf(gStatus, sizeof(gStatus), "GoldHEN plugins folder not found");
        return;
    }

    GetOnlineEntry &entry = gOnlineEntries[index];
    const char *base = strrchr(entry.file, '/');
    base = base ? base + 1 : entry.file;
    char target[600];
    if (snprintf(target, sizeof(target), "%s/%s", PLUGIN_ROOT, base) >= (int)sizeof(target))
    {
        snprintf(gStatus, sizeof(gStatus), "Plugin filename is too long");
        return;
    }
    if (FileExists(target) && !overwrite)
    {
        gOnlinePendingIndex = index;
        gOnlineConfirmOverwrite = true;
        snprintf(gStatus, sizeof(gStatus), "Plugin already downloaded. X: replace, O: cancel");
        return;
    }

    snprintf(gStatus, sizeof(gStatus), "Downloading %s...", entry.title);
    if (!DownloadOnlineFile(entry.file, target, ONLINE_PLUGIN_MAX))
        return;

    // Download the catalog artwork under the plugin basename so the existing
    // plugin grid can discover it without changing its artwork lookup logic.
    if (entry.image[0])
    {
        char imageTarget[600];
        CopyText(imageTarget, sizeof(imageTarget), target);
        char *dot = strrchr(imageTarget, '.');
        if (dot) strcpy(dot, ".png");
        DownloadOnlineFile(entry.image, imageTarget, ONLINE_IMAGE_MAX);
    }

    if (entry.registrationRequired)
        snprintf(gStatus, sizeof(gStatus), "Downloaded %s. Website registration is required to use it.", entry.title);
    else
        snprintf(gStatus, sizeof(gStatus), "Downloaded %s to GoldHEN/plugins", entry.title);
    gOnlineConfirmOverwrite = false;
    gOnlinePendingIndex = -1;
}

static void HandleGetOnlineInput(uint32_t pressed)
{
    if (gOnlineConfirmOverwrite)
    {
        if (pressed & ORBIS_PAD_BUTTON_CROSS)
        {
            int index = gOnlinePendingIndex;
            gOnlineConfirmOverwrite = false;
            gOnlinePendingIndex = -1;
            InstallOnlineEntry(index, true);
        }
        else if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
        {
            gOnlineConfirmOverwrite = false;
            gOnlinePendingIndex = -1;
            snprintf(gStatus, sizeof(gStatus), "Download cancelled");
        }
        return;
    }

    if (sOnlineDetailsView)
    {
        if (pressed & ORBIS_PAD_BUTTON_CROSS)
        {
            if (sOnlineSelectedEntry >= 0 && sOnlineSelectedEntry < gOnlineCount)
                InstallOnlineEntry(sOnlineSelectedEntry, false);
        }
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
            sOnlineDetailsView = false;
        return;
    }

    if (sOnlineCurrentFolder[0])
    {
        // Inside a folder, the left panel is a conventional file list.
        if ((pressed & ORBIS_PAD_BUTTON_UP) && gOnlineCursor > 0) --gOnlineCursor;
        if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gOnlineCursor + 1 < sOnlineBrowserTileCount) ++gOnlineCursor;
    }
    else
    {
        // Root remains a three-column folder library, matching MY GAMES.
        if ((pressed & ORBIS_PAD_BUTTON_UP) && gOnlineCursor >= 3) gOnlineCursor -= 3;
        if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gOnlineCursor + 3 < sOnlineBrowserTileCount) gOnlineCursor += 3;
        if (sOnlineBrowserTileCount > 0 && (pressed & ORBIS_PAD_BUTTON_LEFT))
            gOnlineCursor = (gOnlineCursor + sOnlineBrowserTileCount - 1) % sOnlineBrowserTileCount;
        if (sOnlineBrowserTileCount > 0 && (pressed & ORBIS_PAD_BUTTON_RIGHT))
            gOnlineCursor = (gOnlineCursor + 1) % sOnlineBrowserTileCount;
    }
    if (sOnlineCurrentFolder[0])
    {
        const int visibleRows = 10;
        if (gOnlineCursor < gOnlineScroll) gOnlineScroll = gOnlineCursor;
        if (gOnlineCursor >= gOnlineScroll + visibleRows) gOnlineScroll = gOnlineCursor - visibleRows + 1;
    }
    else gOnlineScroll = (gOnlineCursor / GAME_PAGE_SIZE) * GAME_PAGE_SIZE;

    if (pressed & ORBIS_PAD_BUTTON_CROSS)
    {
        if (gOnlineCursor >= 0 && gOnlineCursor < sOnlineBrowserTileCount)
        {
            const OnlineBrowserTile &tile = sOnlineBrowserTiles[gOnlineCursor];
            if (tile.isFolder) OpenOnlineBrowserFolder(tile.path);
            else if (tile.entryIndex >= 0)
            {
                sOnlineSelectedEntry = tile.entryIndex;
                sOnlineDetailsView = true;
            }
        }
        else BeginOnlineLoad(true);
    }
    if (pressed & ORBIS_PAD_BUTTON_SQUARE)
        BeginOnlineLoad(true);
    if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
    {
        if (!LeaveOnlineBrowserFolder())
        {
            gGetView = 1;
            gStatus[0] = '\0';
        }
    }
}

void HandleGetOnlineAction(uint32_t pressed)
{
    if (sOnlineLoadStage == ONLINE_STAGE_LOADING)
    {
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
        {
            sOnlineLoadStage = ONLINE_STAGE_READY;
            gGetView = 1;
            gStatus[0] = '\0';
            return;
        }
        // Wait until the loading screen has really been shown, then do the blocking work.
        if (sOnlineLoadingDrawn) RunOnlineLoad();
        return;
    }
    if (sOnlineLoadStage == ONLINE_STAGE_FAILED)
    {
        if (pressed & (ORBIS_PAD_BUTTON_CROSS | ORBIS_PAD_BUTTON_SQUARE)) BeginOnlineLoad(true);
        else if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
        {
            sOnlineLoadStage = ONLINE_STAGE_READY;
            gGetView = 1;
            gStatus[0] = '\0';
        }
        return;
    }
    HandleGetOnlineInput(pressed);
    // Never decode/download while the cursor is auto-repeating across the list.
    if (!gNavFast) LoadNextOnlineImage();
}

// Loading / failure screen shown while the catalog list is being fetched.
static void DrawOnlineLoadingScreen(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    const bool failed = sOnlineLoadStage == ONLINE_STAGE_FAILED;
    DrawPanel(scene, 410, 250, 1100, 470, PANEL_BG);
    const int cx = 960, cy = 395;
    if (!failed)
    {
        static const int dx[12] = {0, 23, 40, 46, 40, 23, 0, -23, -40, -46, -40, -23};
        static const int dy[12] = {-46, -40, -23, 0, 23, 40, 46, 40, 23, 0, -23, -40};
        const int head = (sOnlineSpinnerTick / 2) % 12;
        for (int i = 0; i < 12; ++i)
        {
            const int age = (head - i + 12) % 12;
            Color c = age == 0 ? ACCENT : (age == 1 ? (Color){64, 134, 180}
                                  : (age == 2 ? (Color){48, 98, 132}
                                  : (age == 3 ? (Color){38, 74, 98} : (Color){34, 44, 60})));
            FillRound(scene, cx + dx[i] - 7, cy + dy[i] - 7, 14, 14, 7, c);
        }
        ++sOnlineSpinnerTick;
    }
    else
    {
        FillRound(scene, cx - 40, cy - 40, 80, 80, 40, RED_DIM);
        scene->DrawText((char *)"!", largeFont, cx - TextWidthPx("!", 42) / 2, cy + 15, RED_DIM, RED);
    }
    const char *title = failed ? "COULD NOT LOAD THE CATALOG" : "LOADING ONLINE CATALOG";
    scene->DrawText((char *)title, largeFont, cx - TextWidthPx(title, 42) / 2, 520, PANEL_BG, failed ? RED : WHITE);
    char status[260];
    ShortenToWidth(smallFont, gStatus, status, sizeof(status), 1000);
    scene->DrawText(status, smallFont, cx - TextWidth(smallFont, status) / 2, 580, PANEL_BG, AMBER);
    const char *hint = failed ? "Check that the console is online, then try again."
                              : "Please wait - the console has not frozen.";
    scene->DrawText((char *)hint, smallFont, cx - TextWidth(smallFont, hint) / 2, 632, PANEL_BG, WHITE);
    if (!failed)
    {
        const char *note = "The list is saved after the first load, so next time it opens instantly.";
        scene->DrawText((char *)note, smallFont, cx - TextWidth(smallFont, note) / 2, 678, PANEL_BG, MUTED);
        DrawPanel(scene, 48, 934, 1824, 98, PANEL_BG);
        const char *wait = "PLEASE WAIT...";
        scene->DrawText((char *)wait, smallFont, 960 - TextWidth(smallFont, wait) / 2, 990, PANEL_BG, MUTED);
        sOnlineLoadingDrawn = true;
    }
    else
    {
        const LegendButton legend[] = {{"X", "TRY AGAIN"}, {"O", "BACK"}};
        DrawButtonLegend(scene, smallFont, legend, 2);
    }
}

void DrawGetOnline(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    // Keep the Internet view self-contained: unlike the three main sections,
    // it is a browsing surface and deliberately has no oversized tab strip.
    scene->FrameBufferFill(BG);
    DrawTopBar(scene, largeFont, smallFont, "Plugin Manager", "GET PLUGINS  /  INTERNET");
    if (sOnlineLoadStage != ONLINE_STAGE_READY)
    {
        DrawOnlineLoadingScreen(scene, largeFont, smallFont);
        return;
    }
    if (sOnlineDetailsView && sOnlineSelectedEntry >= 0 && sOnlineSelectedEntry < gOnlineCount)
    {
        const GetOnlineEntry &entry = gOnlineEntries[sOnlineSelectedEntry];
        DrawPanel(scene, 55, 115, 1810, 730, PANEL_BG);
        scene->DrawText((char *)entry.title, largeFont, 105, 180, PANEL_BG, WHITE);
        char byline[180];
        snprintf(byline, sizeof(byline), "%s  /  %s", entry.category, entry.author);
        scene->DrawText(byline, smallFont, 108, 225, PANEL_BG, MUTED);
        if (sOnlineImages[sOnlineSelectedEntry].rgba)
        {
            const PngImage &image = sOnlineImages[sOnlineSelectedEntry];
            const int boxW = 640, boxH = 390;
            float sx = (float)boxW / image.width, sy = (float)boxH / image.height;
            float scale = sx < sy ? sx : sy;
            int w = (int)(image.width * scale), h = (int)(image.height * scale);
            DrawImage(scene, image, 105 + (boxW - w) / 2, 270 + (boxH - h) / 2, w, h, PANEL_BG);
        }
        else scene->DrawText((char *)(entry.image[0] && !sOnlineImageTried[sOnlineSelectedEntry] ? "Loading artwork..." : "Artwork unavailable"),
                             smallFont, 300, 475, PANEL_BG, MUTED);
        scene->DrawText((char *)"DESCRIPTION", smallFont, 850, 310, PANEL_BG, MUTED);
        int descriptionLines = DrawWrappedCatalogText(scene, entry.description, smallFont,
                                                      850, 350, 76, 29, PANEL_BG, WHITE);
        int infoY = 370 + descriptionLines * 29;
        if (entry.websiteUrl[0])
        {
            char websiteLine[300];
            snprintf(websiteLine, sizeof(websiteLine), "WEBSITE: %s", entry.websiteUrl);
            scene->DrawText(websiteLine, smallFont, 850, infoY, PANEL_BG, ACCENT);
            infoY += 42;
        }
        const PngImage &siteQr = sOnlineQrSiteImages[sOnlineSelectedEntry];
        const PngImage &discordQr = sOnlineQrDiscordImages[sOnlineSelectedEntry];
        if (siteQr.rgba || discordQr.rgba)
        {
            int qrY = infoY + 12;
            
            if (siteQr.rgba)
            {
                scene->DrawText((char *)"WEBSITE", smallFont, 850, qrY + 38, PANEL_BG, WHITE);
                float scale = 150.0f / (siteQr.width > siteQr.height ? siteQr.width : siteQr.height);
                int w = (int)(siteQr.width * scale), h = (int)(siteQr.height * scale);
                DrawImage(scene, siteQr, 850 + (150 - w) / 2, qrY + 55 + (150 - h) / 2, w, h, PANEL_BG);
            }
            if (discordQr.rgba)
            {
                int discordX = siteQr.rgba ? 1040 : 850;
                scene->DrawText((char *)"DISCORD", smallFont, discordX, qrY + 38, PANEL_BG, WHITE);
                float scale = 150.0f / (discordQr.width > discordQr.height ? discordQr.width : discordQr.height);
                int w = (int)(discordQr.width * scale), h = (int)(discordQr.height * scale);
                DrawImage(scene, discordQr, discordX + (150 - w) / 2, qrY + 55 + (150 - h) / 2, w, h, PANEL_BG);
            }
        }
        scene->DrawText(gStatus, smallFont, 105, 880, BG, AMBER);
        const LegendButton detailsLegend[] = {{"X", "DOWNLOAD / INSTALL"}, {"O", "BACK"}};
        DrawButtonLegend(scene, smallFont, detailsLegend, 2);
        return;
    }

    char heading[160];
    if (sOnlineCurrentFolder[0])
        snprintf(heading, sizeof(heading), "ONLINE CATALOG  /  %s", OnlineBrowserFolderLabel());
    else
        snprintf(heading, sizeof(heading), "ONLINE CATALOG  /  FOLDERS");
    scene->DrawText(heading, largeFont, 70, 125, BG, WHITE);
    char countText[96];
    snprintf(countText, sizeof(countText), "%d item(s)  |  Source: GitHub", sOnlineBrowserTileCount);
    scene->DrawText(countText, smallFont, 72, 170, BG, MUTED);

    if (sOnlineCurrentFolder[0])
    {
        // File-browser view: the list is intentionally fixed to the left so
        // D-pad navigation never competes with the informational panel.
        const int leftX = 55, leftW = 455, rightX = 530, rightW = 1335;
        const int panelY = 195, panelH = 690;
        DrawPanel(scene, leftX, panelY, leftW, panelH, PANEL_BG);
        DrawPanel(scene, rightX, panelY, rightW, panelH, PANEL_BG);
        scene->DrawText((char *)"FILES", smallFont, leftX + 28, panelY + 38, PANEL_BG, MUTED);
        scene->DrawText((char *)"INFORMATION", smallFont, rightX + 30, panelY + 38, PANEL_BG, ACCENT);

        const int visibleRows = 10;
        for (int row = 0; row < visibleRows; ++row)
        {
            int index = gOnlineScroll + row;
            if (index >= sOnlineBrowserTileCount) break;
            const OnlineBrowserTile &tile = sOnlineBrowserTiles[index];
            int y = panelY + 66 + row * 64;
            bool active = index == gOnlineCursor;
            Color fill = active ? CARD_ALT : CARD;
            scene->DrawRectangle(leftX + 14, y - 8, leftW - 28, 56, fill);
            if (active)
                scene->DrawRectangle(leftX + 14, y - 8, 5, 56, ACCENT);

            if (!tile.isFolder && tile.entryIndex >= 0)
            {
                const PngImage &thumb = sOnlineImages[tile.entryIndex];
                if (thumb.rgba && thumb.width > 0 && thumb.height > 0)
                {
                    const int box = 54;
                    float scale = (float)box / (thumb.width > thumb.height ? thumb.width : thumb.height);
                    int w = (int)(thumb.width * scale), h = (int)(thumb.height * scale);
                    DrawImage(scene, thumb, leftX + 30 + (box - w) / 2, y - 3 + (box - h) / 2, w, h, fill);
                }
            }
            else
                scene->DrawText((char *)"DIR", smallFont, leftX + 30, y + 26, fill, active ? ACCENT : AMBER);
            char label[112];
            ShortenText(tile.label, label, sizeof(label), 26);
            // Render each label once to keep glyph edges crisp (no offset
            // overdraw). Selected and idle rows use distinct high-contrast text.
            scene->DrawText(label, smallFont, leftX + 92, y + 27, fill,
                            active ? WHITE : (Color){205, 215, 228});
        }

        if (gOnlineCursor >= 0 && gOnlineCursor < sOnlineBrowserTileCount)
        {
            const OnlineBrowserTile &selected = sOnlineBrowserTiles[gOnlineCursor];
            if (selected.isFolder)
            {
                scene->DrawText((char *)selected.label, largeFont, rightX + 35, panelY + 105, PANEL_BG, WHITE);
                scene->DrawText((char *)"Folder", smallFont, rightX + 38, panelY + 150, PANEL_BG, MUTED);
            }
            else if (selected.entryIndex >= 0)
            {
                const GetOnlineEntry &entry = gOnlineEntries[selected.entryIndex];
                scene->DrawText((char *)entry.title, largeFont, rightX + 35, panelY + 105, PANEL_BG, WHITE);
                char metadata[180];
                snprintf(metadata, sizeof(metadata), "%s", entry.author[0] ? entry.author : "");
                scene->DrawText(metadata, smallFont, rightX + 38, panelY + 150, PANEL_BG, MUTED);
                if (entry.websiteUrl[0])
                {
                    char websiteLine[300];
                    snprintf(websiteLine, sizeof(websiteLine), "WEBSITE: %s", entry.websiteUrl);
                    scene->DrawText(websiteLine, smallFont, rightX + 38, panelY + 178, PANEL_BG, ACCENT);
                }
                const PngImage &image = sOnlineImages[selected.entryIndex];
                if (image.rgba && image.width > 0 && image.height > 0)
                {
                    // Larger artwork well in the expanded information panel.
                    const int boxW = 260, boxH = 260;
                    float sx = (float)boxW / image.width, sy = (float)boxH / image.height;
                    float scale = sx < sy ? sx : sy;
                    int w = (int)(image.width * scale), h = (int)(image.height * scale);
                    DrawImage(scene, image, rightX + 35 + (boxW - w) / 2, panelY + 205 + (boxH - h) / 2, w, h, PANEL_BG);
                }
                else scene->DrawText((char *)(entry.image[0] && !sOnlineImageTried[selected.entryIndex] ? "Loading artwork..." : "Artwork unavailable"),
                                     smallFont, rightX + 75, panelY + 335, PANEL_BG, MUTED);
                scene->DrawText((char *)"DESCRIPTION", smallFont, rightX + 390, panelY + 205, PANEL_BG, ACCENT);
                int descriptionLines = DrawWrappedCatalogText(scene, entry.description, smallFont,
                                                              rightX + 390, panelY + 250,
                                                              72, 28, PANEL_BG, WHITE);
                int descriptionEndY = panelY + 250 + descriptionLines * 28;
                if (IsOnlineEntryDownloaded(selected.entryIndex))
                {
                    scene->DrawText((char *)"DOWNLOADED", smallFont,
                                    rightX + 390, descriptionEndY + 30, PANEL_BG, GREEN);
                    descriptionEndY += 58;
                }
                int connectY = descriptionEndY + 35;
                const PngImage &siteQr = sOnlineQrSiteImages[selected.entryIndex];
                const PngImage &discordQr = sOnlineQrDiscordImages[selected.entryIndex];
                bool hasSiteQr = siteQr.rgba && siteQr.width > 0 && siteQr.height > 0;
                bool hasDiscordQr = discordQr.rgba && discordQr.width > 0 && discordQr.height > 0;
                int qrY = connectY;
                if (qrY < panelY + 430) qrY = panelY + 430;
                if (hasSiteQr || hasDiscordQr)
                {
                    int siteX = rightX + 390;
                    int discordX = rightX + 560;
                    if (hasSiteQr && !hasDiscordQr) siteX = rightX + 500;
                    if (!hasSiteQr && hasDiscordQr) discordX = rightX + 500;
                    if (hasSiteQr)
                    {
                        scene->DrawText((char *)"WEBSITE", smallFont, siteX + 4, qrY + 48, PANEL_BG, WHITE);
                        float scale = 150.0f / (siteQr.width > siteQr.height ? siteQr.width : siteQr.height);
                        int w = (int)(siteQr.width * scale), h = (int)(siteQr.height * scale);
                        DrawImage(scene, siteQr, siteX + (150 - w) / 2, qrY + 66 + (150 - h) / 2, w, h, PANEL_BG);
                    }
                    if (hasDiscordQr)
                    {
                        scene->DrawText((char *)"DISCORD", smallFont, discordX + 4, qrY + 48, PANEL_BG, WHITE);
                        float scale = 150.0f / (discordQr.width > discordQr.height ? discordQr.width : discordQr.height);
                        int w = (int)(discordQr.width * scale), h = (int)(discordQr.height * scale);
                        DrawImage(scene, discordQr, discordX + (150 - w) / 2, qrY + 66 + (150 - h) / 2, w, h, PANEL_BG);
                    }
                }
                if (entry.registrationRequired)
                {
                    int noticeY = qrY + ((hasSiteQr || hasDiscordQr) ? 255 : 24);
                    scene->DrawText((char *)"Website registration is required before using this service.",
                                    smallFont, rightX + 390, noticeY, PANEL_BG, RED);
                }
            }
        }
        scene->DrawText(gStatus, smallFont, 75, 915, BG, AMBER);
        const LegendButton folderLegend[] = {{"ARROWS", "BROWSE FILES"}, {"X", "OPEN"},
                                             {"SQUARE", "REFRESH"}, {"O", "BACK"}};
        DrawButtonLegend(scene, smallFont, folderLegend, 4);
        return;
    }

    const int pageStart = (gOnlineCursor / GAME_PAGE_SIZE) * GAME_PAGE_SIZE;
    // Spacious category tiles: artwork first, crisp large title below.
    const int cardW = 590, cardH = 248, startX = 55, startY = 190, gapX = 20, gapY = 16;
    for (int slot = 0; slot < GAME_PAGE_SIZE; ++slot)
    {
        int index = pageStart + slot;
        if (index >= sOnlineBrowserTileCount) break;
        int x = startX + (slot % 3) * (cardW + gapX);
        int y = startY + (slot / 3) * (cardH + gapY);
        bool active = index == gOnlineCursor;
        const OnlineBrowserTile &tile = sOnlineBrowserTiles[index];
        // Each category is a distinct soft card instead of floating artwork.
        // The RGB surface is intentionally close to the page background.
        Color fill = active ? CARD_ALT : CARD;
        DrawPanel(scene, x, y, cardW, cardH, fill);
        if (active)
            scene->DrawRectangle(x + 2, y + 2, 4, cardH - 4, ACCENT);
        const int boxW = 340, boxH = 180;
        if (tile.isFolder)
        {
            const PngImage *categoryImage = FindOnlineCategoryArtwork(tile.label);
            if (categoryImage && categoryImage->width > 0 && categoryImage->height > 0)
            {
                float sx = (float)boxW / categoryImage->width, sy = (float)boxH / categoryImage->height;
                float scale = sx < sy ? sx : sy;
                int w = (int)(categoryImage->width * scale), h = (int)(categoryImage->height * scale);
                DrawImage(scene, *categoryImage, x + (cardW - w) / 2, y + 4 + (boxH - h) / 2, w, h, fill);
            }
            else
            {
                // A folder glyph is used only when the manifest has no usable
                // category image (or its download/decode failed).
                scene->DrawText((char *)(FolderArtworkPending(tile.label, true) ? "LOADING..." : "FOLDER"),
                                smallFont, x + 245, y + 95, fill, active ? ACCENT : MUTED);
            }
        }
        else
        {
            const PngImage &image = sOnlineImages[tile.entryIndex];
            if (image.rgba && image.width > 0 && image.height > 0)
            {
                float sx = (float)boxW / image.width, sy = (float)boxH / image.height;
                float scale = sx < sy ? sx : sy;
                int w = (int)(image.width * scale), h = (int)(image.height * scale);
                DrawImage(scene, image, x + 16 + (boxW - w) / 2, y + 13 + (boxH - h) / 2, w, h, fill);
            }
            else scene->DrawText((char *)(gOnlineEntries[tile.entryIndex].image[0] && !sOnlineImageTried[tile.entryIndex] ? "LOADING..." : "NO ARTWORK"),
                                 smallFont, x + 210, y + 83, fill, MUTED);
        }
        char title[120];
        ShortenText(tile.label, title, sizeof(title), 34);
        // Draw category names once with the 42px face. The old offset
        // duplicate draws caused a soft/doubled edge that looked low quality.
        int estimatedTextWidth = (int)strlen(title) * 23;
        int titleX = x + (cardW - estimatedTextWidth) / 2;
        if (titleX < x + 18) titleX = x + 18;
        Color titleColor = active ? ACCENT : WHITE;
        scene->DrawText(title, largeFont, titleX, y + 232, fill, titleColor);
    }
    if (sOnlineBrowserTileCount > GAME_PAGE_SIZE)
    {
        char pageText[64];
        snprintf(pageText, sizeof(pageText), "PAGE %d / %d", pageStart / GAME_PAGE_SIZE + 1,
                 (sOnlineBrowserTileCount + GAME_PAGE_SIZE - 1) / GAME_PAGE_SIZE);
        scene->DrawText(pageText, smallFont, 1630, 925, BG, MUTED);
    }

    if (gOnlineConfirmOverwrite)
    {
        DrawPanel(scene, 360, 420, 1200, 210, HEADER);
        scene->DrawText((char *)"PLUGIN ALREADY DOWNLOADED", largeFont, 410, 475, HEADER, AMBER);
        scene->DrawText((char *)"Press X to replace the existing file, or O to cancel.",
                        smallFont, 410, 545, HEADER, WHITE);
    }

    scene->DrawText(gStatus, smallFont, 75, 925, BG, AMBER);
    const LegendButton legend[] = {{"ARROWS", "BROWSE"}, {"X", "OPEN"},
                                   {"SQUARE", "REFRESH"}, {"O", "BACK"}};
    DrawButtonLegend(scene, smallFont, legend, 4);
}
