// test_simd_math.cpp — comprehensive unit tests for uz::simd (simd_math.h)
//
// Tests cover every function in the uz::simd namespace:
//   - SimdCaps::detect()
//   - load_aligned / store_aligned
//   - vec4_add / vec4_sub / vec4_mul / vec4_scale / vec4_negate
//   - vec4_dot / vec3_dot / extract_x
//   - mat4_mul / mat4_inverse
//   - quat_mul / quat_normalize / quat_slerp
//
// All suite names are prefixed with "Simd" to avoid linker collisions with
// test_math.cpp which exercises the higher-level uz:: wrappers.
//
// All memory passed to SIMD load/store functions is declared with alignas(16)
// to satisfy the _mm_load_ps alignment requirement.
//
// Float tolerance: 1e-5f for most comparisons; 1e-4f for transcendental/slerp paths.

#include <gtest/gtest.h>
#include "engine/core/simd_math.h"

#include <cmath>
#include <cstring>

using namespace uz::simd;

// ============================================================================
// Helpers
// ============================================================================

static constexpr float kTol  = 1e-5f;
static constexpr float kTolW = 1e-4f; // wider tolerance for trig / slerp paths

/// Load 4 floats into a __m128 via an aligned intermediate buffer.
static inline __m128 make_m128(float x, float y, float z, float w) noexcept {
    alignas(16) float buf[4] = {x, y, z, w};
    return load_aligned(buf);
}

/// Unpack a __m128 into a plain struct for individual component assertions.
struct Vec4f { float x, y, z, w; };
static inline Vec4f unpack(__m128 v) noexcept {
    alignas(16) float buf[4];
    store_aligned(buf, v);
    return {buf[0], buf[1], buf[2], buf[3]};
}

/// Fill a 16-float column-major identity matrix.
static void fill_identity(float* m) noexcept {
    const float id[16] = {
        1,0,0,0,  // col 0
        0,1,0,0,  // col 1
        0,0,1,0,  // col 2
        0,0,0,1   // col 3
    };
    std::memcpy(m, id, 64);
}

// ============================================================================
// SimdCaps::detect
// ============================================================================

TEST(SimdSimdCaps, Detect_DoesNotCrash) {
    // Running on x64 Windows — SSE4.1 is the minimum.
    const SimdCaps caps = SimdCaps::detect();
    EXPECT_TRUE(caps.has_sse41)
        << "SSE4.1 expected on all x64 CPUs built since 2008";
}

TEST(SimdSimdCaps, Detect_ReturnedTwice_SameResult) {
    // Each call re-probes; results must be consistent.
    const SimdCaps a = SimdCaps::detect();
    const SimdCaps b = SimdCaps::detect();
    EXPECT_EQ(a.has_sse41, b.has_sse41);
    EXPECT_EQ(a.has_avx,   b.has_avx);
    EXPECT_EQ(a.has_avx2,  b.has_avx2);
    EXPECT_EQ(a.has_fma,   b.has_fma);
}

TEST(SimdSimdCaps, DefaultStruct_AllFalse) {
    SimdCaps caps;
    EXPECT_FALSE(caps.has_sse41);
    EXPECT_FALSE(caps.has_avx);
    EXPECT_FALSE(caps.has_avx2);
    EXPECT_FALSE(caps.has_fma);
}

// ============================================================================
// load_aligned / store_aligned — round-trip
// ============================================================================

