/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/music.h"


#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VOICES 9
#define BANK_INSTRUMENTS 175
#define MAX_EVENTS 65536

typedef struct op2_voice {
    uint8_t mod_char, mod_attack, mod_sustain, mod_wave, mod_ksl, mod_level;
    uint8_t feedback;
    uint8_t car_char, car_attack, car_sustain, car_wave, car_ksl, car_level;
    int16_t note_offset;
} op2_voice;

typedef struct op2_instrument {
    uint16_t flags;      /* 1 fixed pitch, 4 double voice */
    uint8_t fine_tune;
    uint8_t fixed_note;
    op2_voice voice[2];
} op2_instrument;

typedef struct event {
    uint32_t tick;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
    uint32_t tempo;      /* for tempo meta events */
    uint32_t sequence;   /* file order, keeps the merge stable */
} event;

typedef struct voice {
    int active;
    int channel;
    int note;            /* MIDI key that started it */
    int played;          /* key after fixed-pitch and offsets */
    int velocity;
    int second;          /* the double-voice half */
    const op2_instrument *instrument;
    uint32_t age;
} voice;

typedef struct channel {
    int program;
    int volume;
    int expression;
    int pan;
    int bend;            /* -8192..8191 */
} midi_channel;

/* ---- OPL2 model ---------------------------------------------------- */

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct fm_operator {
    uint32_t phase;      /* 2^32 per cycle */
    uint32_t step;
    float level;         /* total level attenuation, dB */
    float ksl_db;        /* key scale attenuation for the current note */
    float attenuation;   /* envelope, dB (0 loud .. 96 silent) */
    float attack_coef;   /* per-sample attack factor, 0 = never */
    float decay_step;    /* dB per sample */
    float release_step;  /* dB per sample */
    int state;
    uint8_t reg20, reg40, reg60, reg80, regE0;
} fm_operator;

typedef struct fm_channel {
    fm_operator op[2];
    uint8_t feedback_conn;
    int fnum;
    int block;
    int key;
    float previous[2];
} fm_channel;

typedef struct fm_chip {
    fm_channel channel[VOICES];
    double rate;
    uint32_t am_phase;
    uint32_t am_step;
    uint32_t vib_phase;
    uint32_t vib_step;
    float lfo_am;        /* tremolo, dB */
    float lfo_vib;       /* vibrato step factor */
} fm_chip;

struct vcc_music {
    fm_chip chip;
    int sample_rate;
    op2_instrument bank[BANK_INSTRUMENTS];
    event *events;
    size_t event_count;
    uint16_t division;
    size_t position;
    double tick_samples;      /* samples per MIDI tick at the current tempo */
    double sample_clock;      /* samples until the next tick boundary */
    uint32_t tick;
    int playing;
    voice voices[VOICES];
    midi_channel channels[16];
    uint32_t age;
};

static const uint8_t operator_offsets[VOICES] = {0, 1, 2, 8, 9, 10, 16, 17, 18};

