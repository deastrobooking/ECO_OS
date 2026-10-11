# Music OS implementation plan

Decision record: 2026-10-10 update. User direction: low-level groovebox and DAW
for modern x86-64 mini PCs and Raspberry Pi class systems, with MIDI and CV
sequencing first, owned native plugins, then CLAP. The prior Yocto, x86-64-first,
ARM64-first-class, LIGHT reuse and open-source direction remains. The current
prototype is not yet feature-comparable with Ableton Live. See the
[product roadmap](ROADMAP.md) for the MIDI/CV-first delivery sequence and current
code audit; that document now governs product ordering.

## Recommended stack

| Layer | Choice | Why / boundaries |
|---|---|---|
| Distribution | Yocto/OpenEmbedded, custom ECO distro | Explicit image contents and per-board BSP selection. No desktop distro stage. |
| Reproducibility | kas 4.8, exact layer commit IDs | Reviewed dependency updates; source provenance and license checksums. Root ECO source is the local checkout and must also be committed/tagged for a release. |
| Current baseline | Scarthgap LTS + open-source Qt 6.8.3 layer | Verified compatible BSP/Qt layers and fetchable commits. Upgrade as one tested layer set; not a mixture of release branches. |
| Hardware | Official machine/BSP tunes | x86-64, AArch64, separate ARMv7 test path. No global NEON/AVX flags. |
| Kernel | BSP kernel for bring-up; qualified PREEMPT_RT per board | QEMU RT overlay supplied. Physical-board RT selection/patches require work; RT is a release gate, not an unverified label. |
| Init | systemd | Supervised services, per-process limits, diagnostics and restart behavior. |
| Display | Weston kiosk / Wayland + Qt Quick | Native touch-capable interface, C++ integration; no desktop shell. |
| Audio | PipeWire + WirePlumber + ALSA | Native PipeWire stream; JACK, ALSA and Pulse compatibility packages. One owned audio session. |
| DSP | LIGHT C11 instruments + C++20 host | Preserve useful synthesis; bounded commands; audio-thread ownership. |
| Native instruments | ECO-owned portable device modules | Adapt musical behavior from workspace projects; keep JUCE/Qt/ALSA/PipeWire and plugin SDKs out of the engine. See [native engine boundary](NATIVE-ENGINE.md). |
| SIMD | LIGHT AArch64 NEON + scalar reference | Source-portable fallback on x86/ARMv7. Add optional AVX2 or ARMv7 NEON only behind equivalence tests and CPU dispatch. |
| MIDI | ALSA sequencer initially | MIDI 1.0 input/output; fixed-capacity polyphonic clips, absolute input timestamp-to-frame scheduling, and a sequencer output port are implemented, pending Linux hardware validation. |
| Projects | Versioned JSON + Qt QSaveFile | Strictly validated, atomically replaced local file. No database in the callback. |
| Plugins | Native ECO devices first; CLAP adapters and host afterward | ECO devices run in the owned graph; CLAP export is a thin interoperability wrapper. Third-party plugin hosting follows after lifecycle, graph, state and failure handling are sound. Each binary matches host ISA/ABI. |
| Updates | RAUC A/B after hardware/image acceptance | Not implemented in this developer image. Needs board bootloader integration and a persistent data layout. |

## Architecture

```mermaid
flowchart TB
  BSP[Official BSP: x86 / Pi / Qualcomm] --> Kernel[Board kernel + qualified RT configuration]
  Kernel --> ALSA[ALSA audio and MIDI]
  ALSA --> PW[PipeWire + WirePlumber]
  PW --> DSP[Native engine + LIGHT instruments]
  UI[Qt Quick on Weston kiosk] --> Commands[Bounded command queue]
  MIDI[ALSA MIDI control path] --> Commands
  DSP --> MidiEvents[Bounded MIDI output queue]
  MidiEvents --> ALSA
  Commands --> CV[CV/Gate device interface]
  Commands --> DSP
  CV --> Hardware[External DAC / controller]
  DSP --> Telemetry[Atomic UI telemetry]
  Telemetry --> UI
  UI --> Projects[Validated project files / atomic save]
```

`meta-eco-core` contains only reusable distro/application/audio policy.
`meta-eco-bsp` contains ECO's board/kernel additions; machine BSP layers own
bootloaders, device trees, firmware and tuning. kas composes those layers.
This is the proposed `meta-musician-core` / `meta-musician-bsp` separation under
this repository's ECO naming.

