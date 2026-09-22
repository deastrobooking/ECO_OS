// SPDX-License-Identifier: MIT
#pragma once
#include "instruments.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace eco {
constexpr unsigned Tracks = 6, Scenes = 4, Steps = 16;
struct Note {
    int pitch = 60;
    float velocity = 0;
};
struct Track {
    float volume = .5f;
    bool mute = false, solo = false;
    std::array<std::array<Note, Steps>, Scenes> clips{};
};
struct Project {
    float bpm = 112, master = .7f;
    std::array<Track, Tracks> tracks{};
};
inline Project demo() {
    Project p;
    for (unsigned t = 0; t < Tracks; t++)
        for (unsigned c = 0; c < Scenes; c++)
            for (unsigned s = 0; s < Steps; s++) {
                bool hit = t == 0   ? s % 4 == 0
                           : t == 1 ? s % 8 == 4
                           : t == 2 ? s % 4 == 2
                           : t == 3 ? (s == 0 || s == 6 || s == 10)
                           : t == 4 ? s % 8 == 0
                                    : s % 5 == 2;
                if (c == 2)
                    hit = hit && s % 8 < 4;
                if (c == 1 && t == 2)
                    hit = s % 2 == 0;
                p.tracks[t].clips[c][s] = {std::array<int, 6>{36, 38, 42, 36, 60, 72}[t] +
                                               (t > 2 ? int(c) * 2 : 0),
                                           hit ? .8f : 0.f};
            }
    p.tracks[0].volume = .85;
    p.tracks[2].volume = .3;
    p.tracks[4].volume = .32;
    p.tracks[5].volume = .25;
    return p;
}

template <class T, std::size_t N> class Queue {
    static_assert(N > 1);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    std::array<T, N> items{};
    alignas(64) std::atomic<unsigned> read{0};
    alignas(64) std::atomic<unsigned> write{0};

  public:
    bool push(const T &item) noexcept {
        const unsigned w = write.load(std::memory_order_relaxed), next = (w + 1) % N;
        if (next == read.load(std::memory_order_acquire))
            return false;
        items[w] = item;
        write.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T &item) noexcept {
        const auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire))
            return false;
        item = items[r];
        read.store((r + 1) % N, std::memory_order_release);
        return true;
    }
};
enum class Action { Update, Start, Stop, Launch, Audition, NoteOff, Restore };
struct Command {
    Action action = Action::Stop;
    Project project{};
    std::array<int, Tracks> active{};
    int track = 0, clip = 0, pitch = 60;
    float velocity = .8f;
    // Sample offset within the render() block this command should take
    // effect at (Audition/NoteOff only; other actions apply immediately).
    // A default of 0 applies at the start of the block, matching prior
    // behavior for callers that don't set it.
    unsigned frameOffset = 0;
};
class Engine {
    LightInstruments instruments;
    Project project = demo();
    std::array<float, Tracks> gains{};
    float master = 0;
    std::array<int, Tracks> active{}, pending{-1, -1, -1, -1, -1, -1};
    double nextStep = 0;
    uint64_t frame = 0;
    static constexpr unsigned rate = 48000;
    int step = -1;
    bool playing = false;
    void trigger(int track, int pitch, float velocity) noexcept {
        if (track < 0 || track >= int(Tracks))
            return;
        instruments.noteOn(track, std::clamp(pitch, 24, 96), std::clamp(velocity, 0.f, 1.f));
    }

  public:
    Queue<Command, 32> commands;
    std::atomic<int> currentStep{-1};
    std::atomic<unsigned> activeClips{0};
    Engine() = default;
    void render(float *output, std::size_t frames) noexcept {
        Command cmd;
        // Audition/NoteOff carry a sample offset and are applied mid-block,
        // at the exact frame they target, instead of all at the block start.
        std::array<Command, 31> timed{};
        unsigned timedCount = 0;
        for (unsigned count = 0; count < 31 && commands.pop(cmd); count++)
            switch (cmd.action) {
            case Action::Restore:
                project = cmd.project;
                for (unsigned i = 0; i < Tracks; i++)
                    active[i] = std::clamp(cmd.active[i], 0, 3);
                pending.fill(-1);
                instruments.stop();
                playing = false;
                step = -1;
                frame = 0;
                nextStep = 0;
                break;
            case Action::Update:
                project = cmd.project;
                break;
            case Action::Start:
                playing = true;
                step = -1;
                frame = 0;
                nextStep = 0;
                break;
            case Action::Stop:
                playing = false;
                step = -1;
                pending.fill(-1);
                instruments.stop();
                break;
            case Action::Launch:
                if (cmd.track >= 0 && cmd.track < int(Tracks) && cmd.clip >= 0 &&
                    cmd.clip < int(Scenes)) {
                    if (playing)
                        pending[cmd.track] = cmd.clip;
                    else
                        active[cmd.track] = cmd.clip;
                }
                break;
            case Action::Audition:
            case Action::NoteOff:
                cmd.frameOffset =
                    frames > 0 ? std::min<unsigned>(cmd.frameOffset, unsigned(frames) - 1) : 0;
                timed[timedCount++] = cmd;
                break;
            }
        const bool solo = std::any_of(project.tracks.begin(), project.tracks.end(),
                                      [](auto &t) { return t.solo; });
        for (std::size_t n = 0; n < frames; n++) {
            for (unsigned i = 0; i < timedCount; i++)
                if (timed[i].frameOffset == n) {
                    if (timed[i].action == Action::Audition)
                        trigger(timed[i].track, timed[i].pitch, timed[i].velocity);
                    else
                        instruments.noteOff(timed[i].track, timed[i].pitch);
                }
            if (playing && double(frame) >= nextStep) {
                step = (step + 1) % Steps;
                if (step == 0)
                    for (unsigned i = 0; i < Tracks; i++)
                        if (pending[i] >= 0) {
                            active[i] = pending[i];
                            pending[i] = -1;
                        }
                for (unsigned i = 0; i < Tracks; i++) {
                    const auto &note = project.tracks[i].clips[active[i]][step];
                    if (note.velocity > 0)
                        trigger(i, note.pitch, note.velocity);
                }
                nextStep += rate * 60.0 / std::clamp(project.bpm, 40.f, 240.f) / 4;
            }
            master += (std::clamp(project.master, 0.f, 1.f) - master) * .002f;
            for (unsigned i = 0; i < Tracks; i++) {
                const auto &t = project.tracks[i];
                const float target =
                    t.mute || (solo && !t.solo) ? 0 : std::clamp(t.volume, 0.f, 1.f);
                gains[i] += (target - gains[i]) * .003f;
            }
            float sample[2] = {0, 0};
            instruments.render(sample, gains);
            output[n * 2] = sample[0] * master * .5f;
            output[n * 2 + 1] = sample[1] * master * .5f;
            if (playing)
                frame++;
        }
        soft_clip_neon(output, static_cast<uint32_t>(frames), 1.f);
        unsigned clips = 0;
        for (unsigned i = 0; i < Tracks; i++)
            clips |= unsigned(active[i]) << (i * 2);
        activeClips.store(clips, std::memory_order_relaxed);
        currentStep.store(step, std::memory_order_relaxed);
    }
};
} // namespace eco
