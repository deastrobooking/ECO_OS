# Validation record

Environment: Apple Silicon macOS, Apple Clang, 2026-09-21/22.

## Executed and passed

- `tools/check-core.sh`: C11/C++20 compilation with warnings as errors and
  AddressSanitizer/UndefinedBehaviorSanitizer; ARM64 NEON enabled.
- `ECO_FORCE_SCALAR=1 tools/check-core.sh`: same test suite using portable scalar
  mixer kernels on the host CPU.
- LIGHT FM6 produces finite audio and retires voices after note-off.
- Separate LIGHT drum instances produce independent deterministic noise.
- Mixer/peak/soft-clip scalar-versus-NEON equivalence, including zero and odd/tail
  lengths from 0 through 33.
- SPSC queue FIFO, overflow rejection, wraparound and drain behavior.
- Silent stopped state, finite bounded output, 120 BPM sample-clock progression,
  next-bar launch, in-bar pending launch, stop/mute behavior and block-size
  independence.
- Atomic project/clip restoration stops and clears the old session in one
  command.
- C++ allocation guard during render. C instrument render paths were reviewed
  for allocation calls; the guard does not intercept C malloc/calloc/free.
- `clang --target=aarch64-linux-gnu -c native/light/mix_neon.S`: produces an
  AArch64 ELF object. This validates assembly syntax/format, not Linux execution.
- `tools/render-demo.sh`: renders four bars of actual LIGHT instruments to a
  valid 48 kHz, 16-bit stereo WAV. Header/frame count and nonzero audio inspected.
- kas 4.8 official schema/include resolver: all ten configuration files validate;
  upstream revision strings are pinned; local layers/source/license references
  checked. Upstream dependency revisions were read with `git ls-remote`.
- Shell syntax checks for the session supervisor and local test/render helpers.

## Not executed / still required

- Full native Qt/PipeWire/ALSA compilation and GUI runtime on Linux.
- UI-specific coverage (QML component tests, offscreen rendering, touch layouts).
  The current UI is a single-file developer preview; its test strategy depends
  on the view-model/platform split planned in docs/UI.md §U1.
- Native project save/load round-trip integration tests (Qt dependency required).
- BitBake parse, dependency compilation, image generation or SDK generation.
- Any x86-64, Pi, Qualcomm or ARMv7 target execution. Scalar tests here run on
  ARM64; scalar source portability is not proof of x86 hardware validation.
- Real audio-interface playback, ALSA MIDI routing, USB hotplug and controller
  firmware compilation/flashing.
- PREEMPT_RT hardware configuration, real-time privilege verification, callback
  latency distribution, xrun soak or physical round-trip measurement.
- GitHub Actions workflows. They are provided, not remotely submitted here.
- C-versus-Rust performance comparison. No Rust implementation was introduced.

This repository has no flashable artifact yet. The next validation gate is a
Linux x86 Yocto build and the user's first hardware boot, following BUILD.md and
HARDWARE.md. A passing metadata check must not be reported as a passing image
build. Production update/recovery and RT qualification remain explicit milestones.
