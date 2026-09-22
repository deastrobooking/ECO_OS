// SPDX-License-Identifier: MIT
// Adapted from deastrobooking/LIGHT at 15f08cae2ea297c8906ffc2cd37bf5e4aa62c693.
#pragma once
#include <stdint.h>

/*
 * AArch64 NEON audio primitives (mix_neon.s).
 *
 * mix_stereo_add_neon
 *   Add `frames` gain-scaled interleaved stereo frames from src into dst.
 *   dst[i].L += src[i].L * gain_l
 *   dst[i].R += src[i].R * gain_r
 *   Both buffers are interleaved: [L0 R0 L1 R1 …].
 *   Inner loop processes 4 frames/cycle via LD2/FMLA/ST2.
 *
 * peak_abs_neon
 *   Return the maximum |value| in buf[0..count-1].
 *   Dual-pipeline 8-float inner loop.
 *
 * soft_clip_neon
 *   Apply master volume gain and tanh soft-clip in-place.
 *   buf contains `frames` interleaved stereo frames (2*frames floats).
 *   Uses Padé approximation tanh(x) ≈ x(27+x²)/(27+9x²), clamped to [-1,1].
 */

void mix_stereo_add_neon(float *dst, const float *src, uint32_t frames, float gain_l, float gain_r);

float peak_abs_neon(const float *buf, uint32_t count);

void soft_clip_neon(float *buf, uint32_t frames, float master_vol);
