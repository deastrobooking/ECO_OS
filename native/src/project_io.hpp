// SPDX-License-Identifier: MIT
#pragma once
#include "engine.hpp"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Portable ECO project v1 JSON codec: no Qt, no allocation-free requirement
// (this runs off the audio thread), strict validation matching the schema
// written by the Qt host. Kept dependency-free so it is covered by
// tools/check-core.sh on any host, not only a Linux Qt build.
namespace eco {

inline constexpr std::array<const char *, Tracks> TrackNames = {
    "Kick", "Snare", "Hi-hats", "Sub bass", "Soft keys", "Glass"};
inline constexpr std::array<const char *, Tracks> TrackTypes = {
    "kick", "snare", "hat", "bass", "keys", "pluck"};

struct SavedProject {
    Project project;
    std::array<int, Tracks> active{};
    std::string name;
};

namespace detail {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

struct JsonValue {
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> data =
        nullptr;
    bool isNull() const {
        return std::holds_alternative<std::nullptr_t>(data);
    }
    bool isObject() const {
        return std::holds_alternative<JsonObject>(data);
    }
    bool isArray() const {
        return std::holds_alternative<JsonArray>(data);
    }
    const JsonArray *array() const {
        return std::get_if<JsonArray>(&data);
    }
    const JsonObject *object() const {
        return std::get_if<JsonObject>(&data);
    }
    const std::string *string() const {
        return std::get_if<std::string>(&data);
    }
    const double *number() const {
        return std::get_if<double>(&data);
    }
    const bool *boolean() const {
        return std::get_if<bool>(&data);
    }
    const JsonValue *field(std::string_view key) const {
        const auto *obj = object();
        if (!obj)
            return nullptr;
        for (auto &[k, v] : *obj)
            if (k == key)
                return &v;
        return nullptr;
    }
};

class Parser {
    std::string_view s;
    std::size_t i = 0;
    bool ok_ = true;

    void fail() {
        ok_ = false;
    }
    void skipWs() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            i++;
    }
    bool consume(char c) {
        skipWs();
        if (i < s.size() && s[i] == c) {
            i++;
            return true;
        }
        fail();
        return false;
    }
    bool literal(std::string_view lit) {
        skipWs();
        if (s.substr(i, lit.size()) == lit) {
            i += lit.size();
            return true;
        }
        return false;
    }
    std::string parseString() {
        std::string out;
        if (!consume('"'))
            return out;
        while (true) {
            if (i >= s.size()) {
                fail();
                return out;
            }
            char c = s[i++];
            if (c == '"')
                break;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i >= s.size()) {
                fail();
                return out;
            }
            char e = s[i++];
            switch (e) {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                if (i + 4 > s.size()) {
                    fail();
                    return out;
                }
                unsigned code = 0;
                auto [ptr, ec] = std::from_chars(s.data() + i, s.data() + i + 4, code, 16);
                if (ec != std::errc() || ptr != s.data() + i + 4) {
                    fail();
                    return out;
                }
                i += 4;
                if (code < 0x80)
                    out.push_back(char(code));
                else if (code < 0x800) {
                    out.push_back(char(0xC0 | (code >> 6)));
                    out.push_back(char(0x80 | (code & 0x3F)));
                } else {
                    out.push_back(char(0xE0 | (code >> 12)));
                    out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(char(0x80 | (code & 0x3F)));
                }
                break;
            }
            default:
                fail();
                return out;
            }
        }
        return out;
    }
    double parseNumber() {
        skipWs();
        const std::size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+'))
            i++;
        while (i < s.size() &&
               (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' ||
                s[i] == 'E' || s[i] == '+' || s[i] == '-'))
            i++;
        // Floating-point std::from_chars is missing from older libc++; a
        // classic-locale stream is portable and locale-independent.
        std::istringstream in{std::string(s.substr(start, i - start))};
        in.imbue(std::locale::classic());
        double value = 0;
        in >> value;
        if (in.fail() || in.peek() != std::char_traits<char>::eof()) {
            fail();
            return 0;
        }
        return value;
    }

  public:
    explicit Parser(std::string_view text) : s(text) {}
    bool ok() const {
        return ok_;
    }
    JsonValue parseValue() {
        skipWs();
        if (i >= s.size()) {
            fail();
            return {};
        }
        switch (s[i]) {
        case '{': {
            JsonObject obj;
            i++;
            skipWs();
            if (i < s.size() && s[i] == '}') {
                i++;
                return JsonValue{std::move(obj)};
            }
            while (ok_) {
                skipWs();
                std::string key = parseString();
                if (!ok_)
                    break;
                if (!consume(':'))
                    break;
                obj.emplace_back(std::move(key), parseValue());
                skipWs();
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    i++;
                    break;
                }
                fail();
                break;
            }
            return JsonValue{std::move(obj)};
        }
        case '[': {
            JsonArray arr;
            i++;
            skipWs();
            if (i < s.size() && s[i] == ']') {
                i++;
                return JsonValue{std::move(arr)};
            }
            while (ok_) {
                arr.push_back(parseValue());
                skipWs();
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    i++;
                    break;
                }
                fail();
                break;
            }
            return JsonValue{std::move(arr)};
        }
        case '"':
            return JsonValue{parseString()};
        case 't':
            if (literal("true"))
                return JsonValue{true};
            fail();
            return {};
        case 'f':
            if (literal("false"))
                return JsonValue{false};
            fail();
            return {};
        case 'n':
            if (literal("null"))
                return JsonValue{nullptr};
            fail();
            return {};
        default:
            return JsonValue{parseNumber()};
        }
    }
    bool atEnd() {
        skipWs();
        return i == s.size();
    }
};

