#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "glyphs.h"
#include "platform.h"
#include "qrcodegen.h"
#include "screen.h"

#define FONT_FIRST '!'
#define FONT_LAST '_'
#define FONT_COUNT (FONT_LAST - FONT_FIRST + 1)
#define SPACE_WIDTH 4
#define LUMP_ENTRY 16
#define BACKGROUND_SHADE 29
#define COLORMAP_SIZE (34 * 256)
#define MAX_PATCH_HEIGHT 256
#define QR_VERSION_MAX 5
#define QR_QUIET 2

static uint8_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
static uint32_t palette[256];
static uint8_t rgb[768];
static uint8_t shade[256];
static uint8_t inks[INKS][256];
static uint8_t *font[FONT_COUNT];
static uint8_t *title, *logo, *skulls[2];
static uint8_t outline_color, fill_color, paper_color, ink_color;

static uint32_t le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static int s16(const uint8_t *p)
{
    return (int16_t)(p[0] | p[1] << 8);
}

static uint8_t *load(int fd, const uint8_t *directory, uint32_t count, const char *name, uint32_t *size)
{
    uint32_t i = count;

    while (i--)
    {
        const uint8_t *entry = directory + i * LUMP_ENTRY;
        uint32_t length = le32(entry + 4);
        uint8_t *data;

        if (strncasecmp((const char *)entry + 8, name, 8))
            continue;
        data = malloc(length ? length : 1);
        if (!data || lseek(fd, le32(entry), SEEK_SET) < 0 || read(fd, data, length) != (ssize_t)length)
        {
            free(data);
            return NULL;
        }
        if (size)
            *size = length;
        return data;
    }
    return NULL;
}

static uint8_t nearest(int r, int g, int b)
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

