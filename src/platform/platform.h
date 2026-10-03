#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

enum
{
    PAD_UP       = 1 << 0,
    PAD_DOWN     = 1 << 1,
    PAD_LEFT     = 1 << 2,
    PAD_RIGHT    = 1 << 3,
    PAD_CROSS    = 1 << 4,
    PAD_CIRCLE   = 1 << 5,
    PAD_SQUARE   = 1 << 6,
    PAD_TRIANGLE = 1 << 7,
    PAD_L1       = 1 << 8,
    PAD_R1       = 1 << 9,
    PAD_L2       = 1 << 10,
    PAD_R2       = 1 << 11,
    PAD_L3       = 1 << 12,
    PAD_R3       = 1 << 13,
    PAD_OPTIONS  = 1 << 14,
    PAD_TOUCHPAD = 1 << 15,
};

typedef struct
{
    uint32_t buttons;
    int16_t lx, ly, rx, ry;
} pad_state_t;

typedef void (*audio_render_fn)(int16_t *stereo, int frames, void *user);

int plat_init(int argc, char **argv);
void plat_shutdown(void);
_Noreturn void plat_exit(int code);

void plat_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void plat_alert(const char *message);
uint64_t plat_ticks_us(void);
void plat_sleep_us(uint32_t us);

const char *const *plat_wad_dirs(void);
const char *plat_save_dir(void);

int plat_video_init(int width, int height);
void plat_video_present(const uint8_t *pixels, const uint32_t *palette);
void plat_video_shutdown(void);

int plat_pad_read(pad_state_t *state);
void plat_pad_rumble(uint8_t strong, uint8_t weak);

int plat_audio_init(int rate, audio_render_fn render, void *user);
void plat_audio_shutdown(void);

#endif
