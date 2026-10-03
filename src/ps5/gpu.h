#ifndef GPU_H
#define GPU_H

#include <stdint.h>

#define GPU_TARGETS 2

typedef struct
{
    int video;
    uint32_t *targets[GPU_TARGETS];
    int width, height, pitch;
    int view_x, view_width;
    int src_width, src_height;
} gpu_config_t;

int gpu_init(const gpu_config_t *config);
int gpu_present(const uint8_t *pixels, const uint32_t *rgba, int target, int64_t marker);
int gpu_finish(void);

#endif
