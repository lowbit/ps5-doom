#ifndef MUSIC_H
#define MUSIC_H

#include <stdint.h>

int music_init(int sample_rate, const uint8_t *genmidi, int length);
void music_shutdown(void);
int music_load(const uint8_t *mus);
void music_play(int looping);
void music_stop(void);
void music_pause(int paused);
void music_volume(int volume);
void music_render(float *out, int frames);

#endif
