// SPDX-License-Identifier: MIT
#include "devices/light_tb303.hpp"
#include "light_config.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace eco {
namespace {
constexpr DeviceDescriptor kLightTb303Descriptor{
    "eco.light.tb303", "LIGHT TB-303", 1, 0, 2, true};
}

LightTb303Device::LightTb303Device() : synth_(tb303_create()) {
    if (!synth_)
        throw std::bad_alloc();
}

LightTb303Device::~LightTb303Device() {
    tb303_free(synth_);
}

const DeviceDescriptor &LightTb303Device::descriptor() const noexcept {
    return kLightTb303Descriptor;
}

bool LightTb303Device::prepare(double sampleRate, uint32_t maxBlockFrames) noexcept {
    prepared_ = synth_ && std::isfinite(sampleRate) && sampleRate == LIGHT_SAMPLE_RATE &&
                maxBlockFrames > 0;
    return prepared_;
}

void LightTb303Device::reset() noexcept {
    if (synth_)
        tb303_reset(synth_);
    active_ = false;
}

void LightTb303Device::handleEvent(const DeviceEvent &event) noexcept {
    if (!prepared_ || !synth_)
        return;
    switch (event.kind) {
    case DeviceEventKind::NoteOn:
        if (event.value > 0.0f) {
            synth_->volume = std::clamp(event.value, 0.0f, 1.0f);
            tb303_note_on(synth_, event.pitch, event.value > 0.9f, false);
            active_ = true;
        } else {
            tb303_note_off(synth_);
        }
        break;
    case DeviceEventKind::NoteOff:
        tb303_note_off(synth_);
        break;
    case DeviceEventKind::AllNotesOff:
        tb303_note_off(synth_);
        break;
    }
}

void LightTb303Device::process(float *stereoOutput, uint32_t frames, float gain) noexcept {
    if (!prepared_ || !synth_ || !active_ || !stereoOutput || frames == 0)
        return;
    tb303_process(synth_, stereoOutput, frames, std::clamp(gain, 0.0f, 1.5f));
}

} // namespace eco
