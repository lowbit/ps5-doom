#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform.h"
#include "ps5.h"
#include "sce.h"
#include "test_plan.h"

#define SAVE_DIR "/download0"
#define LOG_PATH SAVE_DIR "/doom.log"
#define WAD_DIR "/app0/wads"
#define TEST_PLAN_PATH "/app0/test.cfg"
#define TEST_PLAN_BYTES 8192
#define LOG_DRAIN_US 50000

static const char *const wad_dirs[] = {WAD_DIR, NULL};
static int user = SCE_USER_SYSTEM;
static int log_file = -1;
static int test_cpu_present;

int ps5_user(void)
{
    return user;
}

static void log_text(const char *text, size_t length)
{
    char line[512];
    size_t start = 0, i;

    if (log_file >= 0)
        write(log_file, text, length);
    for (i = 0; i < length; i++)
    {
        if (text[i] != '\n' && i + 1 < length && i - start < sizeof(line) - 2)
            continue;
        memcpy(line, text + start, i - start + 1);
        line[i - start + 1] = 0;
        sceKernelDebugOutText(0, line);
        start = i + 1;
    }
}

static void *pump_output(void *arg)
{
    int pipe_read = (int)(intptr_t)arg;
    char chunk[1024];
    ssize_t length;

    while ((length = read(pipe_read, chunk, sizeof(chunk) - 1)) > 0)
    {
        chunk[length] = 0;
        log_text(chunk, (size_t)length);
    }
    return NULL;
}

static void capture_output(void)
{
    pthread_t thread;
    FILE *output;
    int fds[2];

    log_file = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_file < 0)
        plat_log("ps5: cannot write %s (errno %d)\n", LOG_PATH, errno);
    if (pipe(fds))
    {
        plat_log("ps5: no output pipe (errno %d)\n", errno);
        return;
    }
    output = fdopen(fds[1], "w");
    if (!output)
    {
        plat_log("ps5: cannot redirect output (errno %d)\n", errno);
        return;
    }
    setvbuf(output, NULL, _IOLBF, 0);
    stdout = output;
    stderr = output;
    pthread_create(&thread, NULL, pump_output, (void *)(intptr_t)fds[0]);
}

int ps5_test_cpu_present(void)
{
    return test_cpu_present;
}

static void load_test_plan(void)
{
    static char text[TEST_PLAN_BYTES];
    char *line, *save;
    ssize_t length;
    int fd = open(TEST_PLAN_PATH, O_RDONLY);

    if (fd < 0)
        return;
    length = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (length <= 0)
        return;
    text[length] = 0;

    for (line = strtok_r(text, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save))
    {
        if (!strncmp(line, "input ", 6))
            test_plan_input(line + 6);
        else if (!strncmp(line, "capture ", 8))
            test_plan_captures(line + 8);
        else if (!strncmp(line, "frames ", 7))
            test_plan_limit(atoi(line + 7));
        else if (!strncmp(line, "serve ", 6))
            ps5_capture_serve(atoi(line + 6));
        else if (!strcmp(line, "present cpu"))
            test_cpu_present = 1;
    }
    plat_log("ps5: test plan loaded\n");
}

int plat_init(int argc, char **argv)
{
    int initial;

    (void)argc;
    (void)argv;
    capture_output();

    if (sceUserServiceInitialize(NULL) < 0)
        plat_log("ps5: user service unavailable\n");
    else if (sceUserServiceGetInitialUser(&initial) == 0)
        user = initial;
    plat_log("ps5: user %d\n", user);
    load_test_plan();

    ps5_pad_open();
    return 0;
}

void plat_shutdown(void)
{
    ps5_pad_close();
}

_Noreturn void plat_exit(int code)
{
    plat_log("ps5: exit %d\n", code);
    fflush(NULL);
    sceKernelUsleep(LOG_DRAIN_US);
    sceSystemServiceLoadExec("exit", NULL);
    for (;;)
        sceKernelUsleep(1000000);
}

void plat_log(const char *fmt, ...)
{
    char line[512];
    va_list args;
    int length;

    va_start(args, fmt);
    length = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (length < 0)
        return;
    log_text(line, (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1);
}

void plat_alert(const char *message)
{
    SceNotificationRequest request;

    memset(&request, 0, sizeof(request));
    snprintf(request.message, sizeof(request.message), "DOOM: %s", message);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

uint64_t plat_ticks_us(void)
{
    return sceKernelGetProcessTime();
}

void plat_sleep_us(uint32_t us)
{
    sceKernelUsleep(us);
}

const char *const *plat_wad_dirs(void)
{
    return wad_dirs;
}

const char *plat_save_dir(void)
{
    return SAVE_DIR;
}
