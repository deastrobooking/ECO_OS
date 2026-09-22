# LIGHT integration audit

Reviewed repository: https://github.com/deastrobooking/LIGHT
Pinned source: `15f08cae2ea297c8906ffc2cd37bf5e4aa62c693`.

| Component | Finding | Integration decision |
|---|---|---|
| FM6 | C-only DSP, six operators/eight voices, hard-coded 44.1 kHz | Imported; common 48 kHz contract; two host instances, one piano and one bell preset. |
| TB-303 | Useful ladder filter/voice; own sequencer samples position per render call | Imported; host owns sample-clock note dispatch; internal sequencer not timing authority. Slide/accent editor remains future work. |
| Drum rack | Six sound types/sample capability; global noise generator | Imported; per-pad noise state; direct-pad API; first three drum voices exposed. Full 16-pad/sample UI later. |
| NEON primitives | ARM64 code with Mach-O section/underscore symbols | Adapted to preprocessed ELF/Mach-O assembly; scalar fallback; both implementations tested on host. Linux ELF assembly also compiled. |
| Audio host | miniaudio callback holds session mutex | Replaced with native PipeWire adapter and bounded SPSC commands; no callback mutex. |
| MIDI | CoreMIDI implementation despite README saying no MIDI | Linux ALSA sequencer input added; current control-thread polling is an MVP limitation. |
| Session/save | Human-readable `.light` parser and instrument serialization; docs disagree | Keep as reference for future validated importer; do not claim compatibility. Current new format is `.eco.json` v1. |
| UI | raylib, extensive 8×8 grid/instrument panels; shared-state assumptions | Not imported into the running host. Retain native Qt shell; port interactions after backend/model stabilization. |
| Bundled dependencies | Repo actually includes miniaudio, ARM macOS raylib libraries, objects and executable | No binaries or third-party library copies imported. Source-built dependencies come from Yocto recipes. |

This is a source-level component integration, not a Git history merge. The original
repository is unchanged. `native/light/upstream.json` records original source
hashes; `native/light/UPSTREAM.md` records local modifications. Imported source
and new code use MIT with the owner's explicit authorization. Qt, Linux,
PipeWire and other distributed packages retain their own licenses.

## Next reuse work

1. Define a unified project model supporting audio clips and timed note events.
2. Port decoded sample ownership and waveform generation to a worker thread;
   use immutable buffers with deferred reclamation on the control thread.
3. Implement a strict `.light` importer with bounds/range validation, relative
   asset paths and missing-sample reporting. Preserve existing `.light` files.
4. Reintroduce the original 8×8 workflow and instrument parameters through
   commands and atomic/snapshot telemetry, never direct RT-state access.
5. Test original LIGHT sessions against golden rendered references. The 48 kHz
   port intentionally differs from original 44.1 kHz rendering; compare timing,
   pitch and envelope duration, not bit identity between sample rates.
