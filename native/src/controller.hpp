// SPDX-License-Identifier: MIT
#pragma once
#include "audio.hpp"
#include "project_io.hpp"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantList>
#include <alsa/asoundlib.h>
#include <memory>
#include <poll.h>
#include <time.h>
#include <vector>

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(int step READ step NOTIFY tick)
    Q_PROPERTY(int selected READ selected NOTIFY changed)
    Q_PROPERTY(int scene READ scene NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool armed READ armed NOTIFY changed)
    Q_PROPERTY(double bpm READ bpm NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY tick)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    Q_PROPERTY(QVariantList volumes READ volumes NOTIFY changed)
    Q_PROPERTY(QVariantList mutes READ mutes NOTIFY changed)
    Q_PROPERTY(QVariantList active READ active NOTIFY tick)
    eco::Engine engine;
    eco::Project project = eco::demo();
    std::unique_ptr<Audio> audio;
    QTimer timer;
    snd_seq_t *midi = nullptr;
    int midiQueue = -1;
    int midiPort = -1;
    bool midiQueueRunning = false;
    std::vector<QSocketNotifier *> midiNotifiers;
    int selected_ = 0, scene_ = 0;
    struct CapturedMidiNote {
        bool active = false;
        int pitch = 0, track = 0, scene = 0, channel = 0;
        uint16_t eventIndex = 0, startTick = 0;
    };
    std::array<CapturedMidiNote, 16> capturedNotes{};
    bool playing_ = false, armed_ = false;
    QString message_ = "Select a clip. Make something.";
    QString name_ = "First light";
    bool send(eco::Command command) {
        if (engine.commands.push(command))
            return true;
        message_ = "Command queue busy; try again.";
        emit changed();
        return false;
    }
    bool update() {
        eco::Command c;
        c.action = eco::Action::Update;
        c.project = project;
        return send(c);
    }
    QString path() const {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
               "/session.eco.json";
    }
    void captureMidiOn(int pitch, int channel, float velocity) {
        if (!armed_ || !playing_ || pitch < 0 || pitch > 127)
            return;
        auto &clip = project.tracks[selected_].midiClips[scene_];
        const auto before = clip;
        auto &stepNote = project.tracks[selected_].clips[scene_]
                                                  [std::max(engine.currentStep.load(), 0)];
        const auto beforeStepNote = stepNote;
        if (clip.noteCount == 0) {
            // Promote the existing step pattern when its first MIDI note is
            // recorded so adopting the richer representation does not erase
            // the rest of the clip.
            for (unsigned s = 0; s < eco::Steps && clip.noteCount < eco::MaxClipNotes; s++) {
                const auto &legacy = project.tracks[selected_].clips[scene_][s];
                if (legacy.velocity > 0)
                    clip.notes[clip.noteCount++] = {
                        static_cast<uint16_t>((s * eco::TicksPerStep) % clip.loopTicks),
                        static_cast<uint16_t>(std::min<unsigned>(eco::TicksPerStep,
                                                                 clip.loopTicks - 1)),
                        static_cast<uint8_t>(std::clamp(legacy.pitch, 0, 127)), 0,
                        std::clamp(legacy.velocity, 0.001f, 1.f)};
            }
        }
        if (clip.noteCount >= eco::MaxClipNotes) {
            clip = before;
            message_ = "MIDI clip is full (128 notes).";
            emit changed();
            return;
        }
        unsigned slot = capturedNotes.size();
        for (unsigned i = 0; i < capturedNotes.size(); i++)
            if (!capturedNotes[i].active) {
                slot = i;
                break;
            }
        if (slot == capturedNotes.size()) {
            clip = before;
            message_ = "MIDI capture has 16 held-note slots; release notes and try again.";
            emit changed();
            return;
        }
        const int currentStep = std::max(engine.currentStep.load(std::memory_order_relaxed), 0);
        const auto start = static_cast<uint16_t>((currentStep * eco::TicksPerStep) %
                                                 clip.loopTicks);
        const auto index = clip.noteCount++;
        clip.notes[index] = {start,
                             static_cast<uint16_t>(std::min<unsigned>(eco::TicksPerStep,
                                                                      clip.loopTicks - 1)),
                             static_cast<uint8_t>(pitch), static_cast<uint8_t>(channel & 15),
                             std::clamp(velocity, 0.001f, 1.f)};
        const int displayPitch = std::clamp(pitch, 24, 96);
        stepNote = {displayPitch, std::clamp(velocity, 0.001f, 1.f)};
        if (!update()) {
            clip = before;
            stepNote = beforeStepNote;
            return;
        }
        capturedNotes[slot] = {true, pitch, selected_, scene_, channel & 15, index, start};
    }
    int captureMidiOff(int pitch, int channel) {
        for (auto it = capturedNotes.rbegin(); it != capturedNotes.rend(); ++it) {
            if (!it->active || it->pitch != pitch || it->channel != (channel & 15))
                continue;
            auto &clip = project.tracks[it->track].midiClips[it->scene];
            if (it->eventIndex < clip.noteCount) {
                const auto oldDuration = clip.notes[it->eventIndex].durationTicks;
                const auto end = engine.transportTick.load(std::memory_order_relaxed) %
                                 clip.loopTicks;
                auto duration = static_cast<unsigned>((end + clip.loopTicks - it->startTick) %
                                                     clip.loopTicks);
                duration = std::clamp(duration, 1u, unsigned(clip.loopTicks - 1));
                clip.notes[it->eventIndex].durationTicks = static_cast<uint16_t>(duration);
                if (!update())
                    clip.notes[it->eventIndex].durationTicks = oldDuration;
            }
            it->active = false;
            return it->track;
        }
        return selected_;
    }
    bool timestampMidiCommand(const snd_seq_event_t *event, eco::Command &command) const {
        if (!audio || !midiQueueRunning || midiQueue < 0 || event->queue != midiQueue ||
            (event->flags & SND_SEQ_TIME_STAMP_MASK) != SND_SEQ_TIME_STAMP_REAL ||
            (event->flags & SND_SEQ_TIME_MODE_MASK) != SND_SEQ_TIME_MODE_ABS)
            return false;

        snd_seq_queue_status_t *status = nullptr;
        snd_seq_queue_status_alloca(&status);
        if (snd_seq_get_queue_status(midi, midiQueue, status) < 0)
            return false;
        const auto *queueNow = snd_seq_queue_status_get_real_time(status);
        if (!queueNow)
            return false;
        const int64_t eventNs = int64_t(event->time.time.tv_sec) * 1000000000ll +
                                event->time.time.tv_nsec;
        const int64_t queueNowNs = int64_t(queueNow->tv_sec) * 1000000000ll +
                                   queueNow->tv_nsec;
        timespec ts{};
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
            return false;
        const int64_t monoNowNs = int64_t(ts.tv_sec) * 1000000000ll + ts.tv_nsec;
        const int64_t deltaNs = eventNs - queueNowNs;
        const auto eventMonoNs = deltaNs >= 0
                                     ? uint64_t(monoNowNs + deltaNs)
                                     : uint64_t(std::max<int64_t>(0, monoNowNs + deltaNs));
        uint64_t targetFrame = 0;
        if (!audio->frameForMonotonicNs(eventMonoNs, targetFrame))
            return false;
        command.targetFrame = targetFrame;
        command.hasTargetFrame = true;
        return true;
    }
    void pollMidi() {
        if (!midi)
            return;
        snd_seq_event_t *e = nullptr;
        for (int i = 0; i < 64 && snd_seq_event_input(midi, &e) >= 0; i++) {
            if (e->type == SND_SEQ_EVENT_NOTEON && e->data.note.velocity) {
                eco::Command c;
                c.action = eco::Action::MidiInput;
                c.track = selected_;
                c.pitch = e->data.note.note;
                c.velocity = e->data.note.velocity / 127.f;
                timestampMidiCommand(e, c);
                send(c);
                captureMidiOn(e->data.note.note, e->data.note.channel,
                              e->data.note.velocity / 127.f);
            } else if (e->type == SND_SEQ_EVENT_NOTEOFF ||
                       (e->type == SND_SEQ_EVENT_NOTEON && !e->data.note.velocity)) {
                const int noteTrack = captureMidiOff(e->data.note.note,
                                                     e->data.note.channel);
                eco::Command c;
                c.action = eco::Action::NoteOff;
                c.track = noteTrack;
                c.pitch = e->data.note.note;
                timestampMidiCommand(e, c);
                send(c);
            } else if (e->type == SND_SEQ_EVENT_CONTROLLER) {
                const int cc = e->data.control.param, value = e->data.control.value;
                if (cc >= 20 && cc < 24 && value > 63)
                    launchScene(cc - 20);
                if (cc == 24 && value > 63)
                    togglePlay();
                if (cc == 7)
                    setVolume(selected_, value / 127.0);
            }
            snd_seq_free_event(e);
        }
    }

  public:
    explicit Controller(QObject *parent = nullptr) : QObject(parent) {
        try {
            audio = std::make_unique<Audio>(engine);
        } catch (const std::exception &e) {
            message_ = QString::fromUtf8(e.what());
        }
        if (snd_seq_open(&midi, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) >= 0) {
            snd_seq_set_client_name(midi, "ECO Controller");
            midiQueue = snd_seq_alloc_named_queue(midi, "ECO MIDI input clock");
            if (midiQueue >= 0 && snd_seq_start_queue(midi, midiQueue, nullptr) >= 0 &&
                snd_seq_drain_output(midi) >= 0)
                midiQueueRunning = true;
            midiPort = snd_seq_create_simple_port(
                midi, "Input", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
            // Ask ALSA to stamp received events against our running real-time
            // queue. If timestamp support is unavailable, commands retain the
            // safe next-block fallback.
            if (midiPort >= 0 && midiQueueRunning) {
                snd_seq_port_info_t *info = nullptr;
                snd_seq_port_info_alloca(&info);
                if (snd_seq_get_port_info(midi, midiPort, info) >= 0) {
                    snd_seq_port_info_set_timestamping(info, 1);
                    snd_seq_port_info_set_timestamp_real(info, 1);
                    snd_seq_port_info_set_timestamp_queue(info, midiQueue);
                    snd_seq_set_port_info(midi, midiPort, info);
                }
            }
            // React to MIDI as soon as ALSA has it queued, instead of
            // waiting on the UI timer: that capped input latency at up to
            // one tick (33 ms), well past what "timestamped" MIDI needs.
            const int count = snd_seq_poll_descriptors_count(midi, POLLIN);
            if (count > 0) {
                std::vector<pollfd> fds(static_cast<std::size_t>(count));
                snd_seq_poll_descriptors(midi, fds.data(), static_cast<unsigned>(count), POLLIN);
                for (const auto &pfd : fds) {
                    auto *notifier = new QSocketNotifier(pfd.fd, QSocketNotifier::Read, this);
                    connect(notifier, &QSocketNotifier::activated, this, [this] { pollMidi(); });
                    midiNotifiers.push_back(notifier);
                }
            }
        } else
            midi = nullptr;
        // The timer now only drives UI refresh (playhead/status); MIDI input
        // no longer depends on it.
        connect(&timer, &QTimer::timeout, this, [this] {
            // A SwapPlugin the audio thread just applied hands back the
            // previously installed plugin here for the control thread to
            // free; the audio thread must not allocate or free memory.
            if (auto *old = engine.retiredPlugin.exchange(nullptr, std::memory_order_acquire))
                delete old;
            emit tick();
        });
        timer.start(33);
    }
    ~Controller() {
        timer.stop();
        if (auto *old = engine.retiredPlugin.exchange(nullptr, std::memory_order_acquire))
            delete old;
        for (auto *notifier : midiNotifiers)
            delete notifier;
        if (midi) {
            if (midiQueue >= 0) {
                snd_seq_stop_queue(midi, midiQueue, nullptr);
                snd_seq_free_queue(midi, midiQueue);
            }
            snd_seq_close(midi);
        }
    }
    int step() const {
        return engine.currentStep.load();
    }
    int selected() const {
        return selected_;
    }
    int scene() const {
        return scene_;
    }
    bool playing() const {
        return playing_;
    }
    bool armed() const {
        return armed_;
    }
    double bpm() const {
        return project.bpm;
    }
    QString status() const {
        if (!audio)
            return "Audio unavailable";
        const auto state = audio->state.load();
        return state == PW_STREAM_STATE_STREAMING ? "PIPEWIRE · 48 kHz · STEREO"
               : state == PW_STREAM_STATE_ERROR   ? "PipeWire error — check interface and restart"
                                                  : "Waiting for PipeWire / audio device";
    }
    QString message() const {
        return message_;
    }
    QVariantList steps() const {
        QVariantList list;
        for (auto &n : project.tracks[selected_].clips[scene_])
            list << bool(n.velocity > 0);
        return list;
    }
    QVariantList volumes() const {
        QVariantList list;
        for (auto &t : project.tracks)
            list << double(t.volume);
        return list;
    }
    QVariantList mutes() const {
        QVariantList list;
        for (auto &t : project.tracks)
            list << t.mute;
        return list;
    }
    QVariantList active() const {
        QVariantList list;
        unsigned packed = engine.activeClips.load();
        for (unsigned i = 0; i < eco::Tracks; i++)
            list << int((packed >> (i * 2)) & 3);
        return list;
    }
    Q_INVOKABLE void togglePlay() {
        if (!audio) {
            message_ = "Audio is unavailable; check PipeWire and restart.";
            emit changed();
            return;
        }
        eco::Command c;
        c.action = playing_ ? eco::Action::Stop : eco::Action::Start;
        if (send(c))
            playing_ = !playing_;
        emit changed();
    }
    Q_INVOKABLE void stop() {
        eco::Command c;
        c.action = eco::Action::Stop;
        if (send(c))
            playing_ = false;
        emit changed();
    }
    Q_INVOKABLE void arm() {
        armed_ = !armed_;
        message_ = armed_ ? "MIDI note capture armed. Notes loop with held duration."
                          : "MIDI capture off.";
        emit changed();
    }
    Q_INVOKABLE void select(int track, int clip) {
        if (track < 0 || track >= 6 || clip < 0 || clip >= 4)
            return;
        selected_ = track;
        scene_ = clip;
        emit changed();
    }
    Q_INVOKABLE void launch(int track, int clip) {
        if (track < 0 || track >= 6 || clip < 0 || clip >= 4)
            return;
        eco::Command c;
        c.action = eco::Action::Launch;
        c.track = track;
        c.clip = clip;
        send(c);
        select(track, clip);
        message_ = playing_ ? "Clip queued for the next bar." : "Clip selected. Press Play.";
        emit changed();
    }
    Q_INVOKABLE void launchScene(int clip) {
        if (clip < 0 || clip >= 4)
            return;
        const int track = selected_;
        for (int t = 0; t < 6; t++)
            launch(t, clip);
        select(track, clip);
    }
    Q_INVOKABLE void toggleStep(int s) {
        if (s < 0 || s >= 16)
            return;
        auto before = project;
        auto &n = project.tracks[selected_].clips[scene_][s];
        n.pitch = std::array<int, 6>{36, 38, 42, 36, 60, 72}[selected_];
        n.velocity = n.velocity > 0 ? 0 : .8f;
        auto &midi = project.tracks[selected_].midiClips[scene_];
        if (midi.noteCount > 0 && midi.loopTicks == eco::DefaultClipTicks) {
            const auto tick = static_cast<uint16_t>(s * eco::TicksPerStep);
            for (unsigned i = 0; i < midi.noteCount;) {
                if (midi.notes[i].startTick != tick) {
                    i++;
                    continue;
                }
                for (unsigned j = i + 1; j < midi.noteCount; j++)
                    midi.notes[j - 1] = midi.notes[j];
                midi.noteCount--;
            }
            if (n.velocity > 0 && midi.noteCount < eco::MaxClipNotes)
                midi.notes[midi.noteCount++] = {tick,
                                                static_cast<uint16_t>(eco::TicksPerStep),
                                                static_cast<uint8_t>(n.pitch), 0, n.velocity};
        }
        if (!update())
            project = before;
        emit changed();
    }
    Q_INVOKABLE void setTempo(double value) {
        if (!std::isfinite(value))
            return;
        auto old = project.bpm;
        project.bpm = std::clamp(value, 40., 240.);
        if (!update())
            project.bpm = old;
        emit changed();
    }
    Q_INVOKABLE void setVolume(int t, double value) {
        if (t < 0 || t >= 6 || !std::isfinite(value))
            return;
        auto old = project.tracks[t].volume;
        project.tracks[t].volume = std::clamp(value, 0., 1.);
        if (!update())
            project.tracks[t].volume = old;
        emit changed();
    }
    Q_INVOKABLE void mute(int t) {
        if (t < 0 || t >= 6)
            return;
        project.tracks[t].mute = !project.tracks[t].mute;
        if (!update())
            project.tracks[t].mute = !project.tracks[t].mute;
        emit changed();
    }
    // Loads a CLAP instrument (its first plugin, by factory index) as an
    // independent "seventh track" mixed alongside the 6-track sequencer, not
    // part of it. dlopen/instantiate/activate happen here on the control
    // thread; only the resulting ClapHost pointer crosses to the audio
    // thread, via a SwapPlugin command.
    Q_INVOKABLE void loadPlugin(const QString &path) {
        auto host = std::make_unique<eco::ClapHost>();
        if (!host->load(path.toStdString(), eco::SampleRate)) {
            message_ = "Failed to load CLAP plugin: " + path;
            emit changed();
            return;
        }
        eco::Command c;
        c.action = eco::Action::SwapPlugin;
        c.plugin = host.release();
        if (send(c)) {
            message_ = "Loaded CLAP plugin: " + path;
        } else {
            delete c.plugin; // queue was full; command never reached Engine.
            message_ = "Command queue busy; try again.";
        }
        emit changed();
    }
    Q_INVOKABLE void auditionPlugin(int pitch = 60, double velocity = .8) {
        if (!std::isfinite(velocity))
            return;
        eco::Command c;
        c.action = eco::Action::Audition;
        c.track = int(eco::Tracks);
        c.pitch = std::clamp(pitch, 0, 127);
        c.velocity = std::clamp(velocity, 0., 1.);
        send(c);
    }
    Q_INVOKABLE void releasePluginNote(int pitch) {
        eco::Command c;
        c.action = eco::Action::NoteOff;
        c.track = int(eco::Tracks);
        c.pitch = pitch;
        send(c);
    }
    Q_INVOKABLE void audition(int pitch = 60, double velocity = .8) {
        if (!std::isfinite(velocity))
            return;
        eco::Command c;
        c.action = eco::Action::Audition;
        c.track = selected_;
        c.pitch = std::clamp(pitch, 24, 96);
        c.velocity = std::clamp(velocity, 0., 1.);
        send(c);
        const int captureStep = step();
        if (armed_ && playing_ && captureStep >= 0) {
            auto before = project;
            project.tracks[selected_].clips[scene_][captureStep] = {c.pitch, c.velocity};
            if (!update())
                project = before;
            emit changed();
        }
    }
    Q_INVOKABLE void save() {
        eco::SavedProject saved;
        saved.project = project;
        const auto act = active();
        for (int t = 0; t < 6; t++)
            saved.active[t] = act[t].toInt();
        saved.name = name_.toStdString();
        const auto bytes = QByteArray::fromStdString(eco::toJson(saved));
        QDir().mkpath(QFileInfo(path()).absolutePath());
        QSaveFile file(path());
        const bool ok =
            file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
        message_ = ok ? "Saved " + path() : "Save failed: " + file.errorString();
        emit changed();
    }
    Q_INVOKABLE void load() {
        QFile file(path());
        if (!file.open(QIODevice::ReadOnly)) {
            message_ = "No saved session at " + path();
            emit changed();
            return;
        }
        if (file.size() > 1000000) {
            message_ = "Project exceeds 1 MB limit.";
            emit changed();
            return;
        }
        eco::SavedProject loaded;
        if (!eco::fromJson(file.readAll().toStdString(), loaded)) {
            message_ = "Invalid ECO project; current session retained.";
            emit changed();
            return;
        }
        // Replace project, transport and clip selection in one bounded command.
        eco::Command restore;
        restore.action = eco::Action::Restore;
        restore.project = loaded.project;
        restore.active = loaded.active;
        if (!send(restore))
            return;
        project = loaded.project;
        playing_ = false;
        name_ = QString::fromStdString(loaded.name);
        selected_ = 0;
        scene_ = loaded.active[0];
        message_ = "Loaded " + path();
        emit changed();
    }
  signals:
    void changed();
    void tick();
};
