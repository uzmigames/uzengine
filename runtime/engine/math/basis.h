/// UzEngine Basis — 3x3 rotation/scale matrix (Godot-style).
///
/// A 3x3 matrix stored as three row vectors. Represents rotation, scale,
/// and shear transformations without translation. Analogous to Godot's Basis type.
///
/// # Memory Layout
/// `[rows[0]: Vec3, rows[1]: Vec3, rows[2]: Vec3]` — 36 bytes, no padding.
/// Row 0 = local X axis, Row 1 = local Y axis, Row 2 = local Z axis.
///
/// # Coordinate System
/// Left-handed (DirectX convention). Euler decomposition uses YXZ order
/// (yaw-pitch-roll) to match engine conventions.
///
/// # Thread Safety
/// Trivially copyable value type. Thread-safe by value.
///
/// # Examples
/// ```cpp
/// Basis rot = Basis::FromAxisAngle(Vec3::UnitY(), Math::PI / 4.0f);
/// Vec3 forward = rot * Vec3::UnitZ();
/// Quat q = rot.get_quaternion();
/// ```

#pragma once

#include "vector3.h"
#include "quaternion.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Basis — 3x3 rotation/scale matrix
// ─────────────────────────────────────────────────────────────────────────────

/// 3x3 matrix for rotation, scale, and shear (no translation).
///
/// Stored as three row vectors: `rows[0]` is the local X axis,
/// `rows[1]` is the local Y axis, `rows[2]` is the local Z axis.
/// When the basis is orthonormal, these rows form a right-handed
/// coordinate frame in left-handed world space.
struct Basis {
    /// Row vectors: rows[0]=X axis, rows[1]=Y axis, rows[2]=Z axis.
    Vec3 rows[3];

    // ─────────────────────────────────────────────────────────────────────────
    // Constructors
    // ─────────────────────────────────────────────────────────────────────────