TEST(SimdLoadStore, RoundTrip_FourFloats_Preserved) {
    alignas(16) float src[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    alignas(16) float dst[4] = {};

    const __m128 v = load_aligned(src);
    store_aligned(dst, v);

    EXPECT_EQ(dst[0], 1.0f);
    EXPECT_EQ(dst[1], 2.0f);
    EXPECT_EQ(dst[2], 3.0f);
    EXPECT_EQ(dst[3], 4.0f);
}

TEST(SimdLoadStore, RoundTrip_NegativeValues_Preserved) {
    alignas(16) float src[4] = {-5.0f, 0.0f, -0.0f, 1e10f};
    alignas(16) float dst[4] = {};

    store_aligned(dst, load_aligned(src));

    EXPECT_EQ(dst[0], -5.0f);
    EXPECT_EQ(dst[1],  0.0f);
    EXPECT_EQ(dst[3],  1e10f);
}

TEST(SimdLoadStore, StoreAligned_OverwritesAll4Slots) {
    alignas(16) float buf[4] = {9.0f, 9.0f, 9.0f, 9.0f};
    const __m128 zeros = _mm_setzero_ps();
    store_aligned(buf, zeros);

    EXPECT_EQ(buf[0], 0.0f);
    EXPECT_EQ(buf[1], 0.0f);
    EXPECT_EQ(buf[2], 0.0f);
    EXPECT_EQ(buf[3], 0.0f);
}

TEST(SimdLoadStore, LoadThenStore_DoesNotCrossContaminate) {
    // Two separate buffers: load from a, store to b, verify b unchanged relative to a.
    alignas(16) float a[4] = {10.0f, 20.0f, 30.0f, 40.0f};
    alignas(16) float b[4] = { 1.0f,  2.0f,  3.0f,  4.0f};

    store_aligned(b, load_aligned(a));

    EXPECT_EQ(b[0], 10.0f);
    EXPECT_EQ(b[1], 20.0f);
    EXPECT_EQ(b[2], 30.0f);
    EXPECT_EQ(b[3], 40.0f);
    // a must be unmodified
    EXPECT_EQ(a[0], 10.0f);
    EXPECT_EQ(a[1], 20.0f);
}

// ============================================================================
// extract_x
// ============================================================================

TEST(SimdExtractX, ExtractsFirstLane) {
    const __m128 v = make_m128(7.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_EQ(extract_x(v), 7.0f);
}

TEST(SimdExtractX, ZeroVector_ReturnsZero) {
    EXPECT_EQ(extract_x(_mm_setzero_ps()), 0.0f);
}

TEST(SimdExtractX, NegativeValue_ReturnsNegative) {
    const __m128 v = make_m128(-3.14f, 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(extract_x(v), -3.14f, kTol);
}

TEST(SimdExtractX, DoesNotReturnHigherLanes) {
    // Only lane 0 (x) should be returned regardless of y/z/w.
    const __m128 v = make_m128(5.0f, 100.0f, 200.0f, 300.0f);
    EXPECT_NEAR(extract_x(v), 5.0f, kTol);
}

// ============================================================================
// vec4_add
// ============================================================================

TEST(SimdVec4Add, ZeroPlusZero_IsZero) {
    const auto r = unpack(vec4_add(_mm_setzero_ps(), _mm_setzero_ps()));
    EXPECT_EQ(r.x, 0.0f);
    EXPECT_EQ(r.y, 0.0f);
    EXPECT_EQ(r.z, 0.0f);
    EXPECT_EQ(r.w, 0.0f);
}

TEST(SimdVec4Add, PositiveComponents_SumCorrect) {
    const __m128 a = make_m128(1.0f, 2.0f, 3.0f, 4.0f);
    const __m128 b = make_m128(5.0f, 6.0f, 7.0f, 8.0f);
    const auto r = unpack(vec4_add(a, b));
    EXPECT_NEAR(r.x, 6.0f,  kTol);
    EXPECT_NEAR(r.y, 8.0f,  kTol);
    EXPECT_NEAR(r.z, 10.0f, kTol);
    EXPECT_NEAR(r.w, 12.0f, kTol);
}

TEST(SimdVec4Add, NegativeComponents_SumCorrect) {
    const __m128 a = make_m128(-1.0f, -2.0f, -3.0f, -4.0f);
    const __m128 b = make_m128( 1.0f,  2.0f,  3.0f,  4.0f);
    const auto r = unpack(vec4_add(a, b));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

TEST(SimdVec4Add, MixedComponents_EachIndependent) {
    const __m128 a = make_m128(10.0f, -3.0f, 0.0f, 100.0f);
    const __m128 b = make_m128(-7.0f,  5.0f, 0.5f, -50.0f);
    const auto r = unpack(vec4_add(a, b));
    EXPECT_NEAR(r.x,   3.0f, kTol);
    EXPECT_NEAR(r.y,   2.0f, kTol);
    EXPECT_NEAR(r.z,   0.5f, kTol);
    EXPECT_NEAR(r.w,  50.0f, kTol);
}

TEST(SimdVec4Add, Commutative_ABEqualsBA) {
    const __m128 a = make_m128(1.5f, -2.5f, 3.5f, -4.5f);
    const __m128 b = make_m128(5.0f,  6.0f, -7.0f, 8.0f);
    const auto ab = unpack(vec4_add(a, b));
    const auto ba = unpack(vec4_add(b, a));
    EXPECT_NEAR(ab.x, ba.x, kTol);
    EXPECT_NEAR(ab.y, ba.y, kTol);
    EXPECT_NEAR(ab.z, ba.z, kTol);
    EXPECT_NEAR(ab.w, ba.w, kTol);
}

// ============================================================================
// vec4_sub
// ============================================================================

TEST(SimdVec4Sub, ZeroMinusZero_IsZero) {
    const auto r = unpack(vec4_sub(_mm_setzero_ps(), _mm_setzero_ps()));
    EXPECT_EQ(r.x, 0.0f);
    EXPECT_EQ(r.y, 0.0f);
    EXPECT_EQ(r.z, 0.0f);
    EXPECT_EQ(r.w, 0.0f);
}

TEST(SimdVec4Sub, AMinusA_IsZero) {
    const __m128 a = make_m128(3.0f, 5.0f, 7.0f, 11.0f);
    const auto r = unpack(vec4_sub(a, a));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

TEST(SimdVec4Sub, KnownValues_CorrectResult) {
    const __m128 a = make_m128(10.0f, 8.0f, 6.0f, 4.0f);
    const __m128 b = make_m128( 1.0f, 2.0f, 3.0f, 4.0f);
    const auto r = unpack(vec4_sub(a, b));
    EXPECT_NEAR(r.x, 9.0f, kTol);
    EXPECT_NEAR(r.y, 6.0f, kTol);
    EXPECT_NEAR(r.z, 3.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

TEST(SimdVec4Sub, SubtractNegative_EquivalentToAdd) {
    // a - (-b) == a + b
    const __m128 a  = make_m128(1.0f, 2.0f, 3.0f, 4.0f);
    const __m128 nb = make_m128(-5.0f, -6.0f, -7.0f, -8.0f);
    const auto r = unpack(vec4_sub(a, nb));
    EXPECT_NEAR(r.x, 6.0f,  kTol);
    EXPECT_NEAR(r.y, 8.0f,  kTol);
    EXPECT_NEAR(r.z, 10.0f, kTol);
    EXPECT_NEAR(r.w, 12.0f, kTol);
}

TEST(SimdVec4Sub, SubThenAddBack_RestoresOriginal) {
    const __m128 a = make_m128(10.0f, -3.0f, 5.0f, 2.0f);
    const __m128 b = make_m128( 1.0f,  7.0f, -2.0f, 0.5f);
    const auto r = unpack(vec4_add(vec4_sub(a, b), b));
    EXPECT_NEAR(r.x, 10.0f, kTol);
    EXPECT_NEAR(r.y, -3.0f, kTol);
    EXPECT_NEAR(r.z,  5.0f, kTol);
    EXPECT_NEAR(r.w,  2.0f, kTol);
}

// ============================================================================
// vec4_mul (component-wise)
// ============================================================================

TEST(SimdVec4Mul, OneTimesOne_IsOne) {
    const __m128 ones = _mm_set1_ps(1.0f);
    const auto r = unpack(vec4_mul(ones, ones));
    EXPECT_NEAR(r.x, 1.0f, kTol);
    EXPECT_NEAR(r.y, 1.0f, kTol);
    EXPECT_NEAR(r.z, 1.0f, kTol);
    EXPECT_NEAR(r.w, 1.0f, kTol);
}

TEST(SimdVec4Mul, ZeroTimesAnything_IsZero) {
    const __m128 a = make_m128(100.0f, -200.0f, 300.0f, 0.5f);
    const auto r = unpack(vec4_mul(a, _mm_setzero_ps()));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

TEST(SimdVec4Mul, KnownValues_CorrectResult) {
    const __m128 a = make_m128(2.0f, 3.0f,  4.0f, 5.0f);
    const __m128 b = make_m128(3.0f, 4.0f, -5.0f, 0.0f);
    const auto r = unpack(vec4_mul(a, b));
    EXPECT_NEAR(r.x,   6.0f, kTol);
    EXPECT_NEAR(r.y,  12.0f, kTol);
    EXPECT_NEAR(r.z, -20.0f, kTol);
    EXPECT_NEAR(r.w,   0.0f, kTol);
}

TEST(SimdVec4Mul, NegativeTimesNegative_IsPositive) {
    const __m128 a = make_m128(-2.0f, -3.0f, -4.0f, -5.0f);
    const __m128 b = make_m128(-1.0f, -1.0f, -1.0f, -1.0f);
    const auto r = unpack(vec4_mul(a, b));
    EXPECT_NEAR(r.x, 2.0f, kTol);
    EXPECT_NEAR(r.y, 3.0f, kTol);
    EXPECT_NEAR(r.z, 4.0f, kTol);
    EXPECT_NEAR(r.w, 5.0f, kTol);
}

TEST(SimdVec4Mul, Commutative_ABEqualsBA) {
    const __m128 a = make_m128(2.0f, -3.0f, 4.0f, -5.0f);
    const __m128 b = make_m128(7.0f,  8.0f, -9.0f, 10.0f);
    const auto ab = unpack(vec4_mul(a, b));
    const auto ba = unpack(vec4_mul(b, a));
    EXPECT_NEAR(ab.x, ba.x, kTol);
    EXPECT_NEAR(ab.y, ba.y, kTol);
    EXPECT_NEAR(ab.z, ba.z, kTol);
    EXPECT_NEAR(ab.w, ba.w, kTol);
}

// ============================================================================
// vec4_scale (scalar broadcast)
// ============================================================================

TEST(SimdVec4Scale, ScaleByOne_Unchanged) {
    const __m128 v = make_m128(1.0f, 2.0f, 3.0f, 4.0f);
    const auto r = unpack(vec4_scale(v, 1.0f));
    EXPECT_NEAR(r.x, 1.0f, kTol);
    EXPECT_NEAR(r.y, 2.0f, kTol);
    EXPECT_NEAR(r.z, 3.0f, kTol);
    EXPECT_NEAR(r.w, 4.0f, kTol);
}

TEST(SimdVec4Scale, ScaleByZero_AllZero) {
    const __m128 v = make_m128(10.0f, 20.0f, 30.0f, 40.0f);
    const auto r = unpack(vec4_scale(v, 0.0f));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

TEST(SimdVec4Scale, ScaleByTwo_Doubles) {
    const __m128 v = make_m128(1.5f, 2.5f, -3.5f, 0.0f);
    const auto r = unpack(vec4_scale(v, 2.0f));
    EXPECT_NEAR(r.x,  3.0f, kTol);
    EXPECT_NEAR(r.y,  5.0f, kTol);
    EXPECT_NEAR(r.z, -7.0f, kTol);
    EXPECT_NEAR(r.w,  0.0f, kTol);
}

TEST(SimdVec4Scale, ScaleByNegative_FlipsSign) {
    const __m128 v = make_m128(1.0f, -1.0f, 2.0f, -2.0f);
    const auto r = unpack(vec4_scale(v, -1.0f));
    EXPECT_NEAR(r.x, -1.0f, kTol);
    EXPECT_NEAR(r.y,  1.0f, kTol);
    EXPECT_NEAR(r.z, -2.0f, kTol);
    EXPECT_NEAR(r.w,  2.0f, kTol);
}

TEST(SimdVec4Scale, ScaleByHalf_Halves) {
    const __m128 v = make_m128(4.0f, 8.0f, 12.0f, 16.0f);
    const auto r = unpack(vec4_scale(v, 0.5f));
    EXPECT_NEAR(r.x, 2.0f, kTol);
    EXPECT_NEAR(r.y, 4.0f, kTol);
    EXPECT_NEAR(r.z, 6.0f, kTol);
    EXPECT_NEAR(r.w, 8.0f, kTol);
}

TEST(SimdVec4Scale, ScaleEquivalentToVec4Mul_WithBroadcast) {
    // vec4_scale(v, s) must equal vec4_mul(v, broadcast(s))
    const __m128 v = make_m128(3.0f, -5.0f, 7.0f, -11.0f);
    const __m128 s_vec = _mm_set1_ps(2.5f);
    const auto from_scale = unpack(vec4_scale(v, 2.5f));
    const auto from_mul   = unpack(vec4_mul(v, s_vec));
    EXPECT_NEAR(from_scale.x, from_mul.x, kTol);
    EXPECT_NEAR(from_scale.y, from_mul.y, kTol);
    EXPECT_NEAR(from_scale.z, from_mul.z, kTol);
    EXPECT_NEAR(from_scale.w, from_mul.w, kTol);
}

// ============================================================================
// vec4_negate
// ============================================================================

TEST(SimdVec4Negate, AllPositive_BecomesAllNegative) {
    const __m128 v = make_m128(1.0f, 2.0f, 3.0f, 4.0f);
    const auto r = unpack(vec4_negate(v));
    EXPECT_NEAR(r.x, -1.0f, kTol);
    EXPECT_NEAR(r.y, -2.0f, kTol);
    EXPECT_NEAR(r.z, -3.0f, kTol);
    EXPECT_NEAR(r.w, -4.0f, kTol);
}

TEST(SimdVec4Negate, AllNegative_BecomesAllPositive) {
    const __m128 v = make_m128(-5.0f, -10.0f, -0.5f, -100.0f);
    const auto r = unpack(vec4_negate(v));
    EXPECT_NEAR(r.x,   5.0f, kTol);
    EXPECT_NEAR(r.y,  10.0f, kTol);
    EXPECT_NEAR(r.z,   0.5f, kTol);
    EXPECT_NEAR(r.w, 100.0f, kTol);
}

TEST(SimdVec4Negate, ZeroVector_RemainsZero) {
    const auto r = unpack(vec4_negate(_mm_setzero_ps()));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

TEST(SimdVec4Negate, DoubleNegate_RestoresOriginal) {
    const __m128 v = make_m128(3.0f, -7.0f, 0.5f, -1.0f);
    const auto r = unpack(vec4_negate(vec4_negate(v)));
    EXPECT_NEAR(r.x,  3.0f, kTol);
    EXPECT_NEAR(r.y, -7.0f, kTol);
    EXPECT_NEAR(r.z,  0.5f, kTol);
    EXPECT_NEAR(r.w, -1.0f, kTol);
}

TEST(SimdVec4Negate, AddNegate_CancelsOut) {
    const __m128 v = make_m128(3.0f, -5.0f, 7.0f, -11.0f);
    const auto r = unpack(vec4_add(v, vec4_negate(v)));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

// ============================================================================
// vec4_dot (4-component)
// ============================================================================

TEST(SimdVec4Dot, PerpendicularUnitVectors_DotIsZero) {
    const __m128 a = make_m128(1.0f, 0.0f, 0.0f, 0.0f);
    const __m128 b = make_m128(0.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(extract_x(vec4_dot(a, b)), 0.0f, kTol);
}

TEST(SimdVec4Dot, SameUnitVector_DotIsOne) {
    const __m128 x = make_m128(1.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(extract_x(vec4_dot(x, x)), 1.0f, kTol);
}

TEST(SimdVec4Dot, ArbitraryVectors_KnownResult) {
    // (1,2,3,4) · (4,3,2,1) = 4+6+6+4 = 20
    const __m128 a = make_m128(1.0f, 2.0f, 3.0f, 4.0f);
    const __m128 b = make_m128(4.0f, 3.0f, 2.0f, 1.0f);
    EXPECT_NEAR(extract_x(vec4_dot(a, b)), 20.0f, kTol);
}

TEST(SimdVec4Dot, BroadcastsToAllFourLanes) {
    // vec4_dot uses 0xFF mask — result should appear in all 4 lanes.
    alignas(16) float out[4];
    const __m128 a = make_m128(2.0f, 0.0f, 0.0f, 0.0f);
    const __m128 b = make_m128(3.0f, 0.0f, 0.0f, 0.0f);
    store_aligned(out, vec4_dot(a, b));
    EXPECT_NEAR(out[0], 6.0f, kTol);
    EXPECT_NEAR(out[1], 6.0f, kTol);
    EXPECT_NEAR(out[2], 6.0f, kTol);
    EXPECT_NEAR(out[3], 6.0f, kTol);
}

TEST(SimdVec4Dot, NegativeComponents_NegativeResult) {
    // (-1,-1,-1,-1) · (1,1,1,1) = -4
    const __m128 a = _mm_set1_ps(-1.0f);
    const __m128 b = _mm_set1_ps( 1.0f);
    EXPECT_NEAR(extract_x(vec4_dot(a, b)), -4.0f, kTol);
}

TEST(SimdVec4Dot, ZeroVector_DotIsZero) {
    const __m128 v = make_m128(5.0f, 6.0f, 7.0f, 8.0f);
    EXPECT_NEAR(extract_x(vec4_dot(v, _mm_setzero_ps())), 0.0f, kTol);
}

TEST(SimdVec4Dot, Symmetric_ABEqualsBA) {
    const __m128 a = make_m128(1.0f, 2.0f, 3.0f, 4.0f);
    const __m128 b = make_m128(5.0f, 6.0f, 7.0f, 8.0f);
    EXPECT_NEAR(extract_x(vec4_dot(a, b)), extract_x(vec4_dot(b, a)), kTol);
}

TEST(SimdVec4Dot, ScaleConsistency_Dot2aB_Equals2DotAB) {
    // dot(2*a, b) == 2 * dot(a, b)
    const __m128 a = make_m128(1.0f, 2.0f, 3.0f, 4.0f);
    const __m128 b = make_m128(4.0f, 3.0f, 2.0f, 1.0f);
    const float d_ab  = extract_x(vec4_dot(a, b));
    const float d_2ab = extract_x(vec4_dot(vec4_scale(a, 2.0f), b));
    EXPECT_NEAR(d_2ab, 2.0f * d_ab, kTol);
}

// ============================================================================
// vec3_dot (3-component — ignores w)
// ============================================================================

TEST(SimdVec3Dot, XDotY_IsZero) {
    const __m128 a = make_m128(1.0f, 0.0f, 0.0f, 99.0f); // w=99, ignored
    const __m128 b = make_m128(0.0f, 1.0f, 0.0f, 99.0f);
    EXPECT_NEAR(extract_x(vec3_dot(a, b)), 0.0f, kTol);
}

TEST(SimdVec3Dot, IgnoresWComponent) {
    // (1,0,0,w) · (1,0,0,w) must be 1 regardless of w.
    const __m128 a = make_m128(1.0f, 0.0f, 0.0f, 1000.0f);
    const __m128 b = make_m128(1.0f, 0.0f, 0.0f, 1000.0f);
    EXPECT_NEAR(extract_x(vec3_dot(a, b)), 1.0f, kTol);
}

TEST(SimdVec3Dot, ArbitraryXYZ_KnownResult) {
    // (1,2,3) · (4,5,6) = 4+10+18 = 32
    const __m128 a = make_m128(1.0f, 2.0f, 3.0f, 100.0f);
    const __m128 b = make_m128(4.0f, 5.0f, 6.0f, 100.0f);
    EXPECT_NEAR(extract_x(vec3_dot(a, b)), 32.0f, kTol);
}

TEST(SimdVec3Dot, VsVec4Dot_DifferWhenWIsNonZero) {
    // With w=1: vec4_dot includes w, vec3_dot does not.
    const __m128 a = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const __m128 b = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(extract_x(vec3_dot(a, b)), 0.0f, kTol); // xyz only → 0
    EXPECT_NEAR(extract_x(vec4_dot(a, b)), 1.0f, kTol); // includes w → 1
}

TEST(SimdVec3Dot, BroadcastsToAllFourLanes) {
    // vec3_dot uses 0x7F mask — result should still be in all lanes.
    alignas(16) float out[4];
    const __m128 a = make_m128(3.0f, 0.0f, 0.0f, 0.0f);
    const __m128 b = make_m128(4.0f, 0.0f, 0.0f, 0.0f);
    store_aligned(out, vec3_dot(a, b));
    EXPECT_NEAR(out[0], 12.0f, kTol);
    EXPECT_NEAR(out[1], 12.0f, kTol);
    EXPECT_NEAR(out[2], 12.0f, kTol);
    EXPECT_NEAR(out[3], 12.0f, kTol);
}

TEST(SimdVec3Dot, ZeroWDoesNotAffectResult) {
    // vec3_dot with w=0 should equal vec4_dot (since w contributes 0).
    const __m128 a = make_m128(2.0f, 3.0f, 4.0f, 0.0f);
    const __m128 b = make_m128(1.0f, 1.0f, 1.0f, 0.0f);
    EXPECT_NEAR(extract_x(vec3_dot(a, b)), extract_x(vec4_dot(a, b)), kTol);
}

// ============================================================================
// mat4_mul
// ============================================================================

TEST(SimdMat4Mul, IdentityTimesIdentity_IsIdentity) {
    alignas(16) float a[16], b[16], r[16];
    fill_identity(a);
    fill_identity(b);

    mat4_mul(a, b, r);

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(r[col * 4 + row], expected, kTol)
                << "Identity*Identity mismatch at col=" << col << " row=" << row;
        }
    }
}

TEST(SimdMat4Mul, MatrixTimesIdentity_IsOriginal) {
    alignas(16) float a[16] = {
         1,  2,  3,  4,   // col 0
         5,  6,  7,  8,   // col 1
         9, 10, 11, 12,   // col 2
        13, 14, 15, 16    // col 3
    };
    alignas(16) float id[16], r[16];
    fill_identity(id);

    mat4_mul(a, id, r);

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(r[i], a[i], kTol) << "A*I != A at index " << i;
    }
}

TEST(SimdMat4Mul, IdentityTimesMatrix_IsOriginal) {
    alignas(16) float a[16] = {
        2, 0, 0, 0,
        0, 3, 0, 0,
        0, 0, 5, 0,
        1, 2, 3, 1
    };
    alignas(16) float id[16], r[16];
    fill_identity(id);

    mat4_mul(id, a, r);

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(r[i], a[i], kTol) << "I*A != A at index " << i;
    }
}

TEST(SimdMat4Mul, TwoTranslationMatrices_CombinesCorrectly) {
    // T(1,0,0) * T(2,0,0) = T(3,0,0) in column-major storage.
    alignas(16) float t1[16] = {1,0,0,0,  0,1,0,0,  0,0,1,0,  1,0,0,1};
    alignas(16) float t2[16] = {1,0,0,0,  0,1,0,0,  0,0,1,0,  2,0,0,1};
    alignas(16) float r[16];

    mat4_mul(t1, t2, r);

    // Translation in col 3 (indices 12..15).
    EXPECT_NEAR(r[12], 3.0f, kTol);
    EXPECT_NEAR(r[13], 0.0f, kTol);
    EXPECT_NEAR(r[14], 0.0f, kTol);
    EXPECT_NEAR(r[15], 1.0f, kTol);
    // Rotation submatrix diagonal must remain identity.
    EXPECT_NEAR(r[0], 1.0f, kTol);
    EXPECT_NEAR(r[5], 1.0f, kTol);
    EXPECT_NEAR(r[10], 1.0f, kTol);
}

TEST(SimdMat4Mul, TwoScaleMatrices_MultipliesComponents) {
    // Scale(2) * Scale(3) = Scale(6)
    alignas(16) float s2[16] = {2,0,0,0,  0,2,0,0,  0,0,2,0,  0,0,0,1};
    alignas(16) float s3[16] = {3,0,0,0,  0,3,0,0,  0,0,3,0,  0,0,0,1};
    alignas(16) float r[16];

    mat4_mul(s2, s3, r);

    EXPECT_NEAR(r[0],  6.0f, kTol);
    EXPECT_NEAR(r[5],  6.0f, kTol);
    EXPECT_NEAR(r[10], 6.0f, kTol);
    EXPECT_NEAR(r[15], 1.0f, kTol);
    EXPECT_NEAR(r[1],  0.0f, kTol);
    EXPECT_NEAR(r[4],  0.0f, kTol);
}

TEST(SimdMat4Mul, Rot90ZSquared_IsRot180Z) {
    // 90° rot around Z in column-major: col0=(0,1,0,0), col1=(-1,0,0,0)
    alignas(16) float rot90[16] = {
        0, 1, 0, 0,   // col 0
       -1, 0, 0, 0,   // col 1
        0, 0, 1, 0,   // col 2
        0, 0, 0, 1    // col 3
    };
    alignas(16) float r[16];

    mat4_mul(rot90, rot90, r);

    // 180° around Z: col0=(-1,0,0,0), col1=(0,-1,0,0)
    EXPECT_NEAR(r[0],  -1.0f, kTol);
    EXPECT_NEAR(r[1],   0.0f, kTol);
    EXPECT_NEAR(r[4],   0.0f, kTol);
    EXPECT_NEAR(r[5],  -1.0f, kTol);
    EXPECT_NEAR(r[10],  1.0f, kTol);
    EXPECT_NEAR(r[15],  1.0f, kTol);
}

TEST(SimdMat4Mul, CrossCheck_MatchesScalarResult) {
    // Brute-force scalar column-major 4x4 multiply, then compare with SIMD.
    alignas(16) float a[16] = {
         1, 2, 3, 4,
         5, 6, 7, 8,
         9,10,11,12,
        13,14,15,16
    };
    alignas(16) float b[16] = {
        16,15,14,13,
        12,11,10, 9,
         8, 7, 6, 5,
         4, 3, 2, 1
    };
    alignas(16) float simd_r[16];
    float scalar_r[16] = {};

    // Scalar reference
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            scalar_r[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }

    mat4_mul(a, b, simd_r);

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(simd_r[i], scalar_r[i], kTolW)
            << "SIMD vs scalar mat4_mul mismatch at index " << i;
    }
}

// ============================================================================
// mat4_inverse
// ============================================================================

TEST(SimdMat4Inverse, Identity_ReturnsTrueAndIdentity) {
    alignas(16) float id[16], inv[16];
    fill_identity(id);

    const bool ok = mat4_inverse(id, inv);

    EXPECT_TRUE(ok);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(inv[col * 4 + row], expected, kTol)
                << "Inverse of identity mismatch at col=" << col << " row=" << row;
        }
    }
}

TEST(SimdMat4Inverse, Translation_InverseHasNegativeTranslation) {
    // T(5,3,-7)^-1 = T(-5,-3,7)
    alignas(16) float t[16] = {1,0,0,0,  0,1,0,0,  0,0,1,0,  5,3,-7,1};
    alignas(16) float inv[16];

    const bool ok = mat4_inverse(t, inv);

    EXPECT_TRUE(ok);
    EXPECT_NEAR(inv[12], -5.0f, kTol);
    EXPECT_NEAR(inv[13], -3.0f, kTol);
    EXPECT_NEAR(inv[14],  7.0f, kTol);
    EXPECT_NEAR(inv[15],  1.0f, kTol);
}

TEST(SimdMat4Inverse, ScaleMatrix_InverseHasReciprocalScale) {
    // Scale(2,4,8)^-1 = Scale(0.5, 0.25, 0.125)
    alignas(16) float s[16] = {2,0,0,0,  0,4,0,0,  0,0,8,0,  0,0,0,1};
    alignas(16) float inv[16];

    const bool ok = mat4_inverse(s, inv);

    EXPECT_TRUE(ok);
    EXPECT_NEAR(inv[0],  0.5f,   kTol);
    EXPECT_NEAR(inv[5],  0.25f,  kTol);
    EXPECT_NEAR(inv[10], 0.125f, kTol);
    EXPECT_NEAR(inv[15], 1.0f,   kTol);
}

TEST(SimdMat4Inverse, MatrixTimesInverse_IsIdentity) {
    alignas(16) float a[16] = {2,0,0,0,  0,2,0,0,  0,0,2,0,  3,0,0,1};
    alignas(16) float inv[16], product[16];

    const bool ok = mat4_inverse(a, inv);
    ASSERT_TRUE(ok);

    mat4_mul(a, inv, product);

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(product[col * 4 + row], expected, kTolW)
                << "A * inv(A) != I at col=" << col << " row=" << row;
        }
    }
}

TEST(SimdMat4Inverse, InverseTimesMatrix_IsIdentity) {
    alignas(16) float a[16] = {2,0,0,0,  0,3,0,0,  0,0,5,0,  1,2,3,1};
    alignas(16) float inv[16], product[16];

    const bool ok = mat4_inverse(a, inv);
    ASSERT_TRUE(ok);

    mat4_mul(inv, a, product);

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(product[col * 4 + row], expected, kTolW)
                << "inv(A) * A != I at col=" << col << " row=" << row;
        }
    }
}

TEST(SimdMat4Inverse, SingularMatrix_ReturnsFalseAndStoresIdentity) {
    // All rows the same → det = 0 → singular.
    alignas(16) float singular[16] = {
        1,1,1,1,
        1,1,1,1,
        1,1,1,1,
        1,1,1,1
    };
    alignas(16) float result[16];

    const bool ok = mat4_inverse(singular, result);

    EXPECT_FALSE(ok) << "Expected mat4_inverse to return false for singular matrix";
    // Implementation must store identity on failure.
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(result[col * 4 + row], expected, kTol)
                << "Expected identity fallback at col=" << col << " row=" << row;
        }
    }
}

