/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VCC_MUSIC_H
#define VCC_MUSIC_H

#include <stddef.h>
#include <stdint.h>

/* Standard MIDI file playback through an emulated OPL2 FM chip, standing in
 * for the Windows 3.1 MIDI Mapper and Sound Blaster FM driver that the
 * original's MCI sequencer (8:0110) normally reached. Instruments come from
 * a DMX-format OPL2 General MIDI bank. */

typedef struct vcc_music vcc_music;

vcc_music *vcc_music_create(const uint8_t *bank, size_t bank_size, int sample_rate);
void vcc_music_destroy(vcc_music *music);
/* Loads a format 0 or 1 MIDI file; returns 0 on a malformed file. */
int vcc_music_load(vcc_music *music, const uint8_t *data, size_t size);
/* Starts from the beginning; the song repeats when it ends, as the
 * original restarts it on MM_MCINOTIFY (2:271E). */
void vcc_music_play(vcc_music *music);
void vcc_music_stop(vcc_music *music);
int vcc_music_playing(const vcc_music *music);
/* Renders mono 16-bit samples. */
void vcc_music_render(vcc_music *music, int16_t *out, size_t frames);

#endif
