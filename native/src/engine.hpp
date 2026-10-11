// SPDX-License-Identifier: MIT
#pragma once
#include "clap_host.hpp"
#include "instruments.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace eco {
constexpr unsigned Tracks = 6, Scenes = 4, Steps = 16;
constexpr unsigned SampleRate = 48000;
constexpr unsigned TicksPerQuarter = 960;
constexpr unsigned TicksPerStep = TicksPerQuarter / 4;
constexpr unsigned DefaultClipTicks = Steps * TicksPerStep;
constexpr unsigned MaxClipNotes = 128;
struct Note {
    int pitch = 60;
    float velocity = 0;
};
struct MidiNote {
    uint16_t startTick = 0, durationTicks = TicksPerStep;
    uint8_t pitch = 60, channel = 0;
    float velocity = 0.8f;
};
struct MidiClip {
    uint16_t loopTicks = DefaultClipTicks;
    uint16_t noteCount = 0;
    std::array<MidiNote, MaxClipNotes> notes{};
};
struct Track {
    float volume = .5f;
    bool mute = false, solo = false;
    std::array<std::array<Note, Steps>, Scenes> clips{};
    // Fixed-capacity MIDI event data keeps project snapshots bounded and safe
    // to pass through the audio command queue without callback allocations.
    std::array<MidiClip, Scenes> midiClips{};
};
struct Project {
    float bpm = 112, master = .7f;
    std::array<Track, Tracks> tracks{};
};
inline Project demo() {
    Project p;
    for (unsigned t = 0; t < Tracks; t++)
        for (unsigned c = 0; c < Scenes; c++)
            for (unsigned s = 0; s < Steps; s++) {
                bool hit = t == 0   ? s % 4 == 0
                           : t == 1 ? s % 8 == 4
                           : t == 2 ? s % 4 == 2
                           : t == 3 ? (s == 0 || s == 6 || s == 10)
                           : t == 4 ? s % 8 == 0
                                    : s % 5 == 2;
                if (c == 2)
                    hit = hit && s % 8 < 4;
                if (c == 1 && t == 2)
                    hit = s % 2 == 0;
                p.tracks[t].clips[c][s] = {std::array<int, 6>{36, 38, 42, 36, 60, 72}[t] +
                                               (t > 2 ? int(c) * 2 : 0),
                                           hit ? .8f : 0.f};
            }
    p.tracks[0].volume = .85;
    p.tracks[2].volume = .3;
    p.tracks[4].volume = .32;
    p.tracks[5].volume = .25;
    return p;
}

