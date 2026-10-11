# ECO workstation roadmap

Decision record: 2026-10-10. Product direction: a low-level groovebox and DAW
for modern x86-64 mini PCs and Raspberry Pi class ARM64 systems. The initial
product must make MIDI and control-voltage sequencing dependable before it
tries to match the breadth of Ableton Live. Treat feature parity as a long-term
compatibility goal, not a claim about the current six-track prototype.
The portable native engine boundary and source-adaptation rules are defined in
[NATIVE-ENGINE.md](NATIVE-ENGINE.md): reuse ideas and musical behavior from
workspace projects, while implementing ECO runtime code without JUCE or host
framework dependencies.

Review findings and the ordered improvement plan are in [REVIEW.md](REVIEW.md).

## What the code does today

- `Engine` renders six fixed tracks and one separately auditioned CLAP
  instrument. The legacy step grid remains, and each track/scene now also has a
  fixed 128-note MIDI event clip with 960 PPQ timing, pitch, channel, velocity,
  start, duration, and loop length. The engine schedules note starts/releases
  from its sample counter and publishes note events to an ALSA output port.
  The controller drains a bounded queue on a 1 ms Qt timer and writes MIDI
  events outside the audio callback. Output timing and overflow need hardware
  validation; there is no output port selector or MIDI hardware latency
  compensation. The UI still has only the 16-step editor; there is no piano
  roll, automation lane, arrangement timeline, or CV.
- ALSA MIDI input is event-driven on the Qt control thread. The audio callback
  pairs its absolute sample frame with PipeWire and monotonic clock readings;
  absolute ALSA queue timestamps are mapped to engine frames and held in a
  fixed-capacity audio-thread scheduler. Missing timestamps or clock anchors
  fall back to the next block. Events already late when they reach the engine
  are dispatched at the next available sample, so input timing still needs
  loopback measurement and latency compensation. Armed capture still
  quantizes note starts to the current step and measures duration from the
  latest engine tick. MIDI channels are saved but the built-in LIGHT
  instruments do not yet route by channel.
- `Audio` owns a PipeWire playback stream and requests a 256/48000 node
  latency. It now exposes an internal clock mapping for MIDI scheduling, but
  has no device selection, capture, xrun statistics, or audio graph. This is a
  playback prototype, not yet a DAW audio engine.
- The CLAP host loads the first descriptor in a bundle, assumes stereo output,
  has no parameter/state/GUI/transport support, and drops plugin output events.
  It also calls `start_processing()` and `stop_processing()` on the control
  thread, although CLAP specifies these calls for the audio thread. Plugin
  lifecycle needs redesign before CLAP support is treated as reliable.
- No code currently drives a DAC or gate output. Raspberry Pi GPIO is not an
  audio-rate CV interface; CV needs a specified external multichannel DAC or
  dedicated controller with defined voltage range, calibration, update rate,
  gate behavior, and electrical protection.

## Product sequence

| Phase | Deliverable | Acceptance gate |
|---|---|---|
| A. Timing and realtime foundation | One monotonic audio timeline; timestamped MIDI input/output; fixed-capacity event transport; xrun and callback-time telemetry; explicit device/quantum configuration | Events map to expected sample frames across variable PipeWire quanta; overflow is visible; callback allocates/blocks zero times; measured loopback latency recorded on reference mini PC and Pi |
| B. MIDI-first sequencer | Polyphonic note clips with start, duration, pitch, velocity, channel, and stable event identity; step, piano-roll, and live-recording edit paths; tempo, meter, swing, probability, ratchets, ties; MIDI learn and routing | Save/reload preserves overlapping notes and expressive data; MIDI recording/playback is sample-aligned; clip launch and loop boundaries are deterministic |
| C. CV/Gate sequencing | Hardware abstraction for CV devices; clocked pitch/gate/modulation lanes; calibrated scale and voltage profiles; clock/reset/run I/O; deterministic output scheduling | Reference DAC/controller selected and documented; measured timing/jitter, pitch error, voltage safety, hotplug, and overload behavior pass on target boards |
| D. Audio production core | Multi-device aware audio graph; audio input and recording; tracks, buses, sends, gain/pan, automation, latency reporting/compensation, metering, sample playback and time-stretch decision | Session round trips and renders match; capture/playback sync measured; graph changes do not glitch under the published workload |
| E. Native instruments and CLAP | ECO device contract for owned DSP modules; portable instruments/effects in the native graph; thin CLAP export adapters; then a conforming in-process CLAP host with discovery, parameters/automation, state, ports, transport, bypass and failure handling | Native devices render consistently across supported targets within documented tolerances; project restores device state; CLAP validator/reference fixtures pass; plugin failures cannot corrupt host state |
| F. Arrangement and workflow depth | Song timeline, follow actions, scenes, comping/undo, editing tools, MIDI/audio import/export, templates and recovery | Documented project interchange and workflow acceptance set; reliable long-session editing and recovery |
| G. Platform qualification | Reproducible x86-64 and Pi images, board-specific RT options, update/recovery, thermal and power testing | 60-minute stress tests, measured end-to-end latency, xrun log, temperature/power and supported-interface matrix per release |

