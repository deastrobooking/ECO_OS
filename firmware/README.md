# ECO controller firmware

Arduino is a controller target, not the Linux host. The starter sketch supports
native USB MIDI on Arduino Leonardo / Micro (ATmega32u4). Classic Uno R3 and Mega
boards do not use this firmware's native USB MIDI transport. ARM microcontrollers
need their own USB stack/board support; this is not a claim of universal Arduino compatibility.

Install the Arduino AVR board package and the official MIDIUSB library, open
`eco-controller/eco-controller.ino`, select your board, compile, then upload.
The firmware has not been compiled or flashed in this environment.

Wire five normally-open buttons between digital pins 2–6 and ground. Internal
pull-ups and 20 ms debounce are enabled. Wire a potentiometer to A0, ground and
board VCC. Buttons send channel-1 CC20–23 (scenes) and CC24 (transport). The pot
sends CC7 (selected track level). Press=127, release=0.

On the Yocto target, list ports with `aconnect -l`. Connect the controller source
to `ECO Controller:Input` using the numeric ports shown by `aconnect`. This MVP
requires explicit routing; automatic hotplug mapping is a later milestone.
Use any MIDI keyboard for notes; arm capture in the native UI to write the
current sixteenth-note step. Note duration and release are not recorded yet.

Source: https://github.com/arduino-libraries/MIDIUSB
