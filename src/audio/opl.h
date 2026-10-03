#ifndef OPL_H
#define OPL_H

#include <stdint.h>

#define OPL_CHANNELS 18

typedef struct opl opl_t;

opl_t *opl_create(int sample_rate);
void opl_destroy(opl_t *opl);
void opl_reset(opl_t *opl);
void opl_write(opl_t *opl, int reg, uint8_t value);
void opl_render(opl_t *opl, float *out, int frames);

#endif
