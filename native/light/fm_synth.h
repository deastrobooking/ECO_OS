// SPDX-License-Identifier: MIT
// Adapted from deastrobooking/LIGHT at 15f08cae2ea297c8906ffc2cd37bf5e4aa62c693.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FM_OPS 6
#define FM_VOICES 8

typedef enum { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE } EnvStage;

typedef struct {
    float ratio;    /* frequency multiplier, e.g. 1.0, 2.0, 0.5 */
    float level;    /* output amplitude 0–1                       */
    float feedback; /* self-mod strength 0–1 (op[0] in most algos) */
    float attack;   /* seconds 0.001–4.0                          */
    float decay;    /* seconds 0.001–4.0                          */
    float sustain;  /* 0–1                                        */
    float release;  /* seconds 0.001–4.0                          */
} FMOp;

typedef struct {
    float phase;   /* oscillator phase [0, 2π) */
    float fb_prev; /* previous output (feedback self-mod)    */
    float env;     /* current envelope level 0–1             */
    EnvStage env_stage;
} FMOpState;

typedef struct {
    float base_freq; /* Hz                    */
    float velocity;  /* 0–1                   */
    bool gate;       /* note is held           */
    bool in_use;     /* slot is occupied        */
    int midi_note;   /* for note-off matching   */
    FMOpState op[FM_OPS];
} FM6Voice;

typedef struct {
    FMOp ops[FM_OPS];
    int algorithm;    /* 0–7                   */
    float master_vol; /* 0–1.5                 */
    int octave_shift; /* semitone offset (-24…+24) */
    FM6Voice voices[FM_VOICES];
    int next_voice; /* round-robin allocator  */
} FM6Synth;

FM6Synth *fm6_create(void);
void fm6_free(FM6Synth *fm);
void fm6_note_on(FM6Synth *fm, int midi_note, float velocity);
void fm6_note_off(FM6Synth *fm, int midi_note);
void fm6_render(FM6Synth *fm, float *stereo_out, uint32_t frames, float track_vol);
void fm6_load_preset(FM6Synth *fm, int preset); /* 0=E.Piano 1=Brass 2=Organ 3=Bell */
bool fm6_is_carrier(int algorithm, int op);
