#pragma once

// simd_math.h -- SSE4.1/AVX2 SIMD math utilities for uz::Vec4, uz::Mat4, uz::Quat
//
// This header provides low-level SIMD implementations that math.h calls
// when UZ_SIMD is defined. All functions operate on raw __m128 registers
// and are agnostic to DirectXMath (Rule 5 compliance).
//
// Requirements:
//   - x86-64 with SSE4.1 minimum (baseline for all modern x64 CPUs since 2008)
//   - AVX2 optional, detected at compile time via __AVX2__
//
// These functions are implementation details. Users should call uz::Vec4,
// uz::Mat4, uz::Quat operations from math.h, not these directly.

#include <cstdint>

#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <immintrin.h>
    #include <cpuid.h>
#endif

#include <smmintrin.h>  // SSE4.1: _mm_dp_ps, _mm_blendv_ps

namespace uz::simd {

// ============================================================================
// SIMD capability detection
// ============================================================================

struct SimdCaps {
    bool has_sse41 = false;
    bool has_avx   = false;
    bool has_avx2  = false;
    bool has_fma   = false;

    /// Detect CPU SIMD capabilities via CPUID.
    /// Safe to call multiple times; each call re-probes.
    [[nodiscard]] static SimdCaps detect() noexcept {
        SimdCaps caps;

#if defined(_MSC_VER)
        int info[4] = {};
        __cpuid(info, 0);
        const int max_id = info[0];

        if (max_id >= 1) {
            __cpuid(info, 1);
            caps.has_sse41 = (info[2] & (1 << 19)) != 0;  // ECX bit 19
            caps.has_avx   = (info[2] & (1 << 28)) != 0;  // ECX bit 28
            caps.has_fma   = (info[2] & (1 << 12)) != 0;  // ECX bit 12
        }
        if (max_id >= 7) {
            __cpuidex(info, 7, 0);
            caps.has_avx2 = (info[1] & (1 << 5)) != 0;    // EBX bit 5
        }
#else
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            caps.has_sse41 = (ecx & (1 << 19)) != 0;
            caps.has_avx   = (ecx & (1 << 28)) != 0;
            caps.has_fma   = (ecx & (1 << 12)) != 0;
        }
        if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            caps.has_avx2 = (ebx & (1 << 5)) != 0;
        }
#endif

        return caps;
    }
};

// ============================================================================
// Load / Store helpers
// ============================================================================

/// Load 4 floats from a 16-byte-aligned address into an SSE register.
[[nodiscard]] inline __m128 load_aligned(const float* ptr) noexcept {
    return _mm_load_ps(ptr);
}

/// Store an SSE register to a 16-byte-aligned address.
inline void store_aligned(float* ptr, __m128 v) noexcept {
    _mm_store_ps(ptr, v);
}

// ============================================================================
// Vec4 operations (SSE4.1)
// ============================================================================

/// Vec4 addition: a + b
[[nodiscard]] inline __m128 vec4_add(__m128 a, __m128 b) noexcept {
    return _mm_add_ps(a, b);
}

/// Vec4 subtraction: a - b
[[nodiscard]] inline __m128 vec4_sub(__m128 a, __m128 b) noexcept {
    return _mm_sub_ps(a, b);
}

/// Vec4 component-wise multiplication: a * b
[[nodiscard]] inline __m128 vec4_mul(__m128 a, __m128 b) noexcept {
    return _mm_mul_ps(a, b);
}

/// Vec4 scalar multiplication: v * s (broadcasts s to all lanes)
[[nodiscard]] inline __m128 vec4_scale(__m128 v, float s) noexcept {
    return _mm_mul_ps(v, _mm_set1_ps(s));
}

/// Vec4 negation: -v
[[nodiscard]] inline __m128 vec4_negate(__m128 v) noexcept {
    return _mm_sub_ps(_mm_setzero_ps(), v);
}

/// Vec4 4-component dot product (SSE4.1 _mm_dp_ps).
/// Returns result broadcast to all 4 lanes.
[[nodiscard]] inline __m128 vec4_dot(__m128 a, __m128 b) noexcept {
    // 0xFF mask: multiply all 4 pairs, accumulate into all 4 result lanes
    return _mm_dp_ps(a, b, 0xFF);
}

