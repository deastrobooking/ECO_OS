// SPDX-License-Identifier: MIT
#include "engine.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

// Establishes a throughput baseline for the current per-sample LIGHT
// instrument calls (see instruments.hpp: render() takes one frame at a time
// to keep event boundaries sample-exact). PLAN.md backlog item 4 asks to
// benchmark before claiming a low-buffer block-segmentation rewrite is
// needed; this measures whether per-sample call overhead is actually
// significant at the block sizes M3 will qualify (256/128/64 frames).
namespace {
double secondsPerRun(unsigned blockFrames, unsigned totalFrames) {
    eco::Engine engine;
    eco::Command update;
    update.action = eco::Action::Update;
    update.project = eco::demo();
    engine.commands.push(update);
    eco::Command play;
    play.action = eco::Action::Start;
    engine.commands.push(play);
    std::vector<float> block(blockFrames * 2);
    const auto start = std::chrono::steady_clock::now();
    for (unsigned rendered = 0; rendered < totalFrames; rendered += blockFrames) {
        const auto count = std::min(blockFrames, totalFrames - rendered);
        engine.render(block.data(), count);
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
} // namespace

int main() {
    constexpr unsigned rate = 48000;
    constexpr unsigned totalFrames = rate * 20; // 20 s of fully active LIGHT instruments.
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "block frames | wall seconds for 20s render | realtime multiple\n";
    for (unsigned blockFrames : {256u, 128u, 64u}) {
        // Best-of-3 to reduce scheduler noise; this is a developer signal,
        // not a qualification measurement (that needs the target board).
        double best = -1;
        for (int trial = 0; trial < 3; trial++) {
            const double elapsed = secondsPerRun(blockFrames, totalFrames);
            if (best < 0 || elapsed < best)
                best = elapsed;
        }
        const double realtime = 20.0 / best;
        std::cout << std::setw(12) << blockFrames << " | " << std::setw(28) << best << " | "
                  << realtime << "x\n";
    }
}
