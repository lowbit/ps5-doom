#ifndef TILING_H
#define TILING_H

#define TILE_WIDTH 512
#define TILE_HEIGHT 128
#define TILE_PIXELS (TILE_WIDTH * TILE_HEIGHT)

static inline unsigned int tile_offset(unsigned int x, unsigned int y)
{
    return (x & 1) | (x & 2) | (y & 1) << 2 | (y & 2) << 2 | (y & 4) << 2 | (x & 4) << 3 |
           ((x >> 3 ^ y >> 3) & 1) << 6 | ((x >> 4 ^ y >> 4) & 1) << 7 |
           ((x >> 6 ^ y >> 5) & 1) << 8 | ((x >> 5 ^ y >> 6) & 1) << 9 | (y >> 3 & 1) << 10 |
           (x >> 4 & 1) << 11 | (y >> 6 & 1) << 12 | (x >> 6 & 1) << 13 | (x >> 7 & 1) << 14 |
           (x >> 8 & 1) << 15;
}

static inline unsigned int tile_column(unsigned int x)
{
    return (x / TILE_WIDTH) * TILE_PIXELS + tile_offset(x % TILE_WIDTH, 0);
}

static inline unsigned int tile_row(unsigned int y, unsigned int pitch)
{
    return (y / TILE_HEIGHT) * TILE_HEIGHT * pitch;
}

static inline unsigned int tiled_index(unsigned int x, unsigned int y, unsigned int pitch)
{
    return tile_row(y, pitch) + (tile_column(x) ^ tile_offset(0, y % TILE_HEIGHT));
}

static inline unsigned int tiled_pixels(unsigned int width, unsigned int height)
{
    return tile_row(height - 1, width) + ((width - 1) / TILE_WIDTH) * TILE_PIXELS + TILE_PIXELS;
}

#endif