/* DMX volume curve, as used by OPL2 MIDI drivers of the period. */
static const uint8_t volume_curve[128] = {
    0, 1, 3, 5, 6, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23,
    25, 26, 27, 29, 30, 32, 33, 34, 36, 37, 39, 41, 43, 45, 47, 49,
    50, 52, 54, 55, 57, 59, 60, 61, 63, 64, 66, 67, 68, 69, 71, 72,
    73, 74, 75, 76, 77, 79, 80, 81, 82, 83, 84, 84, 85, 86, 87, 88,
    89, 90, 91, 92, 92, 93, 94, 95, 96, 96, 97, 98, 99, 99, 100, 101,
    101, 102, 103, 103, 104, 105, 105, 106, 107, 107, 108, 109, 109, 110, 110, 111,
    112, 112, 113, 113, 114, 114, 115, 115, 116, 117, 117, 118, 118, 119, 119, 120,
    120, 121, 121, 122, 122, 123, 123, 123, 124, 124, 125, 125, 126, 126, 127, 127
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static float sine_table[1024];

static const double multipliers[16] = {
    0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15
};
/* OPL2 attack and decay times in ms for rates 1..15 (rate 0 never moves). */
static const double attack_ms[16] = {
    0, 2826.24, 1413.12, 706.56, 353.28, 176.64, 88.32, 44.16, 22.08, 11.04,
    5.52, 2.76, 1.38, 0.69, 0.35, 0.0
};
static const double decay_ms[16] = {
    0, 39280.64, 19640.32, 9820.16, 4910.08, 2455.04, 1227.52, 613.76, 306.88,
    153.44, 76.72, 38.36, 19.18, 9.59, 4.79, 2.40
};

static int slot_of(int offset, int *channel_index, int *op_index)
{
    static const int8_t map[22] = {
        0, 1, 2, 0, 1, 2, -1, -1, 3, 4, 5, 3, 4, 5, -1, -1, 6, 7, 8, 6, 7, 8
    };
    if (offset < 0 || offset >= 22 || map[offset] < 0) return 0;
    *channel_index = map[offset];
    *op_index = (offset % 8) >= 3;
    return 1;
}

static double scaled_time(double base_ms, const fm_channel *channel, const fm_operator *op);

static void update_rates(fm_chip *chip, fm_channel *channel, fm_operator *op)
{
    double ms;
    int ar = op->reg60 >> 4;
    int dr = op->reg60 & 0x0F;
    int rr = op->reg80 & 0x0F;
    ms = scaled_time(attack_ms[ar], channel, op);
    op->attack_coef = ar == 0 ? 0.0f
        : (ms <= 0.0 ? 1.0f : (float)((1000.0 / (ms * chip->rate)) * 2.5));
    op->decay_step = dr == 0 ? 0.0f
        : (float)(96.0 * 1000.0 / (scaled_time(decay_ms[dr], channel, op) * chip->rate));
    op->release_step = rr == 0 ? 0.0f
        : (float)(96.0 * 1000.0 / (scaled_time(decay_ms[rr], channel, op) * chip->rate));
}

static void update_pitch(fm_chip *chip, fm_channel *channel)
{
    double frequency = channel->fnum * 49716.0 / (double)(1 << (20 - channel->block));
    double octaves = log(frequency > 1.0 ? frequency / 261.63 : 1.0 / 261.63) / log(2.0);
    int index;
    for (index = 0; index < 2; ++index) {
        fm_operator *op = &channel->op[index];
        static const float ksl_rate[4] = {0.0f, 3.0f, 1.5f, 6.0f};
        op->step = (uint32_t)(frequency * multipliers[op->reg20 & 0x0F] / chip->rate * 4294967296.0);
        op->ksl_db = octaves > 0.0 ? (float)(octaves * ksl_rate[op->reg40 >> 6]) : 0.0f;
        update_rates(chip, channel, op);
    }
}

static void write_reg(vcc_music *music, uint16_t reg, uint8_t value)
{
    fm_chip *chip = &music->chip;
    int channel_index;
    int op_index;
    if (reg >= 0x20 && reg < 0xA0) {
        fm_operator *op;
        if (!slot_of(reg & 0x1F, &channel_index, &op_index)) return;
        op = &chip->channel[channel_index].op[op_index];
        switch (reg & 0xE0) {
        case 0x20: op->reg20 = value; break;
        case 0x40: op->reg40 = value; op->level = (float)(value & 0x3F) * 0.75f; break;
        case 0x60: op->reg60 = value; break;
        case 0x80: op->reg80 = value; break;
        default: break;
        }
        update_pitch(chip, &chip->channel[channel_index]);
    } else if (reg >= 0xE0 && reg < 0xF6) {
        if (slot_of(reg - 0xE0, &channel_index, &op_index))
            chip->channel[channel_index].op[op_index].regE0 = value;
    } else if (reg >= 0xA0 && reg < 0xA9) {
        fm_channel *channel = &chip->channel[reg - 0xA0];
        channel->fnum = (channel->fnum & 0x300) | value;
        update_pitch(chip, channel);
    } else if (reg >= 0xB0 && reg < 0xB9) {
        fm_channel *channel = &chip->channel[reg - 0xB0];
        int key = (value >> 5) & 1;
        channel->fnum = (channel->fnum & 0xFF) | ((value & 3) << 8);
        channel->block = (value >> 2) & 7;
        update_pitch(chip, channel);
        if (key && !channel->key) {
            int index;
            for (index = 0; index < 2; ++index) {
                channel->op[index].state = ENV_ATTACK;
                channel->op[index].phase = 0U;
            }
        } else if (!key && channel->key) {
            int index;
            for (index = 0; index < 2; ++index)
                if (channel->op[index].state != ENV_OFF) channel->op[index].state = ENV_RELEASE;
        }
        channel->key = key;
    } else if (reg >= 0xC0 && reg < 0xC9) {
        chip->channel[reg - 0xC0].feedback_conn = value;
    }
}

/* Rate scaling: each step of four halves the time (KSR selects the finer
 * per-note offset). */
static double scaled_time(double base_ms, const fm_channel *channel, const fm_operator *op)
{
    int offset = (op->reg20 & 0x10) ? channel->block * 2 + ((channel->fnum >> 9) & 1)
                                    : channel->block >> 1;
    return base_ms / pow(2.0, offset / 4.0);
}

static void envelope(fm_operator *op)
{
    float sustain = (float)((op->reg80 >> 4) == 15 ? 93.0 : (op->reg80 >> 4) * 3.0);
    switch (op->state) {
    case ENV_ATTACK:
        if (op->attack_coef <= 0.0f) break;
        if (op->attack_coef >= 1.0f) op->attenuation = 0.0f;
        else op->attenuation -= (op->attenuation + 4.0f) * op->attack_coef;
        if (op->attenuation <= 0.0f) { op->attenuation = 0.0f; op->state = ENV_DECAY; }
        break;
    case ENV_DECAY:
        op->attenuation += op->decay_step;
        if (op->attenuation >= sustain) {
            op->attenuation = sustain;
            op->state = (op->reg20 & 0x20) ? ENV_SUSTAIN : ENV_RELEASE;
        }
        break;
    case ENV_SUSTAIN:
        break;
    case ENV_RELEASE:
        op->attenuation += op->release_step;
        if (op->attenuation >= 96.0f) { op->attenuation = 96.0f; op->state = ENV_OFF; }
        break;
    default:
        break;
    }
}

static float waveform(int shape, uint32_t phase)
{
    unsigned index = phase >> 22;
    float value = sine_table[index];
    switch (shape & 3) {
    case 1: return value < 0.0f ? 0.0f : value;
    case 2: return value < 0.0f ? -value : value;
    case 3: return (index & 256) ? 0.0f : (value < 0.0f ? -value : value);
    default: return value;
    }
}

static float db_table[961];  /* 0.1 dB steps to 96 dB */

static float amplitude(const fm_chip *chip, const fm_operator *op)
{
    float db = op->attenuation + op->level + op->ksl_db;
    if (op->reg20 & 0x80) db += chip->lfo_am;
    if (db >= 96.0f) return 0.0f;
    return db_table[(int)(db * 10.0f)];
}

static float chip_sample(fm_chip *chip)
{
    float sum = 0.0f;
    int index;
    /* Tremolo 1 dB at 3.7 Hz and vibrato 7 cents at 6.1 Hz (register BD 0). */
    chip->am_phase += chip->am_step;
    chip->vib_phase += chip->vib_step;
    chip->lfo_am = 0.5f * (1.0f + sine_table[chip->am_phase >> 22]);
    chip->lfo_vib = 1.0f + 0.004f * sine_table[chip->vib_phase >> 22];
    for (index = 0; index < VOICES; ++index) {
        fm_channel *channel = &chip->channel[index];
        fm_operator *mod = &channel->op[0];
        fm_operator *car = &channel->op[1];
        float mod_out;
        float car_out;
        int feedback = (channel->feedback_conn >> 1) & 7;
        int64_t feedback_phase = 0;
        if (mod->state == ENV_OFF && car->state == ENV_OFF) continue;
        envelope(mod);
        envelope(car);
        /* Feedback reaches 4 pi (two cycles) at level 7, halving per step. */
        if (feedback)
            feedback_phase = (int64_t)((channel->previous[0] + channel->previous[1])
                * (float)(1 << (feedback + 2)) * 8388608.0f);
        mod_out = waveform(mod->regE0, mod->phase + (uint32_t)feedback_phase) * amplitude(chip, mod);
        channel->previous[1] = channel->previous[0];
        channel->previous[0] = mod_out;
        if (channel->feedback_conn & 1)
            car_out = mod_out + waveform(car->regE0, car->phase) * amplitude(chip, car);
        else
            /* Full modulator output shifts the carrier 8 pi (four cycles). */
            car_out = waveform(car->regE0,
                car->phase + (uint32_t)(int64_t)(mod_out * 4.0f * 4294967296.0f))
                * amplitude(chip, car);
        mod->phase += (mod->reg20 & 0x40) ? (uint32_t)((float)mod->step * chip->lfo_vib) : mod->step;
        car->phase += (car->reg20 & 0x40) ? (uint32_t)((float)car->step * chip->lfo_vib) : car->step;
        sum += car_out;
    }
    return sum;
}

vcc_music *vcc_music_create(const uint8_t *bank, size_t bank_size, int sample_rate)
{
    vcc_music *music;
    size_t index;
    if (bank_size < 8U + BANK_INSTRUMENTS * 36U || memcmp(bank, "#OPL_II#", 8) != 0) return NULL;
    music = calloc(1, sizeof *music);
    if (!music) return NULL;
    music->events = calloc(MAX_EVENTS, sizeof *music->events);
    if (!music->events) { free(music); return NULL; }
    for (index = 0U; index < BANK_INSTRUMENTS; ++index) {
        const uint8_t *p = bank + 8U + index * 36U;
        op2_instrument *instrument = &music->bank[index];
        int v;
        instrument->flags = (uint16_t)(p[0] | (p[1] << 8));
        instrument->fine_tune = p[2];
        instrument->fixed_note = p[3];
        for (v = 0; v < 2; ++v) {
            const uint8_t *q = p + 4 + v * 16;
            op2_voice *voice_data = &instrument->voice[v];
            voice_data->mod_char = q[0];
            voice_data->mod_attack = q[1];
            voice_data->mod_sustain = q[2];
            voice_data->mod_wave = q[3];
            voice_data->mod_ksl = q[4];
            voice_data->mod_level = q[5];
            voice_data->feedback = q[6];
            voice_data->car_char = q[7];
            voice_data->car_attack = q[8];
            voice_data->car_sustain = q[9];
            voice_data->car_wave = q[10];
            voice_data->car_ksl = q[11];
            voice_data->car_level = q[12];
            voice_data->note_offset = (int16_t)(q[14] | (q[15] << 8));
        }
    }
    for (index = 0U; index < 1024U; ++index)
        sine_table[index] = (float)sin((double)index * 2.0 * 3.14159265358979323846 / 1024.0);
    for (index = 0U; index < 961U; ++index)
        db_table[index] = (float)pow(10.0, -(double)index / 200.0);
    music->sample_rate = sample_rate;
    music->chip.rate = sample_rate;
    music->chip.am_step = (uint32_t)(3.7 / sample_rate * 4294967296.0);
    music->chip.vib_step = (uint32_t)(6.1 / sample_rate * 4294967296.0);
    for (index = 0U; index < VOICES; ++index) {
        music->chip.channel[index].op[0].attenuation = 96.0f;
        music->chip.channel[index].op[1].attenuation = 96.0f;
    }
    write_reg(music, 0x01, 0x20);  /* waveform select */
    return music;
}

void vcc_music_destroy(vcc_music *music)
{
    if (!music) return;
    free(music->events);
    free(music);
}

static int compare_events(const void *a, const void *b)
{
    const event *x = a;
    const event *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->sequence < y->sequence ? -1 : (x->sequence > y->sequence ? 1 : 0);
}

int vcc_music_load(vcc_music *music, const uint8_t *data, size_t size)
{
    size_t offset;
    unsigned tracks;
    unsigned track;
    music->event_count = 0U;
    music->playing = 0;
    if (size < 14U || memcmp(data, "MThd", 4) != 0 || be32(data + 4) < 6U) return 0;
    tracks = (unsigned)((data[10] << 8) | data[11]);
    music->division = (uint16_t)((data[12] << 8) | data[13]);
    if (music->division == 0U || (music->division & 0x8000U)) return 0;
    offset = 8U + be32(data + 4);
    for (track = 0U; track < tracks && offset + 8U <= size; ++track) {
        size_t length = be32(data + offset + 4);
        size_t cursor = offset + 8U;
        size_t end = cursor + length;
        uint32_t tick = 0U;
        uint8_t running = 0U;
        if (memcmp(data + offset, "MTrk", 4) != 0 || end > size) return 0;
        while (cursor < end) {
            uint32_t delta = 0U;
            uint8_t status;
            do {
                delta = (delta << 7) | (data[cursor] & 0x7FU);
            } while ((data[cursor++] & 0x80U) && cursor < end);
            tick += delta;
            if (cursor >= end) break;
            status = data[cursor];
            if (status & 0x80U) ++cursor;
            else status = running;
            if (status == 0xFFU) {
                uint8_t type;
                uint32_t meta_length = 0U;
                if (cursor >= end) break;
                type = data[cursor++];
                do {
                    meta_length = (meta_length << 7) | (data[cursor] & 0x7FU);
                } while ((data[cursor++] & 0x80U) && cursor < end);
                if (type == 0x51U && meta_length == 3U && music->event_count < MAX_EVENTS) {
                    event *e = &music->events[music->event_count++];
                    e->tick = tick;
                    e->status = 0xFFU;
                    e->tempo = ((uint32_t)data[cursor] << 16) | ((uint32_t)data[cursor + 1] << 8)
                        | data[cursor + 2];
                }
                cursor += meta_length;
                if (type == 0x2FU) break;
            } else if (status == 0xF0U || status == 0xF7U) {
                uint32_t sysex = 0U;
                do {
                    sysex = (sysex << 7) | (data[cursor] & 0x7FU);
                } while ((data[cursor++] & 0x80U) && cursor < end);
                cursor += sysex;
            } else if (status & 0x80U) {
                int two = (status & 0xE0U) != 0xC0U;
                event *e;
                running = status;
                if (cursor + (two ? 2U : 1U) > end) break;
                if (music->event_count >= MAX_EVENTS) break;
                e = &music->events[music->event_count++];
                e->tick = tick;
                e->status = status;
                e->data1 = data[cursor];
                e->data2 = two ? data[cursor + 1] : 0U;
                e->tempo = 0U;
                cursor += two ? 2U : 1U;
            } else {
                break;
            }
        }
        offset = end;
    }
    /* Stable merge of the tracks by time. */
    {
        size_t index;
        for (index = 0U; index < music->event_count; ++index)
            music->events[index].sequence = (uint32_t)index;
        qsort(music->events, music->event_count, sizeof *music->events, compare_events);
    }
    return music->event_count != 0U;
}

static void voice_off(vcc_music *music, int index)
{
    voice *v = &music->voices[index];
    write_reg(music, (uint16_t)(0xB0 + index), 0x00);
    v->active = 0;
}

static void silence(vcc_music *music)
{
    int index;
    for (index = 0; index < VOICES; ++index) {
        write_reg(music, (uint16_t)(0x40 + operator_offsets[index]), 0x3F);
        write_reg(music, (uint16_t)(0x43 + operator_offsets[index]), 0x3F);
        voice_off(music, index);
    }
}

static void reset_channels(vcc_music *music)
{
    int index;
    for (index = 0; index < 16; ++index) {
        music->channels[index].program = 0;
        music->channels[index].volume = 100;
        music->channels[index].expression = 127;
        music->channels[index].pan = 64;
        music->channels[index].bend = 0;
    }
}

void vcc_music_play(vcc_music *music)
{
    silence(music);
    reset_channels(music);
    music->position = 0U;
    music->tick = 0U;
    music->sample_clock = 0.0;
    music->tick_samples = (500000.0 / 1000000.0) * music->sample_rate / music->division;
    music->playing = music->event_count != 0U;
}

void vcc_music_stop(vcc_music *music)
{
    music->playing = 0;
    silence(music);
}

int vcc_music_playing(const vcc_music *music)
{
    return music->playing;
}

static void set_frequency(vcc_music *music, int index)
{
    const voice *v = &music->voices[index];
    const midi_channel *c = &music->channels[v->channel];
    double note = v->played + c->bend / 4096.0;
    double frequency;
    int block = 0;
    int fnum;
    if (v->second) note += (v->instrument->fine_tune - 128) / 64.0;
    frequency = 440.0 * pow(2.0, (note - 69.0) / 12.0);
    fnum = (int)(frequency * 1048576.0 / 49716.0);
    while (fnum >= 1024 && block < 7) {
        fnum >>= 1;
        ++block;
    }
    if (fnum > 1023) fnum = 1023;
    write_reg(music, (uint16_t)(0xA0 + index), (uint8_t)(fnum & 0xFF));
    write_reg(music, (uint16_t)(0xB0 + index),
        (uint8_t)((v->active ? 0x20 : 0x00) | (block << 2) | (fnum >> 8)));
}

static void set_volume(vcc_music *music, int index)
{
    const voice *v = &music->voices[index];
    const midi_channel *c = &music->channels[v->channel];
    const op2_voice *data = &v->instrument->voice[v->second];
    int loudness = volume_curve[(v->velocity * c->volume * c->expression) / (127 * 127)];
    int car = 0x3F - (((0x3F - (data->car_level & 0x3F)) * loudness) >> 7);
    uint8_t mod_offset = operator_offsets[index];
    write_reg(music, (uint16_t)(0x43 + mod_offset), (uint8_t)((data->car_ksl & 0xC0) | car));
    if (data->feedback & 1U) {
        int mod = 0x3F - (((0x3F - (data->mod_level & 0x3F)) * loudness) >> 7);
        write_reg(music, (uint16_t)(0x40 + mod_offset), (uint8_t)((data->mod_ksl & 0xC0) | mod));
    }
}

static void program_voice(vcc_music *music, int index)
{
    const voice *v = &music->voices[index];
    const op2_voice *data = &v->instrument->voice[v->second];
    uint8_t m = operator_offsets[index];
    write_reg(music, (uint16_t)(0x20 + m), data->mod_char);
    write_reg(music, (uint16_t)(0x60 + m), data->mod_attack);
    write_reg(music, (uint16_t)(0x80 + m), data->mod_sustain);
    write_reg(music, (uint16_t)(0xE0 + m), data->mod_wave);
    write_reg(music, (uint16_t)(0x40 + m), (uint8_t)((data->mod_ksl & 0xC0) | (data->mod_level & 0x3F)));
    write_reg(music, (uint16_t)(0x23 + m), data->car_char);
    write_reg(music, (uint16_t)(0x63 + m), data->car_attack);
    write_reg(music, (uint16_t)(0x83 + m), data->car_sustain);
    write_reg(music, (uint16_t)(0xE3 + m), data->car_wave);
    write_reg(music, (uint16_t)(0xC0 + index), (uint8_t)(data->feedback | 0x30));
}

static int allocate_voice(vcc_music *music)
{
    int index;
    int oldest = 0;
    for (index = 0; index < VOICES; ++index)
        if (!music->voices[index].active) return index;
    for (index = 1; index < VOICES; ++index)
        if (music->voices[index].age < music->voices[oldest].age) oldest = index;
    voice_off(music, oldest);
    return oldest;
}

static void start_voice(vcc_music *music, int channel_index, int note, int velocity,
    const op2_instrument *instrument, int second)
{
    int index = allocate_voice(music);
    voice *v = &music->voices[index];
    int played = (instrument->flags & 1U) ? instrument->fixed_note : note;
    played += instrument->voice[second].note_offset;
    while (played < 0) played += 12;
    while (played > 127) played -= 12;
    v->channel = channel_index;
    v->note = note;
    v->played = played;
    v->velocity = velocity;
    v->second = second;
    v->instrument = instrument;
    v->age = ++music->age;
    v->active = 0;
    program_voice(music, index);
    set_volume(music, index);
    v->active = 1;
    set_frequency(music, index);
}

static void note_off(vcc_music *music, int channel_index, int note)
{
    int index;
    for (index = 0; index < VOICES; ++index) {
        voice *v = &music->voices[index];
        if (v->active && v->channel == channel_index && v->note == note) {
            v->active = 0;
            set_frequency(music, index);
        }
    }
}

/* The game's MIDI files are Windows 3.1 dual-mode files: channels 1-10 for
 * extended synthesizers and 13-16 for base-level ones such as FM cards.
 * The MIDI Mapper sends an FM card only the base-level part, with channel
 * 16 as percussion. */
#define BASE_FIRST 12
#define BASE_PERCUSSION 15

static void note_on(vcc_music *music, int channel_index, int note, int velocity)
{
    const op2_instrument *instrument;
    if (channel_index < BASE_FIRST) return;
    if (velocity == 0) {
        note_off(music, channel_index, note);
        return;
    }
    if (channel_index == BASE_PERCUSSION) {
        if (note < 35 || note > 81) return;
        instrument = &music->bank[128 + note - 35];
    } else {
        instrument = &music->bank[music->channels[channel_index].program & 0x7F];
    }
    start_voice(music, channel_index, note, velocity, instrument, 0);
    if (instrument->flags & 4U) start_voice(music, channel_index, note, velocity, instrument, 1);
}

static void dispatch(vcc_music *music, const event *e)
{
    int channel_index = e->status & 0x0F;
    midi_channel *c = &music->channels[channel_index];
    int index;
    switch (e->status & 0xF0U) {
    case 0x80: note_off(music, channel_index, e->data1); break;
    case 0x90: note_on(music, channel_index, e->data1, e->data2); break;
    case 0xB0:
        if (e->data1 == 7) c->volume = e->data2;
        else if (e->data1 == 11) c->expression = e->data2;
        else if (e->data1 == 10) c->pan = e->data2;
        else if (e->data1 == 120 || e->data1 == 123) {
            for (index = 0; index < VOICES; ++index)
                if (music->voices[index].active && music->voices[index].channel == channel_index)
                    voice_off(music, index);
        }
        if (e->data1 == 7 || e->data1 == 11)
            for (index = 0; index < VOICES; ++index)
                if (music->voices[index].active && music->voices[index].channel == channel_index)
                    set_volume(music, index);
        break;
    case 0xC0: c->program = e->data1; break;
    case 0xE0:
        c->bend = ((e->data2 << 7) | e->data1) - 8192;
        for (index = 0; index < VOICES; ++index)
            if (music->voices[index].active && music->voices[index].channel == channel_index)
                set_frequency(music, index);
        break;
    default: break;
    }
}

static void advance_tick(vcc_music *music)
{
    while (music->position < music->event_count
        && music->events[music->position].tick <= music->tick) {
        const event *e = &music->events[music->position++];
        if (e->status == 0xFFU)
            music->tick_samples = (e->tempo / 1000000.0) * music->sample_rate / music->division;
        else
            dispatch(music, e);
    }
    if (music->position >= music->event_count) {
        /* MM_MCINOTIFY success: play the song again. */
        vcc_music_play(music);
        return;
    }
    ++music->tick;
}

void vcc_music_render(vcc_music *music, int16_t *out, size_t frames)
{
    size_t index;
    for (index = 0U; index < frames; ++index) {
        float sample;
        if (music->playing) {
            music->sample_clock -= 1.0;
            while (music->playing && music->sample_clock <= 0.0) {
                advance_tick(music);
                music->sample_clock += music->tick_samples;
            }
        }
        sample = chip_sample(&music->chip) * 5000.0f;
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;
        out[index] = (int16_t)sample;
    }
}
