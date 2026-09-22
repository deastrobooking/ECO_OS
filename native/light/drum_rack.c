// SPDX-License-Identifier: MIT
// Adapted from deastrobooking/LIGHT at 15f08cae2ea297c8906ffc2cd37bf5e4aa62c693.
/*
 * drum_rack.c — 16-pad MPC/TR-style drum machine.
 *
 * Each pad carries a built-in synthesis engine (kick, snare, hihat, clap,
 * perc) or can play a user-loaded stereo audio sample.  A TR-style 16/32-
 * step sequencer is synced to transport global_frame, identical to tb303.
 */
#include "drum_rack.h"
#include "light_config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR LIGHT_SAMPLE_RATE
#define INV_SR (1.0f / ((float)LIGHT_SAMPLE_RATE))
#define TWO_PI 6.28318530717959f

/* Padé tanh approximation (matches soft_clip_neon) */
static inline float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* Per-pad noise allows independent render instances and offline tests. */
static inline float drum_noise(DrumPad *pad) {
    pad->noise_state = pad->noise_state * 1664525u + 1013904223u;
    return (float)(int32_t)pad->noise_state * (1.0f / 2147483648.0f);
}

static inline float midi_hz(float note) {
    return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

/* -------------------------------------------------------------- create/free */

DrumRack *drum_rack_create(void) {
    DrumRack *dr = calloc(1, sizeof(DrumRack));
    if (!dr)
        return NULL;

    dr->steps = 16;
    dr->last_step = -1;
    dr->volume = 1.0f;

    for (int i = 0; i < DRUM_PADS; i++) {
        DrumPad *p = &dr->pads[i];
        snprintf(p->name, sizeof(p->name), "Pad %d", i + 1);
        p->noise_state = 0xCAFEBABEu + (uint32_t)i;
        p->volume = 0.9f;
        p->decay = 0.30f;
        p->tone = 0.50f;
        p->snap = 0.50f;
        p->color = 0.50f;
        for (int s = 0; s < DRUM_STEPS; s++)
            p->step_vel[s] = 0.85f;
    }

    /* 8-voice default kit */
    static const struct {
        int idx;
        DrumType type;
        const char *name;
        float vol, decay, pitch, tone, snap, color;
    } K[] = {
        {0, DRUM_KICK, "Kick", 1.00f, 0.45f, 0.0f, 0.65f, 0.50f, 0.10f},
        {1, DRUM_SNARE, "Snare", 1.00f, 0.22f, 0.0f, 0.45f, 0.60f, 0.55f},
        {2, DRUM_HIHAT, "Closed HH", 0.90f, 0.04f, 0.0f, 0.00f, 0.00f, 0.90f},
        {3, DRUM_HIHAT, "Open HH", 0.85f, 0.32f, 0.0f, 0.00f, 0.00f, 0.85f},
        {4, DRUM_CLAP, "Clap", 0.90f, 0.18f, 0.0f, 0.00f, 0.70f, 0.50f},
        {5, DRUM_PERC, "Tom Hi", 0.80f, 0.25f, 7.0f, 0.50f, 0.30f, 0.40f},
        {6, DRUM_PERC, "Tom Lo", 0.80f, 0.30f, -5.0f, 0.50f, 0.30f, 0.40f},
        {7, DRUM_PERC, "Rim", 0.75f, 0.09f, 15.0f, 0.30f, 0.80f, 0.80f},
    };
    for (int i = 0; i < 8; i++) {
        DrumPad *p = &dr->pads[K[i].idx];
        p->type = K[i].type;
        p->volume = K[i].vol;
        p->decay = K[i].decay;
        p->pitch = K[i].pitch;
        p->tone = K[i].tone;
        p->snap = K[i].snap;
        p->color = K[i].color;
        strncpy(p->name, K[i].name, sizeof(p->name) - 1);
    }

    /* Default 4/4 kick/snare/hihat/clap pattern */
    dr->pads[0].step_on[0] = dr->pads[0].step_on[8] = true;  /* kick */
    dr->pads[1].step_on[4] = dr->pads[1].step_on[12] = true; /* snare */
    {
        int hh[] = {0, 2, 4, 6, 8, 10, 12, 14};
        for (int i = 0; i < 8; i++)
            dr->pads[2].step_on[hh[i]] = true;
    }
    dr->pads[4].step_on[4] = dr->pads[4].step_on[12] = true; /* clap */

    return dr;
}

void drum_rack_free(DrumRack *dr) {
    if (!dr)
        return;
    for (int i = 0; i < DRUM_PADS; i++)
        free(dr->pads[i].samples);
    free(dr);
}

/* --------------------------------------------------------------- trigger */

void drum_pad_trigger(DrumPad *pad, float velocity) {
    if (pad->type == DRUM_NONE)
        return;
    pad->active = true;
    pad->phase = 0.0f;
    pad->env = velocity;
    pad->env2 = 1.0f;
    pad->noise_s = 0.0f;
    pad->playhead = 0.0f;
    float d = pad->decay < 0.001f ? 0.001f : pad->decay;
    pad->env_decay = expf(-1.0f / (d * (float)SR));
    pad->e2_decay = expf(-1.0f / (0.030f * (float)SR));
    /* Clap: env2 holds remaining burst window duration */
    if (pad->type == DRUM_CLAP)
        pad->env2 = 0.018f + pad->snap * 0.018f;
}

/* ---------------------------------------------------- per-pad DSP tick */

static void pad_tick(DrumPad *pad, float *out_l, float *out_r) {
    if (!pad->active) {
        *out_l = *out_r = 0.0f;
        return;
    }

    float s = 0.0f;

    switch (pad->type) {

    case DRUM_KICK: {
        float base = midi_hz(36.0f + pad->pitch);
        float freq = base + pad->env2 * base * 7.0f * pad->tone;
        pad->env2 *= pad->e2_decay;
        pad->phase += freq * INV_SR;
        pad->phase -= floorf(pad->phase);
        float click = drum_noise(pad) * pad->env2 * pad->snap * 0.4f;
        s = fast_tanh(sinf(pad->phase * TWO_PI) * 1.6f) + click;
        break;
    }

    case DRUM_SNARE: {
        float base = midi_hz(62.0f + pad->pitch);
        pad->phase += base * INV_SR;
        pad->phase -= floorf(pad->phase);
        float body = sinf(pad->phase * TWO_PI) * pad->tone;
        float n = drum_noise(pad);
        float lp_c = 0.04f + pad->color * 0.90f;
        pad->noise_s += (n - pad->noise_s) * lp_c;
        s = body + (n - pad->noise_s) * (0.8f + pad->snap * 0.4f);
        break;
    }

    case DRUM_HIHAT: {
        float n = drum_noise(pad);
        float lp = 0.02f + pad->color * 0.78f;
        pad->noise_s += (n - pad->noise_s) * lp;
        s = n - pad->noise_s;
        break;
    }

    case DRUM_CLAP: {
        float n = drum_noise(pad);
        if (pad->env2 > 0.0f) {
            /* Burst phase: multiple rapid noise pulses */
            float t = pad->env2 / (0.036f * pad->snap + 0.002f);
            float burst = sinf(t * TWO_PI * 4.0f);
            s = n * (0.5f + fabsf(burst) * 0.5f);
            pad->env2 -= INV_SR;
            if (pad->env2 < 0.0f)
                pad->env2 = 0.0f;
        } else {
            /* Main body: filtered noise */
            float lp = 0.30f + pad->color * 0.50f;
            pad->noise_s += (n - pad->noise_s) * lp;
            s = n - pad->noise_s * 0.65f;
        }
        break;
    }

    case DRUM_PERC: {
        float base = midi_hz(57.0f + pad->pitch);
        float freq = base + pad->env2 * base * 2.0f * pad->tone;
        pad->env2 *= pad->e2_decay;
        pad->phase += freq * INV_SR;
        pad->phase -= floorf(pad->phase);
        float n = drum_noise(pad);
        float lp = 0.08f + pad->color * 0.60f;
        pad->noise_s += (n - pad->noise_s) * lp;
        s = sinf(pad->phase * TWO_PI) * (1.0f - pad->snap * 0.5f) + pad->noise_s * pad->snap * 0.5f;
        break;
    }

    case DRUM_SAMPLE: {
        if (!pad->samples || pad->frame_count == 0) {
            pad->active = false;
            *out_l = *out_r = 0.0f;
            return;
        }
        float rate = powf(2.0f, pad->pitch / 12.0f);
        uint32_t i = (uint32_t)pad->playhead;
        if (i + 1 >= pad->frame_count) {
            pad->active = false;
            *out_l = *out_r = 0.0f;
            return;
        }
        float f = pad->playhead - (float)i;
        float sl = pad->samples[i * 2] * (1.0f - f) + pad->samples[(i + 1) * 2] * f;
        float sr = pad->samples[i * 2 + 1] * (1.0f - f) + pad->samples[(i + 1) * 2 + 1] * f;
        *out_l = sl * pad->env;
        *out_r = sr * pad->env;
        pad->playhead += rate;
        pad->env *= pad->env_decay;
        if (pad->env < 1e-5f) {
            pad->env = 0.0f;
            pad->active = false;
        }
        return;
    }

    default:
        break;
    }

    s *= pad->env;
    pad->env *= pad->env_decay;
    if (pad->env < 1e-5f) {
        pad->env = 0.0f;
        pad->active = false;
    }
    *out_l = *out_r = s;
}

/* ---------------------------------------------------------------- render */

void drum_rack_render(DrumRack *dr, float *stereo_out, uint32_t frames, double bpm,
                      uint64_t global_frame, bool playing, float track_vol) {
    float scale = track_vol * dr->volume;
    double fpstep = ((double)SR * 60.0 / bpm) / 4.0;

    for (uint32_t f = 0; f < frames; f++) {

        /* TR sequencer — synced exactly like tb303 */
        if (playing) {
            int cur = (int)(((double)(global_frame + f)) / fpstep) % dr->steps;
            if (cur != dr->last_step) {
                dr->last_step = cur;
                dr->current_step = cur;
                for (int p = 0; p < DRUM_PADS; p++) {
                    DrumPad *pad = &dr->pads[p];
                    if (pad->step_on[cur])
                        drum_pad_trigger(pad, pad->step_vel[cur] * pad->volume);
                }
            }
        } else {
            dr->last_step = -1;
        }

        /* Mix active pads into stereo output */
        for (int p = 0; p < DRUM_PADS; p++) {
            DrumPad *pad = &dr->pads[p];
            if (!pad->active)
                continue;

            float pl, pr;
            pad_tick(pad, &pl, &pr);

            /* Linear balance pan */
            float pan_l = pad->pan < 0.0f ? 1.0f : 1.0f - pad->pan;
            float pan_r = pad->pan > 0.0f ? 1.0f : 1.0f + pad->pan;

            stereo_out[f * 2] += pl * pan_l * scale;
            stereo_out[f * 2 + 1] += pr * pan_r * scale;
        }
    }
}

/* Direct pad rendering: the host owns sequencing and track gains. */
void drum_pad_render(DrumPad *pad, float *out, uint32_t frames, float gain) {
    for (uint32_t f = 0; f < frames; ++f) {
        float left, right;
        pad_tick(pad, &left, &right);
        out[f * 2] += left * gain * (pad->pan > 0 ? 1 - pad->pan : 1);
        out[f * 2 + 1] += right * gain * (pad->pan < 0 ? 1 + pad->pan : 1);
    }
}