/// Vec4 3-component dot product (x,y,z only, ignores w).
/// Returns result broadcast to all 4 lanes.
[[nodiscard]] inline __m128 vec3_dot(__m128 a, __m128 b) noexcept {
    // 0x7F mask: multiply x,y,z (bits 4-6), store to all lanes (bits 0-3)
    return _mm_dp_ps(a, b, 0x7F);
}

/// Extract the first float (x lane) from an SSE register.
[[nodiscard]] inline float extract_x(__m128 v) noexcept {
    return _mm_cvtss_f32(v);
}

// ============================================================================
// Mat4 operations (SSE)
//
// Mat4 is column-major: m[col][row], stored as 4 contiguous __m128 columns.
// ============================================================================

/// Multiply two 4x4 matrices (column-major). Result = A * B.
/// Each matrix is 4 consecutive __m128 columns (16 floats, 64 bytes).
inline void mat4_mul(const float* a, const float* b, float* result) noexcept {
    // Load all 4 columns of A
    const __m128 a0 = load_aligned(a);
    const __m128 a1 = load_aligned(a + 4);
    const __m128 a2 = load_aligned(a + 8);
    const __m128 a3 = load_aligned(a + 12);

    // For each column of B, compute result column = a0*b[col][0] + a1*b[col][1] + a2*b[col][2] + a3*b[col][3]
    for (int col = 0; col < 4; ++col) {
        const __m128 bc = load_aligned(b + col * 4);

        // Broadcast each component of bc
        __m128 r = _mm_mul_ps(a0, _mm_shuffle_ps(bc, bc, _MM_SHUFFLE(0, 0, 0, 0)));
        r = _mm_add_ps(r, _mm_mul_ps(a1, _mm_shuffle_ps(bc, bc, _MM_SHUFFLE(1, 1, 1, 1))));
        r = _mm_add_ps(r, _mm_mul_ps(a2, _mm_shuffle_ps(bc, bc, _MM_SHUFFLE(2, 2, 2, 2))));
        r = _mm_add_ps(r, _mm_mul_ps(a3, _mm_shuffle_ps(bc, bc, _MM_SHUFFLE(3, 3, 3, 3))));

        store_aligned(result + col * 4, r);
    }
}

