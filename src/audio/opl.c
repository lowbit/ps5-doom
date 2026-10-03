#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "opl.h"

#define CHIP_RATE 49716.0
#define WAVE_BITS 10
#define WAVE_SIZE (1 << WAVE_BITS)
#define GAIN_STEPS 1024
#define ENV_SILENT 511.0f
#define SLOTS (OPL_CHANNELS * 2)
#define CHANNEL_SCALE 0.25f
#define TREMOLO_HZ 3.7
#define VIBRATO_HZ 6.07

enum
{
    EG_OFF,
    EG_ATTACK,
    EG_DECAY,
    EG_SUSTAIN,
    EG_RELEASE
};

typedef struct
{
    uint8_t tremolo, vibrato, sustained, ksr, mult;
    uint8_t ksl, level, attack, decay, sustain, release, wave;

    uint32_t phase, step;
    int stage;
    float env, ksl_att, sustain_env;
    float attack_k, decay_inc, release_inc;
    float out, prev;
} slot_t;

typedef struct
{
    uint16_t fnum;
    uint8_t block, key, feedback, additive;
    slot_t *mod, *car;
} channel_t;

struct opl
{
    double rate_scale;
    slot_t slots[SLOTS];
    channel_t channels[OPL_CHANNELS];
    uint8_t deep_tremolo, deep_vibrato;
    double tremolo_phase, tremolo_step;
    double vibrato_phase, vibrato_step;
};

static float wave_table[8][WAVE_SIZE];
static float gain_table[GAIN_STEPS];
static int tables_ready;

static const uint8_t mult_x2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
static const uint8_t ksl_rom[16] = {0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64};
static const uint8_t ksl_shift[4] = {0, 1, 2, 0};

static void build_tables(void)
{
    int i;

    for (i = 0; i < WAVE_SIZE; i++)
    {
        double angle = 2 * M_PI * i / WAVE_SIZE;
        float s = (float)sin(angle);
        float s2 = (float)sin(2 * angle);
        int first_half = i < WAVE_SIZE / 2;
        int quarter = (i / (WAVE_SIZE / 4)) & 1;
        int folded = first_half ? i : WAVE_SIZE - 1 - i;

        wave_table[0][i] = s;
        wave_table[1][i] = first_half ? s : 0;
        wave_table[2][i] = fabsf(s);
        wave_table[3][i] = quarter ? 0 : fabsf(s);
        wave_table[4][i] = first_half ? s2 : 0;
        wave_table[5][i] = first_half ? fabsf(s2) : 0;
        wave_table[6][i] = first_half ? 1.0f : -1.0f;
        wave_table[7][i] = (first_half ? 1.0f : -1.0f) * exp2f(-folded / 32.0f);
    }
    for (i = 0; i < GAIN_STEPS; i++)
        gain_table[i] = exp2f(-i / 32.0f);
    tables_ready = 1;
}

static int effective_rate(int reg_rate, const slot_t *slot, const channel_t *channel)
{
    int ksv = (channel->block << 1) | ((channel->fnum >> 9) & 1);
    int rate;

    if (!reg_rate)
        return 0;
    rate = reg_rate * 4 + (slot->ksr ? ksv : ksv >> 2);
    return rate > 63 ? 63 : rate;
}

static double rate_divisor(int rate)
{
    return (double)(1 << (rate >> 2)) * ((rate & 3) + 4) / 4.0;
}

static float linear_increment(int rate, double rate_scale)
{
    double seconds;

    if (!rate)
        return 0;
    seconds = 78.56 / rate_divisor(rate);
    return (float)(ENV_SILENT / (seconds * CHIP_RATE) * rate_scale);
}

static void update_slot(opl_t *opl, slot_t *slot, const channel_t *channel)
{
    int ksl, attack_rate;
    uint64_t step;

    step = (uint64_t)((double)((uint32_t)channel->fnum << channel->block) *
                      mult_x2[slot->mult] * 2048.0 * opl->rate_scale);
    slot->step = (uint32_t)step;

    ksl = (ksl_rom[channel->fnum >> 6] << 2) - ((8 - channel->block) << 5);
    slot->ksl_att = slot->ksl && ksl > 0 ? (float)(ksl >> ksl_shift[slot->ksl]) : 0;

    attack_rate = effective_rate(slot->attack, slot, channel);
    if (attack_rate >= 60)
        slot->attack_k = 1;
    else if (attack_rate == 0)
        slot->attack_k = 0;
    else
    {
        double samples = 5.65248 / rate_divisor(attack_rate) * CHIP_RATE / opl->rate_scale;
        slot->attack_k = (float)(1.0 - exp(log(1.0 / ENV_SILENT) / samples));
    }
    slot->decay_inc = linear_increment(effective_rate(slot->decay, slot, channel), opl->rate_scale);
    slot->release_inc = linear_increment(effective_rate(slot->release, slot, channel), opl->rate_scale);
    slot->sustain_env = (slot->sustain == 15 ? 31 : slot->sustain) * 16.0f;
}

