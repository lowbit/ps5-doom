#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png.h"

static uint32_t crc_table[256];

static void build_crc_table(void)
{
    uint32_t n, c;
    int k;

    for (n = 0; n < 256; n++)
    {
        c = n;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t length)
{
    while (length--)
        crc = crc_table[(crc ^ *data++) & 0xff] ^ (crc >> 8);
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t length)
{
    uint8_t header[8];
    uint8_t trailer[4];
    uint32_t crc;

    put32(header, length);
    memcpy(header + 4, type, 4);
    fwrite(header, 1, 8, f);
    fwrite(data, 1, length, f);
    crc = crc_update(0xffffffffu, header + 4, 4);
    crc = crc_update(crc, data, length) ^ 0xffffffffu;
    put32(trailer, crc);
    fwrite(trailer, 1, 4, f);
}

int png_write_rgb(const char *path, const uint8_t *rgb, int width, int height)
{
    size_t row = (size_t)width * 3 + 1;
    size_t raw_size = row * height;
    size_t blocks = (raw_size + 65534) / 65535;
    size_t z_size = 2 + raw_size + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_size), *z = malloc(z_size), *zp;
    uint8_t ihdr[13];
    uint32_t a = 1, b = 0;
    size_t i, done;
    FILE *f;

    if (!raw || !z)
    {
        free(raw);
        free(z);
        return -1;
    }
    if (!crc_table[1])
        build_crc_table();

    for (i = 0; i < (size_t)height; i++)
    {
        raw[i * row] = 0;
        memcpy(raw + i * row + 1, rgb + i * width * 3, (size_t)width * 3);
    }
    for (i = 0; i < raw_size; i++)
    {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }

    zp = z;
    *zp++ = 0x78;
    *zp++ = 0x01;
    for (done = 0; done < raw_size;)
    {
        size_t n = raw_size - done > 65535 ? 65535 : raw_size - done;

        *zp++ = done + n == raw_size;
        *zp++ = n & 0xff;
        *zp++ = n >> 8;
        *zp++ = ~n & 0xff;
        *zp++ = (~n >> 8) & 0xff;
        memcpy(zp, raw + done, n);
        zp += n;
        done += n;
    }
    put32(zp, b << 16 | a);
    zp += 4;

    put32(ihdr, width);
    put32(ihdr + 4, height);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;

    f = fopen(path, "wb");
    if (f)
    {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        write_chunk(f, "IHDR", ihdr, 13);
        write_chunk(f, "IDAT", z, (uint32_t)(zp - z));
        write_chunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(raw);
    free(z);
    return f ? 0 : -1;
}
