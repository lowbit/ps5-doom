#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "sce.h"

#define NET_POOL (1024 * 1024)
#define SSL_POOL (304 * 1024)
#define HTTP_POOL (4 * 1024 * 1024)
#define HTTP_1_1 2
#define METHOD_GET 0
#define HEADER_OVERWRITE 0
#define TIMEOUT_US 15000000
#define VERIFY_FLAGS (0x01 | 0x04 | 0x08 | 0x10 | 0x20 | 0x80)

struct plat_http
{
    int connection, request;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int template_id = -1;

static int start_http(void)
{
    int pool, ssl, http, result;

    pool = sceNetPoolCreate("doom", NET_POOL, 0);
    if (pool < 0)
        return pool;
    ssl = sceSslInit(SSL_POOL);
    if (ssl < 0)
        return ssl;
    http = sceHttpInit(pool, ssl, HTTP_POOL);
    if (http < 0)
        return http;
    template_id = sceHttpCreateTemplate(http, "DOOM-PS5", HTTP_1_1, 0);
    if (template_id < 0)
        return template_id;
    if ((result = sceHttpSetAutoRedirect(template_id, 1)) < 0 ||
        (result = sceHttpSetResolveTimeOut(template_id, TIMEOUT_US)) < 0 ||
        (result = sceHttpSetConnectTimeOut(template_id, TIMEOUT_US)) < 0 ||
        (result = sceHttpSetSendTimeOut(template_id, TIMEOUT_US)) < 0 ||
        (result = sceHttpSetRecvTimeOut(template_id, TIMEOUT_US)) < 0 ||
        (result = sceHttpsEnableOption(template_id, VERIFY_FLAGS)) < 0)
        return result;
    return 0;
}

static void header_value(int request, const char *name, char *out, size_t size)
{
    char *headers = NULL;
    const char *value = NULL;
    size_t headers_size = 0, value_size = 0;

    out[0] = 0;
    if (sceHttpGetAllResponseHeaders(request, &headers, &headers_size) < 0 ||
        sceHttpParseResponseHeader(headers, headers_size, name, &value, &value_size) < 0)
        return;
    if (value_size >= size)
        value_size = size - 1;
    memcpy(out, value, value_size);
    out[value_size] = 0;
}

plat_http_t *plat_http_get(const char *url, uint64_t offset, plat_http_info_t *info,
                           char *error, int error_size)
{
    plat_http_t *http;
    char range[40];
    uint64_t length = 0;
    int status = 0, has_length = -1, result;

    pthread_mutex_lock(&lock);
    result = template_id < 0 ? start_http() : 0;
    pthread_mutex_unlock(&lock);
    if (result < 0)
    {
        snprintf(error, error_size, "network setup failed (0x%08x)", (unsigned)result);
        return NULL;
    }

    http = calloc(1, sizeof(*http));
    if (!http)
        return NULL;
    http->request = -1;
    http->connection = result = sceHttpCreateConnectionWithURL(template_id, url, 1);
    if (result >= 0)
        http->request = result = sceHttpCreateRequestWithURL(http->connection, METHOD_GET, url, 0);
    if (result >= 0 && offset)
    {
        snprintf(range, sizeof(range), "bytes=%llu-", (unsigned long long)offset);
        result = sceHttpAddRequestHeader(http->request, "Range", range, HEADER_OVERWRITE);
    }
    if (result >= 0)
        result = sceHttpSendRequest(http->request, NULL, 0);
    if (result >= 0)
        result = sceHttpGetStatusCode(http->request, &status);
    if (result < 0)
    {
        snprintf(error, error_size, "connection failed (0x%08x)", (unsigned)result);
        plat_http_close(http);
        return NULL;
    }
    if (status != 200 && status != 206)
    {
        snprintf(error, error_size, "the server answered %d", status);
        plat_http_close(http);
        return NULL;
    }
    sceHttpGetResponseContentLength(http->request, &has_length, &length);
    info->status = status;
    info->length = has_length == 0 ? (int64_t)length : -1;
    header_value(http->request, "Content-Type", info->type, sizeof(info->type));
    return http;
}

int plat_http_read(plat_http_t *http, void *buffer, int size)
{
    int got = sceHttpReadData(http->request, buffer, (size_t)size);

    if (got < 0)
        plat_log("http: read failed (0x%08x)\n", (unsigned)got);
    return got < 0 ? -1 : got;
}

void plat_http_close(plat_http_t *http)
{
    if (!http)
        return;
    if (http->request >= 0)
        sceHttpDeleteRequest(http->request);
    if (http->connection >= 0)
        sceHttpDeleteConnection(http->connection);
    free(http);
}