static void build_inks(void)
{
    int i;

    for (i = 0; i < 256; i++)
    {
        int r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
        int level = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int bright = level * 5 / 4 + 30 > 255 ? 255 : level * 5 / 4 + 30;

        inks[INK_RED][i] = (uint8_t)i;
        inks[INK_WHITE][i] = nearest(bright, bright, bright);
        inks[INK_GOLD][i] = nearest(bright, bright * 3 / 4, bright / 5);
        inks[INK_GRAY][i] = nearest(level * 3 / 5, level * 3 / 5, level * 3 / 5);
        inks[INK_GREEN][i] = nearest(level / 4, bright, level / 4);
        palette[i] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    outline_color = nearest(150, 150, 150);
    fill_color = nearest(190, 30, 30);
    paper_color = nearest(255, 255, 255);
    ink_color = nearest(0, 0, 0);
}

int screen_init(const char *wad_path)
{
    uint8_t header[12], *directory, *playpal, *colormap;
    uint32_t count, size = 0;
    char name[9];
    int fd = open(wad_path, O_RDONLY), i;

    if (fd < 0 || read(fd, header, sizeof(header)) != sizeof(header))
    {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    count = le32(header + 4);
    directory = malloc((size_t)count * LUMP_ENTRY);
    if (!directory || lseek(fd, le32(header + 8), SEEK_SET) < 0 ||
        read(fd, directory, (size_t)count * LUMP_ENTRY) != (ssize_t)count * LUMP_ENTRY)
    {
        free(directory);
        close(fd);
        return -1;
    }

    playpal = load(fd, directory, count, "PLAYPAL", &size);
    if (playpal && size >= sizeof(rgb))
        memcpy(rgb, playpal, sizeof(rgb));
    colormap = load(fd, directory, count, "COLORMAP", &size);
    for (i = 0; i < 256; i++)
        shade[i] = colormap && size >= COLORMAP_SIZE ? colormap[BACKGROUND_SHADE * 256 + i] : 0;
    for (i = 0; i < FONT_COUNT; i++)
    {
        snprintf(name, sizeof(name), "STCFN%03d", FONT_FIRST + i);
        font[i] = load(fd, directory, count, name, NULL);
    }
    title = load(fd, directory, count, "TITLEPIC", NULL);
    logo = load(fd, directory, count, "M_DOOM", NULL);
    skulls[0] = load(fd, directory, count, "M_SKULL1", NULL);
    skulls[1] = load(fd, directory, count, "M_SKULL2", NULL);
    free(colormap);
    free(directory);
    close(fd);
    if (!playpal || !font['A' - FONT_FIRST])
    {
        free(playpal);
        return -1;
    }
    free(playpal);
    build_inks();
    return 0;
}

static void draw_patch(int x, int y, const uint8_t *patch, const uint8_t *map, int num, int den)
{
    int width = s16(patch), height = s16(patch + 2);
    int target_width = width * num / den, target_height = height * num / den;
    uint8_t column[MAX_PATCH_HEIGHT], mask[MAX_PATCH_HEIGHT];
    int tx, ty;

    if (height > MAX_PATCH_HEIGHT)
        return;
    x -= s16(patch + 4) * num / den;
    y -= s16(patch + 6) * num / den;
    for (tx = 0; tx < target_width; tx++)
    {
        const uint8_t *post;
        int px = x + tx;

        if (px < 0 || px >= SCREEN_WIDTH)
            continue;
        memset(mask, 0, sizeof(mask));
        post = patch + le32(patch + 8 + (tx * den / num) * 4);
        while (*post != 0xff)
        {
            int top = post[0], length = post[1], i;

            for (i = 0; i < length && top + i < MAX_PATCH_HEIGHT; i++)
            {
                column[top + i] = post[3 + i];
                mask[top + i] = 1;
            }
            post += length + 4;
        }
        for (ty = 0; ty < target_height; ty++)
        {
            int sy = ty * den / num, py = y + ty;

            if (mask[sy] && py >= 0 && py < SCREEN_HEIGHT)
                pixels[py * SCREEN_WIDTH + px] = map ? map[column[sy]] : column[sy];
        }
    }
}

void screen_background(void)
{
    int i;

    memset(pixels, 0, sizeof(pixels));
    if (title)
        draw_patch(0, 0, title, NULL, 1, 1);
    for (i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
        pixels[i] = shade[pixels[i]];
}

void screen_logo(int y)
{
    if (logo)
        draw_patch((SCREEN_WIDTH - s16(logo)) / 2 + s16(logo + 4), y + s16(logo + 6), logo, NULL, 1, 1);
}

void screen_skull(int x, int y, int frame)
{
    const uint8_t *skull = skulls[frame & 1] ? skulls[frame & 1] : skulls[0];

    if (skull)
        draw_patch(x + s16(skull + 4) / 2, y + s16(skull + 6) / 2, skull, NULL, 1, 2);
}

void screen_box(int x, int y, int width, int height, int filled)
{
    int i, j;

    for (j = y; j < y + height; j++)
        for (i = x; i < x + width; i++)
        {
            int edge = i == x || j == y || i == x + width - 1 || j == y + height - 1;

            if (i >= 0 && i < SCREEN_WIDTH && j >= 0 && j < SCREEN_HEIGHT && (filled || edge))
                pixels[j * SCREEN_WIDTH + i] = filled ? fill_color : outline_color;
        }
}

static const uint8_t *glyph(char c)
{
    c = (char)toupper((unsigned char)c);
    return c >= FONT_FIRST && c <= FONT_LAST ? font[c - FONT_FIRST] : NULL;
}

int screen_text_width(const char *text, int scale)
{
    int width = 0;

    for (; *text && *text != '\n'; text++)
        if (glyph_is_button(*text))
            width += GLYPH_ADVANCE * scale;
        else
            width += (glyph(*text) ? s16(glyph(*text)) : SPACE_WIDTH) * scale;
    return width;
}

void screen_text(int x, int y, const char *text, ink_t ink, int scale)
{
    for (; *text && *text != '\n'; text++)
    {
        const uint8_t *g = glyph(*text);

        if (glyph_is_button(*text))
        {
            glyph_draw(pixels, SCREEN_WIDTH, SCREEN_HEIGHT, x, y, *text, scale, rgb);
            x += GLYPH_ADVANCE * scale;
            continue;
        }
        if (g)
            draw_patch(x, y, g, inks[ink], scale, 1);
        x += (g ? s16(g) : SPACE_WIDTH) * scale;
    }
}

void screen_text_center(int y, const char *text, ink_t ink, int scale)
{
    screen_text((SCREEN_WIDTH - screen_text_width(text, scale)) / 2, y, text, ink, scale);
}

void screen_text_fit(int x, int y, int width, const char *text, ink_t ink, int keep_end)
{
    char shown[512];
    size_t length = strlen(text), cut;

    if (screen_text_width(text, 1) <= width)
    {
        screen_text(x, y, text, ink, 1);
        return;
    }
    for (cut = 1; cut < length; cut++)
    {
        if (keep_end)
            snprintf(shown, sizeof(shown), "...%s", text + cut);
        else
            snprintf(shown, sizeof(shown), "%.*s...", (int)(length - cut), text);
        if (screen_text_width(shown, 1) <= width)
            break;
    }
    screen_text(x, y, shown, ink, 1);
}

int screen_wrap(int x, int y, int width, const char *text, ink_t ink)
{
    char line[256];
    size_t used = 0;

    while (*text)
    {
        const char *word = text;
        size_t word_length = strcspn(word, " \n");
        char candidate[256];

        snprintf(candidate, sizeof(candidate), "%.*s%s%.*s", (int)used, line, used ? " " : "",
                 (int)word_length, word);
        if (used && screen_text_width(candidate, 1) > width)
        {
            line[used] = 0;
            screen_text(x, y, line, ink, 1);
            y += LINE_HEIGHT;
            used = 0;
            continue;
        }
        used = strlen(candidate) < sizeof(line) - 1 ? strlen(candidate) : sizeof(line) - 1;
        memcpy(line, candidate, used);
        text = word + word_length;
        if (*text == '\n' || !*text)
        {
            line[used] = 0;
            screen_text(x, y, line, ink, 1);
            y += LINE_HEIGHT;
            used = 0;
        }
        if (*text)
            text++;
    }
    return y;
}

static void fill(int x, int y, int width, int height, uint8_t color)
{
    int i, j;

    for (j = y; j < y + height; j++)
        for (i = x; i < x + width; i++)
            if (i >= 0 && i < SCREEN_WIDTH && j >= 0 && j < SCREEN_HEIGHT)
                pixels[j * SCREEN_WIDTH + i] = color;
}

int screen_qr(int x, int y, const char *text, int module)
{
    static char encoded[128];
    static uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
    static int ready;
    uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
    int size, i, j;

    if (strcmp(encoded, text))
    {
        snprintf(encoded, sizeof(encoded), "%s", text);
        ready = qrcodegen_encodeText(text, scratch, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                     QR_VERSION_MAX, qrcodegen_Mask_AUTO, true);
    }
    if (!ready)
        return 0;
    size = qrcodegen_getSize(code);
    fill(x, y, (size + 2 * QR_QUIET) * module, (size + 2 * QR_QUIET) * module, paper_color);
    for (j = 0; j < size; j++)
        for (i = 0; i < size; i++)
            if (qrcodegen_getModule(code, i, j))
                fill(x + (QR_QUIET + i) * module, y + (QR_QUIET + j) * module, module, module, ink_color);
    return (size + 2 * QR_QUIET) * module;
}

void screen_present(void)
{
    plat_video_present(pixels, palette);
}
