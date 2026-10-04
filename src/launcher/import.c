#include <archive.h>
#include <archive_entry.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "games.h"
#include "import.h"
#include "platform.h"
#include "url.h"

#define CHUNK (256 * 1024)
#define PEEK (64 * 1024)
#define LISTING_LIMIT (2 * 1024 * 1024)
#define DISCARD_LIMIT (2 * 1024 * 1024)

typedef struct
{
    int remote;
    const char *url;
    char final_url[LINK_URL]; // where url led after redirects
    plat_http_t *http;
    int fd;
    long long position, size;
    int ranges;
    char type[64];
    uint8_t *pending;
    int pending_length, pending_offset;
} source_t;

typedef int (*fill_fn)(void *context, void *buffer, int size);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static int worker_running;
static volatile int cancelled;
static import_status_t status;
static listing_t listing;
static char sources[MAX_SOURCES][LINK_URL];
static int source_count;
static char dest_dir[GAME_PATH];
static uint8_t peek_buffer[PEEK], read_buffer[CHUNK], data_buffer[CHUNK], discard_buffer[CHUNK];

static void fail(const char *format, ...)
{
    va_list args;

    pthread_mutex_lock(&lock);
    va_start(args, format);
    vsnprintf(status.message, sizeof(status.message), format, args);
    va_end(args);
    pthread_mutex_unlock(&lock);
    plat_log("import: %s\n", status.message);
}

static void upper(char *out, size_t size, const char *text)
{
    size_t i;

    for (i = 0; text[i] && i + 1 < size; i++)
        out[i] = (char)toupper((unsigned char)text[i]);
    out[i] = 0;
}

static void add_found(const char *file)
{
    char name[32];

    upper(name, sizeof(name), file);
    pthread_mutex_lock(&lock);
    if (!strstr(status.found, name))
    {
        if (status.found[0])
            strncat(status.found, " ", sizeof(status.found) - strlen(status.found) - 1);
        strncat(status.found, name, sizeof(status.found) - strlen(status.found) - 1);
        status.found_count++;
    }
    status.current[0] = 0;
    pthread_mutex_unlock(&lock);
}

static long long logical(const source_t *s)
{
    return s->position - (s->pending_length - s->pending_offset);
}

static int open_remote(source_t *s, long long offset)
{
    plat_http_info_t info;
    char error[128];

    plat_http_close(s->http);
    s->http = plat_http_get(s->final_url[0] ? s->final_url : s->url, (uint64_t)offset, &info, error,
                            sizeof(error));
    if (!s->http)
    {
        fail("%s", error);
        return -1;
    }
    snprintf(s->final_url, sizeof(s->final_url), "%s", info.url);
    if (info.status == 206)
    {
        s->ranges = 1;
        s->position = offset;
        if (s->size < 0 && info.length >= 0)
            s->size = offset + info.length;
    }
    else
    {
        if (offset)
            s->ranges = 0;
        s->position = 0;
        if (info.length >= 0)
            s->size = info.length;
    }
    if (!offset)
        snprintf(s->type, sizeof(s->type), "%s", info.type);
    plat_log("import: request from %lld answered %d with %lld bytes\n", offset, info.status,
             (long long)info.length);
    return 0;
}

static int raw_read(source_t *s, void *out, int size)
{
    int got;

    if (cancelled)
        return -1;
    if (s->pending_offset < s->pending_length)
    {
        got = s->pending_length - s->pending_offset;
        if (got > size)
            got = size;
        memcpy(out, s->pending + s->pending_offset, (size_t)got);
        s->pending_offset += got;
        return got;
    }
    if (s->remote)
        got = s->http ? plat_http_read(s->http, out, size) : 0;
    else
        got = (int)read(s->fd, out, (size_t)size);
    if (got > 0)
    {
        s->position += got;
        pthread_mutex_lock(&lock);
        status.done = s->position;
        pthread_mutex_unlock(&lock);
    }
    return got;
}

static int discard(source_t *s, long long count)
{
    while (count > 0)
    {
        int got = raw_read(s, discard_buffer, count < CHUNK ? (int)count : CHUNK);

        if (got <= 0)
            return -1;
        count -= got;
    }
    return 0;
}

static int source_seek(source_t *s, long long target)
{
    long long here = logical(s);

    if (target == here)
        return 0;
    if (!s->remote)
    {
        s->pending_length = s->pending_offset = 0;
        if (lseek(s->fd, target, SEEK_SET) != target)
            return -1;
        s->position = target;
        return 0;
    }
    if (target > here && (target - here <= DISCARD_LIMIT || s->ranges == 0))
        return discard(s, target - here);
    s->pending_length = s->pending_offset = 0;
    if (s->size >= 0 && target >= s->size)
    {
        plat_http_close(s->http);
        s->http = NULL;
        s->position = target;
        return 0;
    }
    if (open_remote(s, s->ranges == 0 ? 0 : target))
        return -1;
    return discard(s, target - s->position);
}

