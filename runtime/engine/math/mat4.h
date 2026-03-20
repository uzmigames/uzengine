/// UzEngine Mat4 — 4x4 matrix for transforms, projections, and view calculations.
///
/// Wraps DirectXMath XMFLOAT4X4 with SIMD-accelerated operations for model/view/projection
/// math, TRS construction, inversion, and point/direction transforms.

#pragma once

#include <cstring>

#include "vector4.h"

namespace uz {

// Forward declaration for TRS overload that takes Quat.
struct Quat;

// ─────────────────────────────────────────────────────────────────────────────
// Mat4 — 4x4 matrix (column-major, DirectX convention)
// ─────────────────────────────────────────────────────────────────────────────

/// 4x4 matrix for transforms, projections, and view calculations (SIMD-accelerated).
///
/// Wraps `XMFLOAT4X4` (column-major, DirectX row-vector convention).
/// Default-constructed to identity. All operations delegate to DirectXMath SIMD intrinsics.
///
/// # Memory Layout
/// 16 floats (64 bytes) stored as `XMFLOAT4X4`. Row-major in memory
/// (row 0 = _11,_12,_13,_14), but multiplication follows DirectX row-vector convention.
///
/// # Examples
/// ```cpp
/// Mat4 view = Mat4::LookAt(eye, target, Vec3::Up());
/// Mat4 proj = Mat4::Perspective(XM_PIDIV4, 16.0f/9.0f, 0.1f, 1000.0f);
/// Mat4 mvp = model * view * proj;
/// Vec3 worldPos = mvp.getTranslation();
/// ```
struct Mat4 {
    /// Raw 4x4 float storage (DirectXMath compatible).
    DirectX::XMFLOAT4X4 m;

    /// Default: identity matrix.
    Mat4() { DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixIdentity()); }
    /// Construct from DirectXMath XMMATRIX.
    Mat4(const DirectX::XMMATRIX& xm) { DirectX::XMStoreFloat4x4(&m, xm); }
    /// Construct from DirectXMath XMFLOAT4X4.
    Mat4(const DirectX::XMFLOAT4X4& f4x4) : m(f4x4) {}
    /// Load from memory (pointer to 16 floats, row-major layout).
    explicit Mat4(const float* ptr) { std::memcpy(&m, ptr, 16 * sizeof(float)); }

    /// Returns the 4x4 identity matrix.
    static Mat4 Identity();
    /// Returns a 4x4 zero matrix (all elements 0).
    static Mat4 Zero();
    /// Creates a translation matrix from individual components.
    static Mat4 Translation(float x, float y, float z);
    /// Creates a translation matrix from a position vector.
    static Mat4 Translation(const Vec3& pos);
    /// Creates a scaling matrix from individual axis scales.
    static Mat4 Scaling(float x, float y, float z);
    /// Creates a scaling matrix from a scale vector.
    static Mat4 Scaling(const Vec3& scale);
    /// Creates a uniform scaling matrix.
    static Mat4 Scaling(float uniform) { return Scaling(uniform, uniform, uniform); }
    /// Creates a rotation matrix around the X axis.
    ///
    /// # Arguments
    /// * `angle` - Rotation in radians.
    static Mat4 RotationX(float angle);
    /// Creates a rotation matrix around the Y axis.
    ///
    /// # Arguments
    /// * `angle` - Rotation in radians.
    static Mat4 RotationY(float angle);
    /// Creates a rotation matrix around the Z axis.
    ///
    /// # Arguments
    /// * `angle` - Rotation in radians.
    static Mat4 RotationZ(float angle);
    /// Creates a left-handed perspective projection matrix.
    ///
    /// # Arguments
    /// * `fov` - Vertical field of view in radians.
    /// * `aspect` - Width/height aspect ratio.
    /// * `near_plane` - Near clipping plane distance (must be > 0).
    /// * `far_plane` - Far clipping plane distance (must be > near_plane).
    static Mat4 Perspective(float fov, float aspect, float near_plane, float far_plane);
    /// Creates a left-handed look-at view matrix.
    ///
    /// # Arguments
    /// * `eye` - Camera position in world space.
    /// * `target` - Point the camera looks at.
    /// * `up` - World up direction (typically `Vec3::Up()`).
    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

    /// Creates a left-handed orthographic projection matrix (standard-Z: near→0, far→1).
    ///
    /// Used for shadow map projections where standard-Z convention is required.
    /// For main-view orthographic projection, use OrthographicReversedZ() instead.
    ///
    /// # Arguments
    /// * `width` - View width in world units.
    /// * `height` - View height in world units.
    /// * `near_plane` - Near clipping plane distance.
    /// * `far_plane` - Far clipping plane distance.
    static Mat4 Orthographic(float width, float height, float near_plane, float far_plane);

    /// Creates a left-handed orthographic projection matrix with reversed-Z (near→1, far→0).
    ///
    /// Standard FReversedZOrthoMatrix (OrthoMatrix.h:65-82). Use for main-view
    /// orthographic rendering (debug views, 2D overlays, UI) where reversed-Z is active.
    /// Shadow maps should use the standard Orthographic() instead.
    ///
    /// # Arguments
    /// * `width` - View width in world units.
    /// * `height` - View height in world units.
    /// * `near_plane` - Near clipping plane distance.
    /// * `far_plane` - Far clipping plane distance.
    static Mat4 OrthographicReversedZ(float width, float height, float near_plane, float far_plane);

    /// Converts to XMMATRIX for SIMD operations (zero-cost in release).
    DirectX::XMMATRIX toXM() const { return DirectX::XMLoadFloat4x4(&m); }