inline void writeFloat(std::ostringstream &out, float value) {
    // Shortest decimal that round-trips the float (as to_chars would give);
    // classic locale keeps '.' decimals.
    std::string best;
    for (int digits = 1; digits <= std::numeric_limits<float>::max_digits10; digits++) {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << std::setprecision(digits) << value;
        best = text.str();
        std::istringstream back(best);
        back.imbue(std::locale::classic());
        float parsed = 0;
        back >> parsed;
        if (parsed == value || !std::isfinite(value))
            break;
    }
    out << best;
}

inline void writeEscaped(std::ostringstream &out, std::string_view text) {
    out << '"';
    for (unsigned char c : text) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out << buf;
            } else
                out << char(c);
        }
    }
    out << '"';
}

inline bool isFiniteNumber(const JsonValue &v, double lo, double hi, double *out) {
    const double *n = v.number();
    if (!n || !std::isfinite(*n) || *n < lo || *n > hi)
        return false;
    *out = *n;
    return true;
}

inline bool isIntegerInRange(const JsonValue &v, int lo, int hi, int *out) {
    double n = 0;
    if (!isFiniteNumber(v, lo, hi, &n))
        return false;
    if (n != std::floor(n))
        return false;
    *out = int(n);
    return true;
}

} // namespace detail

