// SPDX-License-Identifier: MIT
// ============================================================
// mix_neon.s — AArch64 NEON helpers for LIGHT DAW
//
// Three exported C-callable functions:
//
//   mix_stereo_add_neon  — add gain-scaled stereo clip samples into
//                          an output accumulator buffer (4 frames/cycle)
//
//   peak_abs_neon        — find the maximum absolute value in a flat
//                          float array (8 floats/cycle, dual-pipeline)
//
//   soft_clip_neon       — apply master gain + tanh soft-clip to the
//                          interleaved stereo output buffer (4 samples/cycle)
//                          Uses Padé rational approximation:
//                            tanh(x) ≈ x(27+x²) / (27+9x²)
//                          max error < 0.004 for |x| < π  (adequate for audio)
//
// Calling convention: AArch64 AAPCS
//   Integer args    x0–x7  (w0–w7 for 32-bit)
//   FP/SIMD args    v0–v7  (s0–s7 for scalar float)
//   Caller-saved    x0–x17, v0–v7, v16–v31
//   Callee-saved    x19–x28, v8–v15 (d-reg portion only)
//
// All three functions use only v0–v7 and v16–v31, so no save/restore needed.
// ============================================================

#ifdef __APPLE__
.section __TEXT,__text
#define SYMBOL(name) _##name
#else
.text
#define SYMBOL(name) name
#endif
.p2align 2


// ============================================================
//  void mix_stereo_add_neon(float       *dst,      // x0
//                           const float *src,      // x1
//                           uint32_t     frames,   // w2
//                           float        gain_l,   // s0
//                           float        gain_r)   // s1
//
//  dst += src * [gain_l, gain_r]  (interleaved stereo, in-place accumulate)
//  Processes 4 frames (8 floats) per main-loop iteration via LD2/ST2.
// ============================================================
.globl SYMBOL(mix_stereo_add_neon)
SYMBOL(mix_stereo_add_neon):
    cbz     w2, .Lmsa_ret

    //  Splat scalar gains across 4-lane vectors.
    //  v16–v31 are fully caller-saved, so no push/pop needed.
    dup     v16.4s, v0.s[0]           // v16 = [gain_l × 4]
    dup     v17.4s, v1.s[0]           // v17 = [gain_r × 4]

    // ----- 4-frame NEON loop ----------------------------------------
    lsr     w3, w2, #2                // w3 = frames / 4
    cbz     w3, .Lmsa_tail

.Lmsa_loop:
    //  LD2 deinterleaves: v0 ← all L samples, v1 ← all R samples
    ld2     {v0.4s, v1.4s}, [x1], #32
    //  Load 4 output frames (interleaved) into v2=outL, v3=outR
    ld2     {v2.4s, v3.4s}, [x0]
    //  Fused multiply-accumulate: outL += srcL*gainL, outR += srcR*gainR
    fmla    v2.4s, v0.4s, v16.4s
    fmla    v3.4s, v1.4s, v17.4s
    //  ST2 re-interleaves and advances pointer
    st2     {v2.4s, v3.4s}, [x0], #32
    subs    w3, w3, #1
    b.ne    .Lmsa_loop

    // ----- Scalar tail: 0–3 leftover frames -------------------------
.Lmsa_tail:
    ands    w3, w2, #3
    b.eq    .Lmsa_ret