    /// Pointer to the first element (_11) for raw float access.
    float* data() { return &m._11; }
    /// Const pointer to the first element.
    const float* data() const { return &m._11; }
    /// Element access by row and column (0-indexed).
    float& operator()(int row, int col) { return data()[row * 4 + col]; }
    /// Const element access by row and column.
    float operator()(int row, int col) const { return data()[row * 4 + col]; }

    /// Matrix multiplication (SIMD-accelerated).
    Mat4 operator*(const Mat4& other) const;
    /// Transforms a Vec4 by this matrix.
    Vec4 operator*(const Vec4& v) const;
    /// Transforms a Vec3 as a 3D point (w=1 assumed, SIMD-accelerated).
    Vec3 operator*(const Vec3& v) const;
    /// In-place matrix multiplication.
    Mat4& operator*=(const Mat4& other);

    /// Returns the transpose of this matrix.
    Mat4 transpose() const;
    /// Uppercase alias for `transpose()`.
    Mat4 Transposed() const { return transpose(); }
    /// Returns the inverse of this matrix (SIMD-accelerated).
    ///
    /// # Returns
    /// Inverse matrix. Result is undefined if matrix is singular (determinant near zero).
    Mat4 inverse() const;
    /// Uppercase alias for `inverse()`.
    Mat4 Inverse() const { return inverse(); }
    /// Computes the determinant of this matrix.
    float determinant() const;
    /// Uppercase alias for `determinant()`.
    float Determinant() const { return determinant(); }

    /// Transforms a point by this matrix (applies full 4x4 including translation).
    Vec3 TransformPoint(const Vec3& p) const { return *this * p; }
    /// Transforms a direction vector using only the 3x3 rotation/scale part (no translation).
    ///
    /// Internally sets w=0 before multiplying to exclude the translation row.
    /// Use this for normals, tangents, and displacement vectors that should not be affected
    /// by the matrix's position component.
    ///
    /// # Arguments
    /// * `d` — Direction vector to transform.
    ///
    /// # Returns
    /// Direction vector transformed by the upper-left 3x3 submatrix.
    Vec3 TransformDirection(const Vec3& d) const {
        // Transform direction (no translation) — use only 3x3 part
        DirectX::XMMATRIX m = toXM();
        DirectX::XMVECTOR vec = d.toXM();
        // XMVector3TransformCoord applies the full matrix, so we need to manually handle
        // the 3x3 rotation/scale only. Use XMVector4Transform with w=0.
        DirectX::XMVECTOR vec4 = DirectX::XMVectorSetW(vec, 0.0f);
        DirectX::XMVECTOR result4 = DirectX::XMVector4Transform(vec4, m);
        return Vec3(result4);
    }

    /// Extracts the translation component (row 3: _41, _42, _43).
    Vec3 getTranslation() const;
    /// Extracts the scale by computing the length of each basis row.
    Vec3 getScale() const;
    // Quat getRotation() const;  // Forward declared, defined after Quat

    /// Exact element-wise equality comparison.
    bool operator==(const Mat4& other) const {
        for (int i = 0; i < 16; i++) {
            if (data()[i] != other.data()[i])
                return false;
        }
        return true;
    }
    bool operator!=(const Mat4& other) const { return !(*this == other); }

    /// Converts to raw XMFLOAT4X4 for DirectXMath interop.
    DirectX::XMFLOAT4X4 ToFloat4x4() const { return m; }
    /// Creates Mat4 from raw XMFLOAT4X4.
    static Mat4 FromFloat4x4(const DirectX::XMFLOAT4X4& f) { return {f}; }

    /// Returns `this * Translation(pos)` — appends a translation.
    Mat4 translate(const Vec3& pos) const;
    /// Returns `this * Scaling(s)` — appends a scale.
    Mat4 scale(const Vec3& s) const;

    // ─────────────────────────────────────────────────────────────────────────
    // Convenience Factories
    // ─────────────────────────────────────────────────────────────────────────

    /// Builds a TRS (Translation * Rotation * Scale) model matrix.
    ///
    /// # Arguments
    /// * `pos` — World-space position.
    /// * `rot` — Orientation quaternion.
    /// * `scl` — Per-axis scale.
    ///
    /// # Returns
    /// A 4x4 model matrix: `Scale(scl) * Rotate(rot) * Translate(pos)`.
    static Mat4 TRS(const Vec3& pos, const Quat& rot, const Vec3& scl);

    /// Builds a model matrix from position and uniform scale (no rotation).
    ///
    /// # Arguments
    /// * `pos` — World-space position.
    /// * `uniformScale` — Uniform scale factor applied to all axes.
    ///
    /// # Returns
    /// `Scale(s,s,s) * Translate(pos)`.
    static Mat4 TRS(const Vec3& pos, float uniformScale);

    /// Builds a model matrix from position and non-uniform scale (no rotation).
    ///
    /// # Arguments
    /// * `pos` — World-space position.
    /// * `scl` — Per-axis scale vector.
    ///
    /// # Returns
    /// `Scale(scl) * Translate(pos)`.
    static Mat4 TRS(const Vec3& pos, const Vec3& scl);

    /// Computes the normal matrix: transpose of the inverse of this matrix.
    ///
    /// Used to transform surface normals correctly when the model matrix
    /// contains non-uniform scale. Without this, normals become skewed.
    ///
    /// # Returns
    /// `transpose(inverse(*this))`. Result is undefined if the matrix is singular.
    ///
    /// # Examples
    /// ```cpp
    /// Mat4 model = Mat4::TRS(pos, 2.0f);
    /// Mat4 normalMat = model.NormalMatrix();
    /// // Upload normalMat to PerObjectCB::normalMatrix
    /// ```
    [[nodiscard]] Mat4 NormalMatrix() const;
};

} // namespace uz
