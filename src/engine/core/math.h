#pragma once

// Math Types — Vec2, Vec3, Vec4, Quat, Mat4
//
// SIMD-aligned, POD types. Header-only — all functions are inline.
// No external dependencies (no DirectXMath, no GLM).
//
// Conventions:
//   - Column-major Mat4 (matches OpenGL convention). For DX12, transpose
//     before uploading to root constants / CBVs.
//   - Right-handed coordinate system (forward = -Z, up = +Y).
//   - Quaternions stored as (x, y, z, w) where w is the scalar part.
//   - Vec3 has a _pad float so sizeof(Vec3) == 16 for SIMD alignment.
//   - Angles in radians unless explicitly noted.

#include <cassert>
#include <cmath>
#include <cstdint>

#if defined(UZ_SIMD)
#include "engine/core/simd_math.h"
#endif

namespace uz {

// ============================================================================
// Constants
// ============================================================================

inline constexpr float kPi      = 3.14159265358979323846f;
inline constexpr float kTwoPi   = 6.28318530717958647692f;
inline constexpr float kHalfPi  = 1.57079632679489661923f;
inline constexpr float kEpsilon = 1e-6f;

/// Convert degrees to radians.
[[nodiscard]] inline constexpr float radians(float deg) noexcept {
    return deg * (kPi / 180.0f);
}

/// Convert radians to degrees.
[[nodiscard]] inline constexpr float degrees(float rad) noexcept {
    return rad * (180.0f / kPi);
}

// ============================================================================
// Vec2
// ============================================================================

struct alignas(8) Vec2 {
    float x, y;

    [[nodiscard]] constexpr Vec2 operator+(Vec2 o) const noexcept { return {x + o.x, y + o.y}; }
    [[nodiscard]] constexpr Vec2 operator-(Vec2 o) const noexcept { return {x - o.x, y - o.y}; }
    [[nodiscard]] constexpr Vec2 operator*(float s) const noexcept { return {x * s, y * s}; }
    [[nodiscard]] constexpr Vec2 operator-() const noexcept { return {-x, -y}; }

    [[nodiscard]] constexpr bool operator==(Vec2 o) const noexcept { return x == o.x && y == o.y; }
    [[nodiscard]] constexpr bool operator!=(Vec2 o) const noexcept { return !(*this == o); }
};

[[nodiscard]] inline constexpr float vec2_dot(Vec2 a, Vec2 b) noexcept {
    return a.x * b.x + a.y * b.y;
}

[[nodiscard]] inline float vec2_len(Vec2 v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

[[nodiscard]] inline Vec2 vec2_normalize(Vec2 v) noexcept {
    const float len = vec2_len(v);
    if (len <= kEpsilon) return {1.0f, 0.0f};
    const float inv = 1.0f / len;
    return {v.x * inv, v.y * inv};
}

// ============================================================================
// Float3 — 12-byte packed POD for GPU vertex data (no SIMD padding)
//
// Use Float3 for vertex attributes, GPU constant buffers, and serialization.
// Use Vec3 for engine math (SIMD-aligned, has padding).
// ============================================================================

struct Float3 {
    float x, y, z;
};

static_assert(sizeof(Float3) == 12, "Float3 must be 12 bytes (3 floats, no padding)");

// ============================================================================
// Vec3 — padded to 16 bytes for SIMD alignment
// ============================================================================

struct alignas(16) Vec3 {
    float x, y, z;
    float _pad;   // Padding — always 0. Do not use.

    [[nodiscard]] constexpr Vec3 operator+(Vec3 o) const noexcept { return {x + o.x, y + o.y, z + o.z, 0.0f}; }
    [[nodiscard]] constexpr Vec3 operator-(Vec3 o) const noexcept { return {x - o.x, y - o.y, z - o.z, 0.0f}; }
    [[nodiscard]] constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s, 0.0f}; }
    [[nodiscard]] constexpr Vec3 operator-() const noexcept { return {-x, -y, -z, 0.0f}; }