TEST(SimdMat4Inverse, ZeroMatrix_ReturnsFalseAndStoresIdentity) {
    alignas(16) float zero[16] = {};
    alignas(16) float result[16];

    const bool ok = mat4_inverse(zero, result);

    EXPECT_FALSE(ok);
    EXPECT_NEAR(result[0],  1.0f, kTol);
    EXPECT_NEAR(result[5],  1.0f, kTol);
    EXPECT_NEAR(result[10], 1.0f, kTol);
    EXPECT_NEAR(result[15], 1.0f, kTol);
    // Off-diagonal spots should be zero.
    EXPECT_NEAR(result[1],  0.0f, kTol);
    EXPECT_NEAR(result[4],  0.0f, kTol);
}

TEST(SimdMat4Inverse, TRSMatrix_RoundTrips) {
    // Build TRS by composing translation * rotation90Z * scale.
    alignas(16) float t[16]   = {1,0,0,0,  0,1,0,0,  0,0,1,0,  10,20,30,1};
    alignas(16) float rot[16] = {0,1,0,0, -1,0,0,0,   0,0,1,0,   0, 0, 0,1};
    alignas(16) float s[16]   = {2,0,0,0,  0,3,0,0,   0,0,0.5f,0, 0,0,0,1};

    alignas(16) float tr[16], trs[16], inv[16], product[16];
    mat4_mul(t, rot, tr);
    mat4_mul(tr, s, trs);

    const bool ok = mat4_inverse(trs, inv);
    ASSERT_TRUE(ok);

    mat4_mul(trs, inv, product);

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(product[col * 4 + row], expected, kTolW)
                << "TRS * inv(TRS) != I at col=" << col << " row=" << row;
        }
    }
}

