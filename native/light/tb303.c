// SPDX-License-Identifier: MIT
// Adapted from deastrobooking/LIGHT at 15f08cae2ea297c8906ffc2cd37bf5e4aa62c693.
/*
 * tb303.c — TB-303 style mono synthesizer with 16-step sequencer.
 *
 * Architecture:
 *   Oscillator (saw or square)
 *   → 4-pole transistor ladder filter (Padé tanh non-linearity)
 *   → VCA (amplitude envelope)
 *   → optional overdrive waveshaper
 *
 * Sequencer syncs to the DAW transport (16th-note steps).
 * Slide: first-order exponential pitch glide over ~60 ms.
 * Accent: boosts filter cutoff envelope on specific steps.
 */

#include "tb303.h"
#include "light_config.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SR LIGHT_SAMPLE_RATE
#define INV_SR (1.0f / ((float)LIGHT_SAMPLE_RATE))
#define SLIDE_COEFF 0.99965284f /* per-sample: τ ≈ 60 ms at 44100 Hz */

/* Same Padé tanh approximation used by the NEON soft-clipper */
static inline float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* 4-pole transistor ladder lowpass filter.
 *
 * fc_hz: cutoff frequency in Hz.
 * res:   resonance 0–0.95 (self-oscillation at ≈1.0; keep below 0.97).
 *
 * Uses fast_tanh for the non-linearity that gives the 303 its squelch character.
 * Adapted from Stilson & Smith (1996), simplified for stability with Padé tanh. */
static float ladder_step(float *s, float in, float fc_hz, float res) {
    float f = fc_hz * INV_SR * 2.0f;
    if (f > 0.95f)
        f = 0.95f;
    if (f < 2e-4f)
        f = 2e-4f;

    float u = fast_tanh(in - res * 3.98f * s[3]);
    s[0] += f * (fast_tanh(u) - s[0]);
    s[1] += f * (fast_tanh(s[0]) - s[1]);
    s[2] += f * (fast_tanh(s[1]) - s[2]);
    s[3] += f * (fast_tanh(s[2]) - s[3]);
    return s[3];
}

/* ------------------------------------------------------------------ public */

float tb303_note_to_freq(int note) {
    return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

TB303 *tb303_create(void) {
    TB303 *tb = calloc(1, sizeof(TB303));
    if (!tb)
        return NULL;

    tb->cutoff = 700.0f;
    tb->resonance = 0.65f;
    tb->env_mod = 0.45f;
    tb->decay_time = 0.3f;
    tb->accent_vol = 0.5f;
    tb->distortion = 0.1f;
    tb->wave_square = false;
    tb->volume = 0.9f;
    tb->last_step = -1;

    /* Default pattern: sparse kick-pattern bass in C minor */
    static const uint8_t notes[TB303_STEPS] = {36, 36, 43, 43, 36, 36, 43, 41,
                                               36, 36, 43, 43, 38, 38, 41, 43};
    static const bool ons[TB303_STEPS] = {1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 1};
    static const bool acc[TB303_STEPS] = {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};

    for (int i = 0; i < TB303_STEPS; i++) {
        tb->steps[i].note = notes[i];
        tb->steps[i].on = ons[i];
        tb->steps[i].accent = acc[i];
        tb->steps[i].slide = false;
    }

    tb->osc_freq = tb303_note_to_freq(notes[0]);
    tb->target_freq = tb->osc_freq;
    return tb;
}

void tb303_free(TB303 *tb) {
    free(tb);
}

void tb303_render(TB303 *tb, float *stereo_out, uint32_t frames, double bpm, uint64_t global_frame,
                  bool playing, float track_vol) {
    if (!playing) {
        tb->vca_env *= 0.99f; /* natural decay when stopped */
        tb->vcf_env *= 0.99f;
        tb->last_step = -1; /* force retrigger on next play */
        return;
    }

    /* Determine current 16th-note step from transport position */
    double fpstep = ((double)SR * 60.0 / bpm) / 4.0;
    int cur_step = (int)((double)global_frame / fpstep) % TB303_STEPS;

    /* Step-change: fire note events */
    if (cur_step != tb->last_step) {
        tb->last_step = cur_step;
        tb->current_step = cur_step;

        TB303Step *step = &tb->steps[cur_step];
        int prev = (cur_step - 1 + TB303_STEPS) % TB303_STEPS;
        bool slide = step->slide && tb->steps[prev].on;

        if (step->on) {
            tb->target_freq = tb303_note_to_freq(step->note);
            if (!slide) {
                tb->osc_freq = tb->target_freq;
                tb->vca_env = 1.0f;
                tb->vcf_env = step->accent ? (1.0f + tb->accent_vol) : 1.0f;
                memset(tb->filt_s, 0, sizeof(tb->filt_s)); /* crisp new note */
            }
            tb->note_active = true;
        } else {
            tb->note_active = false;
        }
    }

    /* Per-sample decay coefficient (avoids per-sample expf calls) */
    float decay = 1.0f - expf(-1.0f / (tb->decay_time * (float)SR));

    for (uint32_t f = 0; f < frames; f++) {
        /* Slide: exponential pitch glide (τ ≈ 60 ms) */
        if (tb->steps[tb->current_step].slide && tb->note_active) {
            tb->osc_freq = tb->target_freq + (tb->osc_freq - tb->target_freq) * SLIDE_COEFF;
        }

        /* Oscillator */
        float osc;
        if (tb->wave_square) {
            osc = tb->osc_phase < 0.5f ? 0.8f : -0.8f;
        } else {
            osc = 1.0f - 2.0f * tb->osc_phase; /* sawtooth: 1 → -1 */
        }
        tb->osc_phase += tb->osc_freq * INV_SR;
        if (tb->osc_phase >= 1.0f)
            tb->osc_phase -= 1.0f;

        /* Filter cutoff = base + envelope modulation */
        float fc = tb->cutoff * (1.0f + tb->vcf_env * tb->env_mod * 4.0f);
        if (fc > 8000.0f)
            fc = 8000.0f;

        /* Ladder filter (non-linear) */
        float filt = ladder_step(tb->filt_s, osc * 2.5f, fc, tb->resonance);

        /* Overdrive waveshaper */
        if (tb->distortion > 0.01f) {
            float drive = 1.0f + tb->distortion * 9.0f;
            filt = fast_tanh(filt * drive);
        }

        /* VCA */
        float out = filt * tb->vca_env;

        /* Envelope decay (per sample) */
        tb->vca_env -= tb->vca_env * decay;
        tb->vcf_env -= tb->vcf_env * decay;

        float s = out * tb->volume * track_vol;
        stereo_out[f * 2] += s;
        stereo_out[f * 2 + 1] += s;
    }
}
