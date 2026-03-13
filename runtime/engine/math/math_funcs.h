/// UzEngine Math Functions — Scalar utility functions (Godot-style).
///
/// Provides common scalar math operations: clamping, interpolation, easing,
/// snapping, and audio conversions. All functions are inline for zero overhead.

#pragma once

#include "math_defs.h"

namespace uz {
namespace Math {

/// Returns true if value is approximately zero (within `EPSILON`).
///
/// # Arguments
/// * `val` — Value to test.
///
/// # Returns
/// `true` if `|val| < EPSILON`, `false` otherwise.
inline bool is_zero_approx(real_t val) { return std::abs(val) < EPSILON; }

/// Returns true if two values are approximately equal (within `EPSILON`).
///
/// # Arguments
/// * `a` — First value.
/// * `b` — Second value.
///
/// # Returns
/// `true` if `|a - b| < EPSILON`, `false` otherwise.
inline bool is_equal_approx(real_t a, real_t b) { return std::abs(a - b) < EPSILON; }

/// Clamps value to the range [min_val, max_val].
///
/// # Arguments
/// * `val` — Value to clamp.
/// * `min_val` — Minimum allowed value.
/// * `max_val` — Maximum allowed value.
///
/// # Returns
/// `val` clamped to [min_val, max_val]. Assumes min_val <= max_val.
inline real_t clamp(real_t val, real_t min_val, real_t max_val) {
    return val < min_val ? min_val : (val > max_val ? max_val : val);
}

/// Linear interpolation between a and b.
///
/// # Arguments
/// * `a` — Start value (at t=0).
/// * `b` — End value (at t=1).
/// * `t` — Interpolation factor in [0, 1] (unclamped).
///
/// # Returns
/// `a + (b - a) * t`. For t outside [0, 1], extrapolates beyond [a, b].
inline real_t lerp(real_t a, real_t b, real_t t) { return a + (b - a) * t; }

/// Inverse linear interpolation (solve for t in lerp equation).
///
/// # Arguments
/// * `a` — Start value.
/// * `b` — End value.
/// * `val` — Target value between a and b.
///
/// # Returns
/// The parameter t such that `lerp(a, b, t) == val`. Returns (val - a) / (b - a).
inline real_t inverse_lerp(real_t a, real_t b, real_t val) { return (val - a) / (b - a); }

/// Smooth step interpolation (Hermite, aka smoothstep(0, 1, t)).
///
/// Produces smooth acceleration and deceleration with zero derivatives at endpoints.
/// Useful for easing animations and fade-in/fade-out effects.
///
/// # Arguments
/// * `from` — Lower edge of the interpolation range.
/// * `to` — Upper edge of the interpolation range.
/// * `val` — Value to interpolate (clamped to [from, to]).
///
/// # Returns
/// Smoothly interpolated value in [0, 1] using the formula: `3t² - 2t³` where t ∈ [0, 1].
inline real_t smoothstep(real_t from, real_t to, real_t val) {
    real_t t = clamp((val - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// Remaps val from input range [istart, istop] to output range [ostart, ostop].
///
/// # Arguments
/// * `val` — Value in input range.
/// * `istart` — Input range start.
/// * `istop` — Input range end.
/// * `ostart` — Output range start.
/// * `ostop` — Output range end.
///
/// # Returns
/// Value remapped to output range. Equivalent to `lerp(ostart, ostop, inverse_lerp(istart, istop, val))`.
inline real_t remap(real_t val, real_t istart, real_t istop, real_t ostart, real_t ostop) {
    return lerp(ostart, ostop, inverse_lerp(istart, istop, val));
}

/// Snaps value to the nearest multiple of step.
///
/// # Arguments
/// * `val` — Value to snap.
/// * `step` — Step size. If zero, returns val unchanged.
///
/// # Returns
/// Value rounded to the nearest multiple of step.
inline real_t snap(real_t val, real_t step) {
    if (step != 0.0f) return std::floor(val / step + 0.5f) * step;
    return val;
}

/// Wraps float value into the range [0, length).
///
/// # Arguments
/// * `val` — Value to wrap.
/// * `length` — Modulus (period).
///
/// # Returns
/// Wrapped value in [0, length). Handles negative values correctly.
inline real_t fposmod(real_t val, real_t length) {
    real_t r = std::fmod(val, length);
    return r < 0.0f ? r + length : r;
}

/// Wraps integer value into the range [0, length).
///
/// # Arguments
/// * `val` — Value to wrap.
/// * `length` — Modulus (period).
///
/// # Returns
/// Wrapped value in [0, length). Handles negative values correctly.
inline int posmod(int val, int length) {
    int r = val % length;
    return r < 0 ? r + length : r;
}

/// Extracts the sign of a value.
///
/// # Arguments
/// * `val` — Value to test.
///
/// # Returns
/// -1.0 if val < 0, 1.0 if val > 0, 0.0 if val == 0.
inline real_t sign(real_t val) { return val > 0.0f ? 1.0f : (val < 0.0f ? -1.0f : 0.0f); }

/// Extracts the fractional part of a value.
///
/// # Arguments
/// * `val` — Value to extract from.
///
/// # Returns
/// `val - floor(val)`, the value in [0, 1) representing the fractional part.
inline real_t fract(real_t val) { return val - std::floor(val); }

/// Step function — returns 0 or 1 based on comparison.
///
/// # Arguments
/// * `edge` — Threshold value.
/// * `val` — Value to test.
///
/// # Returns
/// 0.0 if val < edge, 1.0 otherwise.
inline real_t step(real_t edge, real_t val) { return val < edge ? 0.0f : 1.0f; }

/// Converts linear amplitude to decibels (dB).
///
/// # Arguments
/// * `linear` — Linear amplitude value (typically > 0).
///
/// # Returns
/// Decibel equivalent using the formula: `log(linear) * 20 / ln(10)`.
inline real_t linear_to_db(real_t linear) { return std::log(linear) * 8.6858896380650365530225783783321f; }

/// Converts decibels to linear amplitude.
///
/// # Arguments
/// * `db` — Decibel value.
///
/// # Returns
/// Linear amplitude using the formula: `exp(db * ln(10) / 20)`.
inline real_t db_to_linear(real_t db) { return std::exp(db * 0.11512925464970228420089957273422f); }

/// Cubic spline interpolation between p1 and p2 using Catmull-Rom basis.
///
/// # Arguments
/// * `p0` — Previous control point (before p1).
/// * `p1` — Start value (at t=0).
/// * `p2` — End value (at t=1).
/// * `p3` — Next control point (after p2).
/// * `t` — Interpolation factor in [0, 1].
///
/// # Returns
/// Cubic interpolated value. Derivatives match the surrounding segments for smooth splines.
inline real_t cubic_interpolate(real_t p0, real_t p1, real_t p2, real_t p3, real_t t) {
    real_t t2 = t * t;
    real_t t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

/// Easing function with adjustable curve shape (ease-in / ease-out / linear).
///
/// # Arguments
/// * `val` — Progress value (will be clamped to [0, 1]).
/// * `curve` — Curve parameter: curve > 0 for ease-in, curve < 0 for ease-out, curve == 0 for linear.
///
/// # Returns
/// Eased value in [0, 1]. The magnitude of curve affects the sharpness of the ease.
inline real_t ease(real_t val, real_t curve) {
    if (val < 0.0f) val = 0.0f;
    else if (val > 1.0f) val = 1.0f;
    if (curve > 0.0f) {
        if (curve < 1.0f) return 1.0f - std::pow(1.0f - val, 1.0f / curve);
        return std::pow(val, curve);
    }
    if (curve < 0.0f) {
        if (val < 0.5f) return std::pow(val * 2.0f, -curve) * 0.5f;
        return (1.0f - std::pow(1.0f - (val - 0.5f) * 2.0f, -curve)) * 0.5f + 0.5f;
    }
    return 0.0f;
}

} // namespace Math
} // namespace uz
