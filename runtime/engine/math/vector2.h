/// UzEngine Vec2 — 2D floating-point vector.
///
/// Provides basic 2D vector operations for screen coordinates, texture UVs,
/// and 2D math. All operations are scalar (no SIMD acceleration).

#pragma once

#include "math_defs.h"
#include "math_funcs.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Vec2 — 2D float vector
// ─────────────────────────────────────────────────────────────────────────────

/// 2D floating-point vector for UI coordinates, texture UVs, and screen-space math.
///
/// Stores x, y as `float` fields. All operations are scalar (no SIMD).
/// Default-constructed to zero.
///
/// # Memory Layout
/// `[x: f32, y: f32]` — 8 bytes, no padding.
///
/// # Examples
/// ```cpp
/// Vec2 uv{0.5f, 0.5f};
/// Vec2 screen = uv * Vec2{1920.0f, 1080.0f};
/// float len = screen.length();
/// ```
struct Vec2 {
    /// X component (horizontal axis).
    float x;
    /// Y component (vertical axis).
    float y;

    /// Zero-initialized vector (0, 0).
    Vec2() : x(0), y(0) {}
    /// Construct from explicit components.
    constexpr Vec2(float x, float y) : x(x), y(y) {}
    /// Construct uniform vector (scalar, scalar).
    explicit constexpr Vec2(float scalar) : x(scalar), y(scalar) {}
    /// Load from memory (pointer to at least 2 floats).
    explicit Vec2(const float* ptr) : x(ptr[0]), y(ptr[1]) {}

    /// Pointer to elements for array-like access.
    float* data() { return &x; }
    /// Const pointer to elements.
    const float* data() const { return &x; }
    /// Array indexing [0]=x, [1]=y.
    float& operator[](int idx) { return data()[idx]; }
    /// Const array indexing.
    float operator[](int idx) const { return data()[idx]; }

    /// Get X component (uppercase for test compatibility).
    float X() const { return x; }
    /// Get Y component.
    float Y() const { return y; }

