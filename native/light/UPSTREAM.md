# LIGHT source integration

Origin: https://github.com/deastrobooking/LIGHT
Commit: `15f08cae2ea297c8906ffc2cd37bf5e4aa62c693`
Imported: 2026-09-21. Project owner explicitly selected MIT for LIGHT source and
new ECO source during this integration. See the repository LICENSE.

Imported components: `fm_synth.c/h`, `tb303.c/h`, `drum_rack.c/h`, `mix_neon.h`,
and `mix_neon.s` (now `mix_neon.S`). No compiled objects, macOS executables,
prebuilt raylib libraries, or miniaudio header were imported.

Local adaptations:

- Common 48 kHz compile-time DSP contract (`light_config.h`).
- TB-303 glide coefficient adjusted for 48 kHz.
- Drum noise moved from global mutable state into each pad.
- Direct drum-pad renderer added so host sequencing owns the sample clock.
- Assembly uses preprocessed ELF/Mach-O sections and symbol conventions.
- Portable scalar implementations share the existing mixer ABI.
- C++ wrapper preallocates instruments before audio starts. UI/MIDI commands go
  through a bounded SPSC queue; instrument state stays on the audio thread.
- Native host currently exposes three drum voices, TB-303, FM6 E.Piano and FM6
  Bell. Imported DSP includes more presets/pads; full parameter editors and
  sample loading are not yet exposed.
- Host sequencer drives TB-303 note changes; the original internal TB sequencer
  API remains available but is not the timing authority in this host.

The upstream raylib UI, clip loader, `.light` parser, CoreMIDI host and miniaudio
callback are **not** integrated. They need separate ports and safety work. The
original session guide calls some non-atomic concurrent accesses “benign”; the
new engine uses atomic telemetry instead. Existing `.light` projects are not yet
loadable; the developer workstation uses a versioned `.eco.json` format.
