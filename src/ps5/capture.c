#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform.h"
#include "ps5.h"
#include "sce.h"

#define MAX_QUEUED 16
#define DRAIN_STEP_US 100000

typedef struct
{
    char name[32];
    void *data;
    size_t size;
} item_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static item_t queue[MAX_QUEUED];
static int queued;
static int listener = -1;

static int send_all(int fd, const void *data, size_t size)
{
    const uint8_t *p = data;

    while (size)
    {
        ssize_t sent = send(fd, p, size, 0);

        if (sent <= 0)
            return -1;
        p += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static int send_item(int fd, const item_t *item)
{
    uint32_t name_length = (uint32_t)strlen(item->name);
    uint64_t size = item->size;

    return send_all(fd, &name_length, sizeof(name_length)) ||
           send_all(fd, item->name, name_length) || send_all(fd, &size, sizeof(size)) ||
           send_all(fd, item->data, item->size);
}

static void *serve(void *arg)
{
    (void)arg;
    for (;;)
    {
        int client = accept(listener, NULL, NULL);
        uint32_t end = 0;

        if (client < 0)
            continue;
        pthread_mutex_lock(&lock);
        while (queued && send_item(client, &queue[0]) == 0)
        {
            munmap(queue[0].data, queue[0].size);
            memmove(queue, queue + 1, (size_t)--queued * sizeof(*queue));
        }
        pthread_mutex_unlock(&lock);
        send_all(client, &end, sizeof(end));
        close(client);
    }
    return NULL;
}

void ps5_capture_serve(int port)
{
    struct sockaddr_in address;
    pthread_t thread;
    int yes = 1;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
    {
        plat_log("capture: no socket\n");
        return;
    }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 1) ||
        pthread_create(&thread, NULL, serve, NULL))
    {
        plat_log("capture: cannot serve on port %d\n", port);
        close(listener);
        listener = -1;
        return;
    }
    plat_log("capture: serving on port %d\n", port);
}

void ps5_capture_add(const char *name, const void *data, size_t size)
{
    void *copy;

    if (listener < 0)
        return;
    copy = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (copy == MAP_FAILED)
        return;
    memcpy(copy, data, size);

    pthread_mutex_lock(&lock);
    if (queued < MAX_QUEUED)
    {
        snprintf(queue[queued].name, sizeof(queue[queued].name), "%s", name);
        queue[queued].data = copy;
        queue[queued].size = size;
        queued++;
        copy = NULL;
    }
    pthread_mutex_unlock(&lock);
    if (copy)
        munmap(copy, size);
}

void ps5_capture_drain(uint32_t timeout_us)
{
    uint32_t waited = 0;
    int pending = 1;

    while (listener >= 0 && waited < timeout_us)
    {
        pthread_mutex_lock(&lock);
        pending = queued;
        pthread_mutex_unlock(&lock);
        if (!pending)
            return;
        sceKernelUsleep(DRAIN_STEP_US);
        waited += DRAIN_STEP_US;
    }
}
