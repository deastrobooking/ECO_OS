// SPDX-License-Identifier: MIT
#include "engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
extern "C" {
void mix_stereo_add_scalar(float *, const float *, uint32_t, float, float);
float peak_abs_scalar(const float *, uint32_t);
void soft_clip_scalar(float *, uint32_t, float);
}
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
    for (unsigned count = 0; count < 34; count++) {
        std::array<float, 68> source{}, reference{}, actual{};
        for (unsigned i = 0; i < 68; i++)
            source[i] = std::sin(float(i) * .31f) * 3;
        mix_stereo_add_scalar(reference.data(), source.data(), count, .7f, -.3f);
        mix_stereo_add_neon(actual.data(), source.data(), count, .7f, -.3f);
        for (unsigned i = 0; i < 68; i++)
            check(std::abs(reference[i] - actual[i]) < 1e-6,
                  "SIMD mixer scalar equivalence, including tails");
        check(std::abs(peak_abs_neon(source.data(), count) -
                       peak_abs_scalar(source.data(), count)) < 1e-6,
              "SIMD peak equivalence");
        soft_clip_scalar(reference.data(), count, .9f);
        soft_clip_neon(actual.data(), count, .9f);
        for (unsigned i = 0; i < 68; i++)
            check(std::abs(reference[i] - actual[i]) < 1e-6, "SIMD limiter scalar equivalence");
    }
    {
        auto *fm = fm6_create();
        check(fm != nullptr, "FM6 creation");
        fm6_note_on(fm, 60, .8f);
        std::vector<float> out(48000 * 2);
        fm6_render(fm, out.data(), 48000, 1);
        double energy = 0;
        for (float v : out) {
            check(std::isfinite(v), "FM6 finite");
            energy += v * v;
        }
        check(energy > 1, "FM6 produces audio");
        fm6_note_off(fm, 60);
        std::fill(out.begin(), out.end(), 0);
        for (int i = 0; i < 8; i++)
            fm6_render(fm, out.data(), 48000, 1);
        bool active = false;
        for (auto &voice : fm->voices)
            active |= voice.in_use;
        check(!active, "FM6 note-off retires voices");
        fm6_free(fm);
    }
    {
        auto *dr = drum_rack_create();
        auto *other = drum_rack_create();
        check(dr && other, "drum creation");
        std::array<float, 512> a{}, b{};
        drum_pad_trigger(&dr->pads[1], .8f);
        drum_pad_trigger(&other->pads[1], .8f);
        drum_pad_render(&dr->pads[1], a.data(), 256, 1);
        drum_pad_render(&other->pads[1], b.data(), 256, 1);
        check(a == b, "independent deterministic LIGHT noise");
        drum_rack_free(dr);
        drum_rack_free(other);
    }
    eco::Queue<int, 4> q;
    check(q.push(1) && q.push(2) && q.push(3) && !q.push(4), "queue overflow is bounded");
    int value = 0;
    check(q.pop(value) && value == 1, "queue FIFO");
    check(q.push(4), "queue wrap");
    check(q.pop(value) && value == 2 && q.pop(value) && value == 3 && q.pop(value) && value == 4 &&
              !q.pop(value),
          "queue drain");
    eco::Engine e;
    std::array<float, 256> block{};
    render(e, block.data(), 128);
    for (auto v : block)
        check(v == 0, "initial silence");
    eco::Command c;
    c.action = eco::Action::Update;
    c.project = eco::demo();
    c.project.bpm = 120;
    e.commands.push(c);
    c.action = eco::Action::Start;
    e.commands.push(c);
    std::vector<float> song(96000 * 2);
    render(e, song.data(), 96000);
    double energy = 0;
    for (auto v : song) {
        check(std::isfinite(v) && std::abs(v) <= 1, "bounded finite output");
        energy += v * v;
    }
    check(energy > 1, "audible synthesis");
    check(e.currentStep.load() == 15, "one bar sample clock");
    c.action = eco::Action::Launch;
    c.track = 3;
    c.clip = 2;
    e.commands.push(c);
    render(e, block.data(), 128);
    check(((e.activeClips.load() >> 6) & 3) == 2, "launch on next bar");
    c.clip = 1;
    e.commands.push(c);
    render(e, block.data(), 128);
    check(((e.activeClips.load() >> 6) & 3) == 2, "launch waits within bar");
    c.action = eco::Action::Stop;
    e.commands.push(c);
    render(e, block.data(), 128);
    for (auto v : block)
        check(v == 0, "stop clears voices");
    check(e.currentStep.load() == -1, "stop clears playhead");
    c.action = eco::Action::Update;
    for (auto &t : c.project.tracks)
        t.mute = true;
    e.commands.push(c);
    c.action = eco::Action::Start;
    e.commands.push(c);
    render(e, song.data(), 96000);
    double tail = 0;
    for (std::size_t i = 48000; i < song.size(); i++)
        tail += song[i] * song[i];
    check(tail < 1e-9, "mute settles to silence");
    c.action = eco::Action::Restore;
    c.project = eco::demo();
    c.active = {3, 2, 1, 0, 2, 3};
    check(e.commands.push(c), "restore command accepted");
    render(e, block.data(), 128);
    check(e.currentStep.load() == -1, "restore stops transport atomically");
    check(e.activeClips.load() == (3u | (2u << 2) | (1u << 4) | (2u << 8) | (3u << 10)),
          "restore selects all clips atomically");
    for (auto v : block)
        check(v == 0, "restore silences old voices");
    eco::Engine first, second;
    c.action = eco::Action::Start;
    first.commands.push(c);
    second.commands.push(c);
    std::array<float, 512> a{}, b{};
    render(first, a.data(), 256);
    render(second, b.data(), 64);
    render(second, b.data() + 128, 192);
    check(a == b, "render independent of block size");
    std::puts("PASS: LIGHT FM6/release, drum isolation, SIMD scalar equivalence/tails, queue "
              "overflow/wrap, silence, sample clock, finite audio, quantized launch, stop, mute, "
              "block-size invariance, no C++ render allocations");
}
