/// UzEngine Quat — Unit quaternion for 3D rotation.
///
/// Provides construction from axis-angle/Euler/matrix, multiplication, slerp/lerp interpolation,
/// vector rotation, and conversion to Mat4. Backed by DirectXMath SIMD intrinsics where applicable.

#pragma once

#include "vector3.h"
#include "math_funcs.h"

namespace uz {

// Forward declaration for toMatrix() return type.
struct Mat4;

// ─────────────────────────────────────────────────────────────────────────────
// Quat — Quaternion (x, y, z, w)
// ─────────────────────────────────────────────────────────────────────────────

/// Unit quaternion for 3D rotation (SIMD-accelerated via DirectXMath).
///
/// Stores (x, y, z, w) where (x, y, z) is the imaginary part and w is the real part.
/// Default-constructed to identity rotation (0, 0, 0, 1).
/// Prefer quaternions over Euler angles to avoid gimbal lock and enable smooth interpolation.
///
/// # Memory Layout
/// `[x: f32, y: f32, z: f32, w: f32]` — 16 bytes, same layout as XMFLOAT4.
///
/// # Examples
/// ```cpp
/// Quat q = Quat::FromAxisAngle(Vec3::Up(), XM_PIDIV2); // 90 deg around Z (world up)
/// Vec3 rotated = q * Vec3::Forward();                    // rotates forward vector
/// Mat4 rotMat = q.toMatrix();                            // convert to rotation matrix
/// Quat blended = Quat::Slerp(a, b, 0.5f);              // smooth interpolation
/// ```
struct Quat {
    float x, y, z, w;

    /// Identity rotation (no rotation).
    Quat() : x(0), y(0), z(0), w(1) {}
    /// Construct from explicit components.
    constexpr Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    /// Construct rotation around axis by angle (radians).
    Quat(const Vec3& axis, float angle);
    /// Convert from DirectXMath XMVECTOR.
    explicit Quat(const DirectX::XMVECTOR& xm) { DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(this), xm); }
    /// Convert from DirectXMath XMFLOAT4.
    explicit Quat(const DirectX::XMFLOAT4& f4) : x(f4.x), y(f4.y), z(f4.z), w(f4.w) {}

    /// Returns the identity quaternion (no rotation).
    static Quat Identity() { return {0, 0, 0, 1}; }
    /// Creates a quaternion from an axis and angle.
    ///
    /// # Arguments
    /// * `axis` - Rotation axis (should be normalized).
    /// * `angle` - Rotation angle in radians.
    static Quat FromAxisAngle(const Vec3& axis, float angle);
    /// Creates a quaternion from Euler angles using the UE5-parity Z-up convention.
    ///
    /// Matches Unreal Engine 4/5 rotation semantics for a Z-up, X-forward coordinate system.
    /// Application order (intrinsic): Roll(X) → Pitch(Y) → Yaw(Z).
    ///
    /// # Arguments
    /// * `pitch` - Elevation from horizon, around the Y-right axis (radians).
    ///             -90° points up (noon/zenith), +90° points underground.
    /// * `yaw`   - Compass bearing, around the Z-up axis (radians).
    ///             0° faces East (+X), +90° faces North (+Y).
    /// * `roll`  - Barrel roll, around the X-forward axis (radians).
    ///
    /// # Example — directional light angles
    /// ```
    /// // Noon sun (overhead): pitch=-90°, yaw=0°
    /// Quat sun = Quat::FromEuler(-XM_PIDIV2, 0.f, 0.f);
    /// // Vec3::Forward() = [1,0,0] → sunDir = [0,0,1] (pointing up toward surface)
    ///
    /// // 60° altitude from horizon, East-facing: pitch=-60°, yaw=0°
    /// Quat sun2 = Quat::FromEuler(-XM_PI/3.f, 0.f, 0.f);
    /// ```
    ///
    /// @ref Shoemake 1994 "Euler Angle Conversion" — Graphic Gems IV, ZYX order
    static Quat FromEuler(float pitch, float yaw, float roll);
    /// Extracts rotation quaternion from a rotation matrix.
    static Quat FromMatrix(const Mat4& m);

