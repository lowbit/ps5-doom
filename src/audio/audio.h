#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_RATE 48000

int audio_init(const uint8_t *genmidi, int genmidi_length);
void audio_shutdown(void);

int audio_sfx_start(const uint8_t *samples, int length, int rate, int volume, int sep, int pitch);
void audio_sfx_stop(int handle);
int audio_sfx_playing(int handle);
void audio_sfx_update(int handle, int volume, int sep, int pitch);

int audio_music_load(const uint8_t *mus);
void audio_music_play(int looping);
void audio_music_stop(void);
void audio_music_pause(int paused);
void audio_music_volume(int volume);

#endif