## Corrections to the source notes

- PREEMPT_RT reduces scheduler latency. Firmware stalls, IRQs, USB scheduling,
  power management and drivers still affect deadlines. A kernel option alone
  does not establish hard real-time guarantees.
- The variable is `TUNE_FEATURES`, not `TUNED_FEATURES`. Do not append `neon` to
  `TARGET_FPU`. Use an official CPU tune; Cortex-M/AVR are not Linux targets.
- ARMv7 hard-float is a sensible ABI choice on compatible hardware, not a
  mathematical requirement for real-time DSP. NEON/VFP variants must match the
  actual CPU. AArch64 and 32-bit ARM need separate binaries and BSPs.
- Crypto extensions accelerate relevant crypto code, not arbitrary audio DSP.
  An NPU has no MVP role in the audio callback.
- `64/48000 = 1.333 ms` is one audio quantum, not round-trip latency. Start at
  256 frames, qualify 128, then 64. Measure the interface with physical loopback.
- Do not universally override `virtual/kernel` with `linux-yocto-rt`. Vendor
  kernels/BSPs may require a different RT patch set and board configuration.
- PipeWire's Pulse compatibility server does not require shipping a second
  standalone PulseAudio daemon. The image starts one owned PipeWire session.
- Architecture-neutral source does not mean any ARM device boots the same image.
  Require a specific machine, device tree/firmware set and tested audio interface.

## LIGHT reuse

Integrated now: FM6, TB-303 synthesis, drum DSP and ARM64 mixer. They sit behind a
new command-driven host; macOS CoreMIDI, Mach-O-only assumptions, global noise
state and the upstream callback mutex are not inherited into the Linux host.

The original 8×8 sample launcher, waveform loader, raylib instrument panels,
full parameter persistence and `.light` session model are valuable next ports.
The current 6×4 MIDI-step workstation is a smaller first native integration,
not a feature-complete replacement for LIGHT. See LIGHT-INTEGRATION.md.

## Milestones and release gates

| Stage | Deliverable | Exit test |
|---|---|---|
| M0: source foundation (this change) | Layers, pinned manifests, native host, integrated/tested LIGHT DSP, controller source | Core sanitizer/scalar/NEON tests; kas metadata validation. Full native build remains pending. |
| M1: first x86 developer image | Build on Linux, boot removable media, fullscreen UI, USB audio + MIDI, save/reload | No manual desktop launch; audio and MIDI ports work; saved project survives restart/power cycle. |
| M2: ARM64 parity | Pi 4/5 image and identical project/audio semantics; ARM64 CI | Hardware boot, display, USB, storage, MIDI and deterministic DSP tests on an actual Pi. |
| M3: real-time qualification | Board-specific RT kernel, preallocated graph, timestamped MIDI, measured xruns | 60-minute stress/recorded latency run at 48 kHz/128 frames; 64-frame mode offered only on passing combinations. |
| M4: preserve LIGHT workflow | Audio clips/samples, 8×8 view, FM/TB/drum editors, portable `.light` importer | Golden sessions, missing-asset recovery, sample-rate correctness, zero callback locks/allocations. |
| M5: developer plugin SDK | One native CLAP synth; LV2/VST3 later | Scan/crash isolation; killed worker cannot terminate transport; saved state reloads. |
| M6: release appliance | Signed A/B updates, persistent projects, recovery environment | Power-cut update tests, automatic rollback, user data preserved and compatibility matrix published. |

Qualcomm X/X2, i.MX and Rockchip get dedicated bring-up tracks after an exact
board is selected. The current experimental Qualcomm manifest targets only
boards named in the pinned `qcom-armv8a` BSP; it does **not** certify Snapdragon
X/X2 or ASUS QN10. A full Wrynose migration needs mutually compatible core,
Qt, Pi and Qualcomm revisions before it replaces this baseline.

## Immediate developer backlog

The ranked defect list and the P0–P3 improvement plan from the 2026-10-10 code
review are in [REVIEW.md](REVIEW.md); it refines the items below.

