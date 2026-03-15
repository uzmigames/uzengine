/// UzEngine Color Utilities — Kelvin-to-RGB and other color space conversions.
///
/// Provides CPU-side color conversion utilities for the lighting pipeline.
/// All outputs are in linear RGB (not sRGB).

#pragma once

#include "vector3.h"

namespace uz {

/// Converts a blackbody color temperature in Kelvin to linear RGB.
///
/// Uses the Tanner Helland approximation (CIE 1931 chromaticity fit).
/// Accurate to within ~1% for temperatures between 1000K and 40000K.
///
/// # Arguments
/// * `kelvin` — Color temperature in Kelvin. Clamped to [1000, 40000].
///
/// # Returns
/// Linear RGB color normalized to [0, 1] range. At 6500K returns ~(1, 1, 1).
///
/// # Examples
/// ```cpp
/// Vec3 warm = KelvinToRgb(3000.f);   // ~(1.0, 0.72, 0.42) — candle/warm
/// Vec3 day  = KelvinToRgb(6500.f);   // ~(1.0, 1.0, 1.0)   — daylight
/// Vec3 cool = KelvinToRgb(10000.f);  // ~(0.79, 0.87, 1.0)  — cool/blue
/// ```
[[nodiscard]] inline Vec3 KelvinToRgb(float kelvin) noexcept
{
    // Clamp to valid range
    float t = kelvin;
    if (t < 1000.f) t = 1000.f;
    if (t > 40000.f) t = 40000.f;
    t /= 100.f;

    float r, g, b;

    // Red
    if (t <= 66.f) {
        r = 1.0f;
    } else {
        float x = t - 60.f;
        r = 329.698727446f * powf(x, -0.1332047592f) / 255.f;
    }

    // Green
    if (t <= 66.f) {
        g = (99.4708025861f * logf(t) - 161.1195681661f) / 255.f;
    } else {
        float x = t - 60.f;
        g = 288.1221695283f * powf(x, -0.0755148492f) / 255.f;
    }

    // Blue
    if (t >= 66.f) {
        b = 1.0f;
    } else if (t <= 19.f) {
        b = 0.0f;
    } else {
        float x = t - 10.f;
        b = (138.5177312231f * logf(x) - 305.0447927307f) / 255.f;
    }

    // Clamp to [0, 1]
    auto clamp01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };
    return Vec3{clamp01(r), clamp01(g), clamp01(b)};
}

} // namespace uz
