// SPDX-License-Identifier: MIT
#include "devices/light_fm6.hpp"
#include "light_config.h"

#include <algorithm>
#include <cmath>

namespace eco {
namespace {
constexpr DeviceDescriptor kLightFm6Descriptor{
    "eco.light.fm6", "LIGHT FM6", 1, 0, 2, true};
}

LightFm6Device::LightFm6Device(int preset) : synth_(fm6_create()) {
    if (synth_)
        fm6_load_preset(synth_, std::clamp(preset, 0, 3));
}

LightFm6Device::~LightFm6Device() {
    fm6_free(synth_);
}

const DeviceDescriptor &LightFm6Device::descriptor() const noexcept {
    return kLightFm6Descriptor;
}

bool LightFm6Device::prepare(double sampleRate, uint32_t maxBlockFrames) noexcept {
    prepared_ = synth_ && std::isfinite(sampleRate) && sampleRate == LIGHT_SAMPLE_RATE &&
                maxBlockFrames > 0;
    return prepared_;
}

void LightFm6Device::reset() noexcept {
    if (!synth_)
        return;
    for (auto &voice : synth_->voices)
        voice.in_use = false;
}

void LightFm6Device::handleEvent(const DeviceEvent &event) noexcept {
    if (!prepared_ || !synth_)
        return;
    switch (event.kind) {
    case DeviceEventKind::NoteOn:
        if (event.value > 0.0f)
            fm6_note_on(synth_, event.pitch, std::clamp(event.value, 0.0f, 1.0f));
        else
            fm6_note_off(synth_, event.pitch);
        break;
    case DeviceEventKind::NoteOff:
        fm6_note_off(synth_, event.pitch);
        break;
    case DeviceEventKind::AllNotesOff:
        for (const auto &voice : synth_->voices)
            if (voice.in_use && voice.gate)
                fm6_note_off(synth_, voice.midi_note);
        break;
    }
}

void LightFm6Device::process(float *stereoOutput, uint32_t frames, float gain) noexcept {
    if (!prepared_ || !synth_ || !stereoOutput || frames == 0)
        return;
    fm6_render(synth_, stereoOutput, frames, std::clamp(gain, 0.0f, 1.5f));
}

} // namespace eco
