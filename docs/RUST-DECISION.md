# Rust migration decision

Status: evaluate a bounded experiment; **no full rewrite authorized or started**.
Context: owner asked about full migration and a reported 13% C speed advantage.

## Recommendation

Keep LIGHT's C11 instruments and tested ARM64/scalar mixer. Ship the first
Yocto-native developer build with the current C++20 host. Consider Rust for new
project/asset services and, if a measured experiment justifies it, graph ownership,
event queues and the render-plan/control boundary.

There is no general law that optimized C runs 13% faster than optimized Rust.
That number needs a named benchmark, hardware, compiler versions, flags, data,
algorithm, numerical tolerances and definition of “faster”. Both can produce
excellent native code; optimization, bounds checks, aliasing, data layout,
vectorization and allocation patterns can change the result in either direction.
We have not benchmarked C versus Rust here and make no percentage claim.

Rust has no required tracing garbage collector. Its ownership and type system
help with lifetimes, aliasing and safe concurrency, but safe Rust can still
allocate, lock, perform I/O and panic. A destructor can deallocate or acquire
resources; dropping the final Arc on the callback is not automatically RT-safe.
FFI and unsafe code still need audit. Type safety is not a proof of bounded
execution time or freedom from deadline misses.

## Component choice

| Component | Present choice | Rust evaluation |
|---|---|---|
| Existing FM6/TB-303/drum DSP | Reused C | Keep; translate only with golden audio tests and a concrete benefit |
| NEON kernels/scalar reference | Assembly/C | Keep through C ABI; no reason to rewrite audited kernels for branding |
| Render ownership/command queue | C++20, bounded SPSC | Good small comparison candidate; Rust producer/consumer ownership could improve API misuse prevention |
| Graph and asset lifecycle | Not yet generalized | Strong candidate for Rust before this subsystem grows |
| Project I/O and system services | Qt/C++ MVP | Rust can improve isolation and maintainability; evaluate deployment cost |
| Qt Quick UI | QML/C++ bridge | A Rust-only rewrite adds a UI binding/framework decision without helping DSP latency |
| Linux/ALSA/PipeWire/driver ecosystem | Existing native projects | No proposed language migration |

The imported host already removes the original session callback mutex and uses
atomic telemetry. Comparing a new Rust queue to the old blocking C host would
measure architecture changes, not language overhead. Keep the same DSP,
preallocation, queue capacity and scheduling policy in any experiment.

## Experiment before migration

1. Freeze native C/C++ baseline and golden project/audio outputs.
2. Define one small C ABI for fixed-size commands, preallocated buffers and
   immutable render plans. Specify ownership, destruction, thread roles, error
   behavior and panic/exception boundaries explicitly.
3. Implement the equivalent Rust control/queue boundary with non-cloneable
   producer/consumer handles. Do not place Tokio, blocking channels, logging or
   heap-backed per-event work on the callback path.
4. Test overflow, wraparound, stale handles, shutdown, graph replacement,
   deferred reclamation, malformed projects and DSP numerical equivalence.
5. On the same x86 and ARM64 boards, compare release builds at 48 kHz and
   256/128/64 frames: callback distributions, observed maxima, p99.9/p99.99,
   xruns, memory and UI/control load. Long-run high percentiles need enough
   observations and are not formal worst-case guarantees.
6. Adopt the Rust boundary if it gives a clearer audited ownership model with
   acceptable deadline headroom and Yocto/toolchain maintenance cost. Expand
   component by component; retain C DSP until measurements justify otherwise.

A 64-frame/48-kHz quantum is about 1.333 ms. Average throughput improvements do
not establish a lower usable buffer setting: scheduler tails, USB/driver
behavior, page faults and plugin stalls still determine whether deadlines hold.
Even if one benchmark favors C, a small difference in a tiny orchestration
fraction does not translate to the same percentage for the entire render graph.

## Sources

- [Rust language overview](https://rust-lang.org/): native performance, no required runtime or garbage collector.
- [Rust's zero-cost abstraction design](https://blog.rust-lang.org/2015/05/11/traits/): design goal, not a universal benchmark result.
- [Rust Performance Book: profiling](https://nnethercote.github.io/perf-book/profiling.html): measure actual workloads before optimizing.
- [Rustonomicon: safe/unsafe interaction](https://doc.rust-lang.org/stable/nomicon/safe-unsafe-meaning.html): limits of safe wrappers and FFI verification.
- [Rustonomicon: FFI](https://doc.rust-lang.org/nomicon/ffi.html): explicit ABI and unwind behavior.
- [Linux RT hardware constraints](https://docs.kernel.org/core-api/real-time/hardware.html): platform behavior still matters.
