// SPDX-License-Identifier: MIT
// Adapted from deastrobooking/LIGHT at 15f08cae2ea297c8906ffc2cd37bf5e4aa62c693.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TB303_STEPS 16

typedef struct {
    uint8_t note; /* MIDI note number, default 36 (C2) */
    bool accent;
    bool slide;
    bool on; /* step plays (vs. rest)             */
} TB303Step;

typedef struct {
    /* Sequencer */
    TB303Step steps[TB303_STEPS];
    int current_step; /* 0–15 */
    int last_step;    /* previous step (change detection) */

    /* Synth parameters */
    float cutoff;     /* Hz 40–8000           */
    float resonance;  /* 0–0.95               */
    float env_mod;    /* filter env depth 0–1  */
    float decay_time; /* VCA+VCF decay 0.05–2s */
    float accent_vol; /* accent boost 0–1      */
    float distortion; /* overdrive 0–1         */
    bool wave_square; /* false=saw, true=sq    */
    float volume;     /* 0–1.5                 */

    /* Oscillator state */
    float osc_phase;   /* 0–1                   */
    float osc_freq;    /* current Hz            */
    float target_freq; /* slide target Hz       */

    /* 4-pole ladder filter state */
    float filt_s[4];

    /* Envelope state */
    float vca_env; /* amplitude envelope 0–1 */
    float vcf_env; /* filter envelope 0–1    */
    bool note_active;
} TB303;

TB303 *tb303_create(void);
void tb303_free(TB303 *tb);
void tb303_render(TB303 *tb, float *stereo_out, uint32_t frames, double bpm, uint64_t global_frame,
                  bool playing, float track_vol);
float tb303_note_to_freq(int midi_note);
