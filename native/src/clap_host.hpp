// SPDX-License-Identifier: MIT
#pragma once
#include <clap/clap.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <dlfcn.h>
#include <string>

namespace eco {

// Loads and hosts a single CLAP instrument plugin.
//
// load()/unload() perform filesystem and dynamic-linking work and must only
// be called from the control thread, never from the audio callback. Once
// loaded, noteOn()/noteOff()/process() are safe on the audio thread and never
// allocate: event and audio storage is fixed-capacity, and process() breaks
// an arbitrarily large block into <=kMaxChunk pieces so the plugin's
// activate()-negotiated frame bound is always honored.
class ClapHost {
    static constexpr uint32_t kMaxChunk = 512;
    static constexpr uint32_t kMaxEventsPerBlock = 64;

    struct EventCursor {
        const clap_event_note_t *events;
        uint32_t count;
    };

    void *library_ = nullptr;
    const clap_plugin_entry_t *entry_ = nullptr;
    const clap_plugin_t *plugin_ = nullptr;
    clap_host_t host_{};
    bool active_ = false;
    bool processing_ = false;

    // Audio-thread only: events queued by noteOn/noteOff for the next
    // process() call, in ascending header.time order (callers are expected
    // to push in time order, matching how Engine already walks a block
    // sample-by-sample for LightInstruments scheduling).
    std::array<clap_event_note_t, kMaxEventsPerBlock> pendingEvents_{};
    uint32_t pendingCount_ = 0;

    std::array<float, kMaxChunk> left_{}, right_{};
    std::array<float *, 2> outPtrs_{};

    static const void *hostGetExtension(const clap_host_t *, const char *) noexcept {
        return nullptr;
    }
    static void hostRequestRestart(const clap_host_t *) noexcept {}
    static void hostRequestProcess(const clap_host_t *) noexcept {}
    static void hostRequestCallback(const clap_host_t *) noexcept {}

    static uint32_t inputSize(const clap_input_events_t *list) noexcept {
        return static_cast<const EventCursor *>(list->ctx)->count;
    }
    static const clap_event_header_t *inputGet(const clap_input_events_t *list,
                                                uint32_t index) noexcept {
        return &static_cast<const EventCursor *>(list->ctx)->events[index].header;
    }
    // MVP: plugin-emitted events (e.g. NOTE_END) are accepted and discarded;
    // nothing in the engine consumes plugin output events yet.
    static bool outputPush(const clap_output_events_t *, const clap_event_header_t *) noexcept {
        return true;
    }

    void reset() noexcept {
        library_ = nullptr;
        entry_ = nullptr;
        plugin_ = nullptr;
        active_ = processing_ = false;
        pendingCount_ = 0;
    }

  public:
    ClapHost() noexcept {
        host_.clap_version = CLAP_VERSION;
        host_.host_data = this;
        host_.name = "ECO";
        host_.vendor = "ECO";
        host_.url = "";
        host_.version = "0.1.0";
        host_.get_extension = hostGetExtension;
        host_.request_restart = hostRequestRestart;
        host_.request_process = hostRequestProcess;
        host_.request_callback = hostRequestCallback;
        outPtrs_ = {left_.data(), right_.data()};
    }
    ~ClapHost() {
        unload();
    }
    ClapHost(const ClapHost &) = delete;
    ClapHost &operator=(const ClapHost &) = delete;

    bool loaded() const noexcept {
        return plugin_ != nullptr;
    }

    // Control thread only. Loads the first plugin the DSO's factory reports
    // and activates/starts it. Returns false and leaves the host unloaded on
    // any failure.
    bool load(const std::string &path, double sampleRate) {
        unload();
        library_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library_) {
            reset();
            return false;
        }
        entry_ = static_cast<const clap_plugin_entry_t *>(dlsym(library_, "clap_entry"));
        if (!entry_ || !entry_->init(path.c_str())) {
            entry_ = nullptr;
            dlclose(library_);
            reset();
            return false;
        }
        const auto *factory = static_cast<const clap_plugin_factory_t *>(
            entry_->get_factory(CLAP_PLUGIN_FACTORY_ID));
        const clap_plugin_descriptor_t *desc =
            factory && factory->get_plugin_count(factory) > 0
                ? factory->get_plugin_descriptor(factory, 0)
                : nullptr;
        if (!desc) {
            unload();
            return false;
        }
        plugin_ = factory->create_plugin(factory, &host_, desc->id);
        if (!plugin_ || !plugin_->init(plugin_)) {
            unload();
            return false;
        }
        if (!plugin_->activate(plugin_, sampleRate, 1, kMaxChunk) ||
            !plugin_->start_processing(plugin_)) {
            unload();
            return false;
        }
        active_ = true;
        processing_ = true;
        pendingCount_ = 0;
        return true;
    }

