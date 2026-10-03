#include <math.h>
#include <pthread.h>
#include <string.h>

#include "audio.h"
#include "music.h"
#include "platform.h"

#define SFX_CHANNELS 16
#define BLOCK_FRAMES 512
#define MUSIC_GAIN 0.7f

typedef struct
{
    const uint8_t *samples;
    uint32_t length;
    uint64_t position, step;
    int rate, left, right;
    int handle;
} sfx_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static sfx_t sfx[SFX_CHANNELS];
static int serial;
static int music_ready;
static float music_block[BLOCK_FRAMES];

static void set_params(sfx_t *s, int volume, int sep, int pitch)
{
    int left, right, swing;

    s->step = (uint64_t)(s->rate * pow(2.0, (pitch - 128) / 64.0) / AUDIO_RATE * 4294967296.0);

    swing = sep + 1;
    left = volume - ((volume * swing * swing) >> 16);
    swing -= 257;
    right = volume - ((volume * swing * swing) >> 16);
    s->left = left < 0 ? 0 : left;
    s->right = right < 0 ? 0 : right;
}

static sfx_t *find(int handle)
{
    sfx_t *s;

    if (handle < 0)
        return NULL;
    s = &sfx[handle % SFX_CHANNELS];
    return s->samples && s->handle == handle ? s : NULL;
}

static int clip(int32_t value)
{
    return value > 32767 ? 32767 : value < -32768 ? -32768 : value;
}

static void mix_block(int16_t *out, int frames)
{
    int i, c;

    if (music_ready)
        music_render(music_block, frames);
    else
        memset(music_block, 0, frames * sizeof(*music_block));

    for (i = 0; i < frames; i++)
    {
        int32_t music = (int32_t)(music_block[i] * MUSIC_GAIN * 32767.0f);
        int32_t left = music, right = music;

        for (c = 0; c < SFX_CHANNELS; c++)
        {
            sfx_t *s = &sfx[c];
            uint32_t index;
            int32_t a, b, sample;

            if (!s->samples)
                continue;
            index = (uint32_t)(s->position >> 32);
            if (index >= s->length)
            {
                s->samples = NULL;
                continue;
            }
            a = s->samples[index] - 128;
            b = index + 1 < s->length ? s->samples[index + 1] - 128 : a;
            sample = (a << 8) + (int32_t)(((int64_t)(b - a) * (uint32_t)s->position) >> 24);
            left += sample * s->left / 127;
            right += sample * s->right / 127;
            s->position += s->step;
        }
        out[i * 2] = (int16_t)clip(left);
        out[i * 2 + 1] = (int16_t)clip(right);
    }
}

static void render(int16_t *stereo, int frames, void *user)
{
    (void)user;
    while (frames > 0)
    {
        int count = frames > BLOCK_FRAMES ? BLOCK_FRAMES : frames;

        pthread_mutex_lock(&lock);
        mix_block(stereo, count);
        pthread_mutex_unlock(&lock);
        stereo += count * 2;
        frames -= count;
    }
}

int audio_init(const uint8_t *genmidi, int genmidi_length)
{
    music_ready = music_init(AUDIO_RATE, genmidi, genmidi_length) == 0;
    if (!music_ready)
        plat_log("audio: no usable GENMIDI, music disabled\n");
    if (plat_audio_init(AUDIO_RATE, render, NULL))
    {
        plat_log("audio: no output device, sound disabled\n");
        return -1;
    }
    return 0;
}

void audio_shutdown(void)
{
    plat_audio_shutdown();
    if (music_ready)
        music_shutdown();
    music_ready = 0;
}

int audio_sfx_start(const uint8_t *samples, int length, int rate, int volume, int sep, int pitch)
{
    sfx_t *s = NULL;
    int c, handle;

    pthread_mutex_lock(&lock);
    for (c = 0; c < SFX_CHANNELS; c++)
        if (!sfx[c].samples)
        {
            s = &sfx[c];
            break;
        }
    if (!s)
    {
        s = &sfx[0];
        for (c = 1; c < SFX_CHANNELS; c++)
            if (sfx[c].handle < s->handle)
                s = &sfx[c];
    }

    serial++;
    handle = serial * SFX_CHANNELS + (int)(s - sfx);
    s->samples = samples;
    s->length = (uint32_t)length;
    s->position = 0;
    s->handle = handle;
    s->rate = rate;
    set_params(s, volume, sep, pitch);
    pthread_mutex_unlock(&lock);
    return handle;
}

void audio_sfx_stop(int handle)
{
    sfx_t *s;

    pthread_mutex_lock(&lock);
    if ((s = find(handle)))
        s->samples = NULL;
    pthread_mutex_unlock(&lock);
}

int audio_sfx_playing(int handle)
{
    int playing;

    pthread_mutex_lock(&lock);
    playing = find(handle) != NULL;
    pthread_mutex_unlock(&lock);
    return playing;
}

void audio_sfx_update(int handle, int volume, int sep, int pitch)
{
    sfx_t *s;

    pthread_mutex_lock(&lock);
    if ((s = find(handle)))
        set_params(s, volume, sep, pitch);
    pthread_mutex_unlock(&lock);
}

int audio_music_load(const uint8_t *mus)
{
    int result;

    pthread_mutex_lock(&lock);
    result = music_ready ? music_load(mus) : -1;
    pthread_mutex_unlock(&lock);
    return result;
}

void audio_music_play(int looping)
{
    pthread_mutex_lock(&lock);
    if (music_ready)
        music_play(looping);
    pthread_mutex_unlock(&lock);
}

void audio_music_stop(void)
{
    pthread_mutex_lock(&lock);
    if (music_ready)
        music_stop();
    pthread_mutex_unlock(&lock);
}

void audio_music_pause(int paused)
{
    pthread_mutex_lock(&lock);
    if (music_ready)
        music_pause(paused);
    pthread_mutex_unlock(&lock);
}

void audio_music_volume(int volume)
{
    pthread_mutex_lock(&lock);
    if (music_ready)
        music_volume(volume);
    pthread_mutex_unlock(&lock);
}