    /// Default: identity matrix (no rotation, unit scale).
    Basis()
        : rows{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}} {}

    /// Construct from three explicit row vectors.
    ///
    /// # Arguments
    /// * `row0` — Local X axis (first row).
    /// * `row1` — Local Y axis (second row).
    /// * `row2` — Local Z axis (third row).
    Basis(const Vec3& row0, const Vec3& row1, const Vec3& row2)
        : rows{row0, row1, row2} {}

    /// Constructs a rotation basis from a unit quaternion.
    ///
    /// # Arguments
    /// * `q` — Unit quaternion. Must be normalized for correct results.
    ///
    /// # Returns
    /// Orthonormal basis representing the same rotation as `q`.
    static Basis FromQuaternion(const Quat& q) {
        float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
        float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
        float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
        float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;

        return {
            Vec3{1.0f - (yy + zz), xy - wz, xz + wy},
            Vec3{xy + wz, 1.0f - (xx + zz), yz - wx},
            Vec3{xz - wy, yz + wx, 1.0f - (xx + yy)}
        };
    }

    /// Constructs a rotation basis from an axis-angle pair.
    ///
    /// # Arguments
    /// * `axis` — Rotation axis (must be normalized).
    /// * `angle` — Rotation angle in radians.
    ///
    /// # Returns
    /// Orthonormal rotation basis.
    static Basis FromAxisAngle(const Vec3& axis, float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        float t = 1.0f - c;

        float tx = t * axis.x, ty = t * axis.y, tz = t * axis.z;
        float sx = s * axis.x, sy = s * axis.y, sz = s * axis.z;

        return {
            Vec3{tx * axis.x + c,  tx * axis.y - sz, tx * axis.z + sy},
            Vec3{ty * axis.x + sz, ty * axis.y + c,  ty * axis.z - sx},
            Vec3{tz * axis.x - sy, tz * axis.y + sx, tz * axis.z + c}
        };
    }

    /// Constructs a rotation basis from Euler angles in YXZ order.
    ///
    /// YXZ order applies: yaw (Y) first, then pitch (X), then roll (Z).
    /// This matches the engine's standard rotation convention.
    ///
    /// # Arguments
    /// * `euler` — Euler angles in radians: `(pitch, yaw, roll)` = `(x, y, z)`.
    ///
    /// # Returns
    /// Rotation basis equivalent to `R_Y(yaw) * R_X(pitch) * R_Z(roll)`.
    static Basis FromEuler(const Vec3& euler) {
        float cx = std::cos(euler.x), sx = std::sin(euler.x);
        float cy = std::cos(euler.y), sy = std::sin(euler.y);
        float cz = std::cos(euler.z), sz = std::sin(euler.z);

        // YXZ rotation order
        return {
            Vec3{cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx},
            Vec3{cx * sz, cx * cz, -sx},
            Vec3{-sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx}
        };
    }

    /// Constructs a non-uniform scale basis (diagonal matrix).
    ///
    /// # Arguments
    /// * `scale` — Scale factors for each axis.
    ///
    /// # Returns
    /// Diagonal matrix with `scale` on the diagonal.
    static Basis FromScale(const Vec3& scale) {
        return {
            Vec3{scale.x, 0, 0},
            Vec3{0, scale.y, 0},
            Vec3{0, 0, scale.z}
        };
    }

    /// Returns the identity basis (no rotation, unit scale).
    static Basis Identity() { return {}; }

    // ─────────────────────────────────────────────────────────────────────────
    // Element Access
    // ─────────────────────────────────────────────────────────────────────────

    /// Returns the i-th column vector (transposed row access).
    ///
    /// # Arguments
    /// * `i` — Column index in [0, 2].
    ///
    /// # Returns
    /// Column vector `(rows[0][i], rows[1][i], rows[2][i])`.
    Vec3 get_column(int i) const {
        return {rows[0][i], rows[1][i], rows[2][i]};
    }
    /// Uppercase alias for `get_column()`.
    Vec3 GetColumn(int i) const { return get_column(i); }

    /// Sets the i-th column to `v`.
    ///
    /// # Arguments
    /// * `i` — Column index in [0, 2].
    /// * `v` — New column value.
    void set_column(int i, const Vec3& v) {
        rows[0][i] = v.x;
        rows[1][i] = v.y;
        rows[2][i] = v.z;
    }
    /// Uppercase alias for `set_column()`.
    void SetColumn(int i, const Vec3& v) { set_column(i, v); }

    /// Mutable element access by (row, col).
    ///
    /// # Arguments
    /// * `row` — Row index in [0, 2].
    /// * `col` — Column index in [0, 2].
    ///
    /// # Returns
    /// Mutable reference to the element.
    float& operator()(int row, int col) { return rows[row][col]; }

    /// Const element access by (row, col).
    ///
    /// # Arguments
    /// * `row` — Row index in [0, 2].
    /// * `col` — Column index in [0, 2].
    ///
    /// # Returns
    /// Value of the element.
    float operator()(int row, int col) const { return rows[row][col]; }

    // ─────────────────────────────────────────────────────────────────────────
    // Matrix Operations
    // ─────────────────────────────────────────────────────────────────────────

    /// Matrix multiplication (combines two basis transforms).
    ///
    /// # Arguments
    /// * `other` — Right-hand basis to multiply with.
    ///
    /// # Returns
    /// Combined basis `*this * other`. Apply `other` first, then `*this`.
    Basis operator*(const Basis& other) const {
        Basis result;
        for (int i = 0; i < 3; ++i) {
            result.rows[i] = Vec3{
                rows[i].dot(other.get_column(0)),
                rows[i].dot(other.get_column(1)),
                rows[i].dot(other.get_column(2))
            };
        }
        return result;
    }

    /// Transforms a vector by this basis (matrix-vector multiply).
    ///
    /// # Arguments
    /// * `v` — Vector to transform.
    ///
    /// # Returns
    /// Transformed vector `(row0.dot(v), row1.dot(v), row2.dot(v))`.
    Vec3 operator*(const Vec3& v) const {
        return {rows[0].dot(v), rows[1].dot(v), rows[2].dot(v)};
    }

    /// Returns the transpose (rows become columns).
    ///
    /// For orthonormal bases, the transpose equals the inverse.
    ///
    /// # Returns
    /// Transposed basis.
    Basis transposed() const {
        return {
            Vec3{rows[0].x, rows[1].x, rows[2].x},
            Vec3{rows[0].y, rows[1].y, rows[2].y},
            Vec3{rows[0].z, rows[1].z, rows[2].z}
        };
    }
    /// Uppercase alias for `transposed()`.
    Basis Transposed() const { return transposed(); }

    /// Computes the determinant of the 3x3 matrix.
    ///
    /// # Returns
    /// Scalar determinant. Positive for right-handed frames, negative for
    /// reflected/mirrored frames. Zero for singular (degenerate) matrices.
    float determinant() const {
        return rows[0].x * (rows[1].y * rows[2].z - rows[1].z * rows[2].y)
             - rows[0].y * (rows[1].x * rows[2].z - rows[1].z * rows[2].x)
             + rows[0].z * (rows[1].x * rows[2].y - rows[1].y * rows[2].x);
    }
    /// Uppercase alias for `determinant()`.
    float Determinant() const { return determinant(); }

    /// Computes the matrix inverse using cofactor expansion.
    ///
    /// # Returns
    /// Inverse basis such that `*this * inverse() ≈ Identity()`.
    /// Result is undefined if the matrix is singular (determinant ≈ 0).
    Basis inverse() const {
        float det = determinant();
        float inv_det = 1.0f / det;

        Basis result;
        result.rows[0] = Vec3{
            (rows[1].y * rows[2].z - rows[1].z * rows[2].y) * inv_det,
            (rows[0].z * rows[2].y - rows[0].y * rows[2].z) * inv_det,
            (rows[0].y * rows[1].z - rows[0].z * rows[1].y) * inv_det
        };
        result.rows[1] = Vec3{
            (rows[1].z * rows[2].x - rows[1].x * rows[2].z) * inv_det,
            (rows[0].x * rows[2].z - rows[0].z * rows[2].x) * inv_det,
            (rows[0].z * rows[1].x - rows[0].x * rows[1].z) * inv_det
        };
        result.rows[2] = Vec3{
            (rows[1].x * rows[2].y - rows[1].y * rows[2].x) * inv_det,
            (rows[0].y * rows[2].x - rows[0].x * rows[2].y) * inv_det,
            (rows[0].x * rows[1].y - rows[0].y * rows[1].x) * inv_det
        };
        return result;
    }
    /// Uppercase alias for `inverse()`.
    Basis Inverse() const { return inverse(); }

    /// Gram-Schmidt orthonormalization of the rows.
    ///
    /// Produces an orthonormal basis by normalizing row 0, then making row 1
    /// perpendicular to row 0, then row 2 perpendicular to both.
    ///
    /// # Returns
    /// Orthonormalized copy. Preserves the general orientation of the original basis.
    Basis orthonormalized() const {
        Vec3 x = rows[0].normalize();
        Vec3 y = (rows[1] - x * x.dot(rows[1])).normalize();
        Vec3 z = (rows[2] - x * x.dot(rows[2]) - y * y.dot(rows[2])).normalize();
        return {x, y, z};
    }
    /// Uppercase alias for `orthonormalized()`.
    Basis Orthonormalized() const { return orthonormalized(); }

    // ─────────────────────────────────────────────────────────────────────────
    // Decomposition
    // ─────────────────────────────────────────────────────────────────────────

    /// Extracts Euler angles from the basis in YXZ order.
    ///
    /// # Returns
    /// Vec3(pitch, yaw, roll) in radians. Subject to gimbal lock when pitch ≈ ±90 deg.
    Vec3 get_euler() const {
        Vec3 euler;
        float sy = rows[0].z;

        // Clamp to avoid NaN from asin
        if (sy < -1.0f + Math::EPSILON) {
            euler.x = -Math::PI * 0.5f;
            euler.y = std::atan2(-rows[2].x, rows[1].x);
            euler.z = 0.0f;
        } else if (sy > 1.0f - Math::EPSILON) {
            euler.x = Math::PI * 0.5f;
            euler.y = std::atan2(-rows[2].x, rows[1].x);
            euler.z = 0.0f;
        } else {
            euler.x = std::asin(-rows[1].z);
            euler.y = std::atan2(rows[0].z, rows[2].z);
            euler.z = std::atan2(rows[1].x, rows[1].y);
        }
        return euler;
    }
    /// Uppercase alias for `get_euler()`.
    Vec3 GetEuler() const { return get_euler(); }

    /// Converts the rotation component of this basis to a quaternion.
    ///
    /// For bases with scale, call `get_rotation()` first to remove scale,
    /// then convert to quaternion.
    ///
    /// # Returns
    /// Unit quaternion representing the same rotation. The basis should be
    /// orthonormal for correct results.
    Quat get_quaternion() const {
        // Shepperd's method for robust matrix-to-quaternion conversion
        float trace = rows[0].x + rows[1].y + rows[2].z;
        Quat q;

        if (trace > 0.0f) {
            float s = std::sqrt(trace + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (rows[2].y - rows[1].z) / s;
            q.y = (rows[0].z - rows[2].x) / s;
            q.z = (rows[1].x - rows[0].y) / s;
        } else if (rows[0].x > rows[1].y && rows[0].x > rows[2].z) {
            float s = std::sqrt(1.0f + rows[0].x - rows[1].y - rows[2].z) * 2.0f;
            q.w = (rows[2].y - rows[1].z) / s;
            q.x = 0.25f * s;
            q.y = (rows[0].y + rows[1].x) / s;
            q.z = (rows[0].z + rows[2].x) / s;
        } else if (rows[1].y > rows[2].z) {
            float s = std::sqrt(1.0f + rows[1].y - rows[0].x - rows[2].z) * 2.0f;
            q.w = (rows[0].z - rows[2].x) / s;
            q.x = (rows[0].y + rows[1].x) / s;
            q.y = 0.25f * s;
            q.z = (rows[1].z + rows[2].y) / s;
        } else {
            float s = std::sqrt(1.0f + rows[2].z - rows[0].x - rows[1].y) * 2.0f;
            q.w = (rows[1].x - rows[0].y) / s;
            q.x = (rows[0].z + rows[2].x) / s;
            q.y = (rows[1].z + rows[2].y) / s;
            q.z = 0.25f * s;
        }
        return q.normalize();
    }
    /// Uppercase alias for `get_quaternion()`.
    Quat GetQuaternion() const { return get_quaternion(); }

    /// Extracts the scale factors from each row.
    ///
    /// # Returns
    /// Vec3 with the length of each row vector (X, Y, Z scale).
    Vec3 get_scale() const {
        return {rows[0].length(), rows[1].length(), rows[2].length()};
    }
    /// Uppercase alias for `get_scale()`.
    Vec3 GetScale() const { return get_scale(); }

    /// Returns the rotation-only component with scale removed.
    ///
    /// Each row is normalized to unit length, stripping any scale.
    ///
    /// # Returns
    /// Orthonormal rotation basis.
    Basis get_rotation() const {
        return {rows[0].normalize(), rows[1].normalize(), rows[2].normalize()};
    }
    /// Uppercase alias for `get_rotation()`.
    Basis GetRotation() const { return get_rotation(); }

    // ─────────────────────────────────────────────────────────────────────────
    // Interpolation
    // ─────────────────────────────────────────────────────────────────────────

    /// Spherical linear interpolation between two bases.
    ///
    /// Converts both bases to quaternions, slerps, then converts back.
    /// Scale is linearly interpolated separately.
    ///
    /// # Arguments
    /// * `other` — Target basis (at t=1).
    /// * `t` — Interpolation factor in [0, 1].
    ///
    /// # Returns
    /// Interpolated basis. At t=0 returns `*this`, at t=1 returns `other`.
    Basis slerp(const Basis& other, float t) const {
        Quat q_from = get_quaternion();
        Quat q_to = other.get_quaternion();
        Basis result = FromQuaternion(q_from.slerp(q_to, t));

        // Linearly interpolate scale
        Vec3 s_from = get_scale();
        Vec3 s_to = other.get_scale();
        Vec3 s = Vec3::Lerp(s_from, s_to, t);
        result.rows[0] = result.rows[0] * s.x;
        result.rows[1] = result.rows[1] * s.y;
        result.rows[2] = result.rows[2] * s.z;

        return result;
    }
    /// Uppercase alias for `slerp()`.
    Basis Slerp(const Basis& other, float t) const { return slerp(other, t); }

    // ─────────────────────────────────────────────────────────────────────────
    // Queries
    // ─────────────────────────────────────────────────────────────────────────

    /// Returns true if this basis is approximately equal to `other`.
    ///
    /// # Arguments
    /// * `other` — Basis to compare against.
    ///
    /// # Returns
    /// `true` if all 9 elements are within `Math::EPSILON`.
    bool is_equal_approx(const Basis& other) const {
        return rows[0].is_equal_approx(other.rows[0]) &&
               rows[1].is_equal_approx(other.rows[1]) &&
               rows[2].is_equal_approx(other.rows[2]);
    }
    /// Uppercase alias for `is_equal_approx()`.
    bool IsEqualApprox(const Basis& other) const { return is_equal_approx(other); }

    /// Returns true if this basis is orthogonal (rows are mutually perpendicular
    /// and unit-length, within tolerance).
    ///
    /// # Returns
    /// `true` if `transpose * self ≈ identity`.
    bool is_orthogonal() const {
        Basis t = transposed();
        Basis product = *this * t;
        return product.is_equal_approx(Basis::Identity());
    }
    /// Uppercase alias for `is_orthogonal()`.
    bool IsOrthogonal() const { return is_orthogonal(); }

    /// Returns true if this basis represents a pure rotation (orthogonal with determinant +1).
    ///
    /// # Returns
    /// `true` if orthogonal and determinant ≈ 1.0 (no reflection or scale).
    bool is_rotation() const {
        return is_orthogonal() && Math::is_equal_approx(determinant(), 1.0f);
    }
    /// Uppercase alias for `is_rotation()`.
    bool IsRotation() const { return is_rotation(); }
};

} // namespace uz
