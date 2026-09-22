# Contributing

Use the native/Yocto path. Do not reintroduce a browser runtime without an
explicit architecture decision. Read docs/PLAN.md and native/light/UPSTREAM.md.

- Core DSP: C11 / C++20, no CPU-specific flags in common distro policy.
- RT callback: no allocation/deallocation, filesystem, UI, logging or blocking
  locks. Preallocate on the control thread; defer destruction out of RT.
- Queue: exactly one control-thread producer and one audio-thread consumer.
  Marshal future MIDI/network threads into the control path or use a separately
  designed queue. Bound work per callback and define overflow behavior.
- SIMD: preserve a scalar reference and test zero/odd/tail lengths. Do not
  assume ARM64 means Apple or that x86-64 implies AVX2.
- BSP changes: official machine tune/provider first. Keep board overrides in
  meta-eco-bsp and provide build/boot evidence for support claims.
- Source imports: retain origin revision, original hashes and modification notes.
- Tests: run tools/check-core.sh, scalar mode, metadata checks and Linux native
  build for relevant changes. Do not claim tests that only exist in CI have run.
- Format C/C++ with the repository .clang-format. New source uses MIT SPDX tags.

Hardware reports should follow docs/HARDWARE.md and include image/Git revisions,
exact board/interface, logs and reproducible workload. No credentials or private
project recordings are needed for bug reports.