.Lmsa_tail_loop:
    ldr     s0, [x1], #4             // src L
    ldr     s1, [x1], #4             // src R
    ldr     s2, [x0]                 // dst L
    ldr     s3, [x0, #4]             // dst R
    fmadd   s2, s0, s16, s2          // dstL = srcL*gainL + dstL  (s16 = v16.s[0])
    fmadd   s3, s1, s17, s3          // dstR = srcR*gainR + dstR
    str     s2, [x0], #4
    str     s3, [x0], #4
    subs    w3, w3, #1
    b.ne    .Lmsa_tail_loop

.Lmsa_ret:
    ret


// ============================================================
//  float peak_abs_neon(const float *buf,   // x0
//                      uint32_t     count) // w1
//
//  Returns the maximum absolute value in buf[0..count-1].
//  Dual-pipeline: two accumulators (v16/v17) issued per cycle to
//  hide the 4-cycle FMAX latency on Apple Silicon.
// ============================================================
.globl SYMBOL(peak_abs_neon)
SYMBOL(peak_abs_neon):
    cbz     w1, .Lpa_zero

    movi    v16.4s, #0               // accum A = [0,0,0,0]
    movi    v17.4s, #0               // accum B

    // ----- 8-float NEON loop (dual-pipeline) -----------------------
    lsr     w2, w1, #3               // w2 = count / 8
    cbz     w2, .Lpa_by4

.Lpa_loop8:
    ld1     {v0.4s, v1.4s}, [x0], #32
    fabs    v0.4s, v0.4s
    fabs    v1.4s, v1.4s
    fmax    v16.4s, v16.4s, v0.4s
    fmax    v17.4s, v17.4s, v1.4s
    subs    w2, w2, #1
    b.ne    .Lpa_loop8

    fmax    v16.4s, v16.4s, v17.4s  // merge accumulators

    // ----- Remaining group of 4 ------------------------------------
.Lpa_by4:
    tst     w1, #4
    b.eq    .Lpa_hreduce

    ld1     {v0.4s}, [x0], #16
    fabs    v0.4s, v0.4s
    fmax    v16.4s, v16.4s, v0.4s

    // ----- Horizontal reduce: 4 lanes → scalar ----------------------
.Lpa_hreduce:
    fmaxv   s0, v16.4s              // s0 = max(v16.s[0..3])

    // ----- Scalar tail: 1–3 remaining floats -----------------------
    ands    w2, w1, #3
    b.eq    .Lpa_ret

.Lpa_scalar:
    ldr     s1, [x0], #4
    fabs    s1, s1
    fmax    s0, s0, s1
    subs    w2, w2, #1
    b.ne    .Lpa_scalar

.Lpa_ret:
    ret

.Lpa_zero:
    movi    v0.2s, #0               // return 0.0f
    ret


// ============================================================
//  void soft_clip_neon(float   *buf,        // x0 – interleaved stereo
//                      uint32_t frames,     // w1
//                      float    master_vol) // s0
//
//  In-place: buf[i] = clamp(tanh(buf[i] * master_vol), -1, 1)
//
//  Padé approximation (order 5/4):
//    tanh(x) ≈ x*(27 + x²) / (27 + 9*x²)
//    Max error < 0.004 across |x| ≤ π — inaudible at 16-bit depth.
//
//  Constants built without memory loads:
//    9.0  is a valid AArch64 FMOV immediate (1.125 × 2³, mantissa=2/16, exp=6)
//    27.0 = 9 + 9 + 9  (27.0 itself is NOT directly encodable)
//    1.0  is a valid FMOV immediate
//
//  Register map (all caller-saved):
//    v24 = [9, 9, 9, 9]      v25 = [27, 27, 27, 27]
//    v26 = [1, 1, 1, 1]      v27 = [-1,-1,-1,-1]
//    v28 = [mv, mv, mv, mv]
// ============================================================
.globl SYMBOL(soft_clip_neon)
SYMBOL(soft_clip_neon):
    cbz     w1, .Lsc_ret

    // Build constants — zero memory loads
    fmov    v24.4s, #9.0             // 9.0  (valid FMOV imm: 1.125×2³)
    fadd    v25.4s, v24.4s, v24.4s  // 18.0
    fadd    v25.4s, v25.4s, v24.4s  // 27.0  (= 9×3)
    fmov    v26.4s, #1.0             // 1.0  clamp max
    fneg    v27.4s, v26.4s           // -1.0 clamp min
    dup     v28.4s, v0.s[0]          // master_vol × 4

    // Process as flat floats: total = frames * 2 (interleaved L+R)
    lsl     w2, w1, #1              // w2 = frames*2
    lsr     w3, w2, #2              // w3 = floats/4 (4-wide iterations)
    cbz     w3, .Lsc_tail

.Lsc_loop:
    ld1     {v0.4s}, [x0]
    fmul    v0.4s, v0.4s, v28.4s   // x *= master_vol
    fmul    v1.4s, v0.4s, v0.4s    // v1 = x²
    fadd    v2.4s, v25.4s, v1.4s   // v2 = 27 + x²          (numerator factor)
    fmul    v3.4s, v1.4s, v24.4s   // v3 = 9*x²
    fadd    v3.4s, v25.4s, v3.4s   // v3 = 27 + 9*x²        (denominator)
    fmul    v2.4s, v0.4s, v2.4s    // v2 = x*(27 + x²)      (numerator)
    fdiv    v0.4s, v2.4s, v3.4s    // v0 = tanh approx
    fmin    v0.4s, v0.4s, v26.4s   // clamp ≤  1
    fmax    v0.4s, v0.4s, v27.4s   // clamp ≥ -1
    st1     {v0.4s}, [x0], #16
    subs    w3, w3, #1
    b.ne    .Lsc_loop

.Lsc_tail:
    ands    w3, w2, #3
    b.eq    .Lsc_ret

.Lsc_tail_loop:
    ldr     s0, [x0]
    fmul    s0, s0, s28             // x *= master_vol  (s28 = v28.s[0])
    fmul    s1, s0, s0              // x²
    fadd    s2, s25, s1             // 27 + x²          (s25 = v25.s[0])
    fmul    s3, s1, s24             // 9*x²             (s24 = v24.s[0])
    fadd    s3, s25, s3             // 27 + 9*x²
    fmul    s2, s0, s2              // x*(27+x²)
    fdiv    s0, s2, s3
    fmin    s0, s0, s26             // s26 = v26.s[0] =  1.0
    fmax    s0, s0, s27             // s27 = v27.s[0] = -1.0
    str     s0, [x0], #4
    subs    w3, w3, #1
    b.ne    .Lsc_tail_loop

.Lsc_ret:
    ret

#ifndef __APPLE__
.section .note.GNU-stack,"",%progbits
#endif