TEST(SimdMat4Inverse, DoubleInverse_RestoresOriginal) {
    // inv(inv(A)) == A
    alignas(16) float a[16] = {2,0,0,0,  0,3,0,0,  0,0,5,0,  1,2,3,1};
    alignas(16) float inv1[16], inv2[16];

    ASSERT_TRUE(mat4_inverse(a, inv1));
    ASSERT_TRUE(mat4_inverse(inv1, inv2));

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(inv2[i], a[i], kTolW)
            << "inv(inv(A)) != A at index " << i;
    }
}

// ============================================================================
// quat_mul
// ============================================================================

TEST(SimdQuatMul, IdentityTimesIdentity_IsIdentity) {
    // Identity quat = (0,0,0,1) i.e. x=0, y=0, z=0, w=1.
    const __m128 id = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const auto r = unpack(quat_mul(id, id));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 1.0f, kTol);
}

TEST(SimdQuatMul, IdentityTimesQ_IsQ) {
    const __m128 id = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    // 90° around Z: (0, 0, sin45, cos45)
    const float s = 0.70710678f;
    const __m128 q = make_m128(0.0f, 0.0f, s, s);
    const auto r = unpack(quat_mul(id, q));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, s, kTol);
    EXPECT_NEAR(r.w, s, kTol);
}

