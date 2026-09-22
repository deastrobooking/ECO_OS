// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280; height: 800; minimumWidth: 800; minimumHeight: 600
    visible: true; visibility: Window.FullScreen
    title: "ECO — Music OS"; color: "#181b18"
    property var colors: ["#c3ec87", "#e6ad79", "#e8ca76", "#8abde0", "#b8a0de", "#df9bb4"]
    property var names: ["Kick", "Snare", "Hi-hats", "LIGHT TB-303", "LIGHT FM6", "LIGHT Bell"]
    property var scenes: ["First light", "In motion", "Open space", "After hours"]
    palette.button: "#2c3425"; palette.buttonText: "#e6e8dd"; palette.windowText: "#e6e8dd"
    palette.text: "#e6e8dd"; palette.base: "#20241e"; palette.highlight: "#c3ec87"
    Shortcut { sequence: "Space"; onActivated: workstation.togglePlay() }
    Shortcut { sequence: "Escape"; onActivated: window.visibility = Window.Windowed }
    Shortcut { sequence: "Ctrl+S"; onActivated: workstation.save() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 28; spacing: 18
        RowLayout {
            Label { text: "e"; color: "#c3ec87"; font.pixelSize: 52; font.italic: true }
            Label { text: "ECO / LIGHT MUSIC OS"; font.pixelSize: 22; Layout.fillWidth: true }
            Label { text: "DEVELOPER PREVIEW 0.1"; color: "#969c8d"; font.pixelSize: 11 }
            Button { text: "Reload session"; onClicked: workstation.load() }
            Button { text: "Save session ↗"; onClicked: workstation.save() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#343831" }
        RowLayout {
            Button { text: workstation.playing ? "■ Stop" : "▶ Play"; onClicked: workstation.togglePlay() }
            Button { text: "■ Stop"; onClicked: workstation.stop() }
            Button { text: workstation.armed ? "● Armed" : "○ Capture MIDI"; onClicked: workstation.arm() }
            Label { text: "TEMPO"; color: "#969c8d"; leftPadding: 24 }
            SpinBox { from: 40; to: 240; value: workstation.bpm; onValueModified: workstation.setTempo(value) }
            Label { text: "BPM    /    4:4    /    1 BAR QUANTIZE"; color: "#969c8d"; font.pixelSize: 11; Layout.fillWidth: true }
            Label { text: workstation.step < 0 ? "01 . 01" : "01 . " + String(workstation.step+1).padStart(2,"0"); font.pixelSize: 24; color: "#c3ec87" }
        }
        Label { text: "Session."; font.pixelSize: 32; topPadding: 8 }
        GridLayout {
            columns: 7; columnSpacing: 8; rowSpacing: 8; Layout.fillWidth: true; Layout.fillHeight: true
            Repeater {
                model: 7
                Label { required property int index; text: index<6 ? "0"+(index+1)+"   "+window.names[index] : "SCENES"; color: index<6?window.colors[index]:"#969c8d"; font.pixelSize: 12; padding: 8; Layout.fillWidth: true }
            }
            Repeater {
                model: 28
                Button {
                    id: clipButton
                    required property int index
                    property int track: index%7
                    property int clip: Math.floor(index/7)
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 50
                    text: track===6 ? "▶  "+window.scenes[clip] : "▷  "+window.names[track]+" 0"+(clip+1)
                    font.pixelSize: 12
                    background: Rectangle {
                        radius: 4
                        color: clipButton.track===6 ? "#252c20" : Qt.darker(window.colors[clipButton.track], workstation.active[clipButton.track]===clipButton.clip ? 3.5 : 5.5)
                        border.width: workstation.selected===clipButton.track && workstation.scene===clipButton.clip ? 2 : 1
                        border.color: clipButton.track===6 ? "#414c37" : window.colors[clipButton.track]
                    }
                    onClicked: track===6 ? workstation.launchScene(clip) : workstation.launch(track,clip)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: 6
                ColumnLayout {
                    required property int index
                    Layout.fillWidth: true
                    RowLayout {
                        Button { text: workstation.mutes[index] ? "MUTED" : "M"; onClicked: workstation.mute(index); Layout.preferredWidth: 62 }
                        Label { text: Math.round(workstation.volumes[index]*100)+"%"; color: window.colors[index] }
                    }
                    Slider { Layout.fillWidth: true; from: 0; to: 1; value: workstation.volumes[index]; onMoved: workstation.setVolume(index,value) }
                }
            }
        }
        Rectangle { height: 1; color: "#343831"; Layout.fillWidth: true }
        RowLayout {
            Label { text: "CLIP EDITOR   /   "+window.names[workstation.selected]+"   /   "+window.scenes[workstation.scene]; Layout.fillWidth: true }
            Button { text: "▶ Audition"; onClicked: workstation.audition([36,38,42,36,60,72][workstation.selected],0.8) }
        }
        RowLayout {
            spacing: 6; Layout.fillWidth: true
            Repeater {
                model: 16
                Button {
                    id: stepButton
                    required property int index
                    Layout.fillWidth: true; Layout.preferredHeight: 58
                    text: String(index+1).padStart(2,"0")
                    palette.buttonText: workstation.steps[index] ? "#20251b" : "#969c8d"
                    background: Rectangle {
                        radius: 3; color: workstation.steps[stepButton.index] ? window.colors[workstation.selected] : "#2a3025"
                        border.width: workstation.step===stepButton.index ? 3 : 1
                        border.color: workstation.step===stepButton.index ? "#ffffff" : "#47523e"
                    }
                    onClicked: workstation.toggleStep(index)
                }
            }
        }
        Label { text: workstation.message; color: "#969c8d"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
        RowLayout {
            Label { text: workstation.status; color: "#c3ec87"; font.pixelSize: 10; Layout.fillWidth: true }
            Label { text: "YOCTO NATIVE   /   SPACE: PLAY   /   CTRL+S: SAVE   /   ESC: WINDOW"; color: "#969c8d"; font.pixelSize: 10 }
        }
    }
}
