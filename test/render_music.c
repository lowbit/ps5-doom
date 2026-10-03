#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "music.h"

#define RATE 48000

static uint8_t *wad;
static long wad_size;

static const uint8_t *find_lump(const char *name, int *length)
{
    int count = wad[4] | wad[5] << 8 | wad[6] << 16 | wad[7] << 24;
    int table = wad[8] | wad[9] << 8 | wad[10] << 16 | wad[11] << 24;
    int i;

    for (i = 0; i < count; i++)
    {
        const uint8_t *entry = wad + table + i * 16;

        if (!strncmp((const char *)entry + 8, name, 8))
        {
            *length = entry[4] | entry[5] << 8 | entry[6] << 16 | entry[7] << 24;
            return wad + (entry[0] | entry[1] << 8 | entry[2] << 16 | entry[3] << 24);
        }
    }
    return NULL;
}

static void put_le(uint8_t *p, uint32_t value, int bytes)
{
    while (bytes--)
    {
        *p++ = value & 0xff;
        value >>= 8;
    }
}

int main(int argc, char **argv)
{
    const uint8_t *genmidi, *song;
    int genmidi_length, song_length, seconds, frames, i;
    float *mono;
    int16_t *pcm;
    uint8_t header[44];
    FILE *f;

    if (argc != 5)
    {
        fprintf(stderr, "usage: render_music <wad> <lump> <seconds> <out.wav>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    wad_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    wad = malloc(wad_size);
    if (fread(wad, 1, wad_size, f) != (size_t)wad_size)
        return 1;
    fclose(f);

    genmidi = find_lump("GENMIDI", &genmidi_length);
    song = find_lump(argv[2], &song_length);
    if (!genmidi || !song || music_init(RATE, genmidi, genmidi_length) || music_load(song))
    {
        fprintf(stderr, "cannot load music\n");
        return 1;
    }
    music_play(1);

    seconds = atoi(argv[3]);
    frames = seconds * RATE;
    mono = malloc(frames * sizeof(*mono));
    pcm = malloc(frames * sizeof(*pcm));
    for (i = 0; i < frames; i += 512)
        music_render(mono + i, frames - i < 512 ? frames - i : 512);
    for (i = 0; i < frames; i++)
    {
        float v = mono[i] * 0.7f * 32767.0f;
        pcm[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }

    memcpy(header, "RIFF", 4);
    put_le(header + 4, 36 + frames * 2, 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    put_le(header + 16, 16, 4);
    put_le(header + 20, 1, 2);
    put_le(header + 22, 1, 2);
    put_le(header + 24, RATE, 4);
    put_le(header + 28, RATE * 2, 4);
    put_le(header + 32, 2, 2);
    put_le(header + 34, 16, 2);
    memcpy(header + 36, "data", 4);
    put_le(header + 40, frames * 2, 4);

    f = fopen(argv[4], "wb");
    if (!f)
        return 1;
    fwrite(header, 1, sizeof(header), f);
    fwrite(pcm, sizeof(*pcm), frames, f);
    fclose(f);
    return 0;
}