    [[nodiscard]] constexpr bool operator==(Vec3 o) const noexcept { return x == o.x && y == o.y && z == o.z; }
    [[nodiscard]] constexpr bool operator!=(Vec3 o) const noexcept { return !(*this == o); }
};

static_assert(sizeof(Vec3) == 16, "Vec3 must be 16 bytes (padded for SIMD alignment)");

[[nodiscard]] inline constexpr float vec3_dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] inline float vec3_len(Vec3 v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

[[nodiscard]] inline Vec3 vec3_normalize(Vec3 v) noexcept {
    const float len = vec3_len(v);
    if (len <= kEpsilon) return {0.0f, 0.0f, 1.0f, 0.0f};
    const float inv = 1.0f / len;
    return {v.x * inv, v.y * inv, v.z * inv, 0.0f};
}

[[nodiscard]] inline constexpr Vec3 vec3_cross(Vec3 a, Vec3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
        0.0f
    };
}

[[nodiscard]] inline constexpr Vec3 vec3_lerp(Vec3 a, Vec3 b, float t) noexcept {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        0.0f
    };
}

// ============================================================================
// Vec4
// ============================================================================

struct alignas(16) Vec4 {
    float x, y, z, w;

#if defined(UZ_SIMD)
    [[nodiscard]] Vec4 operator+(Vec4 o) const noexcept {
        Vec4 r;
        simd::store_aligned(&r.x, simd::vec4_add(simd::load_aligned(&x), simd::load_aligned(&o.x)));
        return r;
    }
    [[nodiscard]] Vec4 operator-(Vec4 o) const noexcept {
        Vec4 r;
        simd::store_aligned(&r.x, simd::vec4_sub(simd::load_aligned(&x), simd::load_aligned(&o.x)));
        return r;
    }
    [[nodiscard]] Vec4 operator*(float s) const noexcept {
        Vec4 r;
        simd::store_aligned(&r.x, simd::vec4_scale(simd::load_aligned(&x), s));
        return r;
    }
    [[nodiscard]] Vec4 operator-() const noexcept {
        Vec4 r;
        simd::store_aligned(&r.x, simd::vec4_negate(simd::load_aligned(&x)));
        return r;
    }
#else
    [[nodiscard]] constexpr Vec4 operator+(Vec4 o) const noexcept { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    [[nodiscard]] constexpr Vec4 operator-(Vec4 o) const noexcept { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    [[nodiscard]] constexpr Vec4 operator*(float s) const noexcept { return {x * s, y * s, z * s, w * s}; }
    [[nodiscard]] constexpr Vec4 operator-() const noexcept { return {-x, -y, -z, -w}; }
#endif

    [[nodiscard]] constexpr bool operator==(Vec4 o) const noexcept { return x == o.x && y == o.y && z == o.z && w == o.w; }
    [[nodiscard]] constexpr bool operator!=(Vec4 o) const noexcept { return !(*this == o); }
};

static_assert(sizeof(Vec4) == 16, "Vec4 must be 16 bytes");

#if defined(UZ_SIMD)
[[nodiscard]] inline float vec4_dot(Vec4 a, Vec4 b) noexcept {
    return simd::extract_x(simd::vec4_dot(simd::load_aligned(&a.x), simd::load_aligned(&b.x)));
}
#else
[[nodiscard]] inline constexpr float vec4_dot(Vec4 a, Vec4 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
#endif

// ============================================================================
// Quat — unit quaternion (x, y, z, w) where w is the scalar part
// ============================================================================

struct alignas(16) Quat {
    float x, y, z, w;

    [[nodiscard]] constexpr bool operator==(Quat o) const noexcept {
        return x == o.x && y == o.y && z == o.z && w == o.w;
    }
    [[nodiscard]] constexpr bool operator!=(Quat o) const noexcept { return !(*this == o); }
};

static_assert(sizeof(Quat) == 16, "Quat must be 16 bytes");

/// Returns the identity quaternion (no rotation).
[[nodiscard]] inline constexpr Quat quat_identity() noexcept {
    return {0.0f, 0.0f, 0.0f, 1.0f};
}

/// Construct a quaternion from an axis (must be unit length) and angle in radians.
[[nodiscard]] inline Quat quat_from_axis_angle(Vec3 axis, float angle) noexcept {
    const float half  = angle * 0.5f;
    const float s     = std::sin(half);
    const float c     = std::cos(half);
    return {axis.x * s, axis.y * s, axis.z * s, c};
}

/// Hamilton product of two quaternions.
#if defined(UZ_SIMD)
[[nodiscard]] inline Quat quat_mul(Quat a, Quat b) noexcept {
    Quat r;
    simd::store_aligned(&r.x, simd::quat_mul(simd::load_aligned(&a.x), simd::load_aligned(&b.x)));
    return r;
}
#else
[[nodiscard]] inline constexpr Quat quat_mul(Quat a, Quat b) noexcept {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}
#endif

/// Normalize a quaternion to unit length.
#if defined(UZ_SIMD)
[[nodiscard]] inline Quat quat_normalize(Quat q) noexcept {
    Quat r;
    simd::store_aligned(&r.x, simd::quat_normalize(simd::load_aligned(&q.x)));
    return r;
}
#else
[[nodiscard]] inline Quat quat_normalize(Quat q) noexcept {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len <= kEpsilon) return {0.0f, 0.0f, 0.0f, 1.0f};
    const float inv = 1.0f / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}
#endif

/// Spherical linear interpolation between two quaternions.
/// Takes the shortest path (flips b if the dot product is negative).
#if defined(UZ_SIMD)
[[nodiscard]] inline Quat quat_slerp(Quat a, Quat b, float t) noexcept {
    Quat r;
    simd::store_aligned(&r.x, simd::quat_slerp(
        simd::load_aligned(&a.x), simd::load_aligned(&b.x), t));
    return r;
}
#else
[[nodiscard]] inline Quat quat_slerp(Quat a, Quat b, float t) noexcept {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    // Ensure shortest path — if dot < 0, negate b.
    if (dot < 0.0f) {
        b   = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }

    // If quaternions are nearly identical, use linear interpolation to avoid
    // division by a near-zero sin value.
    if (dot > 0.9995f) {
        Quat result = {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        };
        return quat_normalize(result);
    }

    const float theta_0 = std::acos(dot);        // Angle between inputs
    const float theta   = theta_0 * t;            // Angle for interpolation
    const float sin_theta   = std::sin(theta);
    const float sin_theta_0 = std::sin(theta_0);

    const float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
    const float s1 = sin_theta / sin_theta_0;

    return {
        a.x * s0 + b.x * s1,
        a.y * s0 + b.y * s1,
        a.z * s0 + b.z * s1,
        a.w * s0 + b.w * s1
    };
}
#endif

/// Rotate a vector by a unit quaternion.
/// Uses the optimized formula: v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
[[nodiscard]] inline Vec3 quat_rotate_vec3(Quat q, Vec3 v) noexcept {
    // q.xyz as Vec3
    const Vec3 u = {q.x, q.y, q.z, 0.0f};
    const float s = q.w;

    // v' = 2 * dot(u,v) * u + (s*s - dot(u,u)) * v + 2*s * cross(u,v)
    const float d_uv = vec3_dot(u, v);
    const float d_uu = vec3_dot(u, u);
    const Vec3  c_uv = vec3_cross(u, v);

    return {
        2.0f * d_uv * u.x + (s * s - d_uu) * v.x + 2.0f * s * c_uv.x,
        2.0f * d_uv * u.y + (s * s - d_uu) * v.y + 2.0f * s * c_uv.y,
        2.0f * d_uv * u.z + (s * s - d_uu) * v.z + 2.0f * s * c_uv.z,
        0.0f
    };
}

// ============================================================================
// Mat4 — column-major 4x4 matrix
// ============================================================================

// Storage: m[col][row]
//   m[0] = column 0 = (m00, m10, m20, m30)
//   m[1] = column 1 = (m01, m11, m21, m31)
//   m[2] = column 2 = (m02, m12, m22, m32)
//   m[3] = column 3 = (m03, m13, m23, m33)
//
// This matches OpenGL / HLSL column_major convention. For DX12 row-major root
// constants, transpose before upload.

struct alignas(16) Mat4 {
    float m[4][4];

    /// Returns the 4x4 identity matrix.
    [[nodiscard]] static constexpr Mat4 identity() noexcept {
        return {{
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f}
        }};
    }

    /// Translation matrix.
    [[nodiscard]] static constexpr Mat4 translate(Vec3 t) noexcept {
        Mat4 result = identity();
        result.m[3][0] = t.x;
        result.m[3][1] = t.y;
        result.m[3][2] = t.z;
        return result;
    }

    /// Rotation matrix from a unit quaternion.
    [[nodiscard]] static inline Mat4 rotate(Quat q) noexcept {
        // Assumes q is normalized.
        const float xx = q.x * q.x;
        const float yy = q.y * q.y;
        const float zz = q.z * q.z;
        const float xy = q.x * q.y;
        const float xz = q.x * q.z;
        const float yz = q.y * q.z;
        const float wx = q.w * q.x;
        const float wy = q.w * q.y;
        const float wz = q.w * q.z;

        Mat4 result = {};
        // Column 0
        result.m[0][0] = 1.0f - 2.0f * (yy + zz);
        result.m[0][1] = 2.0f * (xy + wz);
        result.m[0][2] = 2.0f * (xz - wy);
        result.m[0][3] = 0.0f;
        // Column 1
        result.m[1][0] = 2.0f * (xy - wz);
        result.m[1][1] = 1.0f - 2.0f * (xx + zz);
        result.m[1][2] = 2.0f * (yz + wx);
        result.m[1][3] = 0.0f;
        // Column 2
        result.m[2][0] = 2.0f * (xz + wy);
        result.m[2][1] = 2.0f * (yz - wx);
        result.m[2][2] = 1.0f - 2.0f * (xx + yy);
        result.m[2][3] = 0.0f;
        // Column 3
        result.m[3][0] = 0.0f;
        result.m[3][1] = 0.0f;
        result.m[3][2] = 0.0f;
        result.m[3][3] = 1.0f;

        return result;
    }

    /// Non-uniform scale matrix.
    [[nodiscard]] static constexpr Mat4 scale(Vec3 s) noexcept {
        Mat4 result = {};
        result.m[0][0] = s.x;
        result.m[1][1] = s.y;
        result.m[2][2] = s.z;
        result.m[3][3] = 1.0f;
        return result;
    }

    /// Perspective projection matrix (right-handed, depth [0..1] for DX12).
    /// `fov_y` is the vertical field of view in radians.
    [[nodiscard]] static inline Mat4 perspective(float fov_y, float aspect, float near_z, float far_z) noexcept {
        assert(std::abs(aspect) > kEpsilon && "Aspect ratio must not be zero");
        assert(std::abs(fov_y) > kEpsilon && "FOV must not be zero");
        assert(far_z > near_z && "far_z must be greater than near_z");

        const float tan_half_fov = std::tan(fov_y * 0.5f);

        Mat4 result = {};
        // Right-handed, depth [0..1] (reverse-Z friendly).
        result.m[0][0] = 1.0f / (aspect * tan_half_fov);
        result.m[1][1] = 1.0f / tan_half_fov;
        result.m[2][2] = far_z / (near_z - far_z);
        result.m[2][3] = -1.0f;
        result.m[3][2] = (near_z * far_z) / (near_z - far_z);
        return result;
    }

    /// Look-at view matrix (right-handed).
    [[nodiscard]] static inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) noexcept {
        // Forward points from target to eye (right-handed: camera looks along -Z).
        const Vec3 f = vec3_normalize(target - eye);  // Forward direction
        const Vec3 r = vec3_normalize(vec3_cross(f, up)); // Right direction
        const Vec3 u = vec3_cross(r, f); // Recalculated up

        Mat4 result = {};
        // Column 0
        result.m[0][0] =  r.x;
        result.m[0][1] =  u.x;
        result.m[0][2] = -f.x;
        result.m[0][3] =  0.0f;
        // Column 1
        result.m[1][0] =  r.y;
        result.m[1][1] =  u.y;
        result.m[1][2] = -f.y;
        result.m[1][3] =  0.0f;
        // Column 2
        result.m[2][0] =  r.z;
        result.m[2][1] =  u.z;
        result.m[2][2] = -f.z;
        result.m[2][3] =  0.0f;
        // Column 3 — translation: -dot(axis, eye)
        result.m[3][0] = -vec3_dot(r, eye);
        result.m[3][1] = -vec3_dot(u, eye);
        result.m[3][2] =  vec3_dot(f, eye);
        result.m[3][3] =  1.0f;

        return result;
    }