TEST(SimdQuatMul, QTimesIdentity_IsQ) {
    const __m128 id = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const float s = 0.70710678f;
    const __m128 q = make_m128(0.0f, 0.0f, s, s);
    const auto r = unpack(quat_mul(q, id));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, s, kTol);
    EXPECT_NEAR(r.w, s, kTol);
}

TEST(SimdQuatMul, Rot90ZTwice_Is180Z) {
    // 90° * 90° = 180°: (0,0,sin90,cos90) = (0,0,1,0)
    const float s = 0.70710678f;
    const __m128 q90 = make_m128(0.0f, 0.0f, s, s);
    const auto r = unpack(quat_mul(q90, q90));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 1.0f, kTol);
    EXPECT_NEAR(std::abs(r.w), 0.0f, kTol);
}

TEST(SimdQuatMul, HamiltonProduct_ITimesJ_IsK) {
    // i * j = k: (1,0,0,0) * (0,1,0,0) = (0,0,1,0)
    const __m128 i = make_m128(1.0f, 0.0f, 0.0f, 0.0f);
    const __m128 j = make_m128(0.0f, 1.0f, 0.0f, 0.0f);
    const auto r = unpack(quat_mul(i, j));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 1.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

TEST(SimdQuatMul, AntiCommutative_IJNeqJI) {
    // i*j = k, j*i = -k
    const __m128 i = make_m128(1.0f, 0.0f, 0.0f, 0.0f);
    const __m128 j = make_m128(0.0f, 1.0f, 0.0f, 0.0f);
    const auto ij = unpack(quat_mul(i, j));
    const auto ji = unpack(quat_mul(j, i));
    EXPECT_NEAR(ij.z,  1.0f, kTol);
    EXPECT_NEAR(ji.z, -1.0f, kTol);
}

TEST(SimdQuatMul, ITimesI_IsNegativeScalar) {
    // i*i = -1 → (0,0,0,-1)
    const __m128 i = make_m128(1.0f, 0.0f, 0.0f, 0.0f);
    const auto r = unpack(quat_mul(i, i));
    EXPECT_NEAR(r.x, 0.0f,  kTol);
    EXPECT_NEAR(r.y, 0.0f,  kTol);
    EXPECT_NEAR(r.z, 0.0f,  kTol);
    EXPECT_NEAR(r.w, -1.0f, kTol);
}

TEST(SimdQuatMul, JTimesK_IsI) {
    // j*k = i: (0,1,0,0) * (0,0,1,0) = (1,0,0,0)
    const __m128 j = make_m128(0.0f, 1.0f, 0.0f, 0.0f);
    const __m128 k = make_m128(0.0f, 0.0f, 1.0f, 0.0f);
    const auto r = unpack(quat_mul(j, k));
    EXPECT_NEAR(r.x, 1.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

TEST(SimdQuatMul, UnitQuatProduct_RemainsUnit) {
    // Unit * unit must stay unit.
    const float s = 0.70710678f;
    const __m128 q1 = make_m128(0.0f, 0.0f, s, s);
    const __m128 q2 = make_m128(s, 0.0f, 0.0f, s);
    const auto r = unpack(quat_mul(q1, q2));
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTolW);
}

TEST(SimdQuatMul, CrossCheck_ArbitraryQuats_MatchScalarHamilton) {
    // Manually compute Hamilton product and compare with SIMD result.
    const float ax = 0.5f, ay = 0.5f, az = 0.5f, aw = 0.5f; // unit
    const float bx = 0.0f, by = 0.70710678f, bz = 0.0f, bw = 0.70710678f; // unit

    // Scalar reference:
    const float rx = aw*bx + ax*bw + ay*bz - az*by;
    const float ry = aw*by - ax*bz + ay*bw + az*bx;
    const float rz = aw*bz + ax*by - ay*bx + az*bw;
    const float rw = aw*bw - ax*bx - ay*by - az*bz;

    const __m128 va = make_m128(ax, ay, az, aw);
    const __m128 vb = make_m128(bx, by, bz, bw);
    const auto r = unpack(quat_mul(va, vb));

    EXPECT_NEAR(r.x, rx, kTolW);
    EXPECT_NEAR(r.y, ry, kTolW);
    EXPECT_NEAR(r.z, rz, kTolW);
    EXPECT_NEAR(r.w, rw, kTolW);
}

// ============================================================================
// quat_normalize
// ============================================================================

TEST(SimdQuatNormalize, IdentityQuat_RemainsIdentity) {
    const __m128 id = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const auto r = unpack(quat_normalize(id));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 1.0f, kTol);
}

TEST(SimdQuatNormalize, ScaledQuat_BecomesUnit) {
    // (0, 0, 0, 5) → (0, 0, 0, 1)
    const __m128 q = make_m128(0.0f, 0.0f, 0.0f, 5.0f);
    const auto r = unpack(quat_normalize(q));
    EXPECT_NEAR(r.w, 1.0f, kTol);
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTol);
}

