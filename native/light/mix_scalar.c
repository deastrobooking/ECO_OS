// SPDX-License-Identifier: MIT
// Portable implementation of LIGHT's mixer ABI; also the SIMD test reference.
#include "mix_neon.h"
#include <math.h>
void mix_stereo_add_scalar(float *dst, const float *src, uint32_t frames, float gl, float gr) {
    for (uint32_t i = 0; i < frames; i++) {
        dst[2 * i] += src[2 * i] * gl;
        dst[2 * i + 1] += src[2 * i + 1] * gr;
    }
}
float peak_abs_scalar(const float *buf, uint32_t count) {
    float peak = 0;
    for (uint32_t i = 0; i < count; i++)
        peak = fmaxf(peak, fabsf(buf[i]));
    return peak;
}
void soft_clip_scalar(float *buf, uint32_t frames, float gain) {
    for (uint32_t i = 0; i < frames * 2; i++) {
        float x = buf[i] * gain, x2 = x * x;
        float y = x * (27 + x2) / (27 + 9 * x2);
        buf[i] = fmaxf(-1, fminf(1, y));
    }
}
#ifndef LIGHT_USE_NEON
void mix_stereo_add_neon(float *d, const float *s, uint32_t n, float l, float r) {
    mix_stereo_add_scalar(d, s, n, l, r);
}
float peak_abs_neon(const float *b, uint32_t n) {
    return peak_abs_scalar(b, n);
}
void soft_clip_neon(float *b, uint32_t n, float g) {
    soft_clip_scalar(b, n, g);
}
#endif
