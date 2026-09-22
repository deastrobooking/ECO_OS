// SPDX-License-Identifier: MIT
// Adapted from deastrobooking/LIGHT at 15f08cae2ea297c8906ffc2cd37bf5e4aa62c693.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DRUM_PADS 16  /* 4×4 MPC-style grid        */
#define DRUM_STEPS 32 /* max steps (16 or 32 active) */

typedef enum {
    DRUM_NONE = 0,
    DRUM_KICK,   /* sine sweep + click transient   */
    DRUM_SNARE,  /* tone + HP noise                */
    DRUM_HIHAT,  /* HP noise, decay = open/closed  */
    DRUM_CLAP,   /* multi-burst noise              */
    DRUM_PERC,   /* tunable tom / conga / cowbell  */
    DRUM_SAMPLE, /* user-loaded audio file          */
} DrumType;

typedef struct {
    DrumType type;
    char name[32];

    /* Sample data — only valid when type == DRUM_SAMPLE */
    float *samples;
    uint32_t frame_count;
    char path[512];

    /* Common parameters */
    float volume; /* 0–1.5, output gain            */
    float pan;    /* -1 (L) … +1 (R)               */
    float pitch;  /* semitones -24…+24              */
    float decay;  /* 0.05–2.0 s                     */

    /* Synth character knobs */
    float tone;  /* kick: sweep depth; snare: body/noise mix; perc: sweep */
    float snap;  /* kick: click; snare: rim; clap: burst spread           */
    float color; /* snare/hihat: filter brightness; perc: noise texture   */

    /* TR-style step pattern */
    bool step_on[DRUM_STEPS];
    float step_vel[DRUM_STEPS]; /* per-step velocity 0–1 */

    /* ---- Audio-thread voice state (do not touch from UI thread) ---- */
    float phase;          /* oscillator phase 0–1         */
    float env;            /* amplitude envelope 0–1       */
    float env2;           /* pitch env (kick/perc), clap timer */
    uint32_t noise_state; /* independent deterministic noise source */
    float noise_s;        /* 1-pole LP filter state       */
    float playhead;       /* sample playback position     */
    bool active;          /* pad is sounding              */
    float env_decay;      /* precomputed per-sample decay */
    float e2_decay;       /* precomputed env2 decay       */
} DrumPad;

typedef struct {
    DrumPad pads[DRUM_PADS];
    int steps;        /* active pattern length: 16 or 32 */
    int current_step; /* 0 … steps-1                     */
    int last_step;    /* for change detection             */
    float swing;      /* 0=straight, 0.5=full swing       */
    float volume;     /* master drum volume               */
    int selected_pad; /* which pad is being edited (UI)   */
} DrumRack;

DrumRack *drum_rack_create(void);
void drum_rack_free(DrumRack *dr);
void drum_pad_trigger(DrumPad *pad, float velocity);
void drum_rack_render(DrumRack *dr, float *stereo_out, uint32_t frames, double bpm,
                      uint64_t global_frame, bool playing, float track_vol);

void drum_pad_render(DrumPad *pad, float *stereo_out, uint32_t frames, float gain);
