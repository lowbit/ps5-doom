#ifndef PNG_H
#define PNG_H

#include <stdint.h>

int png_write_rgb(const char *path, const uint8_t *rgb, int width, int height);

#endif