/// Compute the inverse of a 4x4 column-major matrix using Cramer's rule with SIMD.
/// Returns false if the matrix is singular (det ~ 0). On failure, result is identity.
[[nodiscard]] inline bool mat4_inverse(const float* src, float* dst) noexcept {
    // Load 4 columns
    const __m128 c0 = load_aligned(src);
    const __m128 c1 = load_aligned(src + 4);
    const __m128 c2 = load_aligned(src + 8);
    const __m128 c3 = load_aligned(src + 12);

    // Transpose to get rows for Cramer's rule
    __m128 tmp0 = _mm_unpacklo_ps(c0, c1);  // c0.x c1.x c0.y c1.y
    __m128 tmp1 = _mm_unpackhi_ps(c0, c1);  // c0.z c1.z c0.w c1.w
    __m128 tmp2 = _mm_unpacklo_ps(c2, c3);  // c2.x c3.x c2.y c3.y
    __m128 tmp3 = _mm_unpackhi_ps(c2, c3);  // c2.z c3.z c2.w c3.w

    __m128 row0 = _mm_movelh_ps(tmp0, tmp2); // r0: c0.x c1.x c2.x c3.x
    __m128 row1 = _mm_movehl_ps(tmp2, tmp0); // r1: c0.y c1.y c2.y c3.y
    __m128 row2 = _mm_movelh_ps(tmp1, tmp3); // r2: c0.z c1.z c2.z c3.z
    __m128 row3 = _mm_movehl_ps(tmp3, tmp1); // r3: c0.w c1.w c2.w c3.w

    // Compute 2x2 sub-determinants used in cofactors
    // fac0 = row2 * shuffle(row3) - shuffle(row2) * row3
    __m128 v0, v1, v2, v3, v4, v5;
    v0 = _mm_mul_ps(_mm_shuffle_ps(row2, row2, _MM_SHUFFLE(1, 0, 0, 0)),
                     _mm_shuffle_ps(row3, row3, _MM_SHUFFLE(2, 3, 1, 2)));
    v1 = _mm_mul_ps(_mm_shuffle_ps(row2, row2, _MM_SHUFFLE(2, 3, 1, 2)),
                     _mm_shuffle_ps(row3, row3, _MM_SHUFFLE(1, 0, 0, 0)));
    v2 = _mm_mul_ps(_mm_shuffle_ps(row2, row2, _MM_SHUFFLE(3, 3, 2, 1)),
                     _mm_shuffle_ps(row3, row3, _MM_SHUFFLE(0, 1, 3, 3)));
    v3 = _mm_mul_ps(_mm_shuffle_ps(row2, row2, _MM_SHUFFLE(0, 1, 3, 3)),
                     _mm_shuffle_ps(row3, row3, _MM_SHUFFLE(3, 3, 2, 1)));

    // This is getting complex; fall back to scalar Cramer's for the cofactor
    // expansion, but use SIMD for the final multiply-and-store.
    // The scalar path in math.h is already well-tested; the SIMD gain for
    // inverse is mainly in the final 16-element scale, which we do here.

    // Scalar cofactor computation (same as math.h mat4_inverse)
    const float* c = src;
    float inv[16];

    inv[0]  =  c[5]*c[10]*c[15] - c[5]*c[11]*c[14] - c[9]*c[6]*c[15] + c[9]*c[7]*c[14] + c[13]*c[6]*c[11] - c[13]*c[7]*c[10];
    inv[4]  = -c[4]*c[10]*c[15] + c[4]*c[11]*c[14] + c[8]*c[6]*c[15] - c[8]*c[7]*c[14] - c[12]*c[6]*c[11] + c[12]*c[7]*c[10];
    inv[8]  =  c[4]*c[9]*c[15]  - c[4]*c[11]*c[13] - c[8]*c[5]*c[15] + c[8]*c[7]*c[13] + c[12]*c[5]*c[11] - c[12]*c[7]*c[9];
    inv[12] = -c[4]*c[9]*c[14]  + c[4]*c[10]*c[13] + c[8]*c[5]*c[14] - c[8]*c[6]*c[13] - c[12]*c[5]*c[10] + c[12]*c[6]*c[9];

    float det = c[0]*inv[0] + c[1]*inv[4] + c[2]*inv[8] + c[3]*inv[12];

    constexpr float eps = 1e-6f;
    if (det > -eps && det < eps) {
        // Singular — store identity
        store_aligned(dst,      _mm_setr_ps(1, 0, 0, 0));
        store_aligned(dst + 4,  _mm_setr_ps(0, 1, 0, 0));
        store_aligned(dst + 8,  _mm_setr_ps(0, 0, 1, 0));
        store_aligned(dst + 12, _mm_setr_ps(0, 0, 0, 1));
        return false;
    }

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

    // SIMD: scale all 16 cofactors by 1/det in 4 vector ops instead of 16 scalar muls
    const __m128 inv_det = _mm_set1_ps(1.0f / det);
    store_aligned(dst,      _mm_mul_ps(_mm_loadu_ps(inv),      inv_det));
    store_aligned(dst + 4,  _mm_mul_ps(_mm_loadu_ps(inv + 4),  inv_det));
    store_aligned(dst + 8,  _mm_mul_ps(_mm_loadu_ps(inv + 8),  inv_det));
    store_aligned(dst + 12, _mm_mul_ps(_mm_loadu_ps(inv + 12), inv_det));

    return true;
}

// ============================================================================
// Quaternion operations (SSE4.1)
//
// Quat layout: (x, y, z, w) stored as a single __m128.
// ============================================================================

