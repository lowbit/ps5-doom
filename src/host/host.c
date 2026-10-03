#define _GNU_SOURCE
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"
#include "png.h"
#include "test_plan.h"

#define AUDIO_BLOCK 480

static const char *wad_dirs[2];
static const char *save_dir;
static const char *out_dir;
static int frame;
static int video_width, video_height;

static FILE *wav;
static uint32_t wav_frames;
static pthread_t audio_thread;
static volatile int audio_running;
static audio_render_fn audio_render;
static void *audio_user;

static const char *env_or(const char *name, const char *fallback)
{
    const char *value = getenv(name);

    return value && *value ? value : fallback;
}

int plat_init(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    wad_dirs[0] = realpath(env_or("DOOM_WADDIR", "wads"), NULL);
    wad_dirs[1] = NULL;
    save_dir = env_or("DOOM_SAVEDIR", "/tmp/doom-save");
    out_dir = realpath(env_or("DOOM_OUT", "out"), NULL);
    test_plan_limit(atoi(env_or("DOOM_FRAMES", "0")));
    test_plan_captures(getenv("DOOM_CAPTURE"));
    test_plan_input(getenv("DOOM_INPUT"));
    mkdir(save_dir, 0755);
    if (!wad_dirs[0] || !out_dir)
    {
        fprintf(stderr, "host: DOOM_WADDIR and DOOM_OUT must exist\n");
        return -1;
    }
    return 0;
}

void plat_shutdown(void)
{
}

_Noreturn void plat_exit(int code)
{
    plat_audio_shutdown();
    fflush(NULL);
    exit(code);
}

void plat_log(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void plat_alert(const char *message)
{
    fprintf(stderr, "ALERT: %s\n", message);
}

uint64_t plat_ticks_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void plat_sleep_us(uint32_t us)
{
    usleep(us);
}

const char *const *plat_wad_dirs(void)
{
    return wad_dirs;
}

const char *plat_save_dir(void)
{
    return save_dir;
}

int plat_video_init(int width, int height)
{
    video_width = width;
    video_height = height;
    return 0;
}

static void capture(const uint8_t *pixels, const uint32_t *palette)
{
    uint8_t *rgb = malloc((size_t)video_width * video_height * 3);
    char path[512];
    FILE *raw;
    int i;

    if (!rgb)
        return;
    for (i = 0; i < video_width * video_height; i++)
    {
        uint32_t c = palette[pixels[i]];

        rgb[i * 3] = c >> 16;
        rgb[i * 3 + 1] = c >> 8;
        rgb[i * 3 + 2] = c;
    }
    snprintf(path, sizeof(path), "%s/frame%05d.png", out_dir, frame);
    png_write_rgb(path, rgb, video_width, video_height);
    free(rgb);

    snprintf(path, sizeof(path), "%s/frame%05d.raw", out_dir, frame);
    raw = fopen(path, "wb");
    if (raw)
    {
        fwrite(pixels, 1, (size_t)video_width * video_height, raw);
        fwrite(palette, sizeof(*palette), 256, raw);
        fclose(raw);
    }
}

void plat_video_present(const uint8_t *pixels, const uint32_t *palette)
{
    frame++;
    if (test_plan_captures_frame(frame))
        capture(pixels, palette);
    if (test_plan_finished(frame))
        plat_exit(0);
}

void plat_video_shutdown(void)
{
}

int plat_pad_read(pad_state_t *state)
{
    memset(state, 0, sizeof(*state));
    test_plan_apply(frame, state);
    return 0;
}

void plat_pad_rumble(uint8_t strong, uint8_t weak)
{
    if (strong || weak)
        fprintf(stderr, "host: rumble %u/%u at frame %d\n", strong, weak, frame);
}

static void put_le(uint8_t *p, uint32_t value, int bytes)
{
    while (bytes--)
    {
        *p++ = value & 0xff;
        value >>= 8;
    }
}

static void write_wav_header(uint32_t frames)
{
    uint8_t h[44];
    uint32_t data = frames * 4;

    memcpy(h, "RIFF", 4);
    put_le(h + 4, 36 + data, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le(h + 16, 16, 4);
    put_le(h + 20, 1, 2);
    put_le(h + 22, 2, 2);
    put_le(h + 24, 48000, 4);
    put_le(h + 28, 48000 * 4, 4);
    put_le(h + 32, 4, 2);
    put_le(h + 34, 16, 2);
    memcpy(h + 36, "data", 4);
    put_le(h + 40, data, 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), wav);
    fseek(wav, 0, SEEK_END);
}

static void *audio_main(void *arg)
{
    int16_t block[AUDIO_BLOCK * 2];
    uint64_t next = plat_ticks_us();

    (void)arg;
    while (audio_running)
    {
        audio_render(block, AUDIO_BLOCK, audio_user);
        fwrite(block, sizeof(block), 1, wav);
        wav_frames += AUDIO_BLOCK;
        next += AUDIO_BLOCK * 1000000ull / 48000;
        while (plat_ticks_us() < next && audio_running)
            usleep(1000);
    }
    return NULL;
}

int plat_audio_init(int rate, audio_render_fn render, void *user)
{
    const char *path = getenv("DOOM_AUDIO");

    if (!path || rate != 48000)
        return 0;
    wav = fopen(path, "wb");
    if (!wav)
        return -1;
    write_wav_header(0);
    audio_render = render;
    audio_user = user;
    audio_running = 1;
    return pthread_create(&audio_thread, NULL, audio_main, NULL) ? -1 : 0;
}

void plat_audio_shutdown(void)
{
    if (!audio_running)
        return;
    audio_running = 0;
    pthread_join(audio_thread, NULL);
    write_wav_header(wav_frames);
    fclose(wav);
    wav = NULL;
}
