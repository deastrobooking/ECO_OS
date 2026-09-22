# Developer builds

## Prerequisites and status

Yocto/BitBake builds run on Linux. The repository was prepared on macOS; no Linux
container runtime or target board was available. No `.wic` image has been built
or booted yet. Use a supported Linux host (for example Ubuntu 24.04) or a Linux
VM with native Linux storage. This is a build-host choice, not the target OS.

Budget at least 200–300 GB free disk and 16 GB RAM for an initial Qt-enabled
image; 32 GB and more storage make multi-machine builds easier. These are
planning estimates, not measured ECO image requirements. Install the host
packages listed by [Yocto's Scarthgap quick build guide](https://docs.yoctoproject.org/scarthgap/brief-yoctoprojectqs/index.html).
Use a normal unprivileged user and a case-sensitive filesystem.

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install 'kas==4.8'
python tools/check-metadata.py
```

kas checks here use its actual schema/include resolver without downloading
layers. Passing this check does not mean BitBake can build the image.

## Build targets

Run from the repository root:

| Target | Command | Current status |
|---|---|---|
| x86-64 PC | `KAS_BUILD_DIR="$PWD/build-x86" kas build yocto/kas/x86-64.yml` | First physical test target; image unbuilt |
| Pi 4 ARM64 | `KAS_BUILD_DIR="$PWD/build-rpi4" kas build yocto/kas/rpi4.yml` | BSP configured; hardware untested |
| Pi 5 ARM64 | `KAS_BUILD_DIR="$PWD/build-rpi5" kas build yocto/kas/rpi5.yml` | BSP configured; hardware untested |
| QEMU x86 | `KAS_BUILD_DIR="$PWD/build-qemu" kas build yocto/kas/qemu-x86-64.yml` | Emulated integration target |
| QEMU x86 RT | `KAS_BUILD_DIR="$PWD/build-rt" kas build yocto/kas/qemu-rt.yml` | Explicit RT kernel experiment |
| QEMU ARM64 | `KAS_BUILD_DIR="$PWD/build-arm64" kas build yocto/kas/qemu-arm64.yml` | ISA/application portability |
| QEMU ARMv7 | `KAS_BUILD_DIR="$PWD/build-armv7" kas build yocto/kas/qemu-armv7.yml` | 32-bit portability; not a supported physical board |
| Qualcomm ARM64 | `KAS_BUILD_DIR="$PWD/build-qcom" kas build yocto/kas/qcom-arm64.yml` | Experimental; only upstream-listed boards; no X/X2 certification |

The initial target is `eco-image-dev`. Application source is picked up from
`native/` by the local recipe; no GitHub push is required to build changes.
Layers are checked out under `layers/`; per-machine build directories are ignored
by Git. kas pins upstream layer commits, but a release must pin the ECO repository
commit too. Retain image manifests, buildhistory and artifact hashes.

For a metadata parse before compilation:

```sh
KAS_BUILD_DIR="$PWD/build-x86" kas shell yocto/kas/x86-64.yml -c 'bitbake -p'
```

Use `bitbake eco-workstation -c compile -f` inside the kas shell to isolate native
build failures. Build an SDK with `bitbake eco-image-dev -c populate_sdk` after
the image builds; install/source that SDK to iterate on the Qt/PipeWire host.

The x86 artifacts should appear in `build-x86/tmp/deploy/images/genericx86-64/`.
Pi artifacts use their machine directories and compressed WIC output. Choose a
spare/removable test device and verify its identity before flashing with a tool
such as bmaptool. No destructive flashing command is embedded in this repository.
The experimental Qualcomm BSP has board-specific boot requirements; follow the
BSP documentation rather than treating its output as a universal PC image.

## Native application build without a full image

A Linux contributor can compile against distro-provided development libraries;
this is only a host-side development convenience. Production target packages
are built by Yocto.

Dependencies: C/C++20 compiler, CMake ≥3.21, Qt ≥6.5 Quick/QuickControls2 and QML
runtime modules, pkg-config, PipeWire headers, ALSA headers, pthreads.

```sh
cmake -S native -B build-native -DCMAKE_BUILD_TYPE=Debug
cmake --build build-native
ctest --test-dir build-native --output-on-failure
./build-native/eco-workstation
```

The runtime needs a working PipeWire/WirePlumber session and a Wayland/X11
platform integration on the contributor host. The image runs fullscreen on
Weston. Escape returns the UI to windowed mode; Space toggles transport;
Ctrl+S saves. Missing audio is shown in the status area.

To build only the portable DSP, tests and WAV renderer on macOS/Linux:

```sh
cmake -S native -B build-core -DECO_CORE_ONLY=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
./build-core/eco-render /tmp/eco-demo.wav
```

The shell helpers in the root README also work without CMake.

## Boot/session configuration

`weston-init` starts the compositor as `weston`. `eco-session.service` owns a
private `/run/eco` runtime directory and uses the upstream Weston socket at
`/run/wayland-0`. A private D-Bus session supervises PipeWire, WirePlumber,
PipeWire-Pulse and the native application. If any child exits, systemd restarts
the owned session. Audio runs under the non-root `weston` user with explicit
RT-priority/memory-lock limits; the image does not run a desktop environment.

Project path on the image:

```text
/home/weston/.local/share/ECO/eco-workstation/session.eco.json
```

Qt's standard application-data location determines the precise path; the UI
prints it after save. Reload is explicit. The current root filesystem is
writable; A/B layout, a dedicated persistent data partition and power-loss-safe
hardware qualification remain future work.

Local root console access is enabled by `debug-tweaks` for bring-up. No SSH
server is installed. Remove development access and complete update/recovery
policy before creating a production image.

## Audio and MIDI bring-up

From the root console:

```sh
systemctl status weston eco-session
journalctl -b -u weston -u eco-session
aplay -l
aconnect -l
```

Run audio-session diagnostics as its owner:

```sh
su -s /bin/sh weston -c 'XDG_RUNTIME_DIR=/run/eco PIPEWIRE_RUNTIME_DIR=/run/eco wpctl status'
su -s /bin/sh weston -c 'XDG_RUNTIME_DIR=/run/eco PIPEWIRE_RUNTIME_DIR=/run/eco pw-top'
```

If HDMI is chosen instead of the USB interface, use `wpctl set-default NODE_ID`
in that same user/runtime environment, then restart the stream. The current UI
does not expose a device selector. MIDI routing is explicit: list ports with
`aconnect -l`, then connect the controller's numeric source port to the
`ECO Controller:Input` destination. Arm capture and play notes. Capture currently
writes the current sixteenth step; it does not record note duration or provide
sample-accurate live-MIDI timestamps.

Default requested graph quantum is 256 at 48 kHz. Test 128/64 using a temporary
PipeWire metadata force-quantum setting in the owned session, observe the actual
quantum in `pw-top`, and restore automatic policy afterwards. Do not label a
configuration “64-frame certified” merely because a request was accepted.

## CI

`.github/workflows/native.yml` describes Linux native compilation/QML startup,
x86 scalar tests, ARM64 scalar/NEON tests and kas metadata checks. It has been
written but not executed on GitHub in this session. Full Yocto builds belong on
an adequately sized Linux runner; this change does not submit remote jobs.