static void update_channel(opl_t *opl, channel_t *channel)
{
    update_slot(opl, channel->mod, channel);
    update_slot(opl, channel->car, channel);
}

static void key_on(slot_t *slot)
{
    slot->phase = 0;
    slot->stage = EG_ATTACK;
    if (slot->attack_k >= 1)
    {
        slot->env = 0;
        slot->stage = EG_DECAY;
    }
}

static void key_off(slot_t *slot)
{
    if (slot->stage != EG_OFF)
        slot->stage = EG_RELEASE;
}

static void advance_envelope(slot_t *slot)
{
    switch (slot->stage)
    {
    case EG_ATTACK:
        slot->env -= slot->env * slot->attack_k;
        if (slot->env < 0.5f)
        {
            slot->env = 0;
            slot->stage = EG_DECAY;
        }
        break;
    case EG_DECAY:
        slot->env += slot->decay_inc;
        if (slot->env >= slot->sustain_env)
        {
            slot->env = slot->sustain_env;
            slot->stage = slot->sustained ? EG_SUSTAIN : EG_RELEASE;
        }
        break;
    case EG_RELEASE:
        slot->env += slot->release_inc;
        if (slot->env >= ENV_SILENT)
        {
            slot->env = ENV_SILENT;
            slot->stage = EG_OFF;
        }
        break;
    default:
        break;
    }
}

static float slot_output(slot_t *slot, int modulation, float tremolo, double vibrato)
{
    float att;
    int index;

    advance_envelope(slot);
    slot->phase += slot->vibrato ? (uint32_t)(uint64_t)(slot->step * vibrato) : slot->step;

    att = slot->env + slot->level * 4 + slot->ksl_att + (slot->tremolo ? tremolo : 0);
    if (slot->stage == EG_OFF || att >= GAIN_STEPS)
        return 0;
    index = ((int)(slot->phase >> (32 - WAVE_BITS)) + modulation) & (WAVE_SIZE - 1);
    return wave_table[slot->wave][index] * gain_table[(int)att];
}

static float channel_output(channel_t *channel, float tremolo, double vibrato)
{
    slot_t *mod = channel->mod, *car = channel->car;
    int feedback = 0;

    if (mod->stage == EG_OFF && car->stage == EG_OFF)
    {
        mod->out = mod->prev = 0;
        return 0;
    }

    if (channel->feedback)
        feedback = (int)((mod->prev + mod->out) * 4095.0f) >> (9 - channel->feedback);
    mod->prev = mod->out;
    mod->out = slot_output(mod, feedback, tremolo, vibrato);

    if (channel->additive)
        return mod->out + slot_output(car, 0, tremolo, vibrato);
    return slot_output(car, (int)(mod->out * 4095.0f), tremolo, vibrato);
}

opl_t *opl_create(int sample_rate)
{
    opl_t *opl;

    if (!tables_ready)
        build_tables();
    opl = calloc(1, sizeof(*opl));
    if (!opl)
        return NULL;
    opl->rate_scale = CHIP_RATE / sample_rate;
    opl->tremolo_step = TREMOLO_HZ / sample_rate;
    opl->vibrato_step = VIBRATO_HZ / sample_rate;
    opl_reset(opl);
    return opl;
}

void opl_destroy(opl_t *opl)
{
    free(opl);
}

void opl_reset(opl_t *opl)
{
    int c;

    memset(opl->slots, 0, sizeof(opl->slots));
    memset(opl->channels, 0, sizeof(opl->channels));
    for (c = 0; c < OPL_CHANNELS; c++)
    {
        int bank = c / 9, local = c % 9;
        int mod = bank * 18 + (local / 3) * 6 + local % 3;

        opl->channels[c].mod = &opl->slots[mod];
        opl->channels[c].car = &opl->slots[mod + 3];
    }
    for (c = 0; c < SLOTS; c++)
    {
        opl->slots[c].env = ENV_SILENT;
        opl->slots[c].stage = EG_OFF;
    }
    for (c = 0; c < OPL_CHANNELS; c++)
        update_channel(opl, &opl->channels[c]);
}

