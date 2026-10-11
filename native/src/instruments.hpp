// SPDX-License-Identifier: MIT
#pragma once
extern "C" {
#include "drum_rack.h"
#include "mix_neon.h"
#include "light_config.h"
}
#include "devices/light_fm6.hpp"
#include "devices/light_drum_pad.hpp"
#include "devices/light_tb303.hpp"
#include <algorithm>
#include <array>
#include <new>

// Construct/destruct only on the control thread. DSP state has one RT owner.
class LightInstruments {
    std::array<eco::LightDrumPadDevice, 3> drums{
        eco::LightDrumPadDevice{0}, eco::LightDrumPadDevice{1}, eco::LightDrumPadDevice{2}};
    eco::LightTb303Device bass;
    std::array<eco::LightFm6Device, 2> fm{eco::LightFm6Device{0}, eco::LightFm6Device{3}};
    std::array<int, 2> releaseIn{};

  public:
    LightInstruments() {
        for (auto &drum : drums)
            if (!drum.prepare(LIGHT_SAMPLE_RATE, 256))
                throw std::bad_alloc();
        if (!bass.prepare(LIGHT_SAMPLE_RATE, 256))
            throw std::bad_alloc();
        if (!fm[0].prepare(LIGHT_SAMPLE_RATE, 256) ||
            !fm[1].prepare(LIGHT_SAMPLE_RATE, 256))
            throw std::bad_alloc();
    }
    ~LightInstruments() = default;
    LightInstruments(const LightInstruments &) = delete;
    LightInstruments &operator=(const LightInstruments &) = delete;
    void noteOn(int track, int pitch, float velocity, int gateFrames = 7200) noexcept {
        if (track < 3) {
            drums[track].handleEvent({eco::DeviceEventKind::NoteOn, 0,
                                      static_cast<uint8_t>(pitch), velocity});
            return;
        }
        if (track == 3) {
            bass.handleEvent({eco::DeviceEventKind::NoteOn, 0,
                              static_cast<uint8_t>(pitch), velocity});
            return;
        }
        auto &synth = fm[track - 4];
        synth.handleEvent({eco::DeviceEventKind::NoteOn, 0,
                           static_cast<uint8_t>(pitch), velocity});
        releaseIn[track - 4] = std::max(gateFrames, 1); // Default 150 ms audition gate.
    }
    void midiNoteOn(int track, int pitch, float velocity) noexcept {
        if (track < 0 || track >= 6)
            return;
        if (track < 4) {
            noteOn(track, pitch, velocity);
            return;
        }
        // Sequenced MIDI notes use their event duration and explicit note-off;
        // they must not inherit the short audition/legacy-step release timer.
        releaseIn[track - 4] = 0;
        fm[track - 4].handleEvent({eco::DeviceEventKind::NoteOn, 0,
                                   static_cast<uint8_t>(pitch), velocity});
    }
    void noteOff(int track, int pitch) noexcept {
        if (track == 3)
            bass.handleEvent({eco::DeviceEventKind::NoteOff, 0,
                              static_cast<uint8_t>(pitch), 0.0f});
        else if (track >= 4 && track < 6)
            fm[track - 4].handleEvent({eco::DeviceEventKind::NoteOff, 0,
                                       static_cast<uint8_t>(pitch), 0.0f});
    }
    void allNotesOff(int track) noexcept {
        if (track >= 0 && track < 3) {
            drums[track].handleEvent({eco::DeviceEventKind::AllNotesOff, 0, 0, 0.0f});
            return;
        }
        if (track == 3) {
            bass.handleEvent({eco::DeviceEventKind::AllNotesOff, 0, 0, 0.0f});
            return;
        }
        if (track < 4 || track >= 6)
            return;
        fm[track - 4].handleEvent({eco::DeviceEventKind::AllNotesOff, 0, 0, 0.0f});
    }
    void stop() noexcept {
        for (auto &drum : drums)
            drum.reset();
        bass.reset();
        for (auto &synth : fm)
            synth.reset();
        releaseIn.fill(0);
    }
    // Host calls one sample at a time to preserve sample-exact event boundaries.
    // A later optimization may segment blocks without changing this contract.
    void render(float *out, const std::array<float, 6> &gains) noexcept {
        for (int t = 0; t < 3; t++)
            drums[t].process(out, 1, gains[t]);
        bass.process(out, 1, gains[3]);
        for (int i = 0; i < 2; i++) {
            if (releaseIn[i] > 0 && --releaseIn[i] == 0)
                fm[i].handleEvent({eco::DeviceEventKind::AllNotesOff, 0, 0, 0.0f});
            fm[i].process(out, 1, gains[i + 4]);
        }
    }
};
