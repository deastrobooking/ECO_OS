// SPDX-License-Identifier: MIT
#pragma once
#include "engine.hpp"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <stdexcept>
#include <thread>

class Audio {
    eco::Engine &engine;
    pw_main_loop *loop = nullptr;
    pw_stream *stream = nullptr;
    std::thread thread;
    static void process(void *data) {
        auto &self = *static_cast<Audio *>(data);
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
