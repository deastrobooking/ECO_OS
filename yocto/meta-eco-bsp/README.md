# Board integration boundary

Machine selection lives in kas; official BSP layers supply CPU tunes, firmware,
device trees, bootloaders, and kernels. This layer holds our board-specific
kernel/config changes. It must not contain instrument or application policy.

`recipes-kernel/linux/linux-yocto-rt_%.bbappend` adds audio prerequisites to the
optional QEMU RT qualification kernel only. It does not force an RT provider on
Pi/Qualcomm kernels. Physical-board RT support is pending board-specific kernel
selection, patch compatibility, kernel config audit, and measured latency tests.

ARMv7-A is a portability target (`qemu-armv7.yml`), ARM64 and x86-64 are primary
architectures. Cortex-M / AVR boards run separate controller firmware, not Yocto.
Do not set `-march=native`, globally append `neon` to TARGET_FPU, or force an
x86/QEMU kernel onto a vendor BSP. Use the machine's `DEFAULTTUNE`/`TUNE_FEATURES`.
