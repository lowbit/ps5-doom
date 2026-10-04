#include "glyphs.h"

#define SIZE 7

typedef struct
{
    uint8_t rows[SIZE];
    uint8_t r, g, b;
} glyph_t;

// One bit per pixel, most significant of the low 7 bits on the left.
static const glyph_t glyphs[] = {
    // Cross
    {{0x63, 0x77, 0x3e, 0x1c, 0x3e, 0x77, 0x63}, 124, 178, 232},
    // Circle
    {{0x1c, 0x3e, 0x63, 0x63, 0x63, 0x3e, 0x1c}, 255, 102, 102},
    // Square
    {{0x7f, 0x7f, 0x63, 0x63, 0x63, 0x7f, 0x7f}, 228, 153, 214},
    // Triangle
    {{0x08, 0x1c, 0x1c, 0x36, 0x36, 0x7f, 0x7f}, 64, 224, 176},
};

int glyph_is_button(int c)
{
    return c >= 1 && c <= (int)(sizeof(glyphs) / sizeof(glyphs[0]));
}

static uint8_t nearest(const uint8_t *rgb, int r, int g, int b)
{
    int best = 0, best_distance = 1 << 30, i;

    for (i = 0; i < 256; i++)
    {
        int dr = rgb[i * 3] - r, dg = rgb[i * 3 + 1] - g, db = rgb[i * 3 + 2] - b;
        int distance = dr * dr + dg * dg + db * db;

        if (distance < best_distance)
        {
            best = i;
            best_distance = distance;
        }
    }
    return (uint8_t)best;
}

static void plot(uint8_t *screen, int width, int height, int x, int y, int scale, uint8_t color)
{
    int i, j;

    for (j = 0; j < scale; j++)
        for (i = 0; i < scale; i++)
            if (x + i >= 0 && x + i < width && y + j >= 0 && y + j < height)
                screen[(y + j) * width + x + i] = color;
}

void glyph_draw(uint8_t *screen, int width, int height, int x, int y, int c, int scale,
                const uint8_t *rgb)
{
    const glyph_t *glyph;
    uint8_t color, shadow;
    int pass, row, column;

    if (!glyph_is_button(c))
        return;
    glyph = &glyphs[c - 1];
    color = nearest(rgb, glyph->r, glyph->g, glyph->b);
    shadow = nearest(rgb, 0, 0, 0);

    // A shadow one pixel down and right keeps the symbol readable over any background.
    for (pass = 0; pass < 2; pass++)
        for (row = 0; row < SIZE; row++)
            for (column = 0; column < SIZE; column++)
                if (glyph->rows[row] & (0x40 >> column))
                    plot(screen, width, height, x + (column + !pass) * scale,
                         y + (row + !pass) * scale, scale, pass ? color : shadow);
}
