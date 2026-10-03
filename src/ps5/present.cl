#include "tiling.h"

#define GROUP 64

static unsigned int blend(unsigned int a, unsigned int b, float t)
{
    unsigned int out = 0;

    for (int shift = 0; shift < 24; shift += 8)
    {
        float ca = (a >> shift) & 0xff, cb = (b >> shift) & 0xff;

        out |= (unsigned int)(ca + (cb - ca) * t + 0.5f) << shift;
    }
    return out | 0xff000000u;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static float saturate(float v)
{
    return __builtin_fminf(__builtin_fmaxf(v, 0.0f), 1.0f);
}

__kernel __attribute__((reqd_work_group_size(GROUP, 1, 1))) void
present(__global const unsigned char *src, __global const unsigned int *palette,
        __global unsigned int *dst, unsigned int pitch, unsigned int out_width,
        unsigned int out_height, unsigned int view_x, unsigned int view_width,
        unsigned int src_width, unsigned int src_height)
{
    unsigned int y = __builtin_amdgcn_workgroup_id_x();
    float scale_x = (float)view_width / src_width;
    float scale_y = (float)out_height / src_height;
    float fy = (y + 0.5f) / scale_y - 0.5f;
    float iy = __builtin_floorf(fy);
    float ty = saturate((fy - iy - 0.5f) * scale_y + 0.5f);
    int y0 = clampi((int)iy, 0, src_height - 1), y1 = clampi((int)iy + 1, 0, src_height - 1);
    __global const unsigned char *row0 = src + y0 * src_width;
    __global const unsigned char *row1 = src + y1 * src_width;

    if (y >= out_height)
        return;

    for (unsigned int x = __builtin_amdgcn_workitem_id_x(); x < out_width; x += GROUP)
    {
        unsigned int color = 0xff000000u;

        if (x >= view_x && x < view_x + view_width)
        {
            float fx = (x - view_x + 0.5f) / scale_x - 0.5f;
            float ix = __builtin_floorf(fx);
            float tx = saturate((fx - ix - 0.5f) * scale_x + 0.5f);
            int x0 = clampi((int)ix, 0, src_width - 1), x1 = clampi((int)ix + 1, 0, src_width - 1);
            unsigned int top = blend(palette[row0[x0]], palette[row0[x1]], tx);
            unsigned int bottom = blend(palette[row1[x0]], palette[row1[x1]], tx);

            color = blend(top, bottom, ty);
        }
        dst[tiled_index(x, y, pitch)] = color;
    }
}