    // Control thread only.
    void unload() {
        if (plugin_) {
            if (processing_)
                plugin_->stop_processing(plugin_);
            if (active_)
                plugin_->deactivate(plugin_);
            plugin_->destroy(plugin_);
        }
        if (entry_)
            entry_->deinit();
        if (library_)
            dlclose(library_);
        reset();
    }

    // Audio thread: schedule a note on/off within the next process() call, at
    // sample offset `frameOffset` (already clamped to the block by the
    // caller, matching Engine's existing Audition/NoteOff contract). Dropped
    // once the fixed event capacity for a block is exhausted.
    void noteOn(int key, float velocity, uint32_t frameOffset) noexcept {
        pushEvent(CLAP_EVENT_NOTE_ON, key, velocity, frameOffset);
    }
    void noteOff(int key, uint32_t frameOffset) noexcept {
        pushEvent(CLAP_EVENT_NOTE_OFF, key, 0.f, frameOffset);
    }

    // Audio thread: render `frames` of stereo audio, scaled by `gain`, added
    // into interleaved `output`. No-op if no plugin is loaded. Never
    // allocates: processes in <=kMaxChunk pieces using fixed-size buffers.
    void process(float *output, uint32_t frames, float gain) noexcept {
        if (!plugin_ || !active_ || !processing_)
            return;
        const uint32_t total = pendingCount_;
        pendingCount_ = 0;
        uint32_t eventIndex = 0, done = 0;
        while (done < frames) {
            const uint32_t chunk = std::min(kMaxChunk, frames - done);
            std::array<clap_event_note_t, kMaxEventsPerBlock> localEvents{};
            uint32_t chunkEvents = 0;
            while (eventIndex < total && pendingEvents_[eventIndex].header.time < done + chunk) {
                localEvents[chunkEvents] = pendingEvents_[eventIndex];
                localEvents[chunkEvents].header.time -= done;
                chunkEvents++;
                eventIndex++;
            }
            EventCursor cursor{localEvents.data(), chunkEvents};
            clap_input_events_t inEvents{&cursor, inputSize, inputGet};
            clap_output_events_t outEvents{nullptr, outputPush};
            std::fill_n(left_.data(), chunk, 0.f);
            std::fill_n(right_.data(), chunk, 0.f);
            clap_audio_buffer_t outBuf{};
            outBuf.data32 = outPtrs_.data();
            outBuf.channel_count = 2;
            clap_process_t proc{};
            proc.steady_time = -1;
            proc.frames_count = chunk;
            proc.audio_outputs = &outBuf;
            proc.audio_outputs_count = 1;
            proc.in_events = &inEvents;
            proc.out_events = &outEvents;
            plugin_->process(plugin_, &proc);
            for (uint32_t i = 0; i < chunk; i++) {
                output[(done + i) * 2] += left_[i] * gain;
                output[(done + i) * 2 + 1] += right_[i] * gain;
            }
            done += chunk;
        }
    }

  private:
    void pushEvent(uint16_t type, int key, float velocity, uint32_t frameOffset) noexcept {
        if (pendingCount_ >= kMaxEventsPerBlock)
            return;
        clap_event_note_t event{};
        event.header.size = sizeof(clap_event_note_t);
        event.header.time = frameOffset;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = -1;
        event.port_index = 0;
        event.channel = 0;
        event.key = static_cast<int16_t>(key);
        event.velocity = velocity;
        pendingEvents_[pendingCount_++] = event;
    }
};

} // namespace eco
