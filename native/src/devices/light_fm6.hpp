// SPDX-License-Identifier: MIT
#pragma once

#include "device.hpp"
extern "C" {
#include "fm_synth.h"
}

namespace eco {

class LightFm6Device final : public NativeDevice {
    FM6Synth *synth_ = nullptr;
    bool prepared_ = false;

  public:
    explicit LightFm6Device(int preset = 0);
    ~LightFm6Device() override;
    LightFm6Device(const LightFm6Device &) = delete;
    LightFm6Device &operator=(const LightFm6Device &) = delete;
    LightFm6Device(LightFm6Device &&) = delete;
    LightFm6Device &operator=(LightFm6Device &&) = delete;

    const DeviceDescriptor &descriptor() const noexcept override;
    bool prepare(double sampleRate, uint32_t maxBlockFrames) noexcept override;
    void reset() noexcept override;
    void handleEvent(const DeviceEvent &event) noexcept override;
    void process(float *stereoOutput, uint32_t frames, float gain) noexcept override;
};

} // namespace eco
