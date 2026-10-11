// SPDX-License-Identifier: MIT
#pragma once
#include "engine.hpp"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <time.h>

class Audio {
    eco::Engine &engine;
    pw_main_loop *loop = nullptr;
    pw_stream *stream = nullptr;
    std::thread thread;
    // A seqlock-style snapshot pairs Engine's absolute frame count with both
    // PipeWire's clock and CLOCK_MONOTONIC. MIDI queue stamps are converted to
    // monotonic time on the control thread, then projected onto the PW clock.
    std::atomic<uint64_t> clockSequence{0}, clockFrame{0}, clockPipeWireNs{0},
        clockMonotonicNs{0};
    void publishClockAnchor(uint64_t pwNs, uint64_t monotonicNs) noexcept {
        clockSequence.fetch_add(1, std::memory_order_acq_rel);
        clockFrame.store(engine.renderedFrameSnapshot.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
        clockPipeWireNs.store(pwNs, std::memory_order_relaxed);
        clockMonotonicNs.store(monotonicNs, std::memory_order_relaxed);
        clockSequence.fetch_add(1, std::memory_order_release);
    }
    static void process(void *data) {
        auto &self = *static_cast<Audio *>(data);
        // Pair the current callback frame with the PipeWire clock. The API is
        // realtime-safe; monotonic time provides the bridge to ALSA queue time.
        pw_time pwTime{};
        const bool hasPwTime =
            pw_stream_get_time_n(self.stream, &pwTime, sizeof(pwTime)) >= 0;
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const auto monotonicNs = uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
        if (hasPwTime && pwTime.now)
            self.publishClockAnchor(pwTime.now, monotonicNs);
        auto *b = pw_stream_dequeue_buffer(self.stream);
        if (!b)
            return;
        auto *spa = b->buffer;
        if (spa->n_datas && spa->datas[0].data) {
            auto &d = spa->datas[0];
            const auto capacity = d.maxsize / (sizeof(float) * 2);
            const auto frames =
                b->requested ? std::min<uint64_t>(b->requested, capacity) : capacity;
            self.engine.render(static_cast<float *>(d.data), frames);
            d.chunk->offset = 0;
            d.chunk->stride = sizeof(float) * 2;
            d.chunk->size = frames * sizeof(float) * 2;
            b->size = frames;
        }
        pw_stream_queue_buffer(self.stream, b);
    }
    static void stateChanged(void *data, pw_stream_state, pw_stream_state state, const char *) {
        static_cast<Audio *>(data)->state.store(int(state));
    }
    pw_stream_events events{};

  public:
    std::atomic<int> state{PW_STREAM_STATE_UNCONNECTED};
    bool frameForMonotonicNs(uint64_t eventMonotonicNs, uint64_t &outFrame) const noexcept {
        uint64_t sequence = 0, frame = 0, pwNs = 0, monoNs = 0;
        do {
            sequence = clockSequence.load(std::memory_order_acquire);
            if (sequence & 1)
                continue;
            frame = clockFrame.load(std::memory_order_relaxed);
            pwNs = clockPipeWireNs.load(std::memory_order_relaxed);
            monoNs = clockMonotonicNs.load(std::memory_order_relaxed);
        } while ((sequence & 1) || sequence != clockSequence.load(std::memory_order_acquire));
        if (!pwNs)
            return false;
        const int64_t monoDelta = eventMonotonicNs >= monoNs
                                      ? int64_t(eventMonotonicNs - monoNs)
                                      : -int64_t(monoNs - eventMonotonicNs);
        // Project through the paired monotonic/PipeWire anchor. Keeping the
        // PipeWire subtraction explicit documents the clock domain mapping.
        const int64_t eventPwNs = int64_t(pwNs) + monoDelta;
        const int64_t pwDelta = eventPwNs - int64_t(pwNs);
        if (pwDelta >= 0) {
            outFrame = frame + uint64_t(pwDelta) * eco::SampleRate / 1000000000ull;
        } else {
            const auto framesBack = uint64_t(-pwDelta) * eco::SampleRate / 1000000000ull;
            outFrame = framesBack > frame ? 0 : frame - framesBack;
        }
        return true;
    }
    explicit Audio(eco::Engine &e) : engine(e) {
        pw_init(nullptr, nullptr);
        loop = pw_main_loop_new(nullptr);
        if (!loop)
            throw std::runtime_error("Could not create PipeWire loop");
        events.version = PW_VERSION_STREAM_EVENTS;
        events.process = process;
        events.state_changed = stateChanged;
        stream = pw_stream_new_simple(pw_main_loop_get_loop(loop), "ECO Workstation",
                                      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                                                        PW_KEY_MEDIA_CATEGORY, "Playback",
                                                        PW_KEY_MEDIA_ROLE, "Music",
                                                        PW_KEY_NODE_LATENCY, "256/48000", nullptr),
                                      &events, this);
        if (!stream) {
            pw_main_loop_destroy(loop);
            throw std::runtime_error("Could not create PipeWire stream");
        }
        uint8_t buffer[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = 48000;
        info.channels = 2;
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod *params[] = {
            spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
        const int result = pw_stream_connect(stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                             pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT |
                                                             PW_STREAM_FLAG_MAP_BUFFERS |
                                                             PW_STREAM_FLAG_RT_PROCESS),
                                             params, 1);
        if (result < 0) {
            pw_stream_destroy(stream);
            pw_main_loop_destroy(loop);
            throw std::runtime_error("PipeWire connection failed");
        }
        thread = std::thread([this] { pw_main_loop_run(loop); });
    }
    ~Audio() {
        pw_main_loop_quit(loop);
        if (thread.joinable())
            thread.join();
        pw_stream_destroy(stream);
        pw_main_loop_destroy(loop);
    }
};
