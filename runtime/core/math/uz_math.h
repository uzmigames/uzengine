#pragma once

/// UzEngine Math Foundation — Unified include for all math types.
///
/// # Components
/// - **Math Types** (Vec2, Vec3, Vec4): SIMD-optimized vector types
/// - **Matrix** (Mat4): 4x4 transformation matrix
/// - **Quaternion** (Quat): Normalized rotation representation
/// - **Transform**: Combined position, rotation, scale
/// - **Color** (RGBA, HSV conversions)
/// - **Bounds** (AABB, sphere collision)
///
/// # Conventions
/// - **Right-handed coordinates**: +X right, +Y up, +Z away
/// - **Row-major matrices**: Mult(vec, mat) convention
/// - **Degrees vs Radians**: Explicit Deg2Rad/Rad2Deg conversions
/// - **Normalized quaternions**: Required for slerp/lerp operations
///
/// # Precision
/// - **Single precision (float)**: All calculations use 32-bit floats
/// - **Epsilon (1e-6f)**: Default tolerance for equality checks

#include "uz_math_types.h"
#include "uz_mat4.h"
#include "uz_quat.h"
#include "uz_transform.h"
#include "uz_color.h"
#include "uz_bounds.h"

namespace uz {

/// Fundamental mathematical constants (single-precision).
///
/// # Usage
/// ```cpp
/// float radians = angle_deg * uz::kDeg2Rad;
/// float degrees = radians * uz::kRad2Deg;
/// if (uz::NearEqual(a, b)) { /* nearly equal */ }
/// ```

/// π (3.14159...)
constexpr float kPi       = 3.14159265358979323846f;
/// 2π (full circle)
constexpr float kTwoPi    = 6.28318530717958647692f;
/// π/2 (90 degrees)
constexpr float kHalfPi   = 1.57079632679489661923f;
/// Conversion factor: degrees to radians (π/180)
constexpr float kDeg2Rad  = kPi / 180.0f;
/// Conversion factor: radians to degrees (180/π)
constexpr float kRad2Deg  = 180.0f / kPi;
/// Default epsilon for floating-point comparisons (1e-6)
constexpr float kEpsilon  = 1.0e-6f;

/// Converts degrees to radians.
///
/// # Example
/// ```cpp
/// float rad = Deg2Rad(90.0f);  // π/2
/// ```
inline float Deg2Rad(float deg) { return deg * kDeg2Rad; }

/// Converts radians to degrees.
///
/// # Example
/// ```cpp
/// float deg = Rad2Deg(kPi);  // 180.0f
/// ```
inline float Rad2Deg(float rad) { return rad * kRad2Deg; }

/// Clamps value to [lo, hi] range.
///
/// # Returns
/// - `lo` if val < lo
/// - `hi` if val > hi
/// - `val` otherwise
template <typename T>
T Clamp(T val, T lo, T hi) { return val < lo ? lo : (val > hi ? hi : val); }

/// Clamps value to [0, 1] range (saturate operation).
///
/// # Example
/// ```cpp
/// float alpha = Saturate(1.5f);  // 1.0f
/// ```
inline float Saturate(float x) { return Clamp(x, 0.0f, 1.0f); }

/// Linear interpolation between two values.
///
/// # Arguments
/// * `a` - Start value
/// * `b` - End value
/// * `t` - Blend factor [0, 1] (0=a, 1=b)
///
/// # Example
/// ```cpp
/// Vec3 midpoint = Lerp(start, end, 0.5f);
/// ```
template <typename T>
T Lerp(T a, T b, float t) { return a + (b - a) * t; }

/// Checks if two floats are nearly equal (within epsilon).
///
/// # Arguments
/// * `a`, `b` - Values to compare
/// * `eps` - Tolerance (default 1e-6)
///
/// # Example
/// ```cpp
/// if (NearEqual(dot, 1.0f, 0.01f)) { /* nearly perpendicular */ }
/// ```
inline bool NearEqual(float a, float b, float eps = kEpsilon) {
    return std::abs(a - b) < eps;
}

} // namespace uz
