/// UzEngine Vec3 — 3D floating-point vector with SIMD acceleration.

#pragma once

#include "math_defs.h"
#include "math_funcs.h"

namespace uz {

struct Vec2;

// ─────────────────────────────────────────────────────────────────────────────
// Vec3 — 3D float vector (primary SIMD vector)
// ─────────────────────────────────────────────────────────────────────────────

/// 3D floating-point vector with SIMD acceleration via DirectXMath.
///
/// Stores x, y, z as `float` fields. All arithmetic operations are SIMD-accelerated
/// through implicit conversion to `XMVECTOR` via `toXM()`. Default-constructed to zero.
/// Primary vector type for positions, directions, normals, and scales.
///
/// # Memory Layout
/// `[x: f32, y: f32, z: f32]` — 12 bytes, no padding. NOT 16-byte aligned.
/// Use `toXM()` for SIMD operations that require `XMVECTOR` (16-byte aligned).
///
/// # Coordinate System
/// Left-handed: +X right, +Y up, +Z forward.
///
/// # Examples
/// ```cpp
/// Vec3 a{1.0f, 2.0f, 3.0f};
/// Vec3 b{4.0f, 5.0f, 6.0f};
/// float d = a.Dot(b);           // 32.0
/// Vec3 c = a.Cross(b);          // {-3, 6, -3}
/// Vec3 n = a.Normalized();      // unit length
/// ```
struct Vec3 {
    /// X component (horizontal axis, positive = right).
    float x;
    /// Y component (vertical axis, positive = up).
    float y;
    /// Z component (depth axis, positive = forward in LH coordinate system).
    float z;

    /// Zero-initialized vector (0, 0, 0).
    Vec3() : x(0), y(0), z(0) {}
    /// Construct from explicit components.
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    /// Construct uniform vector (scalar, scalar, scalar).
    explicit constexpr Vec3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    /// Load from memory (pointer to at least 3 floats).
    explicit Vec3(const float* ptr) : x(ptr[0]), y(ptr[1]), z(ptr[2]) {}
    /// Convert from DirectXMath XMVECTOR.
    explicit Vec3(const XMVECTOR& xm) { XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(this), xm); }
    /// Convert from DirectXMath XMFLOAT3.
    explicit Vec3(const XMFLOAT3& f3) : x(f3.x), y(f3.y), z(f3.z) {}

