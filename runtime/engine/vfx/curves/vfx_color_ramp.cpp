/// VFX Color Ramp -- implementation of IEEE 754 half-float conversion
/// and multi-stop RGBA gradient bake to 256-sample LUT.

#include "vfx_color_ramp.h"

#include <algorithm>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// IEEE 754 Half-Float Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// @ref IEEE 754-2008 half-precision conversion
///
/// Single-precision float (32-bit):
///   [1 sign][8 exponent][23 mantissa]  bias=127
///
/// Half-precision float (16-bit):
///   [1 sign][5 exponent][10 mantissa]  bias=15
///
/// Conversion handles:
/// - Zero and denormals: flush-to-zero for simplicity (denormal half-floats
///   are rarely significant for color LUT data).
/// - Normal numbers: re-bias exponent, truncate mantissa.
/// - Overflow (exponent too large): clamp to half infinity.
/// - Underflow (exponent too small): clamp to zero.
/// - Infinity and NaN: preserved.
uint16_t FloatToHalf(float v) noexcept {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(uint32_t));

    const uint32_t sign     = (bits >> 31) & 0x1;
    const uint32_t exponent = (bits >> 23) & 0xFF;
    const uint32_t mantissa = bits & 0x7FFFFF;

    uint16_t halfSign = static_cast<uint16_t>(sign << 15);

    // Zero or denormal — flush to signed zero
    if (exponent == 0) {
        return halfSign;
    }

    // Infinity or NaN
    if (exponent == 0xFF) {
        if (mantissa == 0) {
            // Infinity
            return halfSign | 0x7C00;
        }
        // NaN — preserve some mantissa bits to keep it NaN
        return halfSign | 0x7C00 | static_cast<uint16_t>(mantissa >> 13);
    }

    // Re-bias exponent from float32 (bias 127) to float16 (bias 15)
    int32_t newExp = static_cast<int32_t>(exponent) - 127 + 15;

    if (newExp >= 31) {
        // Overflow: clamp to infinity
        return halfSign | 0x7C00;
    }

    if (newExp <= 0) {
        // Underflow: attempt denormal half-float
        // Denormals have implicit leading 0 instead of 1, so the effective
        // mantissa is (1 + mantissa/2^23) * 2^(1-15) = (1 + mantissa/2^23) * 2^-14
        // Shift the mantissa right by (1 - newExp) to produce a denormal.
        // If the shift is >= 11 (10 mantissa bits + 1 hidden bit), the result
        // is zero.
        const int32_t shift = 1 - newExp;
        if (shift > 24) {
            return halfSign; // Too small, flush to zero
        }
        // Add the implicit leading 1 bit at position 23
        uint32_t denormMantissa = (mantissa | 0x800000) >> (13 + shift);
        return halfSign | static_cast<uint16_t>(denormMantissa & 0x3FF);
    }

    // Normal case: truncate mantissa from 23 bits to 10 bits
    uint16_t halfExp = static_cast<uint16_t>(newExp << 10);
    uint16_t halfMan = static_cast<uint16_t>(mantissa >> 13);

    return halfSign | halfExp | halfMan;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXColorRamp::AddStop
// ─────────────────────────────────────────────────────────────────────────────

void VFXColorRamp::AddStop(float pos, float r, float g, float b, float a) {
    if (stopCount >= kMaxStops) {
        return;
    }
    stops[stopCount++] = VFXColorKey{pos, r, g, b, a};
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXColorRamp::BakeToRGBA16F
// ─────────────────────────────────────────────────────────────────────────────

std::array<uint16_t, VFXColorRamp::kLutWidth * 4> VFXColorRamp::BakeToRGBA16F() const {
    std::array<uint16_t, kLutWidth * 4> pixels{};

    // Edge case: no stops — fill with white (neutral, non-destructive)
    if (stopCount == 0) {
        const uint16_t one = FloatToHalf(1.0f);
        for (uint32_t x = 0; x < kLutWidth; ++x) {
            pixels[x * 4 + 0] = one;
            pixels[x * 4 + 1] = one;
            pixels[x * 4 + 2] = one;
            pixels[x * 4 + 3] = one;
        }
        return pixels;
    }

    // Sort stops by position (work on a local copy since method is const)
    VFXColorKey sorted[kMaxStops];
    for (int i = 0; i < stopCount; ++i) {
        sorted[i] = stops[i];
    }
    std::sort(sorted, sorted + stopCount,
              [](const VFXColorKey& a, const VFXColorKey& b) {
                  return a.t < b.t;
              });

    // Edge case: single stop — fill entire LUT with that color
    if (stopCount == 1) {
        const uint16_t hr = FloatToHalf(sorted[0].r);
        const uint16_t hg = FloatToHalf(sorted[0].g);
        const uint16_t hb = FloatToHalf(sorted[0].b);
        const uint16_t ha = FloatToHalf(sorted[0].a);
        for (uint32_t x = 0; x < kLutWidth; ++x) {
            pixels[x * 4 + 0] = hr;
            pixels[x * 4 + 1] = hg;
            pixels[x * 4 + 2] = hb;
            pixels[x * 4 + 3] = ha;
        }
        return pixels;
    }

    // Multi-stop: evaluate piecewise-linear interpolation at each texel
    for (uint32_t x = 0; x < kLutWidth; ++x) {
        const float t = static_cast<float>(x) / static_cast<float>(kLutWidth - 1);

        float r, g, b, a;

        if (t <= sorted[0].t) {
            // Before first stop: clamp to first stop
            r = sorted[0].r;
            g = sorted[0].g;
            b = sorted[0].b;
            a = sorted[0].a;
        } else if (t >= sorted[stopCount - 1].t) {
            // After last stop: clamp to last stop
            r = sorted[stopCount - 1].r;
            g = sorted[stopCount - 1].g;
            b = sorted[stopCount - 1].b;
            a = sorted[stopCount - 1].a;
        } else {
            // Find bracketing stops and lerp
            int lo = 0;
            for (int i = 0; i < stopCount - 1; ++i) {
                if (t >= sorted[i].t && t <= sorted[i + 1].t) {
                    lo = i;
                    break;
                }
            }

            const float range = sorted[lo + 1].t - sorted[lo].t;
            // Guard against zero-width intervals (two stops at same position)
            const float localT = (range > 0.0f) ? (t - sorted[lo].t) / range : 0.0f;

            r = sorted[lo].r + localT * (sorted[lo + 1].r - sorted[lo].r);
            g = sorted[lo].g + localT * (sorted[lo + 1].g - sorted[lo].g);
            b = sorted[lo].b + localT * (sorted[lo + 1].b - sorted[lo].b);
            a = sorted[lo].a + localT * (sorted[lo + 1].a - sorted[lo].a);
        }

        pixels[x * 4 + 0] = FloatToHalf(r);
        pixels[x * 4 + 1] = FloatToHalf(g);
        pixels[x * 4 + 2] = FloatToHalf(b);
        pixels[x * 4 + 3] = FloatToHalf(a);
    }

    return pixels;
}

} // namespace uz