template <class T, std::size_t N> class Queue {
    static_assert(N > 1);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    std::array<T, N> items{};
    alignas(64) std::atomic<unsigned> read{0};
    alignas(64) std::atomic<unsigned> write{0};

  public:
    bool push(const T &item) noexcept {
        const unsigned w = write.load(std::memory_order_relaxed), next = (w + 1) % N;
        if (next == read.load(std::memory_order_acquire))
            return false;
        items[w] = item;
        write.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T &item) noexcept {
        const auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire))
            return false;
        item = items[r];
        read.store((r + 1) % N, std::memory_order_release);
        return true;
    }
};
enum class Action { Update, Start, Stop, Launch, Audition, MidiInput, NoteOff, Restore, SwapPlugin };
struct Command {
    Action action = Action::Stop;
    // Immutable snapshot built on the control thread (Engine::submitProject).
    // The audio thread adopts the pointer; the old one returns via
    // Engine::retiredProjects. Keeps Command small: no 40 KB copies.
    Project *snapshot = nullptr;
    std::array<int, Tracks> active{};
    int track = 0, clip = 0, pitch = 60;
    float velocity = .8f;
    // Sample offset within the render() block this live note command should
    // take effect at when it has no absolute target frame.
    // A default of 0 applies at the start of the block, matching prior
    // behavior for callers that don't set it.
    unsigned frameOffset = 0;
    // Optional absolute audio frame target for timestamped MIDI. If false,
    // frameOffset retains its within-next-block meaning.
    uint64_t targetFrame = 0;
    bool hasTargetFrame = false;
    // SwapPlugin only: a loaded ClapHost, already activated on the control
    // thread. Ownership transfers to Engine; the previously installed host
    // (if any) comes back via Engine::retiredPlugin for the control thread
    // to delete.
    ClapHost *plugin = nullptr;
};
struct TimedCommand {
    Action action = Action::Stop;
    int track = 0, pitch = 60;
    float velocity = .8f;
    uint64_t targetFrame = 0;
};
enum class MidiOutKind : uint8_t { NoteOn, NoteOff };
struct MidiOutEvent {
    MidiOutKind kind = MidiOutKind::NoteOff;
    uint8_t channel = 0, pitch = 60, velocity = 0;
};
class Engine {
    LightInstruments instruments;
    Project *project = new Project(demo());
    // Control-thread only: snapshots allocated and not yet reclaimed.
    unsigned projectsInFlight = 1;
    static constexpr unsigned MaxProjectsInFlight = 48;
    std::array<float, Tracks> gains{};
    float master = 0;
    std::array<int, Tracks> active{}, pending{-1, -1, -1, -1, -1, -1};
    uint64_t lastAbsStep = UINT64_MAX;
    uint64_t frame = 0;
    uint64_t renderedFrame = 0;
    double midiTickPosition = 0;
    uint64_t lastMidiTick = UINT64_MAX;
    std::array<TimedCommand, 128> pendingTimed{};
    unsigned pendingTimedCount = 0;
    static constexpr unsigned rate = SampleRate;
    int step = -1;
    bool playing = false;
    // Audio-thread owned once installed via SwapPlugin; the "seventh track"
    // (Command::track == Tracks) auditioned independently of the 6-track
    // step sequencer. Deleted by ~Engine(), which only runs once the audio
    // callback has stopped (Controller destructs Audio before Engine).
    ClapHost *pluginHost = nullptr;
    void adoptProject(Project *next) noexcept {
        if (!next)
            return;
        Project *old = project;
        project = next;
        // Capacity exceeds MaxProjectsInFlight, so this cannot fail.
        retiredProjects.push(old);
    }
    void emitMidi(MidiOutKind kind, int channel, int pitch, float velocity = 0) noexcept {
        const auto scaledVelocity = static_cast<uint8_t>(
            std::clamp(int(std::lround(std::clamp(velocity, 0.f, 1.f) * 127.f)), 0, 127));
        if (!midiOutput.push({kind, static_cast<uint8_t>(std::clamp(channel, 0, 15)),
                              static_cast<uint8_t>(std::clamp(pitch, 0, 127)),
                              scaledVelocity}))
            droppedMidiOutput.fetch_add(1, std::memory_order_relaxed);
    }
    void emitTrackNotesOff(unsigned track) noexcept {
        const auto clipIndex = static_cast<unsigned>(std::clamp(active[track], 0, 3));
        const auto &midiClip = project->tracks[track].midiClips[clipIndex];
        if (midiClip.noteCount > 0) {
            for (unsigned i = 0; i < std::min<unsigned>(midiClip.noteCount, MaxClipNotes); i++) {
                const auto &note = midiClip.notes[i];
                emitMidi(MidiOutKind::NoteOff, note.channel, note.pitch);
            }
        } else {
            for (const auto &note : project->tracks[track].clips[clipIndex])
                if (note.velocity > 0)
                    emitMidi(MidiOutKind::NoteOff, 0, note.pitch);
        }
    }
    void trigger(int track, int pitch, float velocity, int gateFrames = 7200) noexcept {
        if (track < 0 || track >= int(Tracks))
            return;
        instruments.noteOn(track, std::clamp(pitch, 24, 96), std::clamp(velocity, 0.f, 1.f),
                           gateFrames);
    }
    void enqueueTimed(const TimedCommand &event) noexcept {
        if (pendingTimedCount == pendingTimed.size()) {
            droppedTimedEvents.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        unsigned at = pendingTimedCount;
        while (at > 0 && pendingTimed[at - 1].targetFrame > event.targetFrame) {
            pendingTimed[at] = pendingTimed[at - 1];
            at--;
        }
        pendingTimed[at] = event;
        pendingTimedCount++;
    }
    void dispatchTimed(const TimedCommand &event, unsigned frameOffset) noexcept {
        if (event.track == int(Tracks)) {
            if (!pluginHost)
                return;
            if (event.action == Action::NoteOff)
                pluginHost->noteOff(event.pitch, frameOffset);
            else
                pluginHost->noteOn(event.pitch, event.velocity, frameOffset);
        } else if (event.action == Action::MidiInput)
            instruments.midiNoteOn(event.track, event.pitch, event.velocity);
        else if (event.action == Action::Audition)
            trigger(event.track, event.pitch, event.velocity);
        else
            instruments.noteOff(event.track, event.pitch);
    }

  public:
    Queue<Command, 32> commands;
    // Audio thread returns replaced snapshots; the control thread frees them.
    Queue<Project *, 64> retiredProjects;
    // Audio thread produces clip events; the controller drains and sends them
    // through ALSA. No ALSA calls or allocations occur in render().
    Queue<MidiOutEvent, 2048> midiOutput;
    std::atomic<int> currentStep{-1};
    std::atomic<uint64_t> transportTick{0};
    std::atomic<uint64_t> renderedFrameSnapshot{0};
    std::atomic<uint64_t> droppedTimedEvents{0};
    std::atomic<uint64_t> droppedMidiOutput{0};
    std::atomic<unsigned> activeClips{0};
    // Set by render() when SwapPlugin replaces an already-installed plugin.
    // The control thread must exchange this back to nullptr and delete what
    // it finds; render() will refuse a second swap until this is drained, so
    // that it never has to free memory itself.
    std::atomic<ClapHost *> retiredPlugin{nullptr};
    Engine() = default;
    ~Engine() {
        delete pluginHost;
        // Audio has stopped. Free the live snapshot, queued-but-unadopted
        // commands and anything retired but not yet reclaimed.
        Command pending;
        while (commands.pop(pending))
            delete pending.snapshot;
        Project *retired = nullptr;
        while (retiredProjects.pop(retired))
            delete retired;
        delete project;
    }
    // Control thread only. Frees snapshots the audio thread has retired.
    void reclaimProjects() noexcept {
        Project *retired = nullptr;
        while (retiredProjects.pop(retired)) {
            delete retired;
            projectsInFlight--;
        }
    }
    // Control thread only. Copies `next` into a fresh snapshot and queues it
    // with `action` (Update or Restore). Fails, without side effects, if the
    // command queue is full or too many snapshots are awaiting adoption.
    bool submitProject(Action action, const Project &next,
                       const std::array<int, Tracks> &activeClips = {}) {
        reclaimProjects();
        if (projectsInFlight >= MaxProjectsInFlight)
            return false;
        Command cmd;
        cmd.action = action;
        cmd.snapshot = new Project(next);
        cmd.active = activeClips;
        if (!commands.push(cmd)) {
            delete cmd.snapshot;
            return false;
        }
        projectsInFlight++;
        return true;
    }
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    void render(float *output, std::size_t frames) noexcept {
        const uint64_t blockStart = renderedFrame;
        Command cmd;
        // Live events can target a future absolute frame. Keep them sorted in
        // a fixed audio-thread-owned array until their frame enters a block.
        for (unsigned count = 0; count < 31 && commands.pop(cmd); count++)
            switch (cmd.action) {
            case Action::Restore:
                for (unsigned i = 0; i < Tracks; i++)
                    emitTrackNotesOff(i);
                adoptProject(cmd.snapshot);
                for (unsigned i = 0; i < Tracks; i++)
                    active[i] = std::clamp(cmd.active[i], 0, 3);
                pending.fill(-1);
                instruments.stop();
                playing = false;
                step = -1;
                frame = 0;
                lastAbsStep = UINT64_MAX;
                midiTickPosition = 0;
                lastMidiTick = UINT64_MAX;
                break;
            case Action::Update:
                adoptProject(cmd.snapshot);
                break;
            case Action::Start:
                playing = true;
                step = -1;
                frame = 0;
                lastAbsStep = UINT64_MAX;
                midiTickPosition = 0;
                lastMidiTick = UINT64_MAX;
                break;
            case Action::Stop:
                for (unsigned i = 0; i < Tracks; i++)
                    emitTrackNotesOff(i);
                playing = false;
                step = -1;
                pending.fill(-1);
                instruments.stop();
                break;
            case Action::Launch:
                if (cmd.track >= 0 && cmd.track < int(Tracks) && cmd.clip >= 0 &&
                    cmd.clip < int(Scenes)) {
                    if (playing)
                        pending[cmd.track] = cmd.clip;
                    else
                        active[cmd.track] = cmd.clip;
                }
                break;
            case Action::Audition:
            case Action::MidiInput:
            case Action::NoteOff:
                enqueueTimed({cmd.action, cmd.track, cmd.pitch, cmd.velocity,
                              cmd.hasTargetFrame
                                  ? cmd.targetFrame
                                  : blockStart + (frames > 0
                                                      ? std::min<uint64_t>(cmd.frameOffset,
                                                                           frames - 1)
                                                      : 0)});
                break;
            case Action::SwapPlugin: {
                ClapHost *old = pluginHost;
                pluginHost = cmd.plugin;
                // SwapPlugin is a rare control-thread action (loading a
                // plugin), not a per-block one; if the control thread hasn't
                // yet collected the previous retiree this drops it rather
                // than freeing it here, since the audio thread must not
                // allocate or free.
                if (old && retiredPlugin.load(std::memory_order_relaxed) == nullptr)
                    retiredPlugin.store(old, std::memory_order_release);
                break;
            }
            }
        const bool solo = std::any_of(project->tracks.begin(), project->tracks.end(),
                                      [](auto &t) { return t.solo; });
        for (std::size_t n = 0; n < frames; n++) {
            while (pendingTimedCount && pendingTimed[0].targetFrame <= blockStart + n) {
                const auto event = pendingTimed[0];
                for (unsigned i = 1; i < pendingTimedCount; i++)
                    pendingTimed[i - 1] = pendingTimed[i];
                pendingTimedCount--;
                dispatchTimed(event, static_cast<unsigned>(n));
            }
            // One clock: steps are derived from the same tick position the
            // MIDI clips use, so tempo changes cannot separate them.
            const uint64_t absStep =
                static_cast<uint64_t>(midiTickPosition) / TicksPerStep;
            if (playing && (lastAbsStep == UINT64_MAX || absStep != lastAbsStep)) {
                lastAbsStep = absStep;
                step = static_cast<int>(absStep % Steps);
                if (step == 0)
                    for (unsigned i = 0; i < Tracks; i++)
                        if (pending[i] >= 0) {
                            emitTrackNotesOff(i);
                            instruments.allNotesOff(i);
                            active[i] = pending[i];
                            pending[i] = -1;
                        }
                for (unsigned i = 0; i < Tracks; i++) {
                    const auto &track = project->tracks[i];
                    const auto &legacy = track.clips[active[i]][step];
                    const auto &midi = track.midiClips[active[i]];
                    if (midi.noteCount == 0 && legacy.velocity > 0)
                        // Gate 90% of a step so slow tempos hold, fast ones don't smear.
                        trigger(i, legacy.pitch, legacy.velocity,
                                int(rate * 60.0 / std::clamp(project->bpm, 40.f, 240.f) / 4 * .9));
                }
            }
            if (playing) {
                const auto tick = static_cast<uint64_t>(midiTickPosition);
                if (tick != lastMidiTick) {
                    lastMidiTick = tick;
                    // Release first so a repeated pitch can retrigger cleanly
                    // when one note ends on the same tick another begins.
                    for (unsigned i = 0; i < Tracks; i++) {
                        const auto &track = project->tracks[i];
                        const auto &clip = track.midiClips[active[i]];
                        if (clip.noteCount > 0) {
                            if (clip.loopTicks == 0)
                                continue;
                            const auto clipTick = unsigned(tick % clip.loopTicks);
                            for (unsigned j = 0;
                                 j < std::min<unsigned>(clip.noteCount, MaxClipNotes); j++) {
                                const auto &note = clip.notes[j];
                                if ((unsigned(note.startTick) + note.durationTicks) %
                                        clip.loopTicks == clipTick) {
                                    instruments.noteOff(i, note.pitch);
                                    emitMidi(MidiOutKind::NoteOff, note.channel, note.pitch);
                                }
                            }
                        } else {
                            const auto &legacy = track.clips[active[i]];
                            const unsigned clipTick = unsigned(tick % DefaultClipTicks);
                            if (clipTick % TicksPerStep != 0)
                                continue;
                            const unsigned endedStep =
                                (clipTick / TicksPerStep + Steps - 1) % Steps;
                            const auto &note = legacy[endedStep];
                            if (note.velocity > 0)
                                emitMidi(MidiOutKind::NoteOff, 0, note.pitch);
                        }
                    }
                    for (unsigned i = 0; i < Tracks; i++) {
                        const auto &track = project->tracks[i];
                        const auto &clip = track.midiClips[active[i]];
                        if (clip.noteCount > 0) {
                            if (clip.loopTicks == 0)
                                continue;
                            const auto clipTick = unsigned(tick % clip.loopTicks);
                            for (unsigned j = 0;
                                 j < std::min<unsigned>(clip.noteCount, MaxClipNotes); j++) {
                                const auto &note = clip.notes[j];
                                if (note.startTick == clipTick) {
                                    instruments.midiNoteOn(i, note.pitch, note.velocity);
                                    if (!track.mute && (!solo || track.solo))
                                        emitMidi(MidiOutKind::NoteOn, note.channel, note.pitch,
                                                 note.velocity);
                                }
                            }
                        } else {
                            const unsigned clipTick = unsigned(tick % DefaultClipTicks);
                            if (clipTick % TicksPerStep == 0) {
                                const unsigned startedStep = clipTick / TicksPerStep;
                                const auto &note = track.clips[active[i]][startedStep];
                                if (note.velocity > 0 && !track.mute && (!solo || track.solo))
                                    emitMidi(MidiOutKind::NoteOn, 0, note.pitch, note.velocity);
                            }
                        }
                    }
                }
            }
            master += (std::clamp(project->master, 0.f, 1.f) - master) * .002f;
            for (unsigned i = 0; i < Tracks; i++) {
                const auto &t = project->tracks[i];
                const float target =
                    t.mute || (solo && !t.solo) ? 0 : std::clamp(t.volume, 0.f, 1.f);
                gains[i] += (target - gains[i]) * .003f;
            }
            float sample[2] = {0, 0};
            instruments.render(sample, gains);
            output[n * 2] = sample[0] * master * .5f;
            output[n * 2 + 1] = sample[1] * master * .5f;
            if (playing)
                frame++;
            if (playing)
                midiTickPosition += double(std::clamp(project->bpm, 40.f, 240.f)) * TicksPerQuarter / (60.0 * rate);
            renderedFrame++;
        }
        // Mixed in after the per-sample loop, at block rate rather than the
        // per-sample master smoothing above: a CLAP plugin renders its own
        // block internally, so its gain can only be applied once per call.
        if (pluginHost)
            pluginHost->process(output, static_cast<uint32_t>(frames), master * .5f);
        soft_clip_neon(output, static_cast<uint32_t>(frames), 1.f);
        unsigned clips = 0;
        for (unsigned i = 0; i < Tracks; i++)
            clips |= unsigned(active[i]) << (i * 2);
        activeClips.store(clips, std::memory_order_relaxed);
        currentStep.store(step, std::memory_order_relaxed);
        transportTick.store(
            playing ? static_cast<uint64_t>(midiTickPosition) : 0,
            std::memory_order_relaxed);
        renderedFrameSnapshot.store(renderedFrame, std::memory_order_relaxed);
    }
};
} // namespace eco
