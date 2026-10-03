#ifndef SCREEN_H
#define SCREEN_H

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define LINE_HEIGHT 10

typedef enum
{
    INK_RED,
    INK_WHITE,
    INK_GOLD,
    INK_GRAY,
    INK_GREEN,
    INKS
} ink_t;

int screen_init(const char *wad_path);
void screen_background(void);
void screen_logo(int y);
void screen_skull(int x, int y, int frame);
void screen_box(int x, int y, int width, int height, int filled);
int screen_text_width(const char *text, int scale);
void screen_text(int x, int y, const char *text, ink_t ink, int scale);
void screen_text_center(int y, const char *text, ink_t ink, int scale);
void screen_text_fit(int x, int y, int width, const char *text, ink_t ink, int keep_end);
int screen_wrap(int x, int y, int width, const char *text, ink_t ink);
void screen_present(void);

#endif
