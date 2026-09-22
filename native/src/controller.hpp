// SPDX-License-Identifier: MIT
#pragma once
#include "audio.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantList>
#include <alsa/asoundlib.h>
#include <memory>

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
    int selected_ = 0, scene_ = 0;
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
    void pollMidi() {
        if (!midi)
            return;
        snd_seq_event_t *e = nullptr;
        for (int i = 0; i < 64 && snd_seq_event_input(midi, &e) >= 0; i++) {
            if (e->type == SND_SEQ_EVENT_NOTEON && e->data.note.velocity) {
                audition(e->data.note.note, e->data.note.velocity / 127.0);
            } else if (e->type == SND_SEQ_EVENT_NOTEOFF ||
                       (e->type == SND_SEQ_EVENT_NOTEON && !e->data.note.velocity)) {
                eco::Command c;
                c.action = eco::Action::NoteOff;
                c.track = selected_;
                c.pitch = e->data.note.note;
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
            snd_seq_create_simple_port(
                midi, "Input", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        } else
            midi = nullptr;
        connect(&timer, &QTimer::timeout, this, [this] {
            pollMidi();
            emit tick();
        });
        timer.start(33);
    }
    ~Controller() {
        timer.stop();
        if (midi)
            snd_seq_close(midi);
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
        message_ = armed_ ? "MIDI step capture armed. Play your controller while running."
                          : "Step capture off.";
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
        QJsonArray tracks;
        const auto act = active();
        const QStringList types = {"kick", "snare", "hat", "bass", "keys", "pluck"};
        const QStringList names = {"Kick", "Snare", "Hi-hats", "Sub bass", "Soft keys", "Glass"};
        for (int t = 0; t < 6; t++) {
            QJsonArray clips;
            for (auto &c : project.tracks[t].clips) {
                QJsonArray notes;
                for (auto &n : c) {
                    if (n.velocity > 0)
                        notes.append(QJsonObject{{"note", n.pitch}, {"velocity", n.velocity}});
                    else
                        notes.append(QJsonValue::Null);
                }
                clips.append(notes);
            }
            const auto &tr = project.tracks[t];
            tracks.append(QJsonObject{{"name", names[t]},
                                      {"type", types[t]},
                                      {"volume", tr.volume},
                                      {"mute", tr.mute},
                                      {"solo", tr.solo},
                                      {"active", act[t].toInt()},
                                      {"patterns", clips}});
        }
        QJsonObject root{{"version", 1},
                         {"name", name_},
                         {"bpm", project.bpm},
                         {"master", project.master},
                         {"tracks", tracks}};
        QDir().mkpath(QFileInfo(path()).absolutePath());
        QSaveFile file(path());
        const auto bytes = QJsonDocument(root).toJson();
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
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        const auto root = doc.object();
        auto number = [](QJsonValue v, double lo, double hi) {
            return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= lo &&
                   v.toDouble() <= hi;
        };
        bool valid = error.error == QJsonParseError::NoError && root["version"].toInt() == 1 &&
                     root["name"].isString() && root["name"].toString().size() <= 80 &&
                     number(root["bpm"], 40, 240) && number(root["master"], 0, 1) &&
                     root["tracks"].isArray();
        auto next = project;
        next.bpm = root["bpm"].toDouble();
        next.master = root["master"].toDouble();
        const auto tracks = root["tracks"].toArray();
        valid &= tracks.size() == 6;
        std::array<int, 6> clips{};
        const QStringList types = {"kick", "snare", "hat", "bass", "keys", "pluck"};
        for (int t = 0; t < 6 && valid; t++) {
            const auto tr = tracks[t].toObject();
            const auto patterns = tr["patterns"].toArray();
            valid = patterns.size() == 4 && tr["type"].toString() == types[t] &&
                    tr["mute"].isBool() && tr["solo"].isBool() && number(tr["volume"], 0, 1) &&
                    number(tr["active"], 0, 3) && tr["active"].toDouble() == tr["active"].toInt();
            next.tracks[t].volume = tr["volume"].toDouble();
            next.tracks[t].mute = tr["mute"].toBool();
            next.tracks[t].solo = tr["solo"].toBool();
            clips[t] = tr["active"].toInt();
            for (int c = 0; c < 4 && valid; c++) {
                const auto notes = patterns[c].toArray();
                valid = notes.size() == 16;
                for (int s = 0; s < 16 && valid; s++) {
                    const auto v = notes[s];
                    if (v.isNull()) {
                        next.tracks[t].clips[c][s].velocity = 0;
                        continue;
                    }
                    const auto n = v.toObject();
                    valid = v.isObject() && number(n["note"], 24, 96) &&
                            n["note"].toInt() == n["note"].toDouble() &&
                            number(n["velocity"], 0, 1);
                    next.tracks[t].clips[c][s] = {n["note"].toInt(),
                                                  float(n["velocity"].toDouble())};
                }
            }
        }
        if (!valid) {
            message_ = "Invalid ECO v1 project; current session retained.";
            emit changed();
            return;
        }
        // Replace project, transport and clip selection in one bounded command.
        eco::Command restore;
        restore.action = eco::Action::Restore;
        restore.project = next;
        restore.active = clips;
        if (!send(restore))
            return;
        project = next;
        playing_ = false;
        name_ = root["name"].toString();
        selected_ = 0;
        scene_ = clips[0];
        message_ = "Loaded " + path();
        emit changed();
    }
  signals:
    void changed();
    void tick();
};