TEST(SimdQuatNormalize, ArbitraryQuat_ResultIsUnit) {
    const __m128 q = make_m128(1.0f, 2.0f, 3.0f, 4.0f);
    const auto r = unpack(quat_normalize(q));
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTol);
}

TEST(SimdQuatNormalize, PureXQuat_NormalisesCorrectly) {
    // (3, 0, 0, 0) → (1, 0, 0, 0)
    const __m128 q = make_m128(3.0f, 0.0f, 0.0f, 0.0f);
    const auto r = unpack(quat_normalize(q));
    EXPECT_NEAR(r.x, 1.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 0.0f, kTol);
}

TEST(SimdQuatNormalize, NearZeroQuat_ReturnsIdentityFallback) {
    // Length < 1e-6 → must fallback to identity (0,0,0,1).
    const __m128 q = _mm_setzero_ps();
    const auto r = unpack(quat_normalize(q));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 1.0f, kTol);
}

TEST(SimdQuatNormalize, VerySmallButNonZero_ReturnsIdentityFallback) {
    // Components below epsilon threshold → also identity.
    const __m128 q = make_m128(1e-8f, 0.0f, 0.0f, 0.0f);
    const auto r = unpack(quat_normalize(q));
    EXPECT_NEAR(r.w, 1.0f, kTol) << "Sub-epsilon quat should fallback to identity";
}

