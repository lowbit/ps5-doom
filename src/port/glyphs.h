#ifndef GLYPHS_H
#define GLYPHS_H

#include <stdint.h>

// DualSense face buttons inside text: each is one control character that the text drawers
// (Doom's menu font and the launcher) draw as the button symbol in its PlayStation colour.
#define GLYPH_CROSS "\001"
#define GLYPH_CIRCLE "\002"
#define GLYPH_SQUARE "\003"
#define GLYPH_TRIANGLE "\004"

// Horizontal advance of a glyph at scale 1, matching the 7-pixel-high menu font.
#define GLYPH_ADVANCE 10

int glyph_is_button(int c);

// Draws button glyph c at (x, y) into an 8-bit screen, picking the colours from rgb (768 bytes).
void glyph_draw(uint8_t *screen, int width, int height, int x, int y, int c, int scale,
                const uint8_t *rgb);

#endif
