// SPDX-License-Identifier: MIT
#pragma once
extern "C" {
#include "drum_rack.h"
#include "fm_synth.h"
#include "mix_neon.h"
#include "tb303.h"
}
#include <algorithm>
#include <array>
#include <memory>
#include <new>

// Construct/destruct only on the control thread. DSP state has one RT owner.
class LightInstruments {
    std::unique_ptr<DrumRack, decltype(&drum_rack_free)> drums{drum_rack_create(), drum_rack_free};
    std::unique_ptr<TB303, decltype(&tb303_free)> bass{tb303_create(), tb303_free};
    std::array<FM6Synth *, 2> fm{};
    std::array<int, 2> releaseIn{};
    bool bassSounding = false;

  public:
    LightInstruments() {
        if (!drums || !bass)
            throw std::bad_alloc();
        fm[0] = fm6_create();
        fm[1] = fm6_create();
        if (!fm[0] || !fm[1]) {
            fm6_free(fm[0]);
            fm6_free(fm[1]);
            throw std::bad_alloc();
        }
        fm6_load_preset(fm[1], 3);
        for (auto &step : bass->steps)
            step = {36, false, false, false};
        bass->last_step = 0;
        bass->current_step = 0;
    }
    ~LightInstruments() {
        for (auto *synth : fm)
            fm6_free(synth);
    }
    LightInstruments(const LightInstruments &) = delete;
    LightInstruments &operator=(const LightInstruments &) = delete;
    void noteOn(int track, int pitch, float velocity) noexcept {
        if (track < 3) {
            drum_pad_trigger(&drums->pads[track], velocity);
            return;
        }
        if (track == 3) {
            bass->steps[0] = {static_cast<uint8_t>(pitch), velocity > .9f, false, true};
            bass->last_step = -1;
            bass->volume = velocity;
            bassSounding = true;
            return;
        }
        auto *synth = fm[track - 4];
        for (auto &voice : synth->voices)
            if (voice.gate)
                fm6_note_off(synth, voice.midi_note);
        fm6_note_on(synth, pitch, velocity);
        releaseIn[track - 4] = 7200; // MVP clip/audition gate: 150 ms.
    }
    void noteOff(int track, int pitch) noexcept {
        if (track >= 4 && track < 6)
            fm6_note_off(fm[track - 4], pitch);
    }
    void stop() noexcept {
        for (auto &pad : drums->pads)
            pad.active = false;
        for (auto *synth : fm)
            for (auto &voice : synth->voices)
                voice.in_use = false;
        bassSounding = false;
        bass->vca_env = 0;
        bass->vcf_env = 0;
        bass->last_step = -1;
        releaseIn.fill(0);
    }
    // Host calls one sample at a time to preserve sample-exact event boundaries.
    // A later optimization may segment blocks without changing this contract.
    void render(float *out, const std::array<float, 6> &gains) noexcept {
        for (int t = 0; t < 3; t++)
            drum_pad_render(&drums->pads[t], out, 1, gains[t]);
        if (bassSounding)
            tb303_render(bass.get(), out, 1, 120, 0, true, gains[3]);
        for (int i = 0; i < 2; i++) {
            if (releaseIn[i] > 0 && --releaseIn[i] == 0)
                for (auto &voice : fm[i]->voices)
                    if (voice.gate)
                        fm6_note_off(fm[i], voice.midi_note);
            fm6_render(fm[i], out, 1, gains[i + 4]);
        }
    }
};