    /// Matrix multiplication (this * o). Column-major: result = A * B means
    /// transform by B first, then A.
#if defined(UZ_SIMD)
    [[nodiscard]] Mat4 operator*(const Mat4& o) const noexcept {
        Mat4 result;
        simd::mat4_mul(&m[0][0], &o.m[0][0], &result.m[0][0]);
        return result;
    }
#else
    [[nodiscard]] constexpr Mat4 operator*(const Mat4& o) const noexcept {
        Mat4 result = {};
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                result.m[col][row] =
                    m[0][row] * o.m[col][0] +
                    m[1][row] * o.m[col][1] +
                    m[2][row] * o.m[col][2] +
                    m[3][row] * o.m[col][3];
            }
        }
        return result;
    }
#endif
};

static_assert(sizeof(Mat4) == 64, "Mat4 must be 64 bytes (4x4 floats)");

/// General 4x4 matrix inverse (Cramer's rule). Returns identity if singular.
#if defined(UZ_SIMD)
[[nodiscard]] inline Mat4 mat4_inverse(const Mat4& m) noexcept {
    Mat4 result;
    simd::mat4_inverse(&m.m[0][0], &result.m[0][0]);
    return result;
}
#else
[[nodiscard]] inline Mat4 mat4_inverse(const Mat4& m) noexcept {
    // Flatten column-major: c[col*4+row]
    const float* c = &m.m[0][0];
    float inv[16];

    inv[0]  =  c[5]*c[10]*c[15] - c[5]*c[11]*c[14] - c[9]*c[6]*c[15] + c[9]*c[7]*c[14] + c[13]*c[6]*c[11] - c[13]*c[7]*c[10];
    inv[4]  = -c[4]*c[10]*c[15] + c[4]*c[11]*c[14] + c[8]*c[6]*c[15] - c[8]*c[7]*c[14] - c[12]*c[6]*c[11] + c[12]*c[7]*c[10];
    inv[8]  =  c[4]*c[9]*c[15]  - c[4]*c[11]*c[13] - c[8]*c[5]*c[15] + c[8]*c[7]*c[13] + c[12]*c[5]*c[11] - c[12]*c[7]*c[9];
    inv[12] = -c[4]*c[9]*c[14]  + c[4]*c[10]*c[13] + c[8]*c[5]*c[14] - c[8]*c[6]*c[13] - c[12]*c[5]*c[10] + c[12]*c[6]*c[9];

    float det = c[0]*inv[0] + c[1]*inv[4] + c[2]*inv[8] + c[3]*inv[12];
    if (std::abs(det) < kEpsilon) return Mat4::identity();

    inv[1]  = -c[1]*c[10]*c[15] + c[1]*c[11]*c[14] + c[9]*c[2]*c[15] - c[9]*c[3]*c[14] - c[13]*c[2]*c[11] + c[13]*c[3]*c[10];
    inv[5]  =  c[0]*c[10]*c[15] - c[0]*c[11]*c[14] - c[8]*c[2]*c[15] + c[8]*c[3]*c[14] + c[12]*c[2]*c[11] - c[12]*c[3]*c[10];
    inv[9]  = -c[0]*c[9]*c[15]  + c[0]*c[11]*c[13] + c[8]*c[1]*c[15] - c[8]*c[3]*c[13] - c[12]*c[1]*c[11] + c[12]*c[3]*c[9];
    inv[13] =  c[0]*c[9]*c[14]  - c[0]*c[10]*c[13] - c[8]*c[1]*c[14] + c[8]*c[2]*c[13] + c[12]*c[1]*c[10] - c[12]*c[2]*c[9];

    inv[2]  =  c[1]*c[6]*c[15]  - c[1]*c[7]*c[14]  - c[5]*c[2]*c[15] + c[5]*c[3]*c[14] + c[13]*c[2]*c[7]  - c[13]*c[3]*c[6];
    inv[6]  = -c[0]*c[6]*c[15]  + c[0]*c[7]*c[14]  + c[4]*c[2]*c[15] - c[4]*c[3]*c[14] - c[12]*c[2]*c[7]  + c[12]*c[3]*c[6];
    inv[10] =  c[0]*c[5]*c[15]  - c[0]*c[7]*c[13]  - c[4]*c[1]*c[15] + c[4]*c[3]*c[13] + c[12]*c[1]*c[7]  - c[12]*c[3]*c[5];
    inv[14] = -c[0]*c[5]*c[14]  + c[0]*c[6]*c[13]  + c[4]*c[1]*c[14] - c[4]*c[2]*c[13] - c[12]*c[1]*c[6]  + c[12]*c[2]*c[5];

    inv[3]  = -c[1]*c[6]*c[11]  + c[1]*c[7]*c[10]  + c[5]*c[2]*c[11] - c[5]*c[3]*c[10] - c[9]*c[2]*c[7]   + c[9]*c[3]*c[6];
    inv[7]  =  c[0]*c[6]*c[11]  - c[0]*c[7]*c[10]  - c[4]*c[2]*c[11] + c[4]*c[3]*c[10] + c[8]*c[2]*c[7]   - c[8]*c[3]*c[6];
    inv[11] = -c[0]*c[5]*c[11]  + c[0]*c[7]*c[9]   + c[4]*c[1]*c[11] - c[4]*c[3]*c[9]  - c[8]*c[1]*c[7]   + c[8]*c[3]*c[5];
    inv[15] =  c[0]*c[5]*c[10]  - c[0]*c[6]*c[9]   - c[4]*c[1]*c[10] + c[4]*c[2]*c[9]  + c[8]*c[1]*c[6]   - c[8]*c[2]*c[5];

    float invDet = 1.0f / det;
    Mat4 result;
    for (int i = 0; i < 16; ++i)
        reinterpret_cast<float*>(&result)[i] = inv[i] * invDet;
    return result;
}
#endif