TEST(SimdQuatNormalize, AlreadyUnitQuat_Unchanged) {
    const float s = 0.70710678f;
    const __m128 q = make_m128(0.0f, 0.0f, s, s);
    const auto r = unpack(quat_normalize(q));
    EXPECT_NEAR(r.z, s, kTol);
    EXPECT_NEAR(r.w, s, kTol);
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTol);
}

TEST(SimdQuatNormalize, NegativeComponents_SignsPreserved_LengthIsOne) {
    // (-2,-2,-2,-2) → (-0.5,-0.5,-0.5,-0.5)
    const __m128 q = make_m128(-2.0f, -2.0f, -2.0f, -2.0f);
    const auto r = unpack(quat_normalize(q));
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTol);
    EXPECT_NEAR(r.x, -0.5f, kTol);
    EXPECT_NEAR(r.y, -0.5f, kTol);
    EXPECT_NEAR(r.z, -0.5f, kTol);
    EXPECT_NEAR(r.w, -0.5f, kTol);
}

TEST(SimdQuatNormalize, LargeComponents_ResultIsUnit) {
    const __m128 q = make_m128(1000.0f, 0.0f, 0.0f, 0.0f);
    const auto r = unpack(quat_normalize(q));
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTol);
    EXPECT_NEAR(r.x, 1.0f, kTol);
}

