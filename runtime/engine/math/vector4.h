/// UzEngine Vec4 — 4D floating-point vector for homogeneous coordinates.
///
/// Provides arithmetic operators, dot product, length, and normalization for 4D vectors.
/// Used for homogeneous coordinates (w=1 points, w=0 directions), RGBA colors, and SIMD interop.

#pragma once

#include "vector3.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Vec4 — 4D float vector
// ─────────────────────────────────────────────────────────────────────────────

/// 4D floating-point vector for homogeneous coordinates and SIMD operations.
///
/// Stores x, y, z, w as `float` fields. Used for matrix-vector multiplication
/// (w=1 for points, w=0 for directions), color (RGBA), and shader constants.
///
/// # Memory Layout
/// `[x: f32, y: f32, z: f32, w: f32]` — 16 bytes. Naturally aligned for XMVECTOR.
///
/// # Examples
/// ```cpp
/// Vec4 pos{1.0f, 2.0f, 3.0f, 1.0f};  // homogeneous point
/// Vec4 dir{1.0f, 0.0f, 0.0f, 0.0f};  // direction (w=0)
/// Vec3 xyz = pos.xyz();               // extract {1, 2, 3}
/// ```
struct Vec4 {
    /// X component.
    float x;
    /// Y component.
    float y;
    /// Z component.
    float z;
    /// W component (1.0 for points, 0.0 for directions in homogeneous space).
    float w;

    /// Zero-initialized vector (0, 0, 0, 0).
    Vec4() : x(0), y(0), z(0), w(0) {}
    /// Construct from explicit components.
    constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    /// Construct from Vec3 + W component (e.g., position w=1, direction w=0).
    constexpr Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
    /// Construct uniform vector (scalar, scalar, scalar, scalar).
    explicit constexpr Vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    /// Load from memory (pointer to at least 4 floats).
    explicit Vec4(const float* ptr) : x(ptr[0]), y(ptr[1]), z(ptr[2]), w(ptr[3]) {}
    /// Convert from DirectXMath XMVECTOR.
    explicit Vec4(const DirectX::XMVECTOR& xm) { DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(this), xm); }
    /// Convert from DirectXMath XMFLOAT4.
    explicit Vec4(const DirectX::XMFLOAT4& f4) : x(f4.x), y(f4.y), z(f4.z), w(f4.w) {}

    /// Convert to XMVECTOR for SIMD operations.
    DirectX::XMVECTOR toXM() const { return DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(this)); }

    /// Extract XYZ as Vec3 (drops W).
    Vec3 xyz() const { return {x, y, z}; }
    /// Uppercase alias.
    Vec3 XYZ() const { return xyz(); }

    /// Pointer to contiguous float storage for array-like access.
    float* data() { return &x; }
    /// Const pointer to contiguous float storage.
    const float* data() const { return &x; }
    /// Array indexing: [0]=x, [1]=y, [2]=z, [3]=w.
    float& operator[](int idx) { return data()[idx]; }
    /// Const array indexing.
    float operator[](int idx) const { return data()[idx]; }

    /// Get X component (uppercase for test compatibility).
    float X() const { return x; }
    /// Get Y component.
    float Y() const { return y; }
    /// Get Z component.
    float Z() const { return z; }
    /// Get W component.
    float W() const { return w; }

    /// Component-wise vector addition.
    ///
    /// # Returns
    /// New Vec4 where each component is the sum of the corresponding components.
    Vec4 operator+(const Vec4& other) const {
        return {x + other.x, y + other.y, z + other.z, w + other.w};
    }
    /// Component-wise vector subtraction.
    ///
    /// # Returns
    /// New Vec4 where each component is the difference of the corresponding components.
    Vec4 operator-(const Vec4& other) const {
        return {x - other.x, y - other.y, z - other.z, w - other.w};
    }
    /// Scalar multiplication (scales all components uniformly).
    ///
    /// # Returns
    /// New Vec4 with each component multiplied by `scalar`.
    Vec4 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar, w * scalar}; }
    /// Scalar division (divides all components uniformly).
    ///
    /// # Returns
    /// New Vec4 with each component divided by `scalar`. Undefined if `scalar` is zero.
    Vec4 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar, w / scalar}; }
    /// Unary negation (flips sign of all components).
    ///
    /// # Returns
    /// New Vec4 with all components negated: `(-x, -y, -z, -w)`.
    Vec4 operator-() const { return {-x, -y, -z, -w}; }
    /// In-place component-wise addition.
    ///
    /// # Returns
    /// Reference to `*this` after adding `other` component-wise.
    Vec4& operator+=(const Vec4& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }
    /// In-place component-wise subtraction.
    ///
    /// # Returns
    /// Reference to `*this` after subtracting `other` component-wise.
    Vec4& operator-=(const Vec4& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }
    /// In-place scalar multiplication.
    ///
    /// # Returns
    /// Reference to `*this` after multiplying all components by `scalar`.
    Vec4& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    /// Exact equality comparison (no epsilon tolerance).
    ///
    /// # Returns
    /// `true` if all four components are bitwise equal.
    bool operator==(const Vec4& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }
    /// Exact inequality comparison.
    ///
    /// # Returns
    /// `true` if any component differs.
    bool operator!=(const Vec4& other) const { return !(*this == other); }

    /// Computes the 4D dot product (SIMD-accelerated).
    ///
    /// # Returns
    /// `x*other.x + y*other.y + z*other.z + w*other.w`.
    float dot(const Vec4& other) const;
    /// Uppercase alias for `dot()`.
    float Dot(const Vec4& other) const { return dot(other); }
    /// Computes the 4D Euclidean length (SIMD-accelerated).
    ///
    /// # Returns
    /// `sqrt(x*x + y*y + z*z + w*w)`. Always non-negative.
    float length() const;
    /// Uppercase alias for `length()`.
    float Length() const { return length(); }
    /// Computes the squared 4D length (avoids sqrt).
    ///
    /// # Returns
    /// `x*x + y*y + z*z + w*w`. Useful for distance comparisons without the sqrt cost.
    float lengthSq() const { return x * x + y * y + z * z + w * w; }
    /// Uppercase alias for `lengthSq()`.
    float LengthSq() const { return lengthSq(); }
    /// Returns a unit-length copy (SIMD-accelerated).
    ///
    /// # Returns
    /// Normalized vector with length 1.0. Result is undefined if the input has zero length.
    Vec4 normalize() const;
    /// Uppercase alias for `normalize()`.
    Vec4 Normalized() const { return normalize(); }
};

/// Scalar-left multiplication for Vec4 (`2.0f * vec`).
inline Vec4 operator*(float scalar, const Vec4& v) {
    return v * scalar;
}

} // namespace uz
