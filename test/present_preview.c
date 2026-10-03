#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "png.h"

static unsigned group_x, item_x;

#define __kernel
#define __global
#define __attribute__(x)
#define __builtin_amdgcn_workgroup_id_x() group_x
#define __builtin_amdgcn_workitem_id_x() item_x
#include "present.cl"

#define SRC_W 320
#define SRC_H 200
#define OUT_W 1920
#define OUT_H 1080
#define VIEW_W 1440

int main(int argc, char **argv)
{
    static uint8_t pixels[SRC_W * SRC_H];
    static uint32_t palette[256], rgba[256];
    uint32_t *out = calloc(tiled_pixels(OUT_W, OUT_H), 4);
    uint8_t *rgb = malloc(OUT_W * OUT_H * 3);
    FILE *f;
    int i;

    if (argc != 3 || !(f = fopen(argv[1], "rb")))
    {
        fprintf(stderr, "usage: present_preview <frame.raw> <out.png>\n");
        return 2;
    }
    if (fread(pixels, 1, sizeof(pixels), f) != sizeof(pixels) ||
        fread(palette, 4, 256, f) != 256)
        return 1;
    fclose(f);

    for (i = 0; i < 256; i++)
        rgba[i] = (palette[i] & 0xff00ff00u) | (palette[i] >> 16 & 0xff) | (palette[i] & 0xff) << 16;

    for (group_x = 0; group_x < OUT_H; group_x++)
        for (item_x = 0; item_x < GROUP; item_x++)
            present(pixels, rgba, out, OUT_W, OUT_W, OUT_H, (OUT_W - VIEW_W) / 2, VIEW_W,
                            SRC_W, SRC_H);

    for (i = 0; i < OUT_W * OUT_H; i++)
    {
        uint32_t c = out[tiled_index(i % OUT_W, i / OUT_W, OUT_W)];

        rgb[i * 3] = c & 0xff;
        rgb[i * 3 + 1] = c >> 8 & 0xff;
        rgb[i * 3 + 2] = c >> 16 & 0xff;
    }
    return png_write_rgb(argv[2], rgb, OUT_W, OUT_H);
}
