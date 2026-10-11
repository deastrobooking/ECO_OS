// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string_view>

namespace eco {

enum class DeviceEventKind : uint8_t { NoteOn, NoteOff, AllNotesOff };

struct DeviceEvent {
    DeviceEventKind kind = DeviceEventKind::NoteOff;
    uint8_t channel = 0;
    uint8_t pitch = 60;
    float value = 0.0f; // velocity for NoteOn
};

struct DeviceDescriptor {
    std::string_view id;
    std::string_view name;
    uint32_t descriptorVersion;
    uint8_t audioInputs;
    uint8_t audioOutputs;
    bool acceptsMidi;
};

// Runtime contract for ECO-owned instruments and effects. Instances are
// created, configured, and destroyed on the control thread. process() and
// handleEvent() run on the audio thread after prepare(). Implementations must
// use caller-owned buffers and avoid allocation, locks, and I/O there.
class NativeDevice {
  public:
    virtual ~NativeDevice() = default;

    virtual const DeviceDescriptor &descriptor() const noexcept = 0;
    virtual bool prepare(double sampleRate, uint32_t maxBlockFrames) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void handleEvent(const DeviceEvent &event) noexcept = 0;
    // Add stereo output to interleaved output; caller initializes the buffer.
    virtual void process(float *stereoOutput, uint32_t frames, float gain) noexcept = 0;
};

} // namespace eco
