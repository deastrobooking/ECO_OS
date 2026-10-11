// SPDX-License-Identifier: MIT
#pragma once

#include "device.hpp"
extern "C" {
#include "drum_rack.h"
}

namespace eco {

class LightDrumPadDevice final : public NativeDevice {
    DrumPad pad_{};
    bool prepared_ = false;

  public:
    explicit LightDrumPadDevice(uint32_t padIndex);
    ~LightDrumPadDevice() override;
    LightDrumPadDevice(const LightDrumPadDevice &) = delete;
    LightDrumPadDevice &operator=(const LightDrumPadDevice &) = delete;

    const DeviceDescriptor &descriptor() const noexcept override;
    bool prepare(double sampleRate, uint32_t maxBlockFrames) noexcept override;
    void reset() noexcept override;
    void handleEvent(const DeviceEvent &event) noexcept override;
    void process(float *stereoOutput, uint32_t frames, float gain) noexcept override;
};

} // namespace eco
