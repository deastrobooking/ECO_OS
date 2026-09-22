// SPDX-License-Identifier: MIT
//
// A minimal CLAP instrument used only to exercise ClapHost/Engine in tests:
// a single-voice gated square wave, no parameters, no extensions beyond the
// bare plugin/factory/entry contract. Not a real instrument.
#include <clap/clap.h>
#include <cmath>
#include <cstring>

namespace {

struct TestSynth {
    const clap_plugin_t plugin;
    double sampleRate = 48000;
    double phase = 0;
    bool gate = false;
};

bool synthInit(const clap_plugin_t *) noexcept {
    return true;
}
void synthDestroy(const clap_plugin_t *plugin) noexcept {
    delete static_cast<TestSynth *>(plugin->plugin_data);
}
bool synthActivate(const clap_plugin_t *plugin, double sampleRate, uint32_t, uint32_t) noexcept {
    static_cast<TestSynth *>(plugin->plugin_data)->sampleRate = sampleRate;
    return true;
}
void synthDeactivate(const clap_plugin_t *) noexcept {}
bool synthStartProcessing(const clap_plugin_t *) noexcept {
    return true;
}
void synthStopProcessing(const clap_plugin_t *) noexcept {}
void synthReset(const clap_plugin_t *plugin) noexcept {
    auto *s = static_cast<TestSynth *>(plugin->plugin_data);
    s->phase = 0;
    s->gate = false;
}
clap_process_status synthProcess(const clap_plugin_t *plugin, const clap_process_t *process) noexcept {
    auto *s = static_cast<TestSynth *>(plugin->plugin_data);
    const uint32_t eventCount = process->in_events->size(process->in_events);
    uint32_t nextEvent = 0;
    float *left = process->audio_outputs[0].data32[0];
    float *right = process->audio_outputs[0].data32[1];
    const double step = 220.0 / s->sampleRate;
    for (uint32_t i = 0; i < process->frames_count; i++) {
        while (nextEvent < eventCount) {
            const auto *header = process->in_events->get(process->in_events, nextEvent);
            if (header->time != i)
                break;
            if (header->type == CLAP_EVENT_NOTE_ON)
                s->gate = true;
            else if (header->type == CLAP_EVENT_NOTE_OFF)
                s->gate = false;
            nextEvent++;
        }
        float sample = 0;
        if (s->gate) {
            s->phase += step;
            if (s->phase >= 1)
                s->phase -= 1;
            sample = s->phase < .5 ? .5f : -.5f;
        }
        left[i] = sample;
        right[i] = sample;
    }
    return CLAP_PROCESS_CONTINUE;
}
const void *synthGetExtension(const clap_plugin_t *, const char *) noexcept {
    return nullptr;
}
void synthOnMainThread(const clap_plugin_t *) noexcept {}

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION,
    "org.eco.test-synth",
    "ECO Test Synth",
    "ECO",
    "",
    "",
    "",
    "0.1.0",
    "Gated square wave used only by ECO's harness tests.",
    nullptr,
};

uint32_t factoryGetPluginCount(const clap_plugin_factory_t *) noexcept {
    return 1;
}
const clap_plugin_descriptor_t *factoryGetPluginDescriptor(const clap_plugin_factory_t *,
                                                            uint32_t index) noexcept {
    return index == 0 ? &kDescriptor : nullptr;
}
const clap_plugin_t *factoryCreatePlugin(const clap_plugin_factory_t *, const clap_host_t *,
                                          const char *pluginId) noexcept {
    if (std::strcmp(pluginId, kDescriptor.id) != 0)
        return nullptr;
    auto *synth = new TestSynth{{
        &kDescriptor,
        nullptr,
        synthInit,
        synthDestroy,
        synthActivate,
        synthDeactivate,
        synthStartProcessing,
        synthStopProcessing,
        synthReset,
        synthProcess,
        synthGetExtension,
        synthOnMainThread,
    }};
    const_cast<clap_plugin_t &>(synth->plugin).plugin_data = synth;
    return &synth->plugin;
}

const clap_plugin_factory_t kFactory = {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

bool entryInit(const char *) noexcept {
    return true;
}
void entryDeinit() noexcept {}
const void *entryGetFactory(const char *factoryId) noexcept {
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
