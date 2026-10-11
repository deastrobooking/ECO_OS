# ECO native engine boundary

Decision record: 2026-10-10. ECO_OS is the source of truth for the portable
DAW and groovebox engine. Other projects in the VSTS workspace are references
for product behavior, algorithms, sound design and workflow. Their JUCE, Max,
Ableton, or platform-specific host code is not part of the ECO runtime.

## Portability contract

- Known violation: `engine.hpp` currently includes `clap_host.hpp` (and thus the
  CLAP SDK and `dlopen`). See [REVIEW.md](REVIEW.md) finding 9.
- The engine and owned instruments build as portable C/C++ without JUCE, Qt,
  ALSA, PipeWire, or a plugin SDK dependency. The current LIGHT C DSP is a
  starting point for this boundary; it is not yet the complete module API.
- Keep device I/O behind platform adapters. PipeWire/ALSA and a selected CV
  DAC/controller belong at the Linux boundary. Qt/QML is a UI client of the
  engine, not a dependency of it.
- Build one source model for x86-64 and ARM64, with scalar behavior as the
  reference. Architecture-specific kernels are optional, selected by supported
  build configuration or runtime dispatch, and must preserve the reference
  behavior.
- A sibling project's source can be inspected for ideas. Reimplement the
  desired behavior against ECO types and constraints; do not copy JUCE-bound
  classes or make the ECO runtime depend on that project. Track provenance and
  license for any algorithm or source that is intentionally reused.

## Native device contract

An ECO device is a host-independent instrument or effect with a versioned
descriptor: stable ID, ports, parameter schema, state format, and capabilities.
Its lifecycle and processing surface should cover prepare, reset, bounded event
delivery, parameter changes, and block processing. Processing receives
preallocated audio/event buffers and does not allocate, lock, perform I/O, or
destroy owned objects. Control-thread graph edits prepare replacement runtime
state; the audio thread adopts it at a defined boundary and retires old state
off-thread.

The initial C++ contract is `native/src/device.hpp`: stable descriptor, prepare
and reset lifecycle, bounded MIDI event delivery, and interleaved stereo block
processing into caller-owned memory. `LightDrumPadDevice`, `LightTb303Device`,
and `LightFm6Device` adapt the existing C DSP. TB-303 now also has a direct
note API and transport-independent render method; ECO's transport owns its
event timing. These adapters retain LIGHT's current 48 kHz limitation. Device
parameter metadata and persistent state serialization are not exposed yet.

Use the same device implementation in the native ECO graph and, where useful,
in a thin CLAP export wrapper. CLAP is an interoperability adapter, not the
native module ABI. Third-party CLAP hosting is a separate host feature with
its own lifecycle, ports, state, latency and failure-isolation requirements.

The DAW model should support tracks with ordered or branched device chains,
MIDI/audio/parameter lanes, and hardware destinations. Session clips and
arrangement refer to the same underlying events and transport. A modular patch
is a device graph whose topology is validated and prepared off the audio
thread; live edits must not mutate an active graph unsafely.

## Extraction rule

When adapting an existing instrument or workflow, preserve the musical behavior
and re-express it as portable ECO code. Keep UI, parameter widgets, persistence
adapters, plugin wrappers and host callbacks in their original host layer or
replace them with ECO adapters. Avoid carrying over JUCE container types,
`AudioBuffer`, `MidiBuffer`, APVTS, audio-device classes, or host-owned object
lifetime into engine code.

Before adopting a DSP module, make a deterministic offline render fixture and
compare important output and parameter behavior. Record intentional differences
when the portable implementation changes oversampling, denormal handling,
randomness, sample-rate behavior or latency.

## Suggested extraction order

1. Extend the device contract with parameter descriptors and versioned state,
   and route project changes to those parameter/state APIs.
2. Add one modular drum device or synth as a portable native implementation.
   Choose from product value; Box_Elder's multi-engine drum behavior and
   GLIESE_710's synth behavior are both candidates, while their JUCE wrappers
   remain outside ECO.
3. Add typed routing, device chains, parameter modulation and saved device
   state to the versioned project model.
4. Add graph patching as a constrained device feature after bounded graph
   processing, state migration and render fixtures are established.
5. Add CLAP export wrappers for ECO devices and then a conforming third-party
   CLAP host after the native graph supports their lifecycle needs.

This keeps x86-64 mini PCs, Raspberry Pi ARM64 builds and future supported
targets on one product architecture. Machine-specific image contents and
optional optimized kernels remain build/deployment choices, not product forks.