static la_ssize_t on_read(struct archive *a, void *user, const void **buffer)
{
    int got = raw_read(user, read_buffer, CHUNK);

    if (got < 0)
    {
        archive_set_error(a, EIO, cancelled ? "cancelled" : "read failed");
        return -1;
    }
    *buffer = read_buffer;
    return got;
}

static la_int64_t on_seek(struct archive *a, void *user, la_int64_t offset, int whence)
{
    source_t *s = user;
    long long target = offset;

    (void)a;
    if (whence == SEEK_CUR)
        target += logical(s);
    else if (whence == SEEK_END)
    {
        if (s->size < 0)
            return ARCHIVE_FATAL;
        target += s->size;
    }
    if (target < 0 || source_seek(s, target))
        return ARCHIVE_FATAL;
    return target;
}

static int fill_raw(void *context, void *buffer, int size)
{
    return raw_read(context, buffer, size);
}

static int fill_archive(void *context, void *buffer, int size)
{
    la_ssize_t got = archive_read_data(context, buffer, (size_t)size);

    if (got < 0)
        plat_log("import: %s\n", archive_error_string(context) ? archive_error_string(context) : "read error");
    return got < 0 ? -1 : (int)got;
}

static int write_all(int fd, const uint8_t *data, int size)
{
    while (size > 0)
    {
        ssize_t written = write(fd, data, (size_t)size);

        if (written <= 0)
            return -1;
        data += written;
        size -= (int)written;
    }
    return 0;
}

static int already_have(const char *path, long long size)
{
    int fd = open(path, O_RDONLY);
    long long existing;

    if (fd < 0)
        return 0;
    existing = lseek(fd, 0, SEEK_END);
    close(fd);
    return size >= 0 && existing == size && games_check_wad(path, NULL) == 0;
}

static int write_game(const char *file, fill_fn fill, void *context, long long size)
{
    char path[GAME_PATH], part[GAME_PATH + 8], name[32];
    int fd, got;

    snprintf(path, sizeof(path), "%s/%s", dest_dir, file);
    snprintf(part, sizeof(part), "%s.part", path);
    upper(name, sizeof(name), file);
    if (already_have(path, size))
    {
        add_found(file);
        return 0;
    }
    pthread_mutex_lock(&lock);
    snprintf(status.current, sizeof(status.current), "%s", name);
    pthread_mutex_unlock(&lock);

    fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        fail("cannot write %s (error %d)", part, errno);
        return -1;
    }
    while ((got = fill(context, data_buffer, CHUNK)) > 0)
        if (write_all(fd, data_buffer, got))
        {
            fail(errno == ENOSPC ? "not enough space for %s" : "writing %s failed", name);
            close(fd);
            unlink(part);
            return -1;
        }
    close(fd);
    if (got < 0)
    {
        if (!cancelled)
            fail("reading %s failed", name);
        unlink(part);
        return -1;
    }
    if (games_check_wad(part, NULL))
    {
        fail("%s is not a valid game wad", name);
        unlink(part);
        return -1;
    }
    unlink(path);
    if (rename(part, path))
    {
        fail("cannot rename %s (error %d)", part, errno);
        unlink(part);
        return -1;
    }
    plat_log("import: wrote %s\n", path);
    add_found(file);
    return 0;
}

static int read_listing(source_t *s)
{
    char base[LINK_URL];
    char *html = malloc(LISTING_LIMIT + 1);
    int used = 0, got;

    if (!html)
        return -1;
    while (used < LISTING_LIMIT && (got = raw_read(s, html + used, LISTING_LIMIT - used)) > 0)
        used += got;
    html[used] = 0;
    url_directory(base, sizeof(base), s->final_url);
    listing_parse(&listing, base, html, (size_t)used);
    free(html);
    pthread_mutex_lock(&lock);
    status.state = IMPORT_LISTING;
    pthread_mutex_unlock(&lock);
    return 0;
}

static int extract(source_t *s, const char *name)
{
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    int result = 0, r;

    archive_read_support_format_zip(a);
    archive_read_support_format_7zip(a);
    archive_read_support_format_rar(a);
    archive_read_support_format_rar5(a);
    archive_read_set_read_callback(a, on_read);
    archive_read_set_seek_callback(a, on_seek);
    archive_read_set_callback_data(a, s);
    if (archive_read_open1(a) != ARCHIVE_OK)
    {
        if (!cancelled)
            fail("%s is not a wad, zip, 7z or rar file", name);
        archive_read_free(a);
        return -1;
    }
    while (!cancelled && (r = archive_read_next_header(a, &entry)) == ARCHIVE_OK)
    {
        const char *path = archive_entry_pathname_utf8(entry);
        const char *base, *file;

        if (!path)
            path = archive_entry_pathname(entry);
        if (!path || archive_entry_filetype(entry) != AE_IFREG)
            continue;
        base = path + strlen(path);
        while (base > path && base[-1] != '/' && base[-1] != '\\')
            base--;
        file = games_known_file(base);
        if (!file || !strcmp(file, "doom1.wad"))
            continue;
        if (write_game(file, fill_archive, a,
                       archive_entry_size_is_set(entry) ? archive_entry_size(entry) : -1))
        {
            result = -1;
            break;
        }
    }
    if (!result && !cancelled && r != ARCHIVE_EOF)
    {
        fail("%s: %s", name, archive_error_string(a) ? archive_error_string(a) : "unreadable");
        result = -1;
    }
    archive_read_free(a);
    return result;
}

