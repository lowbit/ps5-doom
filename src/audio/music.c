#include <math.h>
#include <string.h>

#include "music.h"
#include "opl.h"

#define VOICES 9
#define MUS_CHANNELS 16
#define PERCUSSION_CHANNEL 15
#define PERCUSSION_FIRST 35
#define PERCUSSION_LAST 81
#define INSTRUMENTS 175
#define INSTRUMENT_SIZE 36
#define GENMIDI_HEADER "#OPL_II#"
#define TICK_RATE 140.0
#define CHIP_RATE 49716.0

#define MAX_EVENTS_WITHOUT_DELAY 65536

#define FLAG_FIXED_PITCH 0x0001
#define FLAG_DOUBLE_VOICE 0x0004

typedef struct
{
    uint8_t tremolo, attack, sustain, wave, scale, level;
} operator_t;

typedef struct
{
    operator_t mod, car;
    uint8_t feedback;
    int16_t base_note;
} layer_t;

typedef struct
{
    uint16_t flags;
    uint8_t fine_tuning, fixed_note;
    layer_t layers[2];
} instrument_t;

typedef struct
{
    const instrument_t *instrument;
    int volume, bend, velocity;
} channel_t;

typedef struct
{
    int active, channel, note, velocity, layer;
    const instrument_t *instrument;
    uint32_t age;
} voice_t;

static struct
{
    opl_t *opl;
    double samples_per_tick;
    instrument_t instruments[INSTRUMENTS];

    const uint8_t *score;
    int score_length, position;
    int playing, paused, looping;
    double wait;

    int volume;
    channel_t channels[MUS_CHANNELS];
    voice_t voices[VOICES];
    uint32_t age;
} m;

static void parse_operator(operator_t *op, const uint8_t *p)
{
    op->tremolo = p[0];
    op->attack = p[1];
    op->sustain = p[2];
    op->wave = p[3];
    op->scale = p[4];
    op->level = p[5];
}

static void parse_instrument(instrument_t *ins, const uint8_t *p)
{
    int i;

    ins->flags = p[0] | p[1] << 8;
    ins->fine_tuning = p[2];
    ins->fixed_note = p[3];
    for (i = 0; i < 2; i++)
    {
        const uint8_t *l = p + 4 + i * 16;

        parse_operator(&ins->layers[i].mod, l);
        ins->layers[i].feedback = l[6];
        parse_operator(&ins->layers[i].car, l + 7);
        ins->layers[i].base_note = (int16_t)(l[14] | l[15] << 8);
    }
}

static int operator_offset(int voice, int carrier)
{
    return (voice / 3) * 8 + voice % 3 + (carrier ? 3 : 0);
}

static int attenuation(const voice_t *voice)
{
    double gain = voice->velocity / 127.0 * m.channels[voice->channel].volume / 127.0 *
                  m.volume / 127.0;
    int steps;

    if (gain <= 0)
        return 63;
    steps = (int)(-20.0 * log10(gain) / 0.75 + 0.5);
    return steps > 63 ? 63 : steps;
}

static void write_levels(const voice_t *voice)
{
    const layer_t *layer = &voice->instrument->layers[voice->layer];
    int att = attenuation(voice);
    int car = layer->car.level + att;
    int mod = layer->mod.level;

    if (layer->feedback & 1)
        mod += att;
    opl_write(m.opl, 0x40 + operator_offset(voice - m.voices, 1),
              (layer->car.scale & 0xc0) | (car > 63 ? 63 : car));
    opl_write(m.opl, 0x40 + operator_offset(voice - m.voices, 0),
              (layer->mod.scale & 0xc0) | (mod > 63 ? 63 : mod));
}

static void write_operator(int offset, const operator_t *op)
{
    opl_write(m.opl, 0x20 + offset, op->tremolo);
    opl_write(m.opl, 0x60 + offset, op->attack);
    opl_write(m.opl, 0x80 + offset, op->sustain);
    opl_write(m.opl, 0xe0 + offset, op->wave);
}

