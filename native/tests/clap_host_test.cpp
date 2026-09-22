// SPDX-License-Identifier: MIT
#include "engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#ifndef TEST_SYNTH_CLAP_PATH
#error "TEST_SYNTH_CLAP_PATH must name the built ECO test synth CLAP binary"
#endif

static bool forbidAllocation = false;
void *operator new(std::size_t n) {
    if (forbidAllocation) {
        std::fprintf(stderr, "Allocation in audio render\n");
        std::abort();
    }
    if (auto p = std::malloc(n))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete[](void *p) noexcept {
    std::free(p);
}
static void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
static void render(eco::Engine &e, float *b, std::size_t n) {
    forbidAllocation = true;
    e.render(b, n);
    forbidAllocation = false;
}

int main() {
    {
        // Loading and unloading are control-thread operations and may
        // allocate; a missing/invalid path must fail cleanly rather than
        // crash or leave the host half-initialized.
        eco::ClapHost bad;
        check(!bad.load("/nonexistent/path.clap", eco::SampleRate), "missing plugin path fails");
        check(!bad.loaded(), "failed load leaves host unloaded");
    }
    eco::Engine e;
    {
        auto host = std::make_unique<eco::ClapHost>();
        check(host->load(TEST_SYNTH_CLAP_PATH, eco::SampleRate), "test synth loads");
        check(host->loaded(), "loaded() reflects successful load");
        eco::Command swap;
        swap.action = eco::Action::SwapPlugin;
        swap.plugin = host.release();
        check(e.commands.push(swap), "SwapPlugin command accepted");
    }
    std::array<float, 256> silence{};
    render(e, silence.data(), 128);
    for (auto v : silence)
        check(v == 0, "plugin silent with no notes");

    eco::Command on;
    on.action = eco::Action::Audition;
    on.track = int(eco::Tracks); // the plugin bus sentinel, not a sequencer track
    on.pitch = 60;
    on.velocity = .8f;
    e.commands.push(on);
    std::vector<float> sung(4800 * 2);
    render(e, sung.data(), 4800);
    double energy = 0;
    for (auto v : sung) {
        check(std::isfinite(v) && std::abs(v) <= 1, "plugin output bounded and finite");
        energy += double(v) * v;
    }
    check(energy > 1e-6, "audible output from the CLAP plugin bus");

    eco::Command off;
    off.action = eco::Action::NoteOff;
    off.track = int(eco::Tracks);
    off.pitch = 60;
    e.commands.push(off);
    std::vector<float> silenced(4800 * 2);
    render(e, silenced.data(), 4800);
    render(e, silenced.data(), 4800); // let the test synth's gate close fully
    double tail = 0;
    for (auto v : silenced)
        tail += double(v) * v;
    check(tail < 1e-9, "note off silences the plugin bus");

    {
        // A large block (bigger than ClapHost's internal chunk size and than
        // a plugin's negotiated max frame count) must still process
        // correctly, matching Engine's existing block-size independence
        // contract for the 6-track instruments.
        std::vector<float> big(20000 * 2);
        eco::Command bigOn = on;
        e.commands.push(bigOn);
        render(e, big.data(), 20000);
        double bigEnergy = 0;
        for (auto v : big) {
            check(std::isfinite(v), "large block finite with plugin active");
            bigEnergy += double(v) * v;
        }
        check(bigEnergy > 1e-6, "large block still produces plugin audio");
    }

    // Swapping in a second plugin must retire the first without the audio
    // thread freeing it; the control thread reclaims it via retiredPlugin.
    {
        auto second = std::make_unique<eco::ClapHost>();
        check(second->load(TEST_SYNTH_CLAP_PATH, eco::SampleRate), "second load succeeds");
        eco::Command swap;
        swap.action = eco::Action::SwapPlugin;
        swap.plugin = second.release();
        check(e.commands.push(swap), "second SwapPlugin accepted");
        render(e, silence.data(), 128);
        auto *retired = e.retiredPlugin.exchange(nullptr);
        check(retired != nullptr, "first plugin retired for control-thread cleanup");
        delete retired;
    }

    std::puts("PASS: CLAP harness load/unload, note on/off, block-size independence, "
              "plugin swap retirement, no allocations in render");
}
