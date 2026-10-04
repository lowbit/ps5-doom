#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "platform.h"
#include "upload.h"
#include "upload_page.h"
#include "url.h"

#define HEAD_LIMIT 8192
#define CHUNK (256 * 1024)
#define MAX_FINISHED 16
#define MAX_NOTES 32
#define NOTE_TEXT 160
#define MAX_LEFTOVERS 32
#define RECEIVE_TIMEOUT_S 1
#define SEND_TIMEOUT_S 5
#define IDLE_LIMIT_S 30

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

typedef struct
{
    char path[UPLOAD_PATH];
    char name[UPLOAD_NAME];
} finished_t;

typedef struct
{
    int id, ok;
    char text[NOTE_TEXT];
} note_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t server;
static int listener = -1, running, port;
static volatile int stopping;
static char folder[UPLOAD_PATH];
static char address[64];
static upload_progress_t progress;
static finished_t finished[MAX_FINISHED];
static int finished_count;
static note_t notes[MAX_NOTES];
static int note_count, next_note = 1;

// Only the server thread touches these.
static char head[HEAD_LIMIT + 1];
static uint8_t chunk[CHUNK];

static int send_all(int fd, const void *data, size_t size)
{
    const uint8_t *p = data;

    while (size)
    {
        ssize_t sent = send(fd, p, size, SEND_FLAGS);

        if (sent <= 0)
            return -1;
        p += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static void respond(int fd, const char *status, const char *type, const void *body, size_t length)
{
    char top[256];
    int n = snprintf(top, sizeof(top),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                     status, type, (unsigned long)length);

    if (send_all(fd, top, (size_t)n) == 0 && length)
        send_all(fd, body, length);
}

static void respond_text(int fd, const char *status, const char *text)
{
    respond(fd, status, "text/plain; charset=utf-8", text, strlen(text));
}

// recv that gives up when the server stops or the browser stays quiet for too long.
static ssize_t receive(int fd, void *buffer, size_t size)
{
    int idle = 0;

    for (;;)
    {
        ssize_t got = recv(fd, buffer, size, 0);

        if (got >= 0)
            return got;
        if ((errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) || stopping ||
            ++idle >= IDLE_LIMIT_S / RECEIVE_TIMEOUT_S)
            return -1;
    }
}

// Reads the request head into head[]; returns its length including the blank line, with the
// number of bytes read so far (head plus the start of the body) in *used.
static int read_head(int fd, int *used)
{
    int length = 0;

    while (length < HEAD_LIMIT)
    {
        ssize_t got = receive(fd, head + length, (size_t)(HEAD_LIMIT - length));
        char *end;

        if (got <= 0)
            return -1;
        length += (int)got;
        head[length] = 0;
        if ((end = strstr(head, "\r\n\r\n")))
        {
            *used = length;
            return (int)(end - head) + 4;
        }
    }
    return -1;
}

static const char *header_value(const char *name)
{
    size_t length = strlen(name);
    const char *line = strstr(head, "\r\n");

    while (line && line[2] != '\r')
    {
        line += 2;
        if (!strncasecmp(line, name, length) && line[length] == ':')
        {
            const char *value = line + length + 1;

            while (*value == ' ' || *value == '\t')
                value++;
            return value;
        }
        line = strstr(line, "\r\n");
    }
    return NULL;
}

// Turns the request path after /upload/ into a safe file name: decoded, lower case, with anything
// that could name a folder replaced, and one of the kinds the importer reads.
static int clean_name(char *out, size_t size, const char *path)
{
    char decoded[UPLOAD_NAME * 3];
    const char *dot;
    size_t n;

    url_file_name(decoded, sizeof(decoded), path);
    for (n = 0; decoded[n] && n < size - 1; n++)
    {
        unsigned char c = (unsigned char)decoded[n];

        out[n] = isalnum(c) || strchr(" .-_()[]+", c) ? (char)tolower(c) : '_';
    }
    out[n] = 0;
    dot = strrchr(out, '.');
    return out[0] != '.' && dot &&
                   (!strcmp(dot, ".wad") || !strcmp(dot, ".zip") || !strcmp(dot, ".7z") ||
                    !strcmp(dot, ".rar"))
               ? 0
               : -1;
}

static int write_all(int fd, const uint8_t *data, size_t size)
{
    while (size)
    {
        ssize_t written = write(fd, data, size);

        if (written <= 0)
            return -1;
        data += written;
        size -= (size_t)written;
    }
    return 0;
}

static void set_progress(int receiving, const char *name, long long done, long long total)
{
    pthread_mutex_lock(&lock);
    progress.receiving = receiving;
    snprintf(progress.name, sizeof(progress.name), "%s", name);
    progress.done = done;
    progress.total = total;
    pthread_mutex_unlock(&lock);
}

static void put_file(int fd, const char *target, int head_length, int used)
{
    char name[UPLOAD_NAME], part[UPLOAD_PATH];
    const char *value = header_value("Content-Length");
    long long length = value ? strtoll(value, NULL, 10) : -1, done = 0;
    int out, full, lost = 0, error = 0;

    if (clean_name(name, sizeof(name), target))
    {
        respond_text(fd, "415 Unsupported Media Type", "Send WAD, ZIP, 7Z or RAR files.");
        return;
    }
    if (length < 0)
    {
        respond_text(fd, "411 Length Required", "The upload has no length.");
        return;
    }
    pthread_mutex_lock(&lock);
    full = finished_count == MAX_FINISHED;
    pthread_mutex_unlock(&lock);
    if (full)
    {
        respond_text(fd, "503 Service Unavailable", "The console is still busy with earlier files.");
        return;
    }

    snprintf(part, sizeof(part), "%s/%s.upload", folder, name);
    if ((out = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        plat_log("upload: cannot write %s (error %d)\n", part, errno);
        respond_text(fd, "500 Internal Server Error", "The console cannot write the file.");
        return;
    }
    set_progress(1, name, 0, length);
    plat_log("upload: receiving %s, %lld bytes\n", name, length);

    if (used > head_length)
    {
        long long extra = used - head_length < length ? used - head_length : length;

        if (write_all(out, (const uint8_t *)head + head_length, (size_t)extra))
            error = errno ? errno : EIO;
        done = extra;
    }
    while (!error && !lost && done < length)
    {
        ssize_t got = receive(fd, chunk, length - done < CHUNK ? (size_t)(length - done) : CHUNK);

        if (got <= 0)
            lost = 1;
        else if (write_all(out, chunk, (size_t)got))
            error = errno ? errno : EIO;
        else
        {
            done += got;
            set_progress(1, name, done, length);
        }
    }
    close(out);
    set_progress(0, "", 0, 0);

    if (lost || error)
    {
        unlink(part);
        plat_log("upload: %s stopped at %lld of %lld bytes (error %d)\n", name, done, length, error);
        if (error == ENOSPC)
            respond_text(fd, "507 Insufficient Storage", "Not enough space on the console.");
        else if (error)
            respond_text(fd, "500 Internal Server Error", "Writing on the console failed.");
        return;
    }
    pthread_mutex_lock(&lock);
    snprintf(finished[finished_count].path, UPLOAD_PATH, "%s", part);
    snprintf(finished[finished_count].name, UPLOAD_NAME, "%s", name);
    finished_count++;
    pthread_mutex_unlock(&lock);
    plat_log("upload: received %s\n", name);
    respond_text(fd, "200 OK", "Received.");
}

static void send_status(int fd, const char *query)
{
    static char json[MAX_NOTES * (NOTE_TEXT + 40) + 16];
    const char *after_text = strstr(query, "after=");
    int after = after_text ? atoi(after_text + 6) : 0, n, i, first = 1;

    n = snprintf(json, sizeof(json), "{\"notes\":[");
    pthread_mutex_lock(&lock);
    for (i = 0; i < note_count; i++)
        if (notes[i].id > after)
        {
            n += snprintf(json + n, sizeof(json) - (size_t)n, "%s{\"id\":%d,\"ok\":%s,\"text\":\"%s\"}",
                          first ? "" : ",", notes[i].id, notes[i].ok ? "true" : "false", notes[i].text);
            first = 0;
        }
    pthread_mutex_unlock(&lock);
    n += snprintf(json + n, sizeof(json) - (size_t)n, "]}");
    respond(fd, "200 OK", "application/json", json, (size_t)n);
}

static void handle(int fd)
{
    char method[8], target[UPLOAD_NAME * 3 + 16];
    int used, head_length = read_head(fd, &used);
    size_t i = 0, j = 0;

    if (head_length < 0)
        return;
    for (; head[i] && head[i] != ' ' && i < sizeof(method) - 1; i++)
        method[i] = head[i];
    method[i] = 0;
    while (head[i] == ' ')
        i++;
    while (head[i] && head[i] != ' ' && head[i] != '\r' && j < sizeof(target) - 1)
        target[j++] = head[i++];
    target[j] = 0;

    if (!strcmp(method, "GET") && (!strcmp(target, "/") || !strcmp(target, "/index.html")))
        respond(fd, "200 OK", "text/html; charset=utf-8", upload_page, sizeof(upload_page));
    else if (!strcmp(method, "GET") && !strncmp(target, "/status", 7))
        send_status(fd, target + 7);
    else if (!strcmp(method, "PUT") && !strncmp(target, "/upload/", 8))
        put_file(fd, target + 8, head_length, used);
    else
        respond_text(fd, "404 Not Found", "Not found.");
}

static void *serve(void *arg)
{
    struct timeval receive_timeout = {RECEIVE_TIMEOUT_S, 0}, send_timeout = {SEND_TIMEOUT_S, 0};

    (void)arg;
    while (!stopping)
    {
        int client = accept(listener, NULL, NULL);

        if (client < 0)
        {
            if (!stopping)
                plat_sleep_us(100000);
            continue;
        }
        if (!stopping)
        {
#ifdef SO_NOSIGPIPE
            int yes = 1;

            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
            handle(client);
        }
        close(client);
    }
    return NULL;
}

// Connecting a UDP socket sends nothing; it only picks the route, and with it the local address.
static void find_address(void)
{
    struct sockaddr_in remote, local;
    socklen_t size = sizeof(local);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    address[0] = 0;
    if (fd < 0)
        return;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    remote.sin_addr.s_addr = htonl(0x08080808);
    if (!connect(fd, (struct sockaddr *)&remote, sizeof(remote)) &&
        !getsockname(fd, (struct sockaddr *)&local, &size))
    {
        uint32_t ip = ntohl(local.sin_addr.s_addr);

        if (ip)
            snprintf(address, sizeof(address), "http://%u.%u.%u.%u:%d/", ip >> 24, (ip >> 16) & 255,
                     (ip >> 8) & 255, ip & 255, port);
    }
    close(fd);
}

typedef struct
{
    char names[MAX_LEFTOVERS][UPLOAD_NAME + 8];
    int count;
} leftovers_t;

static void collect_leftover(const char *name, void *user)
{
    leftovers_t *leftovers = user;
    size_t length = strlen(name);

    if (length > 7 && !strcmp(name + length - 7, ".upload") && leftovers->count < MAX_LEFTOVERS &&
        length < sizeof(leftovers->names[0]))
        strcpy(leftovers->names[leftovers->count++], name);
}

// Removes partial files a crash or a power loss left behind.
static void remove_leftovers(void)
{
    leftovers_t leftovers;
    char path[UPLOAD_PATH + UPLOAD_NAME + 8];
    int i;

    leftovers.count = 0;
    plat_list_dir(folder, collect_leftover, &leftovers);
    for (i = 0; i < leftovers.count; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", folder, leftovers.names[i]);
        unlink(path);
    }
}

static int listen_on(int candidate)
{
    struct sockaddr_in local;
    int fd = socket(AF_INET, SOCK_STREAM, 0), yes = 1;

    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons((uint16_t)candidate);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) || listen(fd, 4))
    {
        plat_log("upload: port %d refused (error %d)\n", candidate, errno);
        close(fd);
        return -1;
    }
    return fd;
}

int upload_start(const char *dir)
{
    if (running)
        return 0;
    snprintf(folder, sizeof(folder), "%s", dir);
    remove_leftovers();
    note_count = 0;
    finished_count = 0;
    memset(&progress, 0, sizeof(progress));

    for (port = UPLOAD_PORT; port < UPLOAD_PORT + UPLOAD_PORT_TRIES; port++)
        if ((listener = listen_on(port)) >= 0)
            break;
    if (listener < 0)
        return -1;
    find_address();
    stopping = 0;
    if (pthread_create(&server, NULL, serve, NULL))
    {
        plat_log("upload: no server thread\n");
        close(listener);
        listener = -1;
        return -1;
    }
    running = 1;
    plat_log("upload: serving %s into %s\n", address[0] ? address : "without an address", folder);
    return 0;
}

void upload_stop(void)
{
    struct sockaddr_in self;
    int fd, i;

    if (!running)
        return;
    stopping = 1;

    // accept() does not return on its own; a connection to ourselves wakes the thread up.
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0)
    {
        memset(&self, 0, sizeof(self));
        self.sin_family = AF_INET;
        self.sin_port = htons((uint16_t)port);
        self.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connect(fd, (struct sockaddr *)&self, sizeof(self));
        close(fd);
    }
    pthread_join(server, NULL);
    close(listener);
    listener = -1;
    running = 0;

    // Files that arrived but were never taken are dropped.
    for (i = 0; i < finished_count; i++)
        unlink(finished[i].path);
    finished_count = 0;
    plat_log("upload: stopped\n");
}