    /// Vector addition (element-wise).
    ///
    /// # Returns
    /// Vector with components added: `(x + other.x, y + other.y)`.
    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    /// Vector subtraction (element-wise).
    ///
    /// # Returns
    /// Vector with components subtracted: `(x - other.x, y - other.y)`.
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    /// Scalar multiplication (uniform scaling).
    ///
    /// # Arguments
    /// * `scalar` — Scale factor.
    ///
    /// # Returns
    /// Vector scaled by scalar: `(x * scalar, y * scalar)`.
    Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    /// Scalar division (inverse uniform scaling).
    ///
    /// # Arguments
    /// * `scalar` — Divisor (assumes non-zero).
    ///
    /// # Returns
    /// Vector scaled by 1/scalar: `(x / scalar, y / scalar)`.
    Vec2 operator/(float scalar) const { return {x / scalar, y / scalar}; }
    /// Negation (inverse direction).
    ///
    /// # Returns
    /// Vector pointing in the opposite direction: `(-x, -y)`.
    Vec2 operator-() const { return {-x, -y}; }
    /// In-place addition (modifies this vector).
    ///
    /// # Arguments
    /// * `other` — Vector to add.
    ///
    /// # Returns
    /// Reference to `*this` after modification.
    Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }
    /// In-place subtraction (modifies this vector).
    ///
    /// # Arguments
    /// * `other` — Vector to subtract.
    ///
    /// # Returns
    /// Reference to `*this` after modification.
    Vec2& operator-=(const Vec2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }
    /// In-place scalar multiplication (modifies this vector).
    ///
    /// # Arguments
    /// * `scalar` — Scale factor.
    ///
    /// # Returns
    /// Reference to `*this` after scaling.
    Vec2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    /// Exact equality comparison (bitwise).
    ///
    /// # Arguments
    /// * `other` — Vector to compare.
    ///
    /// # Returns
    /// `true` if both components are exactly equal, `false` otherwise.
    /// For approximate equality, use Math::is_equal_approx() on individual components.
    bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
    /// Inequality comparison.
    ///
    /// # Arguments
    /// * `other` — Vector to compare.
    ///
    /// # Returns
    /// `true` if any component differs, `false` if both are equal.
    bool operator!=(const Vec2& other) const { return !(*this == other); }

    /// Computes the dot product with another vector.
    ///
    /// # Returns
    /// Scalar projection: `x*other.x + y*other.y`.
    float dot(const Vec2& other) const { return x * other.x + y * other.y; }
    /// Uppercase alias for `dot()`.
    float Dot(const Vec2& other) const { return dot(other); }
    /// Computes the Euclidean length (magnitude) of the vector.
    ///
    /// # Returns
    /// `sqrt(x*x + y*y)`. Use `lengthSq()` when comparing distances to avoid sqrt.
    float length() const { return std::sqrt(x * x + y * y); }
    /// Uppercase alias for `length()`.
    float Length() const { return length(); }
    /// Computes the squared Euclidean length (avoids sqrt).
    ///
    /// # Returns
    /// `x*x + y*y`. Prefer over `length()` for distance comparisons.
    float lengthSq() const { return x * x + y * y; }
    /// Uppercase alias for `lengthSq()`.
    float LengthSq() const { return lengthSq(); }
    /// Returns a unit-length copy of this vector.
    ///
    /// # Returns
    /// Normalized vector with length 1. Returns copy of `*this` if length is zero.
    Vec2 normalize() const;
    /// Uppercase alias for `normalize()`.
    Vec2 Normalized() const { return normalize(); }

    /// 2D cross product (z-component of the 3D cross product).
    ///
    /// # Returns
    /// `x * other.y - y * other.x`. Positive if `other` is counter-clockwise from `*this`.
    float cross(const Vec2& other) const { return x * other.y - y * other.x; }

    /// Euclidean distance to another point.
    ///
    /// # Arguments
    /// * `other` — Target point.
    ///
    /// # Returns
    /// `(*this - other).length()`. Use `distance_squared_to()` for comparisons.
    [[nodiscard]] float distance_to(const Vec2& other) const;

    /// Squared Euclidean distance to another point (avoids sqrt).
    ///
    /// # Arguments
    /// * `other` — Target point.
    ///
    /// # Returns
    /// `(*this - other).lengthSq()`.
    float distance_squared_to(const Vec2& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        return dx * dx + dy * dy;
    }

    /// Angle between this vector and `other` in radians.
    ///
    /// Uses `atan2(cross, dot)` for a signed angle in (-pi, pi].
    ///
    /// # Returns
    /// Signed angle from `*this` to `other` in radians.
    [[nodiscard]] float angle_to(const Vec2& other) const;

    /// Angle of the vector from the positive X axis.
    ///
    /// # Returns
    /// `atan2(y, x)` in radians, range (-pi, pi].
    [[nodiscard]] float angle() const;

    /// Returns this vector rotated by `angle` radians.
    ///
    /// # Arguments
    /// * `angle` — Rotation angle in radians (counter-clockwise positive).
    ///
    /// # Returns
    /// Rotated copy of this vector.
    [[nodiscard]] Vec2 rotated(float angle) const;

    /// Spherical linear interpolation between two 2D vectors.
    ///
    /// Interpolates along the shortest arc on the unit circle. Both vectors
    /// should be non-zero; the result preserves interpolated length.
    ///
    /// # Arguments
    /// * `other` — Target vector.
    /// * `t` — Interpolation factor in [0, 1].
    ///
    /// # Returns
    /// Slerp-interpolated vector.
    [[nodiscard]] Vec2 slerp(const Vec2& other, float t) const;

    /// Catmull-Rom cubic interpolation between this and `b`.
    ///
    /// # Arguments
    /// * `b` — End value (at t=1).
    /// * `pre_a` — Control point before `*this`.
    /// * `post_b` — Control point after `b`.
    /// * `t` — Interpolation factor in [0, 1].
    ///
    /// # Returns
    /// Cubic-interpolated vector with smooth tangents.
    [[nodiscard]] Vec2 cubic_interpolate(const Vec2& b, const Vec2& pre_a, const Vec2& post_b, float t) const;

    /// Projects this vector onto the plane defined by `normal`.
    ///
    /// Removes the component of motion along the normal (e.g., sliding along a wall).
    ///
    /// # Arguments
    /// * `normal` — Unit-length surface normal.
    ///
    /// # Returns
    /// `*this - normal * dot(normal)`.
    [[nodiscard]] Vec2 slide(const Vec2& normal) const;

    /// Reflects this vector off a surface and flips direction (bounce).
    ///
    /// # Arguments
    /// * `normal` — Unit-length surface normal.
    ///
    /// # Returns
    /// `-reflect(normal)`.
    [[nodiscard]] Vec2 bounce(const Vec2& normal) const;

    /// Mirrors this vector about a surface normal (standard reflection).
    ///
    /// # Arguments
    /// * `normal` — Unit-length surface normal.
    ///
    /// # Returns
    /// `2.0f * normal * dot(normal) - *this`.
    [[nodiscard]] Vec2 reflect(const Vec2& normal) const;

    /// Moves this vector toward `target` by at most `delta` units.
    ///
    /// # Arguments
    /// * `target` — Destination point.
    /// * `delta` — Maximum distance to move (must be >= 0).
    ///
    /// # Returns
    /// New position at most `delta` closer to `target`, or `target` if already within range.
    [[nodiscard]] Vec2 move_toward(const Vec2& target, float delta) const;

    /// Normalized direction vector from this point to `other`.
    ///
    /// # Arguments
    /// * `other` — Target point.
    ///
    /// # Returns
    /// Unit-length direction. Returns zero vector if points are coincident.
    [[nodiscard]] Vec2 direction_to(const Vec2& other) const;

    /// Clamps the vector's length to at most `max_len`.
    ///
    /// # Arguments
    /// * `max_len` — Maximum allowed length (default 1.0).
    ///
    /// # Returns
    /// Copy with length clamped. If already shorter, returns unchanged copy.
    [[nodiscard]] Vec2 limit_length(float max_len = 1.0f) const;

    /// Component-wise absolute value.
    ///
    /// # Returns
    /// `(|x|, |y|)`.
    Vec2 abs() const { return {std::abs(x), std::abs(y)}; }

    /// Component-wise floor.
    ///
    /// # Returns
    /// `(floor(x), floor(y))`.
    Vec2 floor() const { return {std::floor(x), std::floor(y)}; }

    /// Component-wise ceil.
    ///
    /// # Returns
    /// `(ceil(x), ceil(y))`.
    Vec2 ceil() const { return {std::ceil(x), std::ceil(y)}; }

    /// Component-wise round to nearest integer.
    ///
    /// # Returns
    /// `(round(x), round(y))`.
    Vec2 round() const { return {std::round(x), std::round(y)}; }

    /// Component-wise sign extraction.
    ///
    /// # Returns
    /// Vector where each component is -1, 0, or 1 matching the sign of the original.
    Vec2 sign() const { return {::uz::Math::sign(x), ::uz::Math::sign(y)}; }

    /// Component-wise clamp between min and max vectors.
    ///
    /// # Arguments
    /// * `min` — Minimum bounds per component.
    /// * `max` — Maximum bounds per component.
    ///
    /// # Returns
    /// Vector with each component clamped to [min, max].
    Vec2 clamp(const Vec2& min, const Vec2& max) const {
        return {::uz::Math::clamp(x, min.x, max.x), ::uz::Math::clamp(y, min.y, max.y)};
    }

    /// Snaps each component to the nearest multiple of the corresponding step.
    ///
    /// # Arguments
    /// * `step` — Grid size per component.
    ///
    /// # Returns
    /// Snapped vector.
    Vec2 snap(const Vec2& step) const { return {Math::snap(x, step.x), Math::snap(y, step.y)}; }

    /// Positive float modulo on each component (wraps into [0, mod)).
    ///
    /// # Arguments
    /// * `mod` — Modulus value.
    ///
    /// # Returns
    /// Vector with each component wrapped to [0, mod).
    Vec2 posmod(float mod) const { return {Math::fposmod(x, mod), Math::fposmod(y, mod)}; }

    /// Per-component positive float modulo.
    ///
    /// # Arguments
    /// * `modv` — Modulus per component.
    ///
    /// # Returns
    /// Vector with x wrapped to [0, modv.x) and y wrapped to [0, modv.y).
    Vec2 posmodv(const Vec2& modv) const { return {Math::fposmod(x, modv.x), Math::fposmod(y, modv.y)}; }

    /// Returns true if the vector is approximately zero (within `Math::EPSILON`).
    ///
    /// # Returns
    /// `true` if both components are near zero.
    bool is_zero_approx() const { return Math::is_zero_approx(x) && Math::is_zero_approx(y); }

    /// Returns true if approximately equal to `other` (within `Math::EPSILON`).
    ///
    /// # Arguments
    /// * `other` — Vector to compare.
    ///
    /// # Returns
    /// `true` if both components are within epsilon of each other.
    bool is_equal_approx(const Vec2& other) const {
        return Math::is_equal_approx(x, other.x) && Math::is_equal_approx(y, other.y);
    }

    /// Returns true if this vector is approximately unit-length.
    ///
    /// # Returns
    /// `true` if `|lengthSq() - 1| < UNIT_EPSILON`.
    bool is_normalized() const { return std::abs(lengthSq() - 1.0f) < Math::UNIT_EPSILON; }

    /// Zero vector (0, 0).
    static Vec2 Zero() { return {0, 0}; }
    /// Unit vector (1, 1).
    static Vec2 One() { return {1, 1}; }
    /// Unit vector along X axis (1, 0).
    static Vec2 UnitX() { return {1, 0}; }
    /// Unit vector along Y axis (0, 1).
    static Vec2 UnitY() { return {0, 1}; }
    /// Linear interpolation between a and b (t in [0,1]).
    static Vec2 Lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
    /// Load from XMFLOAT2 (DirectXMath compatible).
    static Vec2 FromFloat2(const DirectX::XMFLOAT2& f) { return {f.x, f.y}; }

    /// Convert to XMFLOAT2 for DirectXMath compatibility.
    DirectX::XMFLOAT2 ToFloat2() const { return {x, y}; }
};

/// Scalar-left multiplication for Vec2 (`2.0f * vec`).
inline Vec2 operator*(float scalar, const Vec2& v) {
    return v * scalar;
}

} // namespace uz