static int run_source(const char *location, int single)
{
    source_t s;
    char name[LINK_NAME];
    const char *file;
    int result = -1, i;

    memset(&s, 0, sizeof(s));
    s.fd = -1;
    s.size = -1;
    s.ranges = -1;
    s.remote = strstr(location, "://") != NULL;
    s.url = location;
    if (s.remote)
        url_file_name(name, sizeof(name), location);
    else
        snprintf(name, sizeof(name), "%s", strrchr(location, '/') ? strrchr(location, '/') + 1 : location);

    pthread_mutex_lock(&lock);
    upper(status.source, sizeof(status.source), name[0] ? name : location);
    status.done = 0;
    status.total = -1;
    pthread_mutex_unlock(&lock);

    if (s.remote)
    {
        if (open_remote(&s, 0))
            return -1;
    }
    else
    {
        if ((s.fd = open(location, O_RDONLY)) < 0)
        {
            fail("cannot open %s", location);
            return -1;
        }
        s.size = lseek(s.fd, 0, SEEK_END);
        lseek(s.fd, 0, SEEK_SET);
    }
    pthread_mutex_lock(&lock);
    status.total = s.size;
    pthread_mutex_unlock(&lock);

    s.pending = peek_buffer;
    s.pending_length = raw_read(&s, peek_buffer, PEEK);
    for (i = 0; i < s.pending_length && isspace(peek_buffer[i]); i++)
        ;
    if (s.pending_length < 0)
    {
        if (!cancelled)
            fail("reading %s failed", name);
    }
    else if (s.remote && single && (strstr(s.type, "text/html") || (i < s.pending_length && peek_buffer[i] == '<')))
        result = read_listing(&s);
    else if (s.pending_length >= 4 && !memcmp(peek_buffer, "PWAD", 4))
        fail("%s is an add-on (pwad), not a game", name);
    else if (s.pending_length >= 4 && !memcmp(peek_buffer, "IWAD", 4))
    {
        if (!(file = games_known_file(name)))
            fail("%s is not a game doom knows", name);
        else
            result = write_game(file, fill_raw, &s, s.size);
    }
    else
        result = extract(&s, name);

    plat_http_close(s.http);
    if (s.fd >= 0)
        close(s.fd);
    return result;
}

static void *work(void *arg)
{
    int failed = 0, i;

    (void)arg;
    for (i = 0; i < source_count && !cancelled; i++)
    {
        pthread_mutex_lock(&lock);
        status.source_index = i;
        pthread_mutex_unlock(&lock);
        if (run_source(sources[i], source_count == 1))
            failed = 1;
        if (status.state == IMPORT_LISTING)
            return NULL;
    }
    pthread_mutex_lock(&lock);
    status.current[0] = 0;
    status.state = cancelled ? IMPORT_CANCELLED
                 : failed && !status.found_count ? IMPORT_FAILED : IMPORT_DONE;
    pthread_mutex_unlock(&lock);
    return NULL;
}

void import_start(char locations[][LINK_URL], int count, const char *dest)
{
    int i;

    import_finish();
    memset(&status, 0, sizeof(status));
    for (i = 0; i < count && i < MAX_SOURCES; i++)
        snprintf(sources[i], LINK_URL, "%s", locations[i]);
    source_count = i;
    snprintf(dest_dir, sizeof(dest_dir), "%s", dest);
    status.state = IMPORT_RUNNING;
    status.source_count = source_count;
    cancelled = 0;
    if (pthread_create(&worker, NULL, work, NULL))
    {
        status.state = IMPORT_FAILED;
        snprintf(status.message, sizeof(status.message), "cannot start the import");
        return;
    }
    worker_running = 1;
}

void import_status(import_status_t *out)
{
    pthread_mutex_lock(&lock);
    *out = status;
    pthread_mutex_unlock(&lock);
}

const listing_t *import_listing(void)
{
    return &listing;
}

void import_cancel(void)
{
    cancelled = 1;
}

void import_finish(void)
{
    if (worker_running)
    {
        pthread_join(worker, NULL);
        worker_running = 0;
    }
    status.state = IMPORT_IDLE;
}