/// Convert a unit quaternion to a rotation matrix.
[[nodiscard]] inline Mat4 quat_to_mat4(Quat q) noexcept {
    return Mat4::rotate(q);
}

// ============================================================================
// World-pivot rotation (authoritative transform function)
// ============================================================================

/// Apply a world-space rotation around a world-space pivot to a world matrix.
///
/// Formula (column-major, column-vector convention):
///   worldNew = T(pivot) * R(deltaQuat) * T(-pivot) * worldStart
///
/// If parentWorld is not identity (has a parent), converts result to local:
///   localNew = inverse(parentWorld) * worldNew
///
/// Returns worldNew if parentWorld is identity, otherwise localNew.
///
/// This is the ONLY function that should apply rotation in the editor.
/// No decompose/recompose. No partial position/rotation updates. Full matrix.
struct PivotRotationResult {
    Mat4 world_new;   // Always set: the resulting world matrix
    Mat4 local_new;   // = inverse(parentWorld) * world_new
};

[[nodiscard]] inline PivotRotationResult apply_world_pivot_rotation(
    const Mat4& parent_world,    // identity if root node
    const Mat4& world_start,     // object's world matrix at drag start
    Vec3        pivot_world,     // world-space pivot (constant during drag)
    Quat        world_delta      // rotation delta (total from start, NOT incremental)
) noexcept {
    // worldNew = T(P) * R(Q) * T(-P) * worldStart
    const Mat4 tPos  = Mat4::translate(pivot_world);
    const Mat4 tNeg  = Mat4::translate({-pivot_world.x, -pivot_world.y, -pivot_world.z, 0.0f});
    const Mat4 rot   = Mat4::rotate(world_delta);

    // Column-major: rightmost applied first
    // worldNew = tPos * rot * tNeg * worldStart
    const Mat4 world_new = tPos * rot * tNeg * world_start;
    const Mat4 local_new = mat4_inverse(parent_world) * world_new;

    return {world_new, local_new};
}