    /// Convert to XMVECTOR for SIMD operations (zero-cost in release).
    XMVECTOR toXM() const { return XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(this)); }

    /// Pointer to elements for array-like access.
    float* data() { return &x; }
    /// Const pointer to elements.
    const float* data() const { return &x; }
    /// Array indexing [0]=x, [1]=y, [2]=z.
    float& operator[](int idx) { return data()[idx]; }
    /// Const array indexing.
    float operator[](int idx) const { return data()[idx]; }

    /// Get X component (uppercase for test compatibility).
    float X() const { return x; }
    /// Get Y component.
    float Y() const { return y; }
    /// Get Z component.
    float Z() const { return z; }

    /// Vector addition (element-wise).
    ///
    /// # Returns
    /// Vector with components added: `(x + other.x, y + other.y, z + other.z)`.
    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    /// Vector subtraction (element-wise).
    ///
    /// # Returns
    /// Vector with components subtracted: `(x - other.x, y - other.y, z - other.z)`.
    Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    /// Scalar multiplication (uniform scaling).
    ///
    /// # Arguments
    /// * `scalar` — Scale factor.
    ///
    /// # Returns
    /// Vector scaled by scalar: `(x * scalar, y * scalar, z * scalar)`.
    Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    /// Component-wise multiplication (Hadamard product, not a standard vector operation).
    ///
    /// Used for scale blending and element-wise multiplication (e.g., color modulation).
    /// Not a cross product or dot product.
    ///
    /// # Returns
    /// Vector with components multiplied: `(x * other.x, y * other.y, z * other.z)`.
    Vec3 operator*(const Vec3& other) const { return {x * other.x, y * other.y, z * other.z}; }
    /// Scalar division (inverse uniform scaling).
    ///
    /// # Arguments
    /// * `scalar` — Divisor (assumes non-zero).
    ///
    /// # Returns
    /// Vector scaled by 1/scalar: `(x / scalar, y / scalar, z / scalar)`.
    Vec3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }
    /// Negation (inverse direction).
    ///
    /// # Returns
    /// Vector pointing in the opposite direction: `(-x, -y, -z)`.
    Vec3 operator-() const { return {-x, -y, -z}; }
    /// In-place addition (modifies this vector).
    ///
    /// # Arguments
    /// * `other` — Vector to add.
    ///
    /// # Returns
    /// Reference to `*this` after modification.
    Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    /// In-place subtraction (modifies this vector).
    ///
    /// # Arguments
    /// * `other` — Vector to subtract.
    ///
    /// # Returns
    /// Reference to `*this` after modification.
    Vec3& operator-=(const Vec3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }
    /// In-place scalar multiplication (modifies this vector).
    ///
    /// # Arguments
    /// * `scalar` — Scale factor.
    ///
    /// # Returns
    /// Reference to `*this` after scaling.
    Vec3& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    /// Exact equality comparison (bitwise).
    ///
    /// # Arguments
    /// * `other` — Vector to compare.
    ///
    /// # Returns
    /// `true` if all components are exactly equal, `false` otherwise.
    /// For approximate equality, use Math::is_equal_approx() on components.
    bool operator==(const Vec3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    /// Inequality comparison.
    ///
    /// # Arguments
    /// * `other` — Vector to compare.
    ///
    /// # Returns
    /// `true` if any component differs, `false` if all are equal.
    bool operator!=(const Vec3& other) const { return !(*this == other); }

    /// Computes the dot product with another vector (SIMD-accelerated).
    ///
    /// # Arguments
    /// * `other` - The vector to dot with.
    ///
    /// # Returns
    /// Scalar projection: `x*other.x + y*other.y + z*other.z`.
    ///
    /// # Examples
    /// ```cpp
    /// Vec3 a{1, 0, 0}, b{0, 1, 0};
    /// assert(a.Dot(b) == 0.0f); // perpendicular
    /// ```
    float dot(const Vec3& other) const;
    /// Uppercase alias for `dot()`.
    float Dot(const Vec3& other) const { return dot(other); }
    /// Computes the cross product with another vector (SIMD-accelerated).
    ///
    /// # Arguments
    /// * `other` - The right-hand operand.
    ///
    /// # Returns
    /// Vector perpendicular to both inputs. Magnitude = `|a|*|b|*sin(theta)`.
    ///
    /// # Examples
    /// ```cpp
    /// Vec3 up = Vec3::Forward().Cross(Vec3::Right()); // {0, 0, 1}
    /// ```
    Vec3 cross(const Vec3& other) const;
    /// Uppercase alias for `cross()`.
    Vec3 Cross(const Vec3& other) const { return cross(other); }
    /// Computes the Euclidean length (SIMD-accelerated).
    ///
    /// # Returns
    /// `sqrt(x*x + y*y + z*z)`. Use `lengthSq()` for distance comparisons.
    float length() const;
    /// Uppercase alias for `length()`.
    float Length() const { return length(); }
    /// Computes the squared Euclidean length (avoids sqrt).
    ///
    /// # Returns
    /// `x*x + y*y + z*z`. Prefer over `length()` for distance comparisons.
    float lengthSq() const { return x * x + y * y + z * z; }
    /// Uppercase alias for `lengthSq()`.
    float LengthSq() const { return lengthSq(); }
    /// Returns a unit-length copy of this vector (SIMD-accelerated).
    ///
    /// # Returns
    /// Vector with length 1. Returns zero vector if input is zero-length.
    Vec3 normalize() const;
    /// Uppercase alias for `normalize()`.
    Vec3 Normalized() const { return normalize(); }
    /// Returns component-wise maximum (SIMD-accelerated).
    ///
    /// # Arguments
    /// * `other` — Vector to compare with.
    ///
    /// # Returns
    /// Vector where each component is the maximum of the two inputs: `(max(x, other.x), max(y, other.y), max(z, other.z))`.
    Vec3 max(const Vec3& other) const;
    /// Returns component-wise minimum (SIMD-accelerated).
    ///
    /// # Arguments
    /// * `other` — Vector to compare with.
    ///
    /// # Returns
    /// Vector where each component is the minimum of the two inputs: `(min(x, other.x), min(y, other.y), min(z, other.z))`.
    Vec3 min(const Vec3& other) const;

    /// Zero vector (0, 0, 0).
    static Vec3 Zero() { return {0, 0, 0}; }
    /// Unit vector (1, 1, 1).
    static Vec3 One() { return {1, 1, 1}; }
    /// Unit vector along X axis (1, 0, 0).
    static Vec3 UnitX() { return {1, 0, 0}; }
    /// Unit vector along Y axis (0, 1, 0).
    static Vec3 UnitY() { return {0, 1, 0}; }
    /// Unit vector along Z axis (0, 0, 1).
    static Vec3 UnitZ() { return {0, 0, 1}; }
    /// World "up" direction (0, 0, 1) — Z-up coordinate system (X-forward, Y-right, Z-up).
    static Vec3 Up() { return UnitZ(); }
    /// World "right" direction (0, 1, 0) — Z-up coordinate system.
    static Vec3 Right() { return UnitY(); }
    /// World "forward" direction (1, 0, 0) — Z-up coordinate system.
    static Vec3 Forward() { return UnitX(); }
    /// Linear interpolation between a and b (t in [0,1]).
    static Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
    /// Component-wise minimum.
    static Vec3 Min(const Vec3& a, const Vec3& b) { return a.min(b); }
    /// Component-wise maximum.
    static Vec3 Max(const Vec3& a, const Vec3& b) { return a.max(b); }
    /// Load from XMFLOAT3 (DirectXMath compatible).
    static Vec3 FromFloat3(const XMFLOAT3& f) { return {f.x, f.y, f.z}; }

    // ─────────────────────────────────────────────────────────────────────────
    // Geometric & Utility Methods
    // ─────────────────────────────────────────────────────────────────────────

    /// Euclidean distance to another point (SIMD-accelerated).
    ///
    /// # Arguments
    /// * `other` — Target point.
    ///
    /// # Returns
    /// `(*this - other).length()`. Use `distance_squared_to()` for comparisons.
    float distance_to(const Vec3& other) const;

    /// Squared Euclidean distance to another point (avoids sqrt).
    ///
    /// # Arguments
    /// * `other` — Target point.
    ///
    /// # Returns
    /// `(*this - other).lengthSq()`. Prefer over `distance_to()` for distance comparisons.
    float distance_squared_to(const Vec3& other) const {
        return (*this - other).lengthSq();
    }

    /// Unsigned angle between this vector and `other` in radians.
    ///
    /// Both vectors are treated as directions from the origin.
    /// Result is always in [0, pi].
    ///
    /// # Arguments
    /// * `other` — Direction to measure angle to.
    ///
    /// # Returns
    /// Angle in radians in [0, pi]. Returns 0 for zero-length vectors.
    float angle_to(const Vec3& other) const;

    /// Signed angle between this vector and `other` around `axis` in radians.
    ///
    /// The sign follows the right-hand rule: positive if `other` is counter-clockwise
    /// from `*this` when viewed from the tip of `axis`.
    ///
    /// # Arguments
    /// * `other` — Direction to measure angle to.
    /// * `axis` — Rotation axis (must be normalized).
    ///
    /// # Returns
    /// Angle in radians in [-pi, pi].
    float signed_angle_to(const Vec3& other, const Vec3& axis) const;

    /// Rotates this vector around `axis` by `angle` radians (SIMD-accelerated).
    ///
    /// Uses DirectXMath `XMMatrixRotationAxis` for the rotation.
    ///
    /// # Arguments
    /// * `axis` — Rotation axis (must be normalized).
    /// * `angle` — Rotation angle in radians.
    ///
    /// # Returns
    /// Rotated copy of this vector.
    Vec3 rotated(const Vec3& axis, float angle) const;

    /// Spherical linear interpolation between this vector and `other`.
    ///
    /// Interpolates along the great arc between the two directions, preserving
    /// angular velocity. Both vectors should be non-zero.
    ///
    /// # Arguments
    /// * `other` — Target direction.
    /// * `t` — Interpolation factor in [0, 1].
    ///
    /// # Returns
    /// Interpolated vector. Falls back to linear interpolation for nearly
    /// parallel or anti-parallel vectors.
    Vec3 slerp(const Vec3& other, float t) const;

    /// Catmull-Rom cubic interpolation between `*this` (a) and `b`.
    ///
    /// Uses `pre_a` and `post_b` as tangent control points. Produces smooth
    /// C1-continuous curves through the control points.
    ///
    /// # Arguments
    /// * `b` — End point (at t=1).
    /// * `pre_a` — Control point before `*this`.
    /// * `post_b` — Control point after `b`.
    /// * `t` — Interpolation factor in [0, 1].
    ///
    /// # Returns
    /// Cubic-interpolated position.
    Vec3 cubic_interpolate(const Vec3& b, const Vec3& pre_a, const Vec3& post_b, float t) const;

    /// Projects this vector onto the plane defined by `normal`.
    ///
    /// Removes the component along `normal`: `*this - normal * dot(normal)`.
    /// Useful for sliding collision response.
    ///
    /// # Arguments
    /// * `normal` — Plane normal (must be normalized).
    ///
    /// # Returns
    /// Vector projected onto the plane.
    Vec3 slide(const Vec3& normal) const { return *this - normal * dot(normal); }

    /// Bounces this vector off a surface with the given `normal`.
    ///
    /// Equivalent to `-reflect(normal)`. The result points away from the surface.
    ///
    /// # Arguments
    /// * `normal` — Surface normal (must be normalized).
    ///
    /// # Returns
    /// Bounced vector.
    Vec3 bounce(const Vec3& normal) const { return -reflect(normal); }

    /// Reflects this vector across the plane defined by `normal`.
    ///
    /// Mirror reflection: `2 * normal * dot(normal) - *this`.
    ///
    /// # Arguments
    /// * `normal` — Reflection plane normal (must be normalized).
    ///
    /// # Returns
    /// Reflected vector.
    Vec3 reflect(const Vec3& normal) const { return normal * (2.0f * dot(normal)) - *this; }

    /// Moves this vector toward `target` by at most `delta` units.
    ///
    /// If the distance to `target` is less than `delta`, returns `target`.
    ///
    /// # Arguments
    /// * `target` — Destination point.
    /// * `delta` — Maximum movement distance (must be >= 0).
    ///
    /// # Returns
    /// Vector moved toward target by at most `delta`.
    Vec3 move_toward(const Vec3& target, float delta) const;

    /// Normalized direction from this point to `other`.
    ///
    /// # Arguments
    /// * `other` — Target point.
    ///
    /// # Returns
    /// Unit vector pointing from `*this` to `other`. Returns zero vector if points coincide.
    Vec3 direction_to(const Vec3& other) const { return (other - *this).normalize(); }

    /// Component-wise absolute value.
    ///
    /// # Returns
    /// Vector with `(|x|, |y|, |z|)`.
    Vec3 abs() const { return {std::abs(x), std::abs(y), std::abs(z)}; }

    /// Component-wise floor.
    ///
    /// # Returns
    /// Vector with each component rounded down to the nearest integer.
    Vec3 floor() const { return {std::floor(x), std::floor(y), std::floor(z)}; }

    /// Component-wise ceil.
    ///
    /// # Returns
    /// Vector with each component rounded up to the nearest integer.
    Vec3 ceil() const { return {std::ceil(x), std::ceil(y), std::ceil(z)}; }

    /// Component-wise round to nearest integer.
    ///
    /// # Returns
    /// Vector with each component rounded to the nearest integer (half rounds away from zero).
    Vec3 round() const { return {std::round(x), std::round(y), std::round(z)}; }

    /// Component-wise sign extraction.
    ///
    /// # Returns
    /// Vector with each component replaced by -1, 0, or 1 based on its sign.
    Vec3 sign() const { return {::uz::Math::sign(x), ::uz::Math::sign(y), ::uz::Math::sign(z)}; }

    /// Component-wise clamp between `min_v` and `max_v`.
    ///
    /// # Arguments
    /// * `min_v` — Minimum values per component.
    /// * `max_v` — Maximum values per component.
    ///
    /// # Returns
    /// Vector with each component clamped to [min_v[i], max_v[i]].
    Vec3 clamp(const Vec3& min_v, const Vec3& max_v) const {
        return {::uz::Math::clamp(x, min_v.x, max_v.x),
                ::uz::Math::clamp(y, min_v.y, max_v.y),
                ::uz::Math::clamp(z, min_v.z, max_v.z)};
    }

    /// Snaps each component to the nearest multiple of the corresponding step.
    ///
    /// # Arguments
    /// * `step` — Step size per component. Zero step leaves that component unchanged.
    ///
    /// # Returns
    /// Snapped vector.
    Vec3 snap(const Vec3& step) const {
        return {Math::snap(x, step.x), Math::snap(y, step.y), Math::snap(z, step.z)};
    }

    /// Positive float modulo applied to each component.
    ///
    /// # Arguments
    /// * `mod` — Modulus value (must be > 0).
    ///
    /// # Returns
    /// Vector with each component wrapped to [0, mod).
    Vec3 posmod(float mod) const {
        return {Math::fposmod(x, mod), Math::fposmod(y, mod), Math::fposmod(z, mod)};
    }

    /// Per-component positive float modulo.
    ///
    /// # Arguments
    /// * `modv` — Modulus per component (each must be > 0).
    ///
    /// # Returns
    /// Vector with each component wrapped to [0, modv[i]).
    Vec3 posmodv(const Vec3& modv) const {
        return {Math::fposmod(x, modv.x), Math::fposmod(y, modv.y), Math::fposmod(z, modv.z)};
    }

    /// Returns true if this vector is approximately zero (all components within `EPSILON`).
    bool is_zero_approx() const {
        return Math::is_zero_approx(x) && Math::is_zero_approx(y) && Math::is_zero_approx(z);
    }

    /// Returns true if this vector is approximately equal to `other` (per-component `EPSILON`).
    ///
    /// # Arguments
    /// * `other` — Vector to compare.
    bool is_equal_approx(const Vec3& other) const {
        return Math::is_equal_approx(x, other.x) &&
               Math::is_equal_approx(y, other.y) &&
               Math::is_equal_approx(z, other.z);
    }

    /// Returns true if this vector has approximately unit length.
    bool is_normalized() const {
        return Math::is_equal_approx(lengthSq(), 1.0f);
    }

    /// Returns a copy of this vector with its length clamped to `max_len`.
    ///
    /// If the vector is shorter than `max_len`, returns it unchanged.
    ///
    /// # Arguments
    /// * `max_len` — Maximum allowed length (default 1.0).
    ///
    /// # Returns
    /// Length-clamped vector.
    Vec3 limit_length(float max_len = 1.0f) const;

    /// Returns any vector perpendicular to this one.
    ///
    /// The result is not deterministic in orientation around `*this`, but is
    /// guaranteed to be perpendicular and non-zero (if `*this` is non-zero).
    ///
    /// # Returns
    /// A perpendicular vector. Zero vector if `*this` is zero.
    Vec3 get_any_perpendicular() const;

    /// Encodes a unit normal into a Vec2 in [0,1]^2 using octahedron mapping.
    ///
    /// Compact 2-component encoding for GBuffer normal storage.
    /// The input vector MUST be normalized for correct results.
    ///
    /// # Returns
    /// Vec2 with both components in [0, 1].
    ///
    /// # See Also
    /// `octahedron_decode()` for the inverse operation.
    Vec2 octahedron_encode() const;

    /// Decodes an octahedron-encoded Vec2 back to a unit normal.
    ///
    /// Inverse of `octahedron_encode()`.
    ///
    /// # Arguments
    /// * `oct` — Encoded coordinates in [0, 1]^2.
    ///
    /// # Returns
    /// Normalized 3D direction vector.
    static Vec3 octahedron_decode(const Vec2& oct);

    /// Convert to XMFLOAT3 for DirectXMath compatibility.
    XMFLOAT3 ToFloat3() const { return {x, y, z}; }
};

/// Scalar-left multiplication for Vec3 (`2.0f * vec`).
inline Vec3 operator*(float scalar, const Vec3& v) {
    return v * scalar;
}

} // namespace uz