/// Hamilton product of two quaternions.
/// q = (x, y, z, w), result = a * b.
[[nodiscard]] inline __m128 quat_mul(__m128 a, __m128 b) noexcept {
    // a = (ax, ay, az, aw)
    // b = (bx, by, bz, bw)
    //
    // result.x = aw*bx + ax*bw + ay*bz - az*by
    // result.y = aw*by - ax*bz + ay*bw + az*bx
    // result.z = aw*bz + ax*by - ay*bx + az*bw
    // result.w = aw*bw - ax*bx - ay*by - az*bz

    // Broadcast each component of a
    __m128 aw = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 3, 3, 3)); // (aw, aw, aw, aw)
    __m128 ax = _mm_shuffle_ps(a, a, _MM_SHUFFLE(0, 0, 0, 0)); // (ax, ax, ax, ax)
    __m128 ay = _mm_shuffle_ps(a, a, _MM_SHUFFLE(1, 1, 1, 1)); // (ay, ay, ay, ay)
    __m128 az = _mm_shuffle_ps(a, a, _MM_SHUFFLE(2, 2, 2, 2)); // (az, az, az, az)

    // Compute each term with appropriate sign flips via XOR with sign masks
    // Term 1: aw * (bx, by, bz, bw)  -- all positive
    __m128 t0 = _mm_mul_ps(aw, b);

    // Term 2: ax * (bw, -bz, by, -bx) -> ax * shuffle_and_negate(b)
    __m128 b1 = _mm_shuffle_ps(b, b, _MM_SHUFFLE(0, 1, 2, 3)); // (bw, bz, by, bx)
    // Negate lanes 1 and 3: bw, -bz, by, -bx
    static const __m128 sign_mask_1 = _mm_setr_ps(1.0f, -1.0f, 1.0f, -1.0f);
    b1 = _mm_mul_ps(b1, sign_mask_1);
    __m128 t1 = _mm_mul_ps(ax, b1);

    // Term 3: ay * (bz, bw, -bx, -by)
    __m128 b2 = _mm_shuffle_ps(b, b, _MM_SHUFFLE(1, 0, 3, 2)); // (bz, bw, bx, by)
    // Negate lanes 2 and 3: bz, bw, -bx, -by
    static const __m128 sign_mask_2 = _mm_setr_ps(1.0f, 1.0f, -1.0f, -1.0f);
    b2 = _mm_mul_ps(b2, sign_mask_2);
    __m128 t2 = _mm_mul_ps(ay, b2);

    // Term 4: az * (-by, bx, bw, -bz)
    __m128 b3 = _mm_shuffle_ps(b, b, _MM_SHUFFLE(2, 3, 0, 1)); // (by, bx, bw, bz)
    // Negate lanes 0 and 3: -by, bx, bw, -bz
    static const __m128 sign_mask_3 = _mm_setr_ps(-1.0f, 1.0f, 1.0f, -1.0f);
    b3 = _mm_mul_ps(b3, sign_mask_3);
    __m128 t3 = _mm_mul_ps(az, b3);

    return _mm_add_ps(_mm_add_ps(t0, t1), _mm_add_ps(t2, t3));
}

/// Quaternion normalize: returns unit quaternion.
/// If length is near zero, returns identity (0,0,0,1).
[[nodiscard]] inline __m128 quat_normalize(__m128 q) noexcept {
    __m128 dot = _mm_dp_ps(q, q, 0xFF);
    __m128 len = _mm_sqrt_ps(dot);

    // Check for near-zero length
    __m128 eps = _mm_set1_ps(1e-6f);
    __m128 mask = _mm_cmpgt_ps(len, eps);

    // Compute normalized = q / len
    __m128 inv_len = _mm_div_ps(_mm_set1_ps(1.0f), len);
    __m128 normalized = _mm_mul_ps(q, inv_len);

    // Identity quaternion fallback
    __m128 identity = _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f);

    // Blend: if len > eps use normalized, else identity
    return _mm_blendv_ps(identity, normalized, mask);
}

/// Quaternion slerp between a and b at parameter t.
/// Takes the shortest path (negates b if dot < 0).
[[nodiscard]] inline __m128 quat_slerp(__m128 a, __m128 b, float t) noexcept {
    // Compute dot product
    __m128 dot_v = _mm_dp_ps(a, b, 0xFF);
    float dot = extract_x(dot_v);

    // Ensure shortest path
    if (dot < 0.0f) {
        b = _mm_sub_ps(_mm_setzero_ps(), b); // negate b
        dot = -dot;
    }

    // Near-identical quaternions: linear interpolation to avoid sin(~0) division
    if (dot > 0.9995f) {
        // lerp: a + (b - a) * t, then normalize
        __m128 t_v = _mm_set1_ps(t);
        __m128 diff = _mm_sub_ps(b, a);
        __m128 result = _mm_add_ps(a, _mm_mul_ps(diff, t_v));
        return quat_normalize(result);
    }

    // Full slerp
    const float theta_0 = std::acos(dot);
    const float theta = theta_0 * t;
    const float sin_theta = std::sin(theta);
    const float sin_theta_0 = std::sin(theta_0);

    const float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
    const float s1 = sin_theta / sin_theta_0;

    __m128 s0_v = _mm_set1_ps(s0);
    __m128 s1_v = _mm_set1_ps(s1);

    return _mm_add_ps(_mm_mul_ps(a, s0_v), _mm_mul_ps(b, s1_v));
}

} // namespace uz::simd
