// SPDX-License-Identifier: MIT
#include "project_io.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

static void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    auto pos = text.find(from);
    if (pos == std::string::npos) {
        std::fprintf(stderr, "FAIL: fixture missing %.*s\n", int(from.size()), from.data());
        std::exit(1);
    }
    return text.replace(pos, from.size(), to);
}

int main() {
    eco::SavedProject saved;
    saved.project = eco::demo();
    saved.active = {1, 0, 2, 3, 0, 1};
    saved.name = "First light";

    const auto json = eco::toJson(saved);
    eco::SavedProject roundTripped;
    check(eco::fromJson(json, roundTripped), "round-trip parses");
    check(roundTripped.name == "First light", "round-trip name");
    check(roundTripped.active == saved.active, "round-trip active clips");
    check(roundTripped.project.bpm == saved.project.bpm, "round-trip bpm");
    check(roundTripped.project.master == saved.project.master, "round-trip master");
    for (unsigned t = 0; t < eco::Tracks; t++) {
        check(roundTripped.project.tracks[t].volume == saved.project.tracks[t].volume,
              "round-trip volume");
        check(roundTripped.project.tracks[t].mute == saved.project.tracks[t].mute,
              "round-trip mute");
        for (unsigned c = 0; c < eco::Scenes; c++)
            for (unsigned s = 0; s < eco::Steps; s++) {
                const auto &a = saved.project.tracks[t].clips[c][s];
                const auto &b = roundTripped.project.tracks[t].clips[c][s];
                check((a.velocity > 0) == (b.velocity > 0), "round-trip step on/off");
                if (a.velocity > 0) {
                    check(a.pitch == b.pitch, "round-trip pitch");
                    check(std::abs(a.velocity - b.velocity) < 1e-4f, "round-trip velocity");
                }
            }
    }

    // A silent project (no active steps) still round-trips: every step is
    // JSON null, not an object, exercising the null-note path both ways.
    eco::SavedProject silent;
    silent.project.bpm = 90;
    silent.project.master = .5f;
    silent.active = {0, 0, 0, 0, 0, 0};
    silent.name = "";
    eco::SavedProject silentBack;
    check(eco::fromJson(eco::toJson(silent), silentBack), "silent project round-trips");
    for (auto &t : silentBack.project.tracks)
        for (auto &c : t.clips)
            for (auto &n : c)
                check(n.velocity == 0, "silent project has no notes");

    eco::SavedProject discard;
    check(!eco::fromJson("not json", discard), "garbage rejected");
    check(!eco::fromJson("", discard), "empty text rejected");
    check(!eco::fromJson("{}", discard), "missing fields rejected");
    check(!eco::fromJson(replaceOnce(json, "\"version\":1", "\"version\":2"), discard),
          "wrong version rejected");
    check(!eco::fromJson(replaceOnce(json, "\"bpm\":112", "\"bpm\":9999"), discard),
          "out-of-range bpm rejected");
    check(!eco::fromJson(replaceOnce(json, "\"bpm\":112", "\"bpm\":\"112\""), discard),
          "non-numeric bpm rejected");
    check(!eco::fromJson(replaceOnce(json, "\"kick\"", "\"snare\""), discard),
          "swapped track type rejected");
    check(!eco::fromJson(json.substr(0, json.size() - 1), discard),
          "truncated trailing bracket rejected");
    {
        std::string tooLongName = json;
        tooLongName = replaceOnce(tooLongName, "\"First light\"",
                                   "\"" + std::string(81, 'x') + "\"");
        check(!eco::fromJson(tooLongName, discard), "oversized name rejected");
    }
    check(!eco::fromJson(std::string(1000001, ' '), discard), "oversized payload rejected");
    check(!eco::fromJson(replaceOnce(json, "{\"note\":36,\"velocity\":0.8}", "{\"note\":36}"),
                          discard),
          "note missing velocity rejected");
    check(!eco::fromJson(replaceOnce(json, "{\"note\":36,\"velocity\":0.8}",
                                      "{\"note\":200,\"velocity\":0.8}"),
                          discard),
          "out-of-range pitch rejected");
    check(!eco::fromJson(R"({"version":1,"name":"n","bpm":120,"master":0.5,"tracks":[1,2,3,4,5,6]})",
                          discard),
          "non-object track rejected");
    {
        // Reuse the verified fixture, but inject stray whitespace around
        // punctuation and give the name an escaped quote, exercising both
        // the tokenizer's whitespace skipping and its \" unescaping.
        std::string spaced = replaceOnce(json, "\"First light\"", "\"n\\\"q\"");
        spaced = replaceOnce(spaced, "\"version\":1,", " \"version\" : 1 ,\n  ");
        check(eco::fromJson(spaced, discard), "whitespace and escaped-quote name accepted");
        check(discard.name == "n\"q", "escaped quote round-trips through parser");
    }

    std::puts("PASS: project v1 JSON round-trip, null steps, and strict schema rejection");
}
