#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#define BUFFER_LIMIT (1 << 20)

struct plat_http
{
    CURL *easy;
    CURLM *multi;
    char *data;
    size_t used, read_at;
    int paused, finished;
    CURLcode result;
};

static pthread_once_t once = PTHREAD_ONCE_INIT;

static void start_curl(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

static size_t receive(char *chunk, size_t size, size_t count, void *user)
{
    plat_http_t *http = user;
    size_t bytes = size * count;
    char *grown;

    if (http->used - http->read_at >= BUFFER_LIMIT)
    {
        http->paused = 1;
        return CURL_WRITEFUNC_PAUSE;
    }
    if (http->read_at)
    {
        memmove(http->data, http->data + http->read_at, http->used - http->read_at);
        http->used -= http->read_at;
        http->read_at = 0;
    }
    grown = realloc(http->data, http->used + bytes);
    if (!grown)
        return 0;
    http->data = grown;
    memcpy(http->data + http->used, chunk, bytes);
    http->used += bytes;
    return bytes;
}

static void pump(plat_http_t *http)
{
    CURLMsg *message;
    int running, left;

    if (http->paused)
    {
        http->paused = 0;
        curl_easy_pause(http->easy, CURLPAUSE_CONT);
    }
    curl_multi_perform(http->multi, &running);
    while ((message = curl_multi_info_read(http->multi, &left)))
        if (message->msg == CURLMSG_DONE)
        {
            http->finished = 1;
            http->result = message->data.result;
        }
    if (!http->finished && http->used == http->read_at)
        curl_multi_poll(http->multi, NULL, 0, 100, NULL);
}

plat_http_t *plat_http_get(const char *url, uint64_t offset, plat_http_info_t *info,
                           char *error, int error_size)
{
    plat_http_t *http = calloc(1, sizeof(*http));
    char range[32];
    long status = 0;
    curl_off_t length = -1;
    const char *type = NULL, *final = NULL;

    pthread_once(&once, start_curl);
    if (!http)
        return NULL;
    http->easy = curl_easy_init();
    http->multi = curl_multi_init();
    curl_easy_setopt(http->easy, CURLOPT_URL, url);
    curl_easy_setopt(http->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(http->easy, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(http->easy, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(http->easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(http->easy, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(http->easy, CURLOPT_USERAGENT, "DOOM-PS5");
    curl_easy_setopt(http->easy, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(http->easy, CURLOPT_WRITEDATA, http);
    if (offset)
    {
        snprintf(range, sizeof(range), "%llu-", (unsigned long long)offset);
        curl_easy_setopt(http->easy, CURLOPT_RANGE, range);
    }
    curl_multi_add_handle(http->multi, http->easy);

    while (!http->finished && !http->used)
        pump(http);
    curl_easy_getinfo(http->easy, CURLINFO_RESPONSE_CODE, &status);
    if (http->finished && http->result != CURLE_OK)
    {
        snprintf(error, error_size, "%s", curl_easy_strerror(http->result));
        plat_http_close(http);
        return NULL;
    }
    if (status != 200 && status != 206)
    {
        snprintf(error, error_size, "the server answered %ld", status);
        plat_http_close(http);
        return NULL;
    }
    curl_easy_getinfo(http->easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    curl_easy_getinfo(http->easy, CURLINFO_CONTENT_TYPE, &type);
    curl_easy_getinfo(http->easy, CURLINFO_EFFECTIVE_URL, &final);
    info->status = (int)status;
    info->length = length;
    snprintf(info->type, sizeof(info->type), "%s", type ? type : "");
    snprintf(info->url, sizeof(info->url), "%s", final ? final : url);
    return http;
}

int plat_http_read(plat_http_t *http, void *buffer, int size)
{
    size_t available;

    while (http->used == http->read_at && !http->finished)
        pump(http);
    available = http->used - http->read_at;
    if (!available)
        return http->result == CURLE_OK ? 0 : -1;
    if (available > (size_t)size)
        available = (size_t)size;
    memcpy(buffer, http->data + http->read_at, available);
    http->read_at += available;
    return (int)available;
}

void plat_http_close(plat_http_t *http)
{
    if (!http)
        return;
    curl_multi_remove_handle(http->multi, http->easy);
    curl_easy_cleanup(http->easy);
    curl_multi_cleanup(http->multi);
    free(http->data);
    free(http);
}
