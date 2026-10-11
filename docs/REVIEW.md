# Code review and improvement plan

Review date: 2026-10-10, against `main` at 717778a plus the test-harness fixes
in this change. Method: read the engine, device adapters, controller, audio
adapter and build/test tooling, and ran the portable test gate on Apple Silicon
macOS (Apple clang 15). Linux, Qt, PipeWire and ALSA code was read, not compiled
or run. Product ordering stays governed by [ROADMAP.md](ROADMAP.md); this
document ranks the concrete defects and gaps that block it.

## Verdict

The portable core is in good shape for a prototype: bounded queues, no render
allocations (guard test), LIGHT DSP behind `eco::NativeDevice`, versioned
project I/O, and honest docs. The Linux host has never been compiled in this
repository's history of validation, and the project's own claims about timing,
CI and sanitizer coverage were ahead of what the tooling actually verified.
Nothing here is a flashable or hardware-qualified artifact.

## Fixed in this change

| # | Problem | Fix |
|---|---|---|
| F1 | `tools/check-core.sh` failed to compile: `engine.hpp` includes `clap_host.hpp`, but the script lacked `-I native/third_party/clap/include`. The gate had been red since the CLAP host was added. | Include path added to every compile step. |
| F2 | `engine_test` segfaulted on startup. `eco::Engine` is ~1.3 MB (`Queue<Command,32>` embeds 32 × 40 KB `Project` copies) and the test put six on the stack. | Test engines are now `static`. |
| F3 | The test's replacement `operator new` (allocation guard) cannot coexist with ASan on macOS, so the guard and sanitizers were mutually exclusive. | Sanitized pass builds with `-DECO_NO_ALLOC_GUARD`; a second unsanitized pass runs the allocation guard. |
| F4 | Hosts with a broken sanitizer runtime (this one aborts on an empty `main`) failed every test with an unrelated ASan error. | Script probes the runtime and falls back with a loud warning instead of failing. |
| F6 | Float JSON I/O used `std::from_chars`/`to_chars`, unavailable in Apple clang 15 libc++ (finding 14). | Classic-locale stream parse and shortest-round-trip format; `project_io_test` passes. |
| F7 | `eco-project-io-tests` in CMake lacked the CLAP include path. | Added; `ctest` (engine, project_io, clap_host) passes on macOS. |
| F8 | Step grid and MIDI clips ran on two clocks (finding 1); tempo changes could split them. | Steps derive from the tick clock; BPM clamp applied to both; new tempo/tick lock test sweeps 45–240 BPM mid-bar. |
| F9 | No CI. | `.github/workflows/native.yml` added (unexecuted; see P0). |
| F5 | Docs claimed GitHub Actions workflows were provided. None exist. | Docs corrected; CI is plan item P1. |

## Open findings, ranked

### Correctness / timing

1. **(Fixed, F8.) Two sequencer clocks.** The step grid advances on `frame >= nextStep`
   (period computed at trigger time from the then-current BPM), while MIDI
   clips advance on `midiTickPosition` (integrated per sample with the current
   BPM). A tempo change mid-bar, or accumulated rounding, lets them diverge, so
   bar-quantized launches (step 0) and MIDI clip loops can disagree. Unify on
   the tick clock and derive steps from ticks.
2. **Whole-`Project` copies through the command queue.** Every toggle, volume
   or mute change copies 40 KB into a 1.3 MB queue; the audio thread copies it
   out (`item = items[r]`, plus the local `Command cmd`) on the render path.
   That is bounded but needlessly expensive in the callback and caps how large
   clips can grow. Replace with small delta commands or immutable snapshots
   with audio-thread adoption and control-thread reclamation.
3. **MIDI output is driven by a 1 ms `QTimer` on the GUI thread** using
   `event_output_direct`. A busy UI thread adds jitter to every outgoing note.
   Move to a dedicated sender thread and schedule on the ALSA queue with
   timestamps derived from engine frames.
4. **Clock anchor ignores output latency.** `Audio::publishClockAnchor` pairs
   the block-start frame with `pw_time.now` but does not account for
   `pw_time.delay`/buffered frames, and `frameForMonotonicNs` cancels the
   PipeWire term (`eventPwNs - pwNs == monoDelta`), so the PipeWire clock adds
   nothing. Either use it (rate-match, detect quantum/xrun discontinuities) or
   drop it. Needs loopback measurement either way.
5. **`NoteOff` for drums/legacy path is a no-op and FM legacy steps use a fixed
   150 ms gate** (`releaseIn = 7200` frames), independent of tempo. Fine for a
   prototype; incorrect for slow tempos or long steps.
6. **Pitch handling is inconsistent.** Instruments clamp to 24–96; MIDI output
   and clip storage use 0–127; capture stores raw pitch. Output and sound can
   disagree for out-of-range notes.
7. **Capture quantizes every note start to the current step** and promotes the
   legacy step pattern on first record. Intentional (documented) but it throws
   away the timing the scheduler now preserves.
8. **MIDI channels are stored and emitted but ignored by the built-in
   instruments.**

### Architecture

9. **`engine.hpp` includes `clap_host.hpp`** (and through it `<clap/clap.h>`
   and `dlfcn`), contradicting the boundary in
   [NATIVE-ENGINE.md](NATIVE-ENGINE.md): the engine must not depend on a plugin
   SDK. The plugin slot should sit behind `eco::NativeDevice` or a host-side
   adapter injected into the engine.
10. **`LightInstruments` hardcodes six tracks, track→instrument mapping and
    per-sample rendering** (`process(out, 1, gain)` through a virtual call per
    track per sample). Replace with a device-chain table and block rendering
    segmented at event boundaries (PLAN backlog item 4); benchmark on Pi first.
