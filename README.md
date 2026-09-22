# ECO Music OS · powered by LIGHT

A Yocto-first, open-source music workstation for **x86-64 and ARM64**, with a
portable ARMv7 path and separate Arduino controller firmware. MIT licensed.
There is no web application and no interim desktop-distribution stage.

This is a **developer foundation**, not a tested flashable OS release. The native
DSP runs locally and passes its tests. Yocto configurations and a Qt/PipeWire
application are supplied; image compilation, native GUI integration, hardware
boot and latency qualification still need a Linux build host and target boards.

## What is here

- LIGHT FM6, TB-303 and drum synthesis, integrated into a bounded-command native
  audio engine at 48 kHz. ARM64 NEON mixer plus scalar fallback.
- A fullscreen Qt Quick workstation: six tracks, four clip scenes, sixteen-step
  patterns, bar-quantized launches, levels/mutes, ALSA MIDI, step capture, atomic
  project save/reload. Built-in sounds work without external samples/plugins.
- Architecture-neutral `meta-eco-core`, hardware integration `meta-eco-bsp`,
  pinned kas manifests for x86-64, Pi 4/5, QEMU ARM64/ARMv7 and experimental QCOM.
- Weston kiosk and supervised PipeWire/WirePlumber/application services.
- Leonardo/Micro USB MIDI controller starter firmware.

Read the [game plan](docs/PLAN.md), [build guide](docs/BUILD.md),
[LIGHT integration audit](docs/LIGHT-INTEGRATION.md), and
[hardware acceptance checklist](docs/HARDWARE.md).

## Run the portable engine today

On macOS or Linux with a C/C++ compiler:

```sh
./tools/check-core.sh
ECO_FORCE_SCALAR=1 ./tools/check-core.sh
./tools/render-demo.sh /tmp/eco-demo.wav
```

The renderer creates four bars of the integrated LIGHT instruments as a 48 kHz
stereo WAV. It refuses to overwrite an existing output. It does not require Qt,
PipeWire, Python packages or a network connection.

## Build the Yocto developer image

On a supported Linux build host with kas 4.8 and Yocto prerequisites installed:

```sh
kas build yocto/kas/x86-64.yml
# Use a separate build directory for each architecture:
KAS_BUILD_DIR="$PWD/build-rpi5" kas build yocto/kas/rpi5.yml
```

See [BUILD.md](docs/BUILD.md) before flashing. No device is flashed automatically.
The developer image deliberately enables local console root access, has no SSH
server, and is not a production image.

## Repository

```text
native/               Qt/PipeWire host, sample-clock engine, tests, WAV renderer
  light/              adapted LIGHT DSP + upstream revision and source hashes
yocto/kas/            pinned dependency sets and per-target build manifests
yocto/meta-eco-core/  shared distro, image, application and audio session policy
yocto/meta-eco-bsp/   board/kernel integration boundary
firmware/            USB MIDI controller firmware
tools/               portable DSP and metadata checks
docs/                decisions, plan, build and hardware validation
```

The existing LIGHT `.light` session format, original raylib interface, sample
clip loader, audio recording, plugin hosting and full instrument editors are
not ported yet. See [VALIDATION.md](docs/VALIDATION.md) for exactly what was tested.
