#include <stdio.h>

#include "doomdef.h"
#include "i_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"

#include "audio.h"

#define SFX_HEADER 8
#define SFX_PADDING 16
#define MUSIC_HANDLE 1

typedef struct
{
    const byte *samples;
    int length;
    int rate;
} sample_t;

static sample_t samples[NUMSFX];
static int sound_ready;

static void cache_sample(int id)
{
    const byte *lump;
    int size, length;

    S_sfx[id].lumpnum = I_GetSfxLumpNum(&S_sfx[id]);
    lump = W_CacheLumpNum(S_sfx[id].lumpnum, PU_STATIC);
    size = W_LumpLength(S_sfx[id].lumpnum);
    S_sfx[id].data = (void *)lump;
    if (size < SFX_HEADER)
        return;

    length = lump[4] | lump[5] << 8 | lump[6] << 16 | lump[7] << 24;
    if (length > size - SFX_HEADER)
        length = size - SFX_HEADER;
    samples[id].samples = lump + SFX_HEADER;
    samples[id].length = length;
    samples[id].rate = lump[2] | lump[3] << 8;
    if (length > 2 * SFX_PADDING)
    {
        samples[id].samples += SFX_PADDING;
        samples[id].length -= 2 * SFX_PADDING;
    }
}

static const sample_t *sample_for(int id)
{
    sfxinfo_t *sfx = &S_sfx[id];

    while (sfx->link)
        sfx = sfx->link;
    return &samples[sfx - S_sfx];
}

void I_InitSound(void)
{
    int lump, id;

    for (id = 1; id < NUMSFX; id++)
        if (!S_sfx[id].link)
            cache_sample(id);
    for (id = 1; id < NUMSFX; id++)
        if (S_sfx[id].link)
            S_sfx[id].data = S_sfx[id].link->data;

    lump = W_CheckNumForName("GENMIDI");
    sound_ready = audio_init(lump >= 0 ? W_CacheLumpNum(lump, PU_STATIC) : NULL,
                             lump >= 0 ? W_LumpLength(lump) : 0) == 0;
}

void I_ShutdownSound(void)
{
    if (sound_ready)
        audio_shutdown();
    sound_ready = 0;
}

void I_SetChannels(void)
{
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[16];

    snprintf(name, sizeof(name), "ds%s", sfx->name);
    if (W_CheckNumForName(name) < 0)
        return W_GetNumForName("dspistol");
    return W_GetNumForName(name);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    const sample_t *sample = sample_for(id);

    (void)priority;
    if (!sound_ready || !sample->samples)
        return -1;
    return audio_sfx_start(sample->samples, sample->length, sample->rate,
                           vol * 127 / 15, sep, pitch);
}

void I_StopSound(int handle)
{
    audio_sfx_stop(handle);
}

int I_SoundIsPlaying(int handle)
{
    return audio_sfx_playing(handle);
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    audio_sfx_update(handle, vol * 127 / 15, sep, pitch);
}

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
    audio_music_stop();
}

void I_SetMusicVolume(int volume)
{
    audio_music_volume(volume * 127 / 15);
}

void I_PauseSong(int handle)
{
    (void)handle;
    audio_music_pause(1);
}

void I_ResumeSong(int handle)
{
    (void)handle;
    audio_music_pause(0);
}

int I_RegisterSong(void *data)
{
    return audio_music_load(data) == 0 ? MUSIC_HANDLE : 0;
}

void I_PlaySong(int handle, int looping)
{
    if (handle == MUSIC_HANDLE)
        audio_music_play(looping);
}

void I_StopSong(int handle)
{
    (void)handle;
    audio_music_stop();
}

void I_UnRegisterSong(int handle)
{
    (void)handle;
    audio_music_stop();
}
