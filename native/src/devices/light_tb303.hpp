// SPDX-License-Identifier: MIT
#pragma once

#include "device.hpp"
extern "C" {
#include "tb303.h"
}

namespace eco {

class LightTb303Device final : public NativeDevice {
    TB303 *synth_ = nullptr;
    bool prepared_ = false;
    bool active_ = false;

  public:
    LightTb303Device();
    ~LightTb303Device() override;
    LightTb303Device(const LightTb303Device &) = delete;
    LightTb303Device &operator=(const LightTb303Device &) = delete;

    const DeviceDescriptor &descriptor() const noexcept override;
    bool prepare(double sampleRate, uint32_t maxBlockFrames) noexcept override;
    void reset() noexcept override;
    void handleEvent(const DeviceEvent &event) noexcept override;
    void process(float *stereoOutput, uint32_t frames, float gain) noexcept override;
};

} // namespace eco