static channel_t *slot_channel(opl_t *opl, int slot_index)
{
    int bank = slot_index / 18, local = slot_index % 18;
    int channel = (local / 6) * 3 + (local % 6) % 3;

    return &opl->channels[bank * 9 + channel];
}

static void write_slot(opl_t *opl, int bank, int offset, int group, uint8_t value)
{
    int index;
    slot_t *slot;

    if ((offset & 7) >= 6 || offset > 0x15)
        return;
    index = bank * 18 + (offset >> 3) * 6 + (offset & 7);
    slot = &opl->slots[index];

    switch (group)
    {
    case 0x20:
        slot->tremolo = (value >> 7) & 1;
        slot->vibrato = (value >> 6) & 1;
        slot->sustained = (value >> 5) & 1;
        slot->ksr = (value >> 4) & 1;
        slot->mult = value & 15;
        break;
    case 0x40:
        slot->ksl = value >> 6;
        slot->level = value & 63;
        break;
    case 0x60:
        slot->attack = value >> 4;
        slot->decay = value & 15;
        break;
    case 0x80:
        slot->sustain = value >> 4;
        slot->release = value & 15;
        break;
    case 0xe0:
        slot->wave = value & 7;
        return;
    }
    update_slot(opl, slot, slot_channel(opl, index));
}

void opl_write(opl_t *opl, int reg, uint8_t value)
{
    int bank = (reg >> 8) & 1;
    int r = reg & 0xff;
    channel_t *channel;

    switch (r & 0xe0)
    {
    case 0x20:
    case 0x40:
    case 0x60:
    case 0x80:
    case 0xe0:
        write_slot(opl, bank, r & 0x1f, r & 0xe0, value);
        return;
    case 0xa0:
        if (r == 0xbd && !bank)
        {
            opl->deep_tremolo = (value >> 7) & 1;
            opl->deep_vibrato = (value >> 6) & 1;
            return;
        }
        if ((r & 0x0f) > 8)
            return;
        channel = &opl->channels[bank * 9 + (r & 0x0f)];
        if (r < 0xb0)
            channel->fnum = (channel->fnum & 0x300) | value;
        else
        {
            uint8_t key = (value >> 5) & 1;

            channel->fnum = (channel->fnum & 0xff) | (value & 3) << 8;
            channel->block = (value >> 2) & 7;
            update_channel(opl, channel);
            if (key && !channel->key)
            {
                key_on(channel->mod);
                key_on(channel->car);
            }
            else if (!key && channel->key)
            {
                key_off(channel->mod);
                key_off(channel->car);
            }
            channel->key = key;
            return;
        }
        update_channel(opl, channel);
        return;
    case 0xc0:
        if ((r & 0x0f) > 8 || r >= 0xd0)
            return;
        channel = &opl->channels[bank * 9 + (r & 0x0f)];
        channel->feedback = (value >> 1) & 7;
        channel->additive = value & 1;
        return;
    }
}

void opl_render(opl_t *opl, float *out, int frames)
{
    float tremolo_depth = opl->deep_tremolo ? 25.6f : 5.3f;
    double vibrato_depth = opl->deep_vibrato ? 0.0081 : 0.0041;
    int i, c;

    for (i = 0; i < frames; i++)
    {
        double tri = opl->tremolo_phase < 0.5 ? opl->tremolo_phase * 2 : 2 - opl->tremolo_phase * 2;
        float tremolo = (float)(tri * tremolo_depth);
        double vibrato = 1.0 + vibrato_depth * sin(2 * M_PI * opl->vibrato_phase);
        float sum = 0;

        for (c = 0; c < OPL_CHANNELS; c++)
            sum += channel_output(&opl->channels[c], tremolo, vibrato);
        out[i] = sum * CHANNEL_SCALE;

        opl->tremolo_phase += opl->tremolo_step;
        if (opl->tremolo_phase >= 1)
            opl->tremolo_phase -= 1;
        opl->vibrato_phase += opl->vibrato_step;
        if (opl->vibrato_phase >= 1)
            opl->vibrato_phase -= 1;
    }
}
