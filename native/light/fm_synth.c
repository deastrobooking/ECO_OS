// SPDX-License-Identifier: MIT
// Adapted from deastrobooking/LIGHT at 15f08cae2ea297c8906ffc2cd37bf5e4aa62c693.
/*
 * fm_synth.c — 6-operator FM synthesizer engine, 8-voice polyphony.
 *
 * 8 algorithms define how operators route into each other.
 * Operators numbered 0–5; higher indices = deeper modulators in most algos.
 * Each operator has an independent ADSR envelope.
 * FM_IDX scales the phase modulation depth (≈ modulation index × 2π).
 */

#include "fm_synth.h"
#include "light_config.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FM_TWO_PI 6.28318530717959f
#define FM_INV_SR (1.0f / ((float)LIGHT_SAMPLE_RATE))
#define FM_IDX (3.0f * 3.14159265f) /* modulation depth scale */
#define FM_SR ((float)LIGHT_SAMPLE_RATE)

/* ----------------------------------------------------------------- helpers */

static float midi_to_freq(int note) {
    return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

/* One sample of ADSR.  Linear attack, linear-to-sustain decay, exponential
 * release.  All arithmetic is a single float add/subtract per sample. */
static float env_step(FMOpState *op, const FMOp *p) {
    switch (op->env_stage) {
    case ENV_ATTACK:
        op->env += 1.0f / (p->attack > 0.001f ? p->attack * FM_SR : FM_SR * 0.001f);
        if (op->env >= 1.0f) {
            op->env = 1.0f;
            op->env_stage = ENV_DECAY;
        }
        break;
    case ENV_DECAY: {
        float span = p->decay > 0.001f ? p->decay * FM_SR : FM_SR * 0.001f;
        op->env -= (1.0f - p->sustain) / span;
        if (op->env <= p->sustain) {
            op->env = p->sustain;
            op->env_stage = ENV_SUSTAIN;
        }
        break;
    }
    case ENV_SUSTAIN:
        op->env = p->sustain;
        break;
    case ENV_RELEASE: {
        float span = p->release > 0.001f ? p->release * FM_SR : FM_SR * 0.001f;
        float rate = op->env / span;
        if (rate < 1e-6f)
            rate = 1e-6f;
        op->env -= rate;
        if (op->env < 0.0001f) {
            op->env = 0.0f;
            op->env_stage = ENV_IDLE;
        }
        break;
    }
    default:
        op->env = 0.0f;
    }
    return op->env;
}

static void voice_trigger(FM6Voice *v, FM6Synth *fm, int midi, float vel) {
    v->in_use = true;
    v->gate = true;
    v->midi_note = midi;
    v->velocity = vel;
    v->base_freq = midi_to_freq(midi + fm->octave_shift);
    for (int i = 0; i < FM_OPS; i++) {
        v->op[i].phase = 0.0f;
        v->op[i].fb_prev = 0.0f;
        v->op[i].env = 0.0f;
        v->op[i].env_stage = ENV_ATTACK;
    }
}

static int alloc_voice(FM6Synth *fm, int midi) {
    /* Prefer idle */
    for (int i = 0; i < FM_VOICES; i++)
        if (!fm->voices[i].in_use)
            return i;
    /* Retrigger same pitch */
    for (int i = 0; i < FM_VOICES; i++)
        if (fm->voices[i].midi_note == midi)
            return i;
    /* Round-robin steal */
    int v = fm->next_voice;
    fm->next_voice = (fm->next_voice + 1) % FM_VOICES;
    return v;
}

/* ------------------------------------------------------------ algorithm defs
 *
 * Notation: A→B means A modulates B's phase.
 * (C) = carrier (contributes to audio output).
 *
 *  0  5→4→3→2→1→0(C)               deep chain
 *  1  (5→4→3)→0(C) + (2→1)→0(C)   two stacks → one carrier
 *  2  5→4→3→2→1(C) | 0(C)          chain + free carrier
 *  3  (4+5)→3→2→1→0(C)             two mods → chain
 *  4  5→2(C) + 4→1(C) + 3→0(C)    three pairs
 *  5  (3+4+5)→2→1→0(C)             three mods → short chain
 *  6  5→(0+1+2+3+4)(C)             one mod → five carriers
 *  7  0+1+2+3+4+5(C)               pure additive
 */

bool fm6_is_carrier(int algorithm, int op) {
    switch (algorithm) {
    case 0:
        return op == 0;
    case 1:
        return op == 0;
    case 2:
        return op == 0 || op == 1;
    case 3:
        return op == 0;
    case 4:
        return op == 0 || op == 1 || op == 2;
    case 5:
        return op == 0;
    case 6:
        return op <= 4;
    case 7:
        return true;
    default:
        return false;
    }
}

/* Render one sample for one voice.  Returns audio sample. */
static float voice_sample(FM6Voice *v, FM6Synth *fm) {
    FMOpState *op = v->op;
    const FMOp *p = fm->ops;
    float e[FM_OPS], a[FM_OPS];
    float freq = v->base_freq;
    float out = 0.0f;

    /* Envelope + phase advance for all ops */
    for (int i = 0; i < FM_OPS; i++) {
        e[i] = env_step(&op[i], &p[i]);
        op[i].phase += FM_TWO_PI * freq * p[i].ratio * FM_INV_SR;
        if (op[i].phase >= FM_TWO_PI)
            op[i].phase -= FM_TWO_PI;
    }

    float fb;

    switch (fm->algorithm) {
    case 0: /* 5→4→3→2→1→0(C) */
        a[5] = sinf(op[5].phase) * e[5] * p[5].level;
        a[4] = sinf(op[4].phase + a[5] * FM_IDX) * e[4] * p[4].level;
        a[3] = sinf(op[3].phase + a[4] * FM_IDX) * e[3] * p[3].level;
        a[2] = sinf(op[2].phase + a[3] * FM_IDX) * e[2] * p[2].level;
        a[1] = sinf(op[1].phase + a[2] * FM_IDX) * e[1] * p[1].level;
        fb = op[0].fb_prev * p[0].feedback;
        a[0] = sinf(op[0].phase + a[1] * FM_IDX + fb) * e[0] * p[0].level;
        op[0].fb_prev = a[0];
        out = a[0];
        break;

    case 1: /* (5→4→3)→0(C) + (2→1)→0(C) */
        a[5] = sinf(op[5].phase) * e[5] * p[5].level;
        a[4] = sinf(op[4].phase + a[5] * FM_IDX) * e[4] * p[4].level;
        a[3] = sinf(op[3].phase + a[4] * FM_IDX) * e[3] * p[3].level;
        a[2] = sinf(op[2].phase) * e[2] * p[2].level;
        a[1] = sinf(op[1].phase + a[2] * FM_IDX) * e[1] * p[1].level;
        fb = op[0].fb_prev * p[0].feedback;
        a[0] = sinf(op[0].phase + (a[3] + a[1]) * FM_IDX + fb) * e[0] * p[0].level;
        op[0].fb_prev = a[0];
        out = a[0];
        break;

    case 2: /* 5→4→3→2→1(C) | 0(C) */
        a[5] = sinf(op[5].phase) * e[5] * p[5].level;
        a[4] = sinf(op[4].phase + a[5] * FM_IDX) * e[4] * p[4].level;
        a[3] = sinf(op[3].phase + a[4] * FM_IDX) * e[3] * p[3].level;
        a[2] = sinf(op[2].phase + a[3] * FM_IDX) * e[2] * p[2].level;
        a[1] = sinf(op[1].phase + a[2] * FM_IDX) * e[1] * p[1].level;
        a[0] = sinf(op[0].phase) * e[0] * p[0].level;
        out = a[1] + a[0];
        break;

    case 3: /* (4+5)→3→2→1→0(C) */
        a[5] = sinf(op[5].phase) * e[5] * p[5].level;
        a[4] = sinf(op[4].phase) * e[4] * p[4].level;
        a[3] = sinf(op[3].phase + (a[4] + a[5]) * FM_IDX) * e[3] * p[3].level;
        a[2] = sinf(op[2].phase + a[3] * FM_IDX) * e[2] * p[2].level;
        a[1] = sinf(op[1].phase + a[2] * FM_IDX) * e[1] * p[1].level;
        fb = op[0].fb_prev * p[0].feedback;
        a[0] = sinf(op[0].phase + a[1] * FM_IDX + fb) * e[0] * p[0].level;
        op[0].fb_prev = a[0];
        out = a[0];
        break;

    case 4: /* 5→2(C) + 4→1(C) + 3→0(C) */
        a[5] = sinf(op[5].phase) * e[5] * p[5].level;
        a[4] = sinf(op[4].phase) * e[4] * p[4].level;
        a[3] = sinf(op[3].phase) * e[3] * p[3].level;
        a[2] = sinf(op[2].phase + a[5] * FM_IDX) * e[2] * p[2].level;
        a[1] = sinf(op[1].phase + a[4] * FM_IDX) * e[1] * p[1].level;
        a[0] = sinf(op[0].phase + a[3] * FM_IDX) * e[0] * p[0].level;
        out = a[2] + a[1] + a[0];
        break;

    case 5: /* (3+4+5)→2→1→0(C) */
        a[5] = sinf(op[5].phase) * e[5] * p[5].level;
        a[4] = sinf(op[4].phase) * e[4] * p[4].level;
        a[3] = sinf(op[3].phase) * e[3] * p[3].level;
        a[2] = sinf(op[2].phase + (a[3] + a[4] + a[5]) * FM_IDX) * e[2] * p[2].level;
        a[1] = sinf(op[1].phase + a[2] * FM_IDX) * e[1] * p[1].level;
        fb = op[0].fb_prev * p[0].feedback;
        a[0] = sinf(op[0].phase + a[1] * FM_IDX + fb) * e[0] * p[0].level;
        op[0].fb_prev = a[0];
        out = a[0];
        break;

    case 6: /* 5→(0+1+2+3+4)(C) */
        a[5] = sinf(op[5].phase) * e[5] * p[5].level;
        for (int i = 0; i < 5; i++)
            a[i] = sinf(op[i].phase + a[5] * FM_IDX) * e[i] * p[i].level;
        out = a[0] + a[1] + a[2] + a[3] + a[4];
        break;

    case 7: /* all carriers */
        for (int i = 0; i < FM_OPS; i++)
            a[i] = sinf(op[i].phase) * e[i] * p[i].level;
        out = a[0] + a[1] + a[2] + a[3] + a[4] + a[5];
        break;

    default:
        out = 0.0f;
    }

    return out;
}

/* ------------------------------------------------------------------ public */

FM6Synth *fm6_create(void) {
    FM6Synth *fm = calloc(1, sizeof(FM6Synth));
    if (!fm)
        return NULL;
    fm->master_vol = 0.75f;
    fm6_load_preset(fm, 0);
    return fm;
}

void fm6_free(FM6Synth *fm) {
    free(fm);
}

void fm6_note_on(FM6Synth *fm, int midi_note, float velocity) {
    int v = alloc_voice(fm, midi_note);
    voice_trigger(&fm->voices[v], fm, midi_note, velocity);
}

void fm6_note_off(FM6Synth *fm, int midi_note) {
    for (int v = 0; v < FM_VOICES; v++) {
        FM6Voice *vp = &fm->voices[v];
        if (vp->in_use && vp->gate && vp->midi_note == midi_note) {
            vp->gate = false;
            for (int i = 0; i < FM_OPS; i++)
                vp->op[i].env_stage = ENV_RELEASE;
        }
    }
}

void fm6_render(FM6Synth *fm, float *stereo_out, uint32_t frames, float track_vol) {
    /* 0.25 normalises 8 simultaneous voices to roughly ±1 */
    float scale = fm->master_vol * track_vol * 0.25f;

    for (uint32_t f = 0; f < frames; f++) {
        float mix = 0.0f;

        for (int v = 0; v < FM_VOICES; v++) {
            FM6Voice *vp = &fm->voices[v];
            if (!vp->in_use)
                continue;

            mix += voice_sample(vp, fm) * vp->velocity;

            /* Auto-retire when all envelopes are idle */
            bool idle = true;
            for (int i = 0; i < FM_OPS; i++)
                if (vp->op[i].env_stage != ENV_IDLE) {
                    idle = false;
                    break;
                }
            if (idle)
                vp->in_use = false;
        }

        float s = mix * scale;
        stereo_out[f * 2] += s;
        stereo_out[f * 2 + 1] += s;
    }
}

void fm6_load_preset(FM6Synth *fm, int preset) {
    memset(fm->ops, 0, sizeof(fm->ops));
    /* Default: silent operators (overridden per preset) */
    for (int i = 0; i < FM_OPS; i++) {
        fm->ops[i].ratio = 1.0f;
        fm->ops[i].attack = 0.002f;
        fm->ops[i].decay = 0.3f;
        fm->ops[i].release = 0.3f;
    }

    switch (preset) {
    case 0: /* E.Piano — algo 0, two-op style */
        fm->algorithm = 0;
        /* Carrier (op0) */
        fm->ops[0].ratio = 1.0f;
        fm->ops[0].level = 1.0f;
        fm->ops[0].attack = 0.002f;
        fm->ops[0].decay = 2.5f;
        fm->ops[0].sustain = 0.2f;
        fm->ops[0].release = 1.2f;
        /* Modulator (op1) */
        fm->ops[1].ratio = 2.0f;
        fm->ops[1].level = 0.6f;
        fm->ops[1].attack = 0.002f;
        fm->ops[1].decay = 0.7f;
        fm->ops[1].sustain = 0.0f;
        fm->ops[1].release = 0.4f;
        break;

    case 1: /* Brass — algo 3, (4+5)→3→2→1→0 */
        fm->algorithm = 3;
        fm->ops[0].ratio = 1.0f;
        fm->ops[0].level = 1.0f;
        fm->ops[0].attack = 0.04f;
        fm->ops[0].decay = 0.8f;
        fm->ops[0].sustain = 0.7f;
        fm->ops[0].release = 0.15f;
        fm->ops[1].ratio = 1.0f;
        fm->ops[1].level = 0.45f;
        fm->ops[1].attack = 0.03f;
        fm->ops[1].decay = 0.5f;
        fm->ops[1].sustain = 0.25f;
        fm->ops[1].release = 0.1f;
        fm->ops[2].ratio = 1.0f;
        fm->ops[2].level = 0.5f;
        fm->ops[2].attack = 0.02f;
        fm->ops[2].decay = 0.6f;
        fm->ops[2].sustain = 0.3f;
        fm->ops[2].release = 0.1f;
        fm->ops[3].ratio = 1.0f;
        fm->ops[3].level = 0.7f;
        fm->ops[3].attack = 0.04f;
        fm->ops[3].decay = 0.5f;
        fm->ops[3].sustain = 0.4f;
        fm->ops[3].release = 0.1f;
        fm->ops[4].ratio = 2.0f;
        fm->ops[4].level = 0.3f;
        fm->ops[4].attack = 0.001f;
        fm->ops[4].decay = 0.3f;
        fm->ops[4].sustain = 0.0f;
        fm->ops[4].release = 0.05f;
        fm->ops[5].ratio = 2.0f;
        fm->ops[5].level = 0.4f;
        fm->ops[5].attack = 0.001f;
        fm->ops[5].decay = 0.4f;
        fm->ops[5].sustain = 0.0f;
        fm->ops[5].release = 0.05f;
        break;

    case 2: /* Organ — algo 4, three pairs */
        fm->algorithm = 4;
        /* Pair A: 3→0(C)  fundamental */
        fm->ops[0].ratio = 1.0f;
        fm->ops[0].level = 0.8f;
        fm->ops[0].attack = 0.004f;
        fm->ops[0].decay = 0.05f;
        fm->ops[0].sustain = 1.0f;
        fm->ops[0].release = 0.05f;
        fm->ops[3].ratio = 1.0f;
        fm->ops[3].level = 0.04f;
        fm->ops[3].attack = 0.004f;
        fm->ops[3].decay = 0.05f;
        fm->ops[3].sustain = 1.0f;
        fm->ops[3].release = 0.05f;
        /* Pair B: 4→1(C)  octave */
        fm->ops[1].ratio = 2.0f;
        fm->ops[1].level = 0.5f;
        fm->ops[1].attack = 0.004f;
        fm->ops[1].decay = 0.05f;
        fm->ops[1].sustain = 1.0f;
        fm->ops[1].release = 0.05f;
        fm->ops[4].ratio = 2.0f;
        fm->ops[4].level = 0.04f;
        fm->ops[4].attack = 0.004f;
        fm->ops[4].decay = 0.05f;
        fm->ops[4].sustain = 1.0f;
        fm->ops[4].release = 0.05f;
        /* Pair C: 5→2(C)  fifth */
        fm->ops[2].ratio = 3.0f;
        fm->ops[2].level = 0.35f;
        fm->ops[2].attack = 0.004f;
        fm->ops[2].decay = 0.05f;
        fm->ops[2].sustain = 1.0f;
        fm->ops[2].release = 0.05f;
        fm->ops[5].ratio = 3.0f;
        fm->ops[5].level = 0.04f;
        fm->ops[5].attack = 0.004f;
        fm->ops[5].decay = 0.05f;
        fm->ops[5].sustain = 1.0f;
        fm->ops[5].release = 0.05f;
        break;

    case 3: /* Bell — algo 2, chain + carrier with long decays */
        fm->algorithm = 2;
        fm->ops[0].ratio = 1.0f;
        fm->ops[0].level = 0.7f;
        fm->ops[0].attack = 0.002f;
        fm->ops[0].decay = 4.0f;
        fm->ops[0].sustain = 0.0f;
        fm->ops[0].release = 2.0f;
        fm->ops[1].ratio = 3.5f;
        fm->ops[1].level = 1.0f;
        fm->ops[1].attack = 0.002f;
        fm->ops[1].decay = 2.0f;
        fm->ops[1].sustain = 0.0f;
        fm->ops[1].release = 1.5f;
        fm->ops[2].ratio = 1.0f;
        fm->ops[2].level = 0.35f;
        fm->ops[2].attack = 0.002f;
        fm->ops[2].decay = 3.0f;
        fm->ops[2].sustain = 0.0f;
        fm->ops[2].release = 1.5f;
        fm->ops[3].ratio = 3.0f;
        fm->ops[3].level = 0.6f;
        fm->ops[3].attack = 0.001f;
        fm->ops[3].decay = 1.5f;
        fm->ops[3].sustain = 0.0f;
        fm->ops[3].release = 1.0f;
        fm->ops[4].ratio = 5.0f;
        fm->ops[4].level = 0.5f;
        fm->ops[4].attack = 0.001f;
        fm->ops[4].decay = 1.0f;
        fm->ops[4].sustain = 0.0f;
        fm->ops[4].release = 0.5f;
        fm->ops[5].ratio = 7.0f;
        fm->ops[5].level = 0.3f;
        fm->ops[5].attack = 0.001f;
        fm->ops[5].decay = 0.8f;
        fm->ops[5].sustain = 0.0f;
        fm->ops[5].release = 0.3f;
        break;
    }
}