11. **Adapters retain LIGHT's 48 kHz-only limit and expose no parameter or state
    API.** Nothing saves instrument settings; presets live only in code.
12. **CLAP host lifecycle is wrong** (`start_processing`/`stop_processing` on the
    control thread; first plugin only; stereo assumed; no params/state). Already
    documented; keep it behind a "not supported" label until redesigned.
13. **`Controller` is a monolith** (project state, ALSA, capture, persistence,
    plugin loading). It cannot be tested without PipeWire/ALSA. UI.md §U1.1
    already plans the split.
14. **(Fixed, F6.) Project I/O uses `std::from_chars`/`std::to_chars` for `float`/`double`**,
    which Apple clang 15's libc++ does not provide, so `project_io_test` does not
    compile on this macOS toolchain. GCC 11+ on Linux is fine. Either require a
    newer toolchain, or fall back to `strtod` in the C locale.

### Process and coverage

15. **(Workflow written, not yet run.) No CI.** The only automated gate is a local script. The Linux-only code
    (`audio.hpp`, `controller.hpp`, `main.cpp`, QML) is never compiled by any
    check.
16. **`clap_host_test` is not part of `check-core.sh`**; only CMake/ctest runs
    it.
17. **No test of the MIDI scheduler path** (timestamp mapping, late events) or
    of ALSA/PipeWire code; no UI tests.
18. **No tempo/clock edge tests** for finding 1 (tempo change mid-bar).
19. **Yocto**: nothing has been built. `eco-workstation_0.1.bb` disables tests
    and uses a custom `ECO_ROOT` path; first real BitBake output will surface
    packaging errors.
20. **Docs drifted**: UI.md §1 still said step data cannot hold duration or
    polyphony (fixed in this change); VALIDATION.md predated the device
    adapters.

## Improvement plan

Ordered by what unblocks the roadmap. "Gate" is the observable exit condition.

### P0 — make verification real (days)

Status 2026-10-10: P0.1–P0.4 are implemented. The workflow file has not run on
GitHub, so Linux sanitizer results and the Qt host compile remain unverified
until the first CI run.

| Item | Gate |
|---|---|
| P0.1 Add Linux CI (GitHub Actions): `check-core.sh` with real ASan/UBSan on `ubuntu-24.04` and `ubuntu-24.04-arm`, scalar and NEON, plus `cmake -DECO_CORE_ONLY=ON && ctest`. | Green on both architectures; includes `project_io_test` and `clap_host_test`. |
| P0.2 Add a Linux job that builds the full Qt/PipeWire/ALSA target (`apt` Qt6, PipeWire, ALSA dev packages), compile-only, plus `tools/check-metadata.py`. | Host code compiles in CI. |
| P0.3 Float parse/format portability (finding 14). | `project_io_test` passes on Apple clang 15 and GCC. |
| P0.4 Add tempo-change and launch/MIDI-agreement tests (findings 1, 18). | Fails today, passes after P1.1. |

### P1 — timing and sequencer core (Roadmap phases A/B)

| Item | Gate |
|---|---|
| P1.1 Single tick clock; steps derived from ticks; tempo changes keep every consumer aligned. | P0.4 passes; block-size invariance retained. |
| P1.2 Delta commands + immutable project snapshots; shrink `Command` to a few dozen bytes; raise clip event cap. | `sizeof(Engine)` small enough to live on the stack; callback copy cost measured. |
| P1.3 Dedicated MIDI output thread with queue-scheduled timestamps; port selection. | Loopback jitter recorded on x86 and Pi. |
| P1.4 Latency-aware clock anchor; xrun and callback-time telemetry shown in UI. | Measured input→sound and MIDI-out offsets documented. |
| P1.5 Unclamped, consistent pitch model; channel routing to built-in instruments; tempo-aware FM gate. | Round-trip save/reload and external MIDI match internal sound. |
| P1.6 Unquantized capture option using tick position at event time. | Recorded timing matches played timing within one tick. |

### P2 — native device contract (Roadmap phase E)

| Item | Gate |
|---|---|
| P2.1 Parameter descriptors and versioned state on `NativeDevice`; save/restore in the project. | Project round-trip restores device settings; golden render fixtures. |
| P2.2 Device-chain table replacing `LightInstruments`; block rendering split at event boundaries; remove the 48 kHz-only restriction. | Benchmarked against current per-sample path on Pi 4/5 before adoption. |
| P2.3 Move the CLAP slot behind an adapter; remove `clap_host.hpp` from `engine.hpp`; then redesign lifecycle (audio-thread start/stop, params, state, ports). | CLAP validator and fixtures pass; plugin failure cannot corrupt the engine. |
| P2.4 One new owned instrument (Box_Elder drum or GLIESE_710 synth behavior, reimplemented). | Deterministic render fixture and provenance record. |

### P3 — host, UI and image (Roadmap phases A/G)

| Item | Gate |
|---|---|
| P3.1 Split `Controller` behind platform interfaces with a mock backend (UI.md §U1.1); QML offscreen tests. | UI iterates and tests on the dev host. |
| P3.2 First Linux build of the image (`x86-64.yml`), fix BitBake issues, record manifest and hashes. | Boots to the kiosk UI on the target mini PC; audio and MIDI visible. |
| P3.3 Hardware loopback and 60-minute soak on x86 and Pi (HARDWARE.md). | Measured latency, xruns, thermals published. |
| P3.4 Choose and spec the first CV/gate DAC (Roadmap phase C) before any CV code. | Written hardware profile with voltage range, calibration and protection. |

Deliberately not scheduled: arrangement view, audio recording, `.light`
importer and third-party plugin hosting. They depend on P1–P2 and stay in the
roadmap phases that already own them.