1. **MIDI/CV sequencing is the product-critical next feature.** The first code
   slice now adds fixed-capacity polyphonic note clips and tick-based playback;
   armed MIDI capture preserves pitch, channel, velocity, and note duration,
   with starts quantized to the active step. Absolute ALSA timestamps now map
   through a PipeWire/monotonic clock anchor into a fixed audio-thread event
   scheduler; validate alignment and late-event behavior on hardware. MIDI
   output now uses a bounded audio-to-control queue and ALSA output port;
   measure external timing and add scheduled output/port routing. Project
   changes now travel as immutable snapshots adopted by pointer swap.
   Design the CV device interface
   alongside this work and choose the first
   external DAC/controller before claiming CV hardware support. Details and
   gates are in [ROADMAP.md](ROADMAP.md).
2. Build the x86 image and Qt host on Linux; fix parse/package/build issues from
   actual BitBake output. Record the full manifest and artifact checksums.
3. Test the user's x86 machine with a class-compliant USB interface. Gather boot
   logs, `pw-dump`, `pw-top`, ALSA/MIDI listings and exact hardware identifiers.
4. Split DSP into event-aligned blocks and cache coefficients. Current per-sample
   LIGHT wrappers prioritize correctness; benchmark before low-buffer claims.
   `tools/bench-render.sh` measures current per-sample throughput at
   256/128/64-frame block sizes (~25x realtime, flat across block sizes, on
   Apple M-series arm64) — a developer host signal, not board qualification.
   Re-run on target boards before deciding block segmentation is warranted.
5. Add project model serialization tests on Linux and then `.light` import with
   relative assets. Add sample ownership/reclamation without callback frees.
6. Keep plugins behind sequencing/audio foundations. The current CLAP wrapper
   is an experiment: its processing lifecycle calls currently violate CLAP's
   audio-thread requirements, and port negotiation/state/parameters are absent.
   Design the owned ECO plugin ABI first; make CLAP conformant after those
   lifecycle and audio graph boundaries are in place.
7. UI work is planned and sequenced in docs/UI.md. First item (U1.1, not gated on
   hardware): split `Controller` behind platform interfaces with a mock backend so
   QML can be iterated and tested on the macOS/Linux dev host without
   PipeWire/ALSA. UI additions follow the milestone dependencies listed there,
   not ad-hoc.
8. Use [NATIVE-ENGINE.md](NATIVE-ENGINE.md) when adapting sibling workspace
   projects: port the musical behavior into portable ECO modules, with no JUCE
   dependency in the native engine. Start by fitting LIGHT to the device
   contract, then select one drum instrument or synth for extraction.

## Research basis

Primary sources reviewed during implementation:

- [Yocto custom distributions](https://docs.yoctoproject.org/dev-manual/custom-distribution.html): own distro layer and configuration.
- [Yocto release policy](https://www.yoctoproject.org/development/releases/): Scarthgap LTS support; migration planning.
- [Yocto tuning variables](https://docs.yoctoproject.org/ref-manual/variables.html#term-TUNE_FEATURES): machine/ABI-specific tuning.
- [Kernel RT theory](https://docs.kernel.org/core-api/real-time/theory.html) and [hardware limits](https://docs.kernel.org/core-api/real-time/hardware.html): scheduling improvement and hardware qualification.
- [PipeWire configuration](https://docs.pipewire.org/page_man_pipewire-jack_conf_5.html): latency requests and graph settings.
- [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html) and [meta-qt6](https://doc.qt.io/Boot2Qt-6.8/b2qt-meta-qt6.html): native interface and Yocto integration. Use open-source Qt modules; do not silently select commercial LTS builds.
- [Weston](https://wayland.pages.freedesktop.org/weston/): minimal compositor and kiosk operation.
- [meta-raspberrypi](https://github.com/agherzan/meta-raspberrypi/tree/scarthgap) and [meta-qcom](https://github.com/qualcomm-linux/meta-qcom/tree/scarthgap): actual layer/machine coverage.
- [LIGHT](https://github.com/deastrobooking/LIGHT/tree/15f08cae2ea297c8906ffc2cd37bf5e4aa62c693): source inspected locally; README/AGENT inconsistencies resolved by reading implementation.
- [CLAP](https://github.com/free-audio/clap), [LV2](https://lv2plug.in/), [VST3 SDK](https://github.com/steinbergmedia/vst3sdk): native plugin roadmap. Not currently hosted.
- [RAUC](https://rauc.readthedocs.io/en/latest/basic.html): future signed update/slot integration.
- [Arduino MIDIUSB](https://github.com/arduino-libraries/MIDIUSB): native USB MIDI controller firmware.