// ============================================================================
// quat_slerp
// ============================================================================

TEST(SimdQuatSlerp, AtT0_ReturnsA) {
    const __m128 a = make_m128(0.0f, 0.0f, 0.0f, 1.0f); // identity
    const float s = 0.70710678f;
    const __m128 b = make_m128(0.0f, 0.0f, s, s);        // 90° around Z

    const auto r = unpack(quat_slerp(a, b, 0.0f));
    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
    EXPECT_NEAR(r.w, 1.0f, kTol);
}

TEST(SimdQuatSlerp, AtT1_ReturnsB) {
    const __m128 a = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const float s = 0.70710678f;
    const __m128 b = make_m128(0.0f, 0.0f, s, s);

    const auto r = unpack(quat_slerp(a, b, 1.0f));
    EXPECT_NEAR(r.x, 0.0f, kTolW);
    EXPECT_NEAR(r.y, 0.0f, kTolW);
    EXPECT_NEAR(r.z, s,    kTolW);
    EXPECT_NEAR(r.w, s,    kTolW);
}

TEST(SimdQuatSlerp, AtT05_Is45Degrees_AndUnit) {
    // Identity to 90°Z at t=0.5 should be 45°Z.
    // 45°Z: sin(22.5°) ≈ 0.38268, cos(22.5°) ≈ 0.92388
    const __m128 a = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const float s = 0.70710678f;
    const __m128 b = make_m128(0.0f, 0.0f, s, s);

    const auto r = unpack(quat_slerp(a, b, 0.5f));
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTolW);
    EXPECT_NEAR(r.z, 0.38268f, kTolW);
    EXPECT_NEAR(r.w, 0.92388f, kTolW);
}

TEST(SimdQuatSlerp, NegativeDot_TakesShortestPath_ResultIsUnit) {
    // b = -a represents the same rotation but negated. dot < 0 triggers short-path.
    const float s = 0.70710678f;
    const __m128 a = make_m128(0.0f, 0.0f, s, s);
    const __m128 b = make_m128(0.0f, 0.0f, -s, -s);

    const auto r = unpack(quat_slerp(a, b, 0.5f));
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTolW);
}

TEST(SimdQuatSlerp, NearlyIdenticalQuats_LinearFallback_ResultIsUnit) {
    // When dot > 0.9995 the code uses linear interpolation + normalize.
    const __m128 a = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    // Very small rotation (0.01 rad around Z).
    const float s_small = 0.004999958f;
    const float w_small = 0.999987500f;
    const __m128 b = make_m128(0.0f, 0.0f, s_small, w_small);

    const auto r = unpack(quat_slerp(a, b, 0.5f));
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTolW);
}

TEST(SimdQuatSlerp, SameQuat_ResultIsUnit) {
    // Slerp(q, q, t) must yield a unit quat for any t.
    const __m128 id = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const auto r = unpack(quat_slerp(id, id, 0.5f));
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTolW);
    EXPECT_NEAR(r.w, 1.0f, kTolW);
}

TEST(SimdQuatSlerp, Monotonic_TValuesIncreaseZComponent) {
    // Slerp(identity, rot90Z, t) — z component must increase monotonically with t.
    const __m128 a = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const float s = 0.70710678f;
    const __m128 b = make_m128(0.0f, 0.0f, s, s);

    float prev_z = -1.0f;
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto r = unpack(quat_slerp(a, b, t));
        EXPECT_GE(r.z, prev_z - kTolW) << "z not monotonically increasing at t=" << t;
        prev_z = r.z;
    }
}

TEST(SimdQuatSlerp, LargeAngle_180Degrees_T05_Is90Degrees) {
    // Slerp(identity, rot180Z, 0.5) = rot90Z.
    // rot180Z: (0,0,1,0). dot(id, rot180Z) = 0 → full slerp path.
    const __m128 a = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const __m128 b = make_m128(0.0f, 0.0f, 1.0f, 0.0f);

    const auto r = unpack(quat_slerp(a, b, 0.5f));
    const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    EXPECT_NEAR(len, 1.0f, kTolW);
    // 90° around Z: z≈sin45, w≈cos45
    EXPECT_NEAR(r.z, 0.70710678f, kTolW);
    EXPECT_NEAR(r.w, 0.70710678f, kTolW);
}

TEST(SimdQuatSlerp, AllTValues_ResultIsAlwaysUnit) {
    const __m128 a = make_m128(0.0f, 0.0f, 0.0f, 1.0f);
    const float s = 0.70710678f;
    const __m128 b = make_m128(s, 0.0f, 0.0f, s); // 90° around X

    for (float t : {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f}) {
        const auto r = unpack(quat_slerp(a, b, t));
        const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
        EXPECT_NEAR(len, 1.0f, kTolW) << "Not unit at t=" << t;
    }
}