static void write_frequency(const voice_t *voice, int key)
{
    const instrument_t *ins = voice->instrument;
    const layer_t *layer = &ins->layers[voice->layer];
    int index = voice - m.voices;
    double note, freq;
    int block = 0, fnum;

    note = (ins->flags & FLAG_FIXED_PITCH) ? ins->fixed_note : voice->note;
    note += layer->base_note;
    if (voice->layer)
        note += (ins->fine_tuning / 2 - 64) / 32.0;
    note += m.channels[voice->channel].bend / 64.0;

    freq = 440.0 * pow(2.0, (note - 69.0) / 12.0);
    while (block < 7 && freq * (1 << (20 - block)) / CHIP_RATE >= 1024)
        block++;
    fnum = (int)(freq * (1 << (20 - block)) / CHIP_RATE + 0.5);
    if (fnum > 1023)
        fnum = 1023;

    opl_write(m.opl, 0xa0 + index, fnum & 0xff);
    opl_write(m.opl, 0xb0 + index, (key ? 0x20 : 0) | block << 2 | fnum >> 8);
}

static void release_voice(voice_t *voice)
{
    if (!voice->active)
        return;
    voice->active = 0;
    write_frequency(voice, 0);
}

static voice_t *find_voice(int may_steal)
{
    voice_t *best = NULL;
    int i;

    for (i = 0; i < VOICES; i++)
    {
        voice_t *v = &m.voices[i];

        if (!v->active && (!best || v->age < best->age))
            best = v;
    }
    if (best || !may_steal)
        return best;

    for (i = 0; i < VOICES; i++)
    {
        voice_t *v = &m.voices[i];

        if (!best || v->channel > best->channel ||
            (v->channel == best->channel && v->age < best->age))
            best = v;
    }
    release_voice(best);
    return best;
}

static void start_voice(voice_t *voice, int channel, int note, int velocity,
                        const instrument_t *ins, int layer)
{
    const layer_t *l = &ins->layers[layer];
    int index = voice - m.voices;

    voice->active = 1;
    voice->channel = channel;
    voice->note = note;
    voice->velocity = velocity;
    voice->instrument = ins;
    voice->layer = layer;
    voice->age = ++m.age;

    write_operator(operator_offset(index, 0), &l->mod);
    write_operator(operator_offset(index, 1), &l->car);
    opl_write(m.opl, 0xc0 + index, l->feedback | 0x30);
    write_levels(voice);
    write_frequency(voice, 1);
}

static void note_on(int channel, int note, int velocity)
{
    const instrument_t *ins;
    voice_t *voice;

    if (channel == PERCUSSION_CHANNEL)
    {
        if (note < PERCUSSION_FIRST || note > PERCUSSION_LAST)
            return;
        ins = &m.instruments[128 + note - PERCUSSION_FIRST];
    }
    else
        ins = m.channels[channel].instrument;

    voice = find_voice(1);
    start_voice(voice, channel, note, velocity, ins, 0);
    if ((ins->flags & FLAG_DOUBLE_VOICE) && (voice = find_voice(0)))
        start_voice(voice, channel, note, velocity, ins, 1);
}

static void note_off(int channel, int note)
{
    int i;

    for (i = 0; i < VOICES; i++)
        if (m.voices[i].active && m.voices[i].channel == channel && m.voices[i].note == note)
            release_voice(&m.voices[i]);
}

static void channel_off(int channel)
{
    int i;

    for (i = 0; i < VOICES; i++)
        if (m.voices[i].active && (channel < 0 || m.voices[i].channel == channel))
            release_voice(&m.voices[i]);
}

static void refresh_channel(int channel, int frequency)
{
    int i;

    for (i = 0; i < VOICES; i++)
    {
        voice_t *v = &m.voices[i];

        if (!v->active || (channel >= 0 && v->channel != channel))
            continue;
        if (frequency)
            write_frequency(v, 1);
        else
            write_levels(v);
    }
}

static void reset_channels(void)
{
    int i;

    for (i = 0; i < MUS_CHANNELS; i++)
    {
        m.channels[i].instrument = &m.instruments[0];
        m.channels[i].volume = 127;
        m.channels[i].bend = 0;
        m.channels[i].velocity = 127;
    }
}

static void controller(int channel, int number, int value)
{
    switch (number)
    {
    case 0:
        m.channels[channel].instrument = &m.instruments[value & 127];
        break;
    case 3:
        m.channels[channel].volume = value > 127 ? 127 : value;
        refresh_channel(channel, 0);
        break;
    default:
        break;
    }
}