const char *upload_address(void)
{
    return address;
}

void upload_progress(upload_progress_t *out)
{
    pthread_mutex_lock(&lock);
    *out = progress;
    pthread_mutex_unlock(&lock);
}

int upload_take(char *path, int path_size, char *name, int name_size)
{
    int taken = 0;

    pthread_mutex_lock(&lock);
    if (finished_count)
    {
        snprintf(path, (size_t)path_size, "%s", finished[0].path);
        snprintf(name, (size_t)name_size, "%s", finished[0].name);
        memmove(finished, finished + 1, (size_t)--finished_count * sizeof(*finished));
        taken = 1;
    }
    pthread_mutex_unlock(&lock);
    return taken;
}

void upload_note(const char *text, int ok)
{
    note_t *note;
    int i;

    pthread_mutex_lock(&lock);
    if (note_count == MAX_NOTES)
        memmove(notes, notes + 1, (size_t)--note_count * sizeof(*notes));
    note = &notes[note_count++];
    note->id = next_note++;
    note->ok = ok;
    // Kept safe to place inside a JSON string as it is.
    for (i = 0; text[i] && i < NOTE_TEXT - 1; i++)
        note->text[i] = text[i] == '"' || text[i] == '\\' ? '\''
                      : (unsigned char)text[i] < ' ' ? ' ' : text[i];
    note->text[i] = 0;
    pthread_mutex_unlock(&lock);
}