inline std::string toJson(const SavedProject &saved) {
    using detail::writeEscaped;
    using detail::writeFloat;
    std::ostringstream out;
    out << "{\"version\":1,\"name\":";
    writeEscaped(out, saved.name);
    out << ",\"bpm\":";
    writeFloat(out, saved.project.bpm);
    out << ",\"master\":";
    writeFloat(out, saved.project.master);
    out << ",\"tracks\":[";
    for (unsigned t = 0; t < Tracks; t++) {
        if (t)
            out << ',';
        const auto &tr = saved.project.tracks[t];
        out << "{\"name\":";
        writeEscaped(out, TrackNames[t]);
        out << ",\"type\":";
        writeEscaped(out, TrackTypes[t]);
        out << ",\"volume\":";
        writeFloat(out, tr.volume);
        out << ",\"mute\":" << (tr.mute ? "true" : "false")
            << ",\"solo\":" << (tr.solo ? "true" : "false") << ",\"active\":" << saved.active[t]
            << ",\"patterns\":[";
        for (unsigned c = 0; c < Scenes; c++) {
            if (c)
                out << ',';
            out << '[';
            for (unsigned st = 0; st < Steps; st++) {
                if (st)
                    out << ',';
                const auto &n = tr.clips[c][st];
                if (n.velocity > 0) {
                    out << "{\"note\":" << n.pitch << ",\"velocity\":";
                    writeFloat(out, n.velocity);
                    out << '}';
                } else
                    out << "null";
            }
            out << ']';
        }
        out << "],\"midiEvents\":[";
        for (unsigned c = 0; c < Scenes; c++) {
            if (c)
                out << ',';
            const auto &clip = tr.midiClips[c];
            out << "{\"loopTicks\":" << clip.loopTicks << ",\"notes\":[";
            for (unsigned i = 0; i < clip.noteCount && i < MaxClipNotes; i++) {
                if (i)
                    out << ',';
                const auto &note = clip.notes[i];
                out << "{\"start\":" << note.startTick << ",\"duration\":"
                    << note.durationTicks << ",\"note\":" << unsigned(note.pitch)
                    << ",\"channel\":" << unsigned(note.channel) << ",\"velocity\":";
                writeFloat(out, note.velocity);
                out << '}';
            }
            out << "]}";
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

// Strict ECO v1 validation: legacy step fields remain required, and the
// additive midiEvents field is optional for backward compatibility. Bounded
// event storage, numeric ranges, and matching per-track type tags are checked.
// Rejects malformed projects and
// leaves `out` untouched so a caller can keep its current session.
inline bool fromJson(std::string_view text, SavedProject &out) {
    using namespace detail;
    if (text.size() > 1000000)
        return false;
    Parser parser(text);
    JsonValue root = parser.parseValue();
    if (!parser.ok() || !parser.atEnd() || !root.isObject())
        return false;
    const auto *version = root.field("version");
    int versionValue = 0;
    if (!version || !isIntegerInRange(*version, 1, 1, &versionValue))
        return false;
    const auto *name = root.field("name");
    const std::string *nameStr = name ? name->string() : nullptr;
    if (!nameStr || nameStr->size() > 80)
        return false;
    const auto *bpmField = root.field("bpm");
    const auto *masterField = root.field("master");
    double bpm = 0, master = 0;
    if (!bpmField || !isFiniteNumber(*bpmField, 40, 240, &bpm))
        return false;
    if (!masterField || !isFiniteNumber(*masterField, 0, 1, &master))
        return false;
    const auto *tracksField = root.field("tracks");
    const JsonArray *tracks = tracksField ? tracksField->array() : nullptr;
    if (!tracks || tracks->size() != Tracks)
        return false;

    SavedProject next;
    next.project.bpm = float(bpm);
    next.project.master = float(master);
    next.name = *nameStr;

    for (unsigned t = 0; t < Tracks; t++) {
        const JsonValue &trackValue = (*tracks)[t];
        if (!trackValue.isObject())
            return false;
        const auto *typeField = trackValue.field("type");
        const auto *volumeField = trackValue.field("volume");
        const auto *muteField = trackValue.field("mute");
        const auto *soloField = trackValue.field("solo");
        const auto *activeField = trackValue.field("active");
        const auto *patternsField = trackValue.field("patterns");
        const auto *midiEventsField = trackValue.field("midiEvents");
        const std::string *type = typeField ? typeField->string() : nullptr;
        if (!type || *type != TrackTypes[t])
            return false;
        double volume = 0;
        if (!volumeField || !isFiniteNumber(*volumeField, 0, 1, &volume))
            return false;
        const bool *mute = muteField ? muteField->boolean() : nullptr;
        const bool *solo = soloField ? soloField->boolean() : nullptr;
        if (!mute || !solo)
            return false;
        int active = 0;
        if (!activeField || !isIntegerInRange(*activeField, 0, int(Scenes) - 1, &active))
            return false;
        const JsonArray *patterns = patternsField ? patternsField->array() : nullptr;
        if (!patterns || patterns->size() != Scenes)
            return false;

        auto &nextTrack = next.project.tracks[t];
        nextTrack.volume = float(volume);
        nextTrack.mute = *mute;
        nextTrack.solo = *solo;
        next.active[t] = active;

        for (unsigned c = 0; c < Scenes; c++) {
            const JsonArray *steps = (*patterns)[c].array();
            if (!steps || steps->size() != Steps)
                return false;
            for (unsigned st = 0; st < Steps; st++) {
                const JsonValue &stepValue = (*steps)[st];
                auto &note = nextTrack.clips[c][st];
                if (stepValue.isNull()) {
                    note = {};
                    continue;
                }
                if (!stepValue.isObject())
                    return false;
                const auto *pitchField = stepValue.field("note");
                const auto *velocityField = stepValue.field("velocity");
                int pitch = 0;
                double velocity = 0;
                if (!pitchField || !isIntegerInRange(*pitchField, 24, 96, &pitch))
                    return false;
                if (!velocityField || !isFiniteNumber(*velocityField, 0, 1, &velocity) ||
                    velocity <= 0)
                    return false;
                note = {pitch, float(velocity)};
            }
        }
        if (midiEventsField) {
            const JsonArray *midiClips = midiEventsField->array();
            if (!midiClips || midiClips->size() != Scenes)
                return false;
            for (unsigned c = 0; c < Scenes; c++) {
                const auto *loopField = (*midiClips)[c].field("loopTicks");
                const auto *notesField = (*midiClips)[c].field("notes");
                int loopTicks = 0;
                const JsonArray *notes = notesField ? notesField->array() : nullptr;
                if (!loopField || !isIntegerInRange(*loopField, 2, 65535, &loopTicks) ||
                    !notes || notes->size() > MaxClipNotes)
                    return false;
                auto &clip = nextTrack.midiClips[c];
                clip.loopTicks = static_cast<uint16_t>(loopTicks);
                clip.noteCount = static_cast<uint16_t>(notes->size());
                for (unsigned i = 0; i < notes->size(); i++) {
                    const auto &event = (*notes)[i];
                    const auto *startField = event.field("start");
                    const auto *durationField = event.field("duration");
                    const auto *pitchField = event.field("note");
                    const auto *channelField = event.field("channel");
                    const auto *velocityField = event.field("velocity");
                    int start = 0, duration = 0, pitch = 0, channel = 0;
                    double velocity = 0;
                    if (!startField || !isIntegerInRange(*startField, 0, loopTicks - 1, &start) ||
                        !durationField || !isIntegerInRange(*durationField, 1, loopTicks - 1,
                                                            &duration) ||
                        !pitchField || !isIntegerInRange(*pitchField, 0, 127, &pitch) ||
                        !channelField || !isIntegerInRange(*channelField, 0, 15, &channel) ||
                        !velocityField || !isFiniteNumber(*velocityField, 0, 1, &velocity) ||
                        velocity <= 0)
                        return false;
                    clip.notes[i] = {static_cast<uint16_t>(start),
                                     static_cast<uint16_t>(duration),
                                     static_cast<uint8_t>(pitch),
                                     static_cast<uint8_t>(channel), float(velocity)};
                }
            }
        }
    }
    out = std::move(next);
    return true;
}

} // namespace eco