## Sequencer model: next implementation boundary

Keep the current engine's single-writer audio-thread ownership. The next project
schema should replace step-as-note storage with versioned clip data. A clip
contains a loop length in musical ticks and a bounded, serializable event list.
Each note event carries a unique ID, start tick, duration tick, pitch, velocity,
channel, and optional per-note expression. Control events carry controller or
automation identity and value. Clip launch quantization and song arrangement
belong in transport/project data, not hidden in UI state.

The current implementation uses fixed event arrays, so it has bounded storage
and performs no event-vector allocation in the callback. Its full `Project`
still travels inside each `Command`, however; replace that growing value copy
with immutable snapshots and audio-thread adoption/reclamation before raising
the event cap substantially. Overflow must be reported rather than silently
dropping arbitrary note-offs. Keep MIDI 1.0 devices as the compatibility
baseline while designing event values wide enough for later MIDI 2.0 resolution
and per-note expression.

CV is a first-class sequencer destination, but not an implicit Pi GPIO feature.
Start with a transport/device interface and one supported external DAC or
controller. Keep software lanes portable across hardware; put voltage mapping,
calibration, gate polarity and device limits in the hardware profile.

## Portable native devices

ECO owns its low-level DAW and groovebox methods. Existing VSTS workspace
projects inform workflow, sound design and algorithm choices; JUCE-, Max- and
host-specific implementations stay outside the ECO runtime. Reimplement
selected behavior against a versioned, bounded native device interface. Keep
the engine independent from Qt, PipeWire, ALSA and plugin SDKs; use adapters at
those boundaries. The detailed contract, real-time constraints and extraction
sequence are in [NATIVE-ENGINE.md](NATIVE-ENGINE.md).

The Bitwig/Ableton/Reaktor direction maps to one native project/runtime model:
clips and scenes with an arrangement view, track device chains and modulation,
and constrained patch graphs inside devices. These are workflow layers over one
transport and audio graph, not separate engines. Develop the graph and native
device contract before exposing arbitrary user-patched topologies.

The current LIGHT drum pads, TB-303 voice and FM6 synths now run through
`eco::NativeDevice`, with stable IDs, prepare/reset lifecycle, MIDI note events
and allocation-free stereo processing. They are compiled as
`eco-native-devices`, a portable target linked by the renderer, workstation
and engine clients. The adapters retain LIGHT's fixed 48 kHz DSP; parameter
metadata and saved state are not exposed yet. The next device slice is to add
those descriptor/state APIs and route project control through them.

## Explicit non-goals for the first usable instrument

- Claiming full Ableton Live feature parity while the audio graph, recording,
  arrangement, editing, plugin, and interchange layers are absent.
- Running arbitrary third-party plugins inside the callback before host/plugin
  lifecycle, state, latency, port negotiation, and failure handling are sound.
- Promising 64-frame or hard-realtime behavior from the requested PipeWire
  quantum or from QEMU benchmarks. Publish only hardware measurements.

## Research notes

- PipeWire's `pw_stream_get_time_n()` is RT-safe. ECO currently pairs its
  `now` clock value with `CLOCK_MONOTONIC` and the engine callback frame. This
  is a useful first mapping, but hardware loopback tests must establish its
  accuracy across graph quantum changes and xruns.
- ALSA input ports request absolute real-time stamps from a private running
  sequencer queue. The controller translates absolute event stamps against
  queue time and maps them through the PipeWire anchor; unsupported or late
  events use next-block/next-sample fallback behavior. Relative stamps and
  timestamp edge cases still need ALSA-device validation.
- MIDI clip and legacy step notes reach `ECO Controller:Output` when a client
  is connected with `aconnect`. Engine-to-controller transport is bounded and
  nonblocking; ALSA sends occur on the controller thread. The current 1 ms
  drain cadence is a prototype and does not guarantee 1 ms output timing.
- CLAP requires hosts to respect each method's thread specification. The
  current processing start/stop calls violate that contract. Its fundamental
  integration surface also includes state, params, audio ports, note ports,
  latency, render, and GUI extensions; current hosting implements only a
  narrow instrument path.
- MIDI 2.0 extends MIDI 1.0 and adds higher resolution and per-note expression,
  but does not replace MIDI 1.0. Implement the common MIDI 1.0 path first and
  keep the internal event schema extensible.

Primary references:

- [PipeWire stream API](https://pipewire.pages.freedesktop.org/pipewire/group__pw__stream.html)
- [PipeWire `pw_time`](https://docs.pipewire.org/structpw__time.html)
- [ALSA Sequencer API](https://www.alsa-project.org/alsa-doc/alsa-lib/seq.html)
- [CLAP repository and extension overview](https://github.com/free-audio/clap)
- [CLAP plugin lifecycle](https://github.com/free-audio/clap/blob/main/include/clap/plugin.h)
- [MIDI 2.0 overview](https://midi.org/midi-2-0)
- [USB MIDI class specifications](https://www.usb.org/documents?search=midi)
