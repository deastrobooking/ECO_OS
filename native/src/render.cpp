// SPDX-License-Identifier: MIT
#include "engine.hpp"
#include <array>
#include <fstream>
#include <iostream>

static void little(std::ostream &out, uint32_t n, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        out.put(static_cast<char>((n >> (i * 8)) & 255));
}
int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: eco-render output.wav\n";
        return 1;
    }
    // Refuse to replace an existing render accidentally.
    if (std::ifstream(argv[1]).good()) {
        std::cerr << "Output already exists\n";
        return 1;
    }
    std::ofstream out(argv[1], std::ios::binary);
    if (!out) {
        std::cerr << "Cannot create output\n";
        return 1;
    }
    constexpr uint32_t rate = 48000;
    const uint32_t frames = static_cast<uint32_t>(std::ceil(16 * 60. / 112 * rate));
    out.write("RIFF", 4);
    little(out, 36 + frames * 4, 4);
    out.write("WAVEfmt ", 8);
    little(out, 16, 4);
    little(out, 1, 2);
    little(out, 2, 2);
    little(out, rate, 4);
    little(out, rate * 4, 4);
    little(out, 4, 2);
    little(out, 16, 2);
    out.write("data", 4);
    little(out, frames * 4, 4);
    eco::Engine engine;
    eco::Command play;
    play.action = eco::Action::Start;
    engine.commands.push(play);
    std::array<float, 512> block{};
    for (uint32_t frame = 0; frame < frames; frame += 256) {
        const auto count = std::min(256u, frames - frame);
        engine.render(block.data(), count);
        for (uint32_t i = 0; i < count; i++)
            for (unsigned channel = 0; channel < 2; channel++) {
                const float fade = std::min(1.f, float(frames - frame - i) / 480.f);
                const auto value =
                    static_cast<int16_t>(std::lround(block[i * 2 + channel] * fade * 32767));
                little(out, static_cast<uint16_t>(value), 2);
            }
    }
    out.close();
    if (!out) {
        std::cerr << "Write failed\n";
        return 1;
    }
    std::cout << "Rendered four bars of LIGHT instruments at 48 kHz: " << argv[1] << '\n';
}
