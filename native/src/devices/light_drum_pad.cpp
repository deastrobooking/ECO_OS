// SPDX-License-Identifier: MIT
#include "devices/light_drum_pad.hpp"
#include "light_config.h"

#include <algorithm>
#include <cmath>

namespace eco {
namespace {
constexpr DeviceDescriptor kKickDescriptor{"eco.light.drum.kick", "LIGHT Kick", 1, 0, 2, true};
constexpr DeviceDescriptor kSnareDescriptor{"eco.light.drum.snare", "LIGHT Snare", 1, 0, 2, true};
constexpr DeviceDescriptor kHatDescriptor{"eco.light.drum.hihat", "LIGHT Hi-hat", 1, 0, 2, true};
constexpr DeviceDescriptor kClapDescriptor{"eco.light.drum.clap", "LIGHT Clap", 1, 0, 2, true};
constexpr DeviceDescriptor kPercDescriptor{"eco.light.drum.perc", "LIGHT Percussion", 1, 0, 2, true};
}

LightDrumPadDevice::LightDrumPadDevice(uint32_t padIndex) {
    drum_pad_init(&pad_, padIndex);
}

LightDrumPadDevice::~LightDrumPadDevice() {
    drum_pad_dispose(&pad_);
}

const DeviceDescriptor &LightDrumPadDevice::descriptor() const noexcept {
    switch (pad_.type) {
    case DRUM_KICK:
        return kKickDescriptor;
    case DRUM_SNARE:
        return kSnareDescriptor;
    case DRUM_HIHAT:
        return kHatDescriptor;
    case DRUM_CLAP:
        return kClapDescriptor;
    case DRUM_PERC:
        return kPercDescriptor;
    default:
        return kPercDescriptor;
    }
}

bool LightDrumPadDevice::prepare(double sampleRate, uint32_t maxBlockFrames) noexcept {
    prepared_ = std::isfinite(sampleRate) && sampleRate == LIGHT_SAMPLE_RATE && maxBlockFrames > 0;
    return prepared_;
}

void LightDrumPadDevice::reset() noexcept {
    pad_.active = false;
}

void LightDrumPadDevice::handleEvent(const DeviceEvent &event) noexcept {
    if (!prepared_)
        return;
    switch (event.kind) {
    case DeviceEventKind::NoteOn:
        if (event.value > 0.0f)
            drum_pad_trigger(&pad_, std::clamp(event.value, 0.0f, 1.0f));
        break;
    case DeviceEventKind::NoteOff:
        // Drum notes remain one-shots; their envelope controls the release.
        break;
    case DeviceEventKind::AllNotesOff:
        pad_.active = false;
        break;
    }
}

void LightDrumPadDevice::process(float *stereoOutput, uint32_t frames, float gain) noexcept {
    if (!prepared_ || !stereoOutput || frames == 0)
        return;
    drum_pad_render(&pad_, stereoOutput, frames, std::clamp(gain, 0.0f, 1.5f));
}

} // namespace eco
