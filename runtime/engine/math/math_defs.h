/// UzEngine Math Definitions — Constants, typedefs, and conversion utilities.
///
/// Provides the foundational numeric constants and type aliases used by all
/// math types in the engine.

#pragma once

#include <cmath>
#include <DirectXMath.h>

using namespace DirectX;

namespace uz {

/// Engine-wide real number type (float by default, double for high-precision builds).
using real_t = float;

namespace Math {
    /// Mathematical constant pi (3.14159...).
    constexpr real_t PI = 3.14159265358979323846f;
    /// Two times pi (full circle in radians).
    constexpr real_t TAU = PI * 2.0f;
    /// Mathematical constant e (Euler's number, base of natural logarithm).
    constexpr real_t E = 2.71828182845904523536f;
    /// Square root of 2 (approximately 1.414).
    constexpr real_t SQRT2 = 1.41421356237309504880f;
    /// Square root of 3 (approximately 1.732).
    constexpr real_t SQRT3 = 1.73205080756887729353f;
    /// Natural logarithm of 2 (approximately 0.693).
    constexpr real_t LN2 = 0.693147180559945309417f;
    /// Positive infinity (single-precision).
    constexpr real_t INF = __builtin_huge_valf();
    /// Not-a-Number (single-precision NaN).
    constexpr real_t NaN = __builtin_nanf("");

    /// Default epsilon for floating-point comparisons.
    constexpr real_t EPSILON = 1e-6f;
    /// Looser epsilon for normalization checks.
    constexpr real_t EPSILON2 = 1e-4f;
    /// Epsilon for point-in-plane tests.
    constexpr real_t PLANE_EPSILON = 1e-5f;
    /// Epsilon for unit-length checks.
    constexpr real_t UNIT_EPSILON = 1e-5f;

    /// Converts degrees to radians.
    ///
    /// # Arguments
    /// * `deg` — Angle in degrees.
    ///
    /// # Returns
    /// The angle in radians (deg * π / 180).
    constexpr real_t deg_to_rad(real_t deg) { return deg * (PI / 180.0f); }
    /// Converts radians to degrees.
    ///
    /// # Arguments
    /// * `rad` — Angle in radians.
    ///
    /// # Returns
    /// The angle in degrees (rad * 180 / π).
    constexpr real_t rad_to_deg(real_t rad) { return rad * (180.0f / PI); }
} // namespace Math

} // namespace uz