    /// Converts to XMVECTOR for SIMD operations.
    DirectX::XMVECTOR toXM() const { return DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(this)); }

    /// Get X component (imaginary i).
    float X() const { return x; }
    /// Get Y component (imaginary j).
    float Y() const { return y; }
    /// Get Z component (imaginary k).
    float Z() const { return z; }
    /// Get W component (real/scalar part).
    float W() const { return w; }

    /// Quaternion multiplication (combines rotations: result = this * other).
    Quat operator*(const Quat& other) const;
    /// Rotates a vector by this quaternion.
    Vec3 operator*(const Vec3& v) const;
    /// Exact equality comparison (no epsilon tolerance).
    ///
    /// # Returns
    /// `true` if all four components (x, y, z, w) are bitwise equal.
    bool operator==(const Quat& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }
    /// Exact inequality comparison.
    ///
    /// # Returns
    /// `true` if any component differs.
    bool operator!=(const Quat& other) const { return !(*this == other); }

    /// Returns the conjugate (negated imaginary part). For unit quaternions, equals inverse.
    Quat conjugate() const { return {-x, -y, -z, w}; }
    /// Returns the multiplicative inverse (SIMD-accelerated).
    Quat inverse() const;
    /// Uppercase alias for `inverse()`.
    Quat Inverse() const { return inverse(); }
    /// Returns a unit-length quaternion (SIMD-accelerated).
    Quat normalize() const;
    /// Uppercase alias for `normalize()`.
    Quat Normalized() const { return normalize(); }
    /// Computes the 4D dot product of two quaternions.
    ///
    /// Useful for measuring alignment: `dot ≈ 1` means same rotation,
    /// `dot ≈ -1` means opposite hemisphere (same rotation, negated representation).
    ///
    /// # Arguments
    /// * `other` — Quaternion to dot against.
    ///
    /// # Returns
    /// `x*other.x + y*other.y + z*other.z + w*other.w`.
    float dot(const Quat& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }
    /// Uppercase alias for `dot()`.
    float Dot(const Quat& other) const { return dot(other); }
    /// Computes the Euclidean length (4D magnitude) of this quaternion.
    ///
    /// For unit quaternions this should be approximately 1.0.
    /// Use `is_normalized()` for a tolerance-based check.
    ///
    /// # Returns
    /// `sqrt(x*x + y*y + z*z + w*w)`. Always non-negative.
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    /// Uppercase alias for `length()`.
    float Length() const { return length(); }
    /// Rotates a 3D vector by this quaternion.
    ///
    /// Equivalent to `operator*(const Vec3&)`. Provided as a named alternative
    /// for readability when the intent is explicitly vector rotation.
    ///
    /// # Arguments
    /// * `v` — Vector to rotate.
    ///
    /// # Returns
    /// Rotated vector `q * v * q^-1` (computed via `operator*`).
    Vec3 RotateVector(const Vec3& v) const { return *this * v; }

    /// Spherical linear interpolation (constant angular velocity, SIMD-accelerated).
    ///
    /// # Arguments
    /// * `other` - Target quaternion.
    /// * `t` - Interpolation factor in [0, 1].
    ///
    /// # Returns
    /// Interpolated quaternion. `t=0` returns `*this`, `t=1` returns `other`.
    Quat slerp(const Quat& other, float t) const;
    /// Uppercase alias for `slerp()`.
    Quat Slerp(const Quat& other, float t) const { return slerp(other, t); }
    /// Static slerp between two quaternions.
    static Quat Slerp(const Quat& a, const Quat& b, float t) { return a.slerp(b, t); }
    /// Linear interpolation (faster but non-constant angular velocity).
    ///
    /// # Arguments
    /// * `other` - Target quaternion.
    /// * `t` - Interpolation factor in [0, 1].
    Quat lerp(const Quat& other, float t) const;
    /// Uppercase alias for `lerp()`.
    Quat Lerp(const Quat& other, float t) const { return lerp(other, t); }

    /// Quaternion logarithm for unit quaternions.
    ///
    /// For a unit quaternion q = (w, v) where v is the imaginary vector part,
    /// computes log(q) = (0, theta * v / |v|) where theta = acos(w).
    /// Useful for quaternion spline interpolation and blending operations.
    ///
    /// # Returns
    /// A pure quaternion (w=0) representing the logarithm. If the quaternion is
    /// near identity (|v| ≈ 0), returns the zero quaternion to avoid division by zero.
    Quat log() const;

    /// Quaternion exponential for pure quaternions.
    ///
    /// For a pure quaternion q = (0, v), computes exp(q) = (cos|v|, sin|v| * v / |v|).
    /// Inverse operation of `log()` for unit quaternions.
    ///
    /// # Returns
    /// A unit quaternion. If |v| ≈ 0, returns the identity quaternion.
    Quat exp() const;

    /// Shortest angle between this rotation and another, in radians.
    ///
    /// Computes the minimal angle required to rotate from one orientation to another.
    /// Always returns a non-negative value in [0, pi].
    ///
    /// # Arguments
    /// * `other` — The target rotation to measure against.
    ///
    /// # Returns
    /// Angle in radians in [0, pi]. Both quaternions should be normalized for correct results.
    float angle_to(const Quat& other) const;

    /// Extracts Euler angles (pitch, yaw, roll) from this quaternion.
    ///
    /// Inverse of `FromEuler()`. Uses ZYX decomposition matching the UE5-parity convention:
    /// - pitch = rotation around Y-right axis (Vec3.x)
    /// - yaw   = rotation around Z-up axis    (Vec3.y)
    /// - roll  = rotation around X-forward axis (Vec3.z)
    ///
    /// # Returns
    /// Vec3(pitch, yaw, roll) in radians. Subject to gimbal lock when pitch ≈ ±90°.
    ///
    /// @ref Shoemake 1994 "Euler Angle Conversion" — Graphic Gems IV, ZYX order
    Vec3 get_euler() const;

    /// Extracts the rotation axis from this quaternion.
    ///
    /// # Returns
    /// Normalized axis of rotation. Returns Vec3::UnitX() if the quaternion is
    /// near identity (no meaningful rotation axis).
    Vec3 get_axis() const;

    /// Extracts the rotation angle from this quaternion, in radians.
    ///
    /// # Returns
    /// Angle in [0, 2*pi] representing the rotation magnitude.
    float get_angle() const;

    /// Returns true if all components are approximately equal to another quaternion.
    ///
    /// Uses `Math::EPSILON` for per-component comparison.
    ///
    /// # Arguments
    /// * `other` — Quaternion to compare against.
    bool is_equal_approx(const Quat& other) const {
        return Math::is_equal_approx(x, other.x) && Math::is_equal_approx(y, other.y) &&
               Math::is_equal_approx(z, other.z) && Math::is_equal_approx(w, other.w);
    }

    /// Returns true if this quaternion has unit length (within `Math::UNIT_EPSILON`).
    bool is_normalized() const {
        return std::abs(length() * length() - 1.0f) < Math::UNIT_EPSILON;
    }

    /// Spherical cubic interpolation (SQUAD) for smooth quaternion curves.
    ///
    /// Produces a C1-continuous path through quaternion keyframes, analogous to
    /// Catmull-Rom splines for scalar values. Suitable for smooth camera paths
    /// and animation blending.
    ///
    /// # Arguments
    /// * `b` — End quaternion (at t=1).
    /// * `pre_a` — Control quaternion before `*this` (used for tangent computation).
    /// * `post_b` — Control quaternion after `b` (used for tangent computation).
    /// * `t` — Interpolation factor in [0, 1].
    ///
    /// # Returns
    /// Smoothly interpolated quaternion. At t=0 returns `*this`, at t=1 returns `b`.
    Quat spherical_cubic_interpolate(const Quat& b, const Quat& pre_a, const Quat& post_b, float t) const;

    /// Converts this quaternion to a 4x4 rotation matrix.
    Mat4 toMatrix() const;
    /// Uppercase alias for `toMatrix()`. Defined out-of-line (Mat4 is incomplete here).
    Mat4 ToMatrix() const;
};

} // namespace uz