static void system_event(int channel, int number)
{
    switch (number)
    {
    case 10:
    case 11:
        channel_off(channel);
        break;
    case 14:
        m.channels[channel].volume = 127;
        m.channels[channel].bend = 0;
        refresh_channel(channel, 1);
        refresh_channel(channel, 0);
        break;
    default:
        break;
    }
}

static int next_byte(void)
{
    if (m.position >= m.score_length)
        return -1;
    return m.score[m.position++];
}

static void end_of_score(void)
{
    if (m.looping)
    {
        m.position = 0;
        return;
    }
    channel_off(-1);
    m.playing = 0;
}

static void run_events(void)
{
    int budget = MAX_EVENTS_WITHOUT_DELAY;

    while (m.playing)
    {
        int event = next_byte();
        int channel, a, b;

        if (!budget--)
        {
            channel_off(-1);
            m.playing = 0;
            return;
        }

        if (event < 0)
        {
            end_of_score();
            continue;
        }
        channel = event & 15;

        switch ((event >> 4) & 7)
        {
        case 0:
            note_off(channel, next_byte() & 127);
            break;
        case 1:
            a = next_byte();
            if (a & 128)
                m.channels[channel].velocity = next_byte() & 127;
            note_on(channel, a & 127, m.channels[channel].velocity);
            break;
        case 2:
            m.channels[channel].bend = (next_byte() & 255) - 128;
            refresh_channel(channel, 1);
            break;
        case 3:
            system_event(channel, next_byte());
            break;
        case 4:
            a = next_byte();
            b = next_byte();
            controller(channel, a, b);
            break;
        case 6:
            end_of_score();
            continue;
        default:
            break;
        }

        if (event & 128)
        {
            int delay = 0;

            do
            {
                b = next_byte();
                if (b < 0)
                    break;
                delay = delay * 128 + (b & 127);
            } while (b & 128);
            if (delay)
            {
                m.wait += delay * m.samples_per_tick;
                return;
            }
        }
    }
}

int music_init(int sample_rate, const uint8_t *genmidi, int length)
{
    int i;

    if (!genmidi || length < 8 + INSTRUMENTS * INSTRUMENT_SIZE ||
        memcmp(genmidi, GENMIDI_HEADER, 8))
        return -1;
    for (i = 0; i < INSTRUMENTS; i++)
        parse_instrument(&m.instruments[i], genmidi + 8 + i * INSTRUMENT_SIZE);

    m.opl = opl_create(sample_rate);
    if (!m.opl)
        return -1;
    m.samples_per_tick = sample_rate / TICK_RATE;
    m.volume = 127;
    reset_channels();
    return 0;
}

void music_shutdown(void)
{
    opl_destroy(m.opl);
    m.opl = NULL;
    m.playing = 0;
}

int music_load(const uint8_t *mus)
{
    int start;

    if (!m.opl || memcmp(mus, "MUS\x1a", 4))
        return -1;
    music_stop();
    m.score_length = mus[4] | mus[5] << 8;
    start = mus[6] | mus[7] << 8;
    m.score = mus + start;
    return 0;
}

void music_play(int looping)
{
    if (!m.score)
        return;
    channel_off(-1);
    reset_channels();
    m.position = 0;
    m.wait = 0;
    m.looping = looping;
    m.paused = 0;
    m.playing = 1;
}

void music_stop(void)
{
    if (!m.opl)
        return;
    channel_off(-1);
    m.playing = 0;
}

void music_pause(int paused)
{
    m.paused = paused;
}

void music_volume(int volume)
{
    m.volume = volume < 0 ? 0 : volume > 127 ? 127 : volume;
    if (m.opl)
        refresh_channel(-1, 0);
}

void music_render(float *out, int frames)
{
    int done = 0;

    if (!m.opl || m.paused)
    {
        memset(out, 0, frames * sizeof(*out));
        return;
    }

    while (done < frames)
    {
        int chunk = frames - done;

        while (m.playing && m.wait < 1)
            run_events();
        if (m.playing && m.wait < chunk)
            chunk = (int)m.wait;

        opl_render(m.opl, out + done, chunk);
        done += chunk;
        if (m.playing)
            m.wait -= chunk;
    }
}