/// Extract translation (column 3) from a column-major Mat4.
[[nodiscard]] inline constexpr Vec3 mat4_get_translation(const Mat4& m) noexcept {
    return {m.m[3][0], m.m[3][1], m.m[3][2], 0.0f};
}

// ============================================================================
// Vec3 / Mat4 transform functions
// ============================================================================

/// Transform a position (point) by a 4x4 matrix. Applies translation.
/// Assumes w=1 for the input, performs perspective divide if w != 1.
[[nodiscard]] inline Vec3 vec3_transform(Vec3 v, const Mat4& m) noexcept {
    const float rx = m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + m.m[3][0];
    const float ry = m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + m.m[3][1];
    const float rz = m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + m.m[3][2];
    const float rw = m.m[0][3] * v.x + m.m[1][3] * v.y + m.m[2][3] * v.z + m.m[3][3];

    // Perspective divide for projection matrices.
    if (std::abs(rw) > kEpsilon && std::abs(rw - 1.0f) > kEpsilon) {
        const float inv_w = 1.0f / rw;
        return {rx * inv_w, ry * inv_w, rz * inv_w, 0.0f};
    }

    return {rx, ry, rz, 0.0f};
}

/// Transform a direction vector by a 4x4 matrix. Ignores translation (column 3).
[[nodiscard]] inline constexpr Vec3 vec3_transform_dir(Vec3 v, const Mat4& m) noexcept {
    return {
        m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z,
        m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
        m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z,
        0.0f
    };
}

// ============================================================================
// Aabb — Axis-Aligned Bounding Box
// ============================================================================

struct Aabb {
    Vec3 min;
    Vec3 max;

    [[nodiscard]] static constexpr Aabb empty() noexcept {
        return {
            { 1e30f,  1e30f,  1e30f, 0.0f},
            {-1e30f, -1e30f, -1e30f, 0.0f}
        };
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return min.x > max.x || min.y > max.y || min.z > max.z;
    }

    void merge(Vec3 p) noexcept {
        if (p.x < min.x) min.x = p.x;
        if (p.y < min.y) min.y = p.y;
        if (p.z < min.z) min.z = p.z;
        if (p.x > max.x) max.x = p.x;
        if (p.y > max.y) max.y = p.y;
        if (p.z > max.z) max.z = p.z;
    }
};

} // namespace uz
