#include <stdio.h>
#include <string.h>

#include "gpu.h"
#include "platform.h"
#include "ps5.h"
#include "sce.h"
#include "test_plan.h"
#include "tiling.h"

#define OUT_WIDTH 1920
#define OUT_HEIGHT 1080
#define VIEW_WIDTH 1440
#define VIEW_X ((OUT_WIDTH - VIEW_WIDTH) / 2)
#define BUFFER_BYTES 0x1000000
#define FRAME_PIXELS tiled_pixels(OUT_WIDTH, OUT_HEIGHT)
#define FRAME_BYTES ((size_t)FRAME_PIXELS * 4)
#define FORCE_CPU_BUTTONS (PAD_L1 | PAD_R1)
#define CAPTURE_DRAIN_US 20000000

static int video = -1;
static uint32_t *buffers[GPU_TARGETS];
static int next_buffer;
static int64_t flips;
static int use_gpu;
static int splash_hidden;

static int src_width, src_height;
static uint16_t column_of[VIEW_WIDTH];
static uint16_t row_of[OUT_HEIGHT];
static uint32_t tile_columns[VIEW_WIDTH];
static uint32_t rgba[256];

static int open_video(void)
{
    SceVideoOutBuffer out[GPU_TARGETS];
    SceVideoOutAttribute attribute = {0};
    uint8_t *memory;
    int i, result;

    video = sceVideoOutOpen(SCE_USER_SYSTEM, SCE_VIDEO_BUS_MAIN, 0, NULL);
    if (video < 0)
    {
        plat_log("video: open %#x\n", video);
        return -1;
    }
    memory = ps5_gpu_memory(BUFFER_BYTES * GPU_TARGETS);
    if (!memory)
        return -1;

    memset(out, 0, sizeof(out));
    for (i = 0; i < GPU_TARGETS; i++)
    {
        uint32_t *p;

        buffers[i] = (uint32_t *)(memory + i * BUFFER_BYTES);
        for (p = buffers[i]; p < buffers[i] + FRAME_PIXELS; p++)
            *p = 0xff000000u;
        ps5_cache_flush(buffers[i], FRAME_BYTES);
        out[i].data = buffers[i];
    }

    sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attribute, SCE_VIDEO_FORMAT_RGBA8_SRGB, SCE_VIDEO_TILING_TILED,
                                   OUT_WIDTH, OUT_HEIGHT, 0, 0, 0);
    result = sceVideoOutRegisterBuffers2(video, 0, 0, out, GPU_TARGETS, &attribute, 0, NULL);
    if (result < 0)
    {
        plat_log("video: register buffers %#x\n", result);
        return -1;
    }
    return 0;
}

static int gpu_wanted(void)
{
    pad_state_t pad;

    if (plat_pad_read(&pad) == 0 && (pad.buttons & FORCE_CPU_BUTTONS) == FORCE_CPU_BUTTONS)
    {
        plat_log("video: L1+R1 held, using the CPU scaler\n");
        return 0;
    }
    return !ps5_test_cpu_present();
}

int ps5_frame(void)
{
    return (int)flips;
}

static void capture(int buffer)
{
    char name[32];

    if (use_gpu && gpu_finish())
        return;
    snprintf(name, sizeof(name), "shot%05d", (int)flips);
    ps5_cache_flush(buffers[buffer], FRAME_BYTES);
    ps5_capture_add(name, buffers[buffer], FRAME_BYTES);
    plat_log("video: captured frame %d\n", (int)flips);
}

int plat_video_init(int width, int height)
{
    gpu_config_t config;
    int i;

    src_width = width;
    src_height = height;
    for (i = 0; i < VIEW_WIDTH; i++)
    {
        column_of[i] = (uint16_t)(i * width / VIEW_WIDTH);
        tile_columns[i] = tile_column(VIEW_X + i);
    }
    for (i = 0; i < OUT_HEIGHT; i++)
        row_of[i] = (uint16_t)(i * height / OUT_HEIGHT);

    if (open_video())
        return -1;

    config.video = video;
    for (i = 0; i < GPU_TARGETS; i++)
        config.targets[i] = buffers[i];
    config.width = OUT_WIDTH;
    config.height = OUT_HEIGHT;
    config.pitch = OUT_WIDTH;
    config.view_x = VIEW_X;
    config.view_width = VIEW_WIDTH;
    config.src_width = width;
    config.src_height = height;
    use_gpu = gpu_wanted() && gpu_init(&config) == 0;
    plat_log("video: %s presentation\n", use_gpu ? "AGC compute" : "CPU");
    return 0;
}

void plat_video_shutdown(void)
{
    if (video < 0)
        return;
    sceVideoOutClose(video);
    video = -1;
}

static void cpu_scale(const uint8_t *pixels, uint32_t *target)
{
    int x, y;

    for (y = 0; y < OUT_HEIGHT; y++)
    {
        const uint8_t *src = pixels + row_of[y] * src_width;
        uint32_t *band = target + tile_row(y, OUT_WIDTH);
        unsigned int row = tile_offset(0, y % TILE_HEIGHT);

        for (x = 0; x < VIEW_WIDTH; x++)
            band[tile_columns[x] ^ row] = rgba[src[column_of[x]]];
    }
    ps5_cache_flush(target, FRAME_BYTES);
}

static void cpu_present(const uint8_t *pixels)
{
    while (sceVideoOutIsFlipPending(video) > 0)
        sceKernelUsleep(500);
    cpu_scale(pixels, buffers[next_buffer]);
    sceVideoOutSubmitFlip(video, next_buffer, SCE_VIDEO_FLIP_VSYNC, flips);
}

void plat_video_present(const uint8_t *pixels, const uint32_t *palette)
{
    int i;

    if (video < 0)
        return;
    for (i = 0; i < 256; i++)
    {
        uint32_t c = palette[i];

        rgba[i] = (c & 0xff00ff00u) | (c >> 16 & 0xff) | (c & 0xff) << 16;
    }

    flips++;
    if (use_gpu && gpu_present(pixels, rgba, next_buffer, flips))
    {
        plat_log("video: GPU presentation failed, switching to the CPU scaler\n");
        use_gpu = 0;
    }
    if (!use_gpu)
        cpu_present(pixels);
    if (test_plan_captures_frame((int)flips))
        capture(next_buffer);
    next_buffer = (next_buffer + 1) % GPU_TARGETS;
    if (test_plan_finished((int)flips))
    {
        ps5_capture_drain(CAPTURE_DRAIN_US);
        plat_exit(0);
    }

    if (!splash_hidden)
    {
        sceSystemServiceHideSplashScreen();
        splash_hidden = 1;
    }
}
