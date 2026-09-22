# Hardware support and acceptance

All physical-board statuses below are **not yet tested**. A supplied manifest
is a build target, not a claim that an image compiled or booted.

| Target | Architecture | Role | Outstanding |
|---|---|---|---|
| User's x86 PC | x86-64 | First integration/reference test | Exact CPU/GPU, firmware and USB interface; build and boot |
| Raspberry Pi 4 | AArch64 | Low-cost ARM reference | BSP image, KMS display, USB, storage and thermal/audio soak |
| Raspberry Pi 5 | AArch64 | Higher-performance ARM reference | Same; validate board kernel/firmware and cooling |
| QEMU ARM64 / ARMv7 | AArch64 / 32-bit ARM | Build/portability exercises | Not latency measurements or physical-board certification |
| Qualcomm upstream-listed boards | AArch64 | Experimental integration | Select exact board, firmware, boot method and test peripherals |
| Snapdragon X/X2 / ASUS QN10 | AArch64 | Research/bring-up roadmap | Compatible BSP/kernel/DT/firmware; no qualified machine here |
| NXP i.MX / Rockchip | ARM variants | Future BSP extension | Exact board + matching official/community BSP layer needed |
| Leonardo / Micro | AVR controller | USB MIDI buttons/knob | Compile/flash firmware and verify routing; not a Linux host |
| Other Arduino/ARM MCUs | Cortex-M etc. | Future controller targets | Board-specific USB implementation; do not use Linux images |

## First x86 acceptance pass

- Record CPU, motherboard/mini-PC, firmware version, GPU, RAM, USB topology,
  interface VID:PID/firmware, MIDI controller, storage, image hash and Git revision.
- Boot removable test media and reach the native workstation without a desktop
  launch step. Retain boot logs and `systemd-analyze` output.
- Verify correct USB output and 48 kHz stream; play all integrated LIGHT sounds.
- Launch a different clip/scene mid-bar and verify transition at the next bar.
- Edit steps, levels and mute; exercise MIDI/Arduino controls and step capture.
- Save, restart application, reload; then power-cycle and reload the saved project.
- Unplug/replug audio and MIDI separately, document current recovery behavior.
- Check service restart paths and malformed project rejection.

## Real-time qualification (separate from boot acceptance)

1. Select the correct board kernel, RT patch level and firmware. Audit final
   kernel configuration; record `/sys/kernel/realtime` where supplied.
2. Confirm the audio thread actually receives intended scheduling privileges;
   service limits alone do not prove real-time scheduling is active.
3. Benchmark DSP callback times at 48 kHz with all tracks, UI activity, MIDI,
   storage I/O and network load. Record workload and thermal/power settings.
4. Run 60 minutes at 256 frames, then 128. Investigate every xrun; move to 64
   only when the exact hardware/interface combination passes.
5. Measure actual round-trip latency using physical output-to-input loopback and
   a measurement tool. This MVP is playback-only, so use an external tool until
   capture is implemented. Quantum duration is not round-trip latency.
6. Repeat under sustained temperature, cold boot and realistic controller load.

CPU pinning/isolation, IRQ affinity and aggressive power tuning are experimental
per-machine changes. Establish baseline evidence before applying them. Do not
publish universal low-latency claims from QEMU, browser clocks or an idle render.

For plugins, match ELF architecture and ABI to the image. x86 plugins do not
run natively in an ARM64 host. Binary translation, Windows VST bridges and NPU
processing are outside the MVP real-time path.
