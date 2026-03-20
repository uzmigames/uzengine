/// UzEngine SH Projection — Implementation.
///
/// Projects cubemap faces into L2 Spherical Harmonics coefficients using
/// the standard real SH basis, then packs into standard 7-float4 format.
/// Includes a GPU readback path that copies RGBA16F cubemap data to CPU,
/// converts half-float to float, and runs the CPU SH projection.

#include "sh_projection.h"

#include "engine/core/simd_dispatch.h"
#include "engine/rhi/rhi.h"
#include "log.h"

#include <cmath>
#include <cstring>
#include <vector>

UZ_DEFINE_LOG_CATEGORY(SHProjection, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kPI = 3.14159265358979323846f;

/// Standard real SH basis normalization constants (orthonormal on the sphere).
/// Y_lm(dir) = K_lm * P_l^|m|(cos_theta) * trig(m * phi)
///
/// For m=0: Y_l0 = K_l0 * P_l(cos_theta)
/// For m>0: Y_lm = sqrt(2) * K_lm * P_l^m(cos_theta) * cos(m*phi)
/// For m<0: Y_l|m| = sqrt(2) * K_l|m| * P_l^|m|(cos_theta) * sin(|m|*phi)
///
/// These are the standard constants used by reference (SHMath.cpp InitSHTables):
/// K_lm = sqrt((2l+1)/(4pi) * (l-|m|)!/(l+|m|)!) * (m!=0 ? sqrt(2) : 1)
///
/// Precomputed for L0..L2:
/// V[0] = Y00:  1/(2*sqrt(pi))                    = 0.282095
/// V[1] = Y1-1: sqrt(3/(4*pi)) * sqrt(2) * sin(phi) * (-sqrt(1-z^2))
///             → but the combined constant = sqrt(3/(4*pi)) = 0.488603
///               (the sqrt(2) cancels with the 1/sqrt(2) from the complex->real conversion)
/// ... and so on. The point is: the reference uses the STANDARD real SH basis.

// ─────────────────────────────────────────────────────────────────────────────
// SH Basis Evaluation (order 3 / L2, 9 coefficients)
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates all 9 real SH basis functions at the given unit direction.
///
/// Uses the standard real spherical harmonics (same as reference SHBasisFunction).
/// Index ordering follows Equivalent: V[L*(L+1)+M] for L=0..2, M=-L..+L.
///
/// # Arguments
/// * `dir` — Unit direction vector (must be normalized).
/// * `out` — Output array of 9 basis function values.
static void EvaluateSHBasis(float x, float y, float z, float out[9])
{
    // L=0
    // Y00 = 1/(2*sqrt(pi))
    out[0] = 0.282094791773878f;

    // L=1
    // Y1,-1 = sqrt(3/(4*pi)) * y
    out[1] = 0.488602511902920f * y;
    // Y1,0  = sqrt(3/(4*pi)) * z
    out[2] = 0.488602511902920f * z;
    // Y1,1  = sqrt(3/(4*pi)) * x
    out[3] = 0.488602511902920f * x;

    // L=2
    // Y2,-2 = 0.5*sqrt(15/pi) * x*y = 1.092548430592079 * x*y
    out[4] = 1.092548430592079f * x * y;
    // Y2,-1 = 0.5*sqrt(15/pi) * y*z
    out[5] = 1.092548430592079f * y * z;
    // Y2,0  = 0.25*sqrt(5/pi) * (3*z*z - 1)
    out[6] = 0.315391565252520f * (3.0f * z * z - 1.0f);
    // Y2,1  = 0.5*sqrt(15/pi) * x*z
    out[7] = 1.092548430592079f * x * z;
    // Y2,2  = 0.25*sqrt(15/pi) * (x*x - y*y)
    out[8] = 0.546274215296040f * (x * x - y * y);
}

// ─────────────────────────────────────────────────────────────────────────────
// SH Basis Evaluation — SSE2 vectorized (4 directions at once)
// ─────────────────────────────────────────────────────────────────────────────

#if UZ_SIMD_SSE2
/// Evaluates all 9 SH basis functions for 4 directions simultaneously using SSE2.
///
/// Each __m128 input holds 4 x/y/z values from 4 different cubemap directions.
/// Output: 9 __m128 values, each containing the basis value for all 4 directions.
/// Uses the same constants as the scalar EvaluateSHBasis — no approximation.
static void EvaluateSHBasis4(__m128 x4, __m128 y4, __m128 z4, __m128 out[9])
{
    const __m128 c0    = _mm_set1_ps(0.282094791773878f);
    const __m128 c1    = _mm_set1_ps(0.488602511902920f);
    const __m128 c2    = _mm_set1_ps(1.092548430592079f);
    const __m128 c3    = _mm_set1_ps(0.315391565252520f);
    const __m128 c4    = _mm_set1_ps(0.546274215296040f);
    const __m128 three = _mm_set1_ps(3.0f);
    const __m128 one   = _mm_set1_ps(1.0f);

    // L=0: Y00 = constant
    out[0] = c0;

    // L=1: Y1,-1 = c1*y,  Y1,0 = c1*z,  Y1,1 = c1*x
    out[1] = _mm_mul_ps(c1, y4);
    out[2] = _mm_mul_ps(c1, z4);
    out[3] = _mm_mul_ps(c1, x4);

    // L=2: Y2,-2 = c2*x*y
    out[4] = _mm_mul_ps(c2, _mm_mul_ps(x4, y4));
    // Y2,-1 = c2*y*z
    out[5] = _mm_mul_ps(c2, _mm_mul_ps(y4, z4));
    // Y2,0  = c3*(3*z*z - 1)
    out[6] = _mm_mul_ps(c3, _mm_sub_ps(_mm_mul_ps(three, _mm_mul_ps(z4, z4)), one));
    // Y2,1  = c2*x*z
    out[7] = _mm_mul_ps(c2, _mm_mul_ps(x4, z4));
    // Y2,2  = c4*(x*x - y*y)
    out[8] = _mm_mul_ps(c4, _mm_sub_ps(_mm_mul_ps(x4, x4), _mm_mul_ps(y4, y4)));
}
#endif // UZ_SIMD_SSE2

// ─────────────────────────────────────────────────────────────────────────────
// Cubemap Direction & Solid Angle Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the unit direction vector for a cubemap texel.
///
/// # Arguments
/// * `face` — Cubemap face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
/// * `u`, `v` — Texel center coordinates in [0, 1].
/// * `ox`, `oy`, `oz` — Output direction components (NOT normalized).
static void CubemapTexelDirection(uint32_t face, float u, float v,
                                  float& ox, float& oy, float& oz)
{
    // Map [0,1] to [-1,+1]
    const float s = 2.0f * u - 1.0f;
    const float t = 2.0f * v - 1.0f;

    // DX cubemap convention: face order +X, -X, +Y, -Y, +Z, -Z.
    // For each face, the direction is computed so that the texel
    // center maps to the correct point on the unit cube.
    switch (face) {
        case 0: ox =  1.0f; oy = -t;    oz =  -s;   break; // +X
        case 1: ox = -1.0f; oy = -t;    oz =   s;   break; // -X
        case 2: ox =  s;    oy =  1.0f; oz =   t;   break; // +Y
        case 3: ox =  s;    oy = -1.0f; oz =  -t;   break; // -Y
        case 4: ox =  s;    oy = -t;    oz =  1.0f;  break; // +Z
        case 5: ox = -s;    oy = -t;    oz = -1.0f;  break; // -Z
        default: ox = oy = oz = 0.0f; break;
    }
}

/// Computes the solid angle subtended by a cubemap texel.
///
/// Uses the exact differential solid angle formula for a cube-projected sphere:
/// dw = 1 / (length(dir)^3) * (1/N^2) * 4
///
/// where dir is the unnormalized direction to the texel center and N is the face size.
///
/// # Arguments
/// * `u`, `v` — Texel center in [-1, +1] (NOT [0,1]).
///
/// # Returns
/// Solid angle in steradians for this texel.
static float CubemapTexelSolidAngle(float u, float v)
{
    const float r2 = 1.0f + u * u + v * v;
    const float r = std::sqrt(r2);
    return 1.0f / (r2 * r);
}

// ─────────────────────────────────────────────────────────────────────────────
// Packing into reference Format
// ─────────────────────────────────────────────────────────────────────────────

/// Packs raw L2 SH coefficients (9 per channel) into the reference engine's 7-float4 format.
///
/// The packing bakes in diffuse convolution (cosine lobe) scaling so that
/// the shader only needs dot products with the normal to reconstruct irradiance.
///
/// Standard SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance()
/// exactly (ReflectionEnvironment.cpp).
///
/// # Arguments
/// * `shR`, `shG`, `shB` — Raw SH coefficients per color channel (9 each).
/// * `out` — Output 7 float4 values.
static void PackSHToCompactFormat(const float shR[9], const float shG[9],
                              const float shB[9], Vec4 out[7])
{
    const float sqrtPI = std::sqrt(kPI);

    const float c0 = 1.0f / (2.0f * sqrtPI);                 // 0.282095
    const float c1 = std::sqrt(3.0f) / (3.0f * sqrtPI);      // 0.325735
    const float c2 = std::sqrt(15.0f) / (8.0f * sqrtPI);     // 0.273137
    const float c3 = std::sqrt(5.0f) / (16.0f * sqrtPI);     // 0.078848
    const float c4 = 0.5f * c2;                               // 0.136569

    // RED channel L0+L1
    out[0].x = -c1 * shR[3];
    out[0].y = -c1 * shR[1];
    out[0].z =  c1 * shR[2];
    out[0].w =  c0 * shR[0] - c3 * shR[6];

    // GREEN channel L0+L1
    out[1].x = -c1 * shG[3];
    out[1].y = -c1 * shG[1];
    out[1].z =  c1 * shG[2];
    out[1].w =  c0 * shG[0] - c3 * shG[6];

    // BLUE channel L0+L1
    out[2].x = -c1 * shB[3];
    out[2].y = -c1 * shB[1];
    out[2].z =  c1 * shB[2];
    out[2].w =  c0 * shB[0] - c3 * shB[6];

    // RED channel L2
    out[3].x =  c2 * shR[4];
    out[3].y = -c2 * shR[5];
    out[3].z =  3.0f * c3 * shR[6];
    out[3].w = -c2 * shR[7];

    // GREEN channel L2
    out[4].x =  c2 * shG[4];
    out[4].y = -c2 * shG[5];
    out[4].z =  3.0f * c3 * shG[6];
    out[4].w = -c2 * shG[7];

    // BLUE channel L2
    out[5].x =  c2 * shB[4];
    out[5].y = -c2 * shB[5];
    out[5].z =  3.0f * c3 * shB[6];
    out[5].w = -c2 * shB[7];

    // Y2,2 term
    out[6].x = c4 * shR[8];
    out[6].y = c4 * shG[8];
    out[6].z = c4 * shB[8];
    out[6].w = 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API — CPU projection from face data
// ─────────────────────────────────────────────────────────────────────────────

SHCoefficients ProjectCubemapToSH(const float* faceData[6], uint32_t faceSize)
{
    float shR[9] = {};
    float shG[9] = {};
    float shB[9] = {};

    float totalWeight = 0.0f;

    const float invN = 1.0f / static_cast<float>(faceSize);
    const float texelArea = 4.0f * invN * invN;

    for (uint32_t face = 0; face < 6; ++face) {
        const float* pixels = faceData[face];
        if (!pixels) {
            UZ_LOG(SHProjection, Warning, "Face {} is null, skipping", face);
            continue;
        }

        for (uint32_t y = 0; y < faceSize; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) * invN;
            const float sv = 2.0f * v - 1.0f;

            uint32_t x = 0;

#if UZ_SIMD_SSE2
            // Process 4 consecutive x-pixels per iteration using SSE2.
            // Direction computation, normalization, solid angle, SH basis, and
            // RGB accumulation are all done in parallel across the 4 texels.
            const uint32_t simdEnd = faceSize & ~3u;

            const __m128 vInvN    = _mm_set1_ps(invN);
            const __m128 vHalf    = _mm_set1_ps(0.5f);
            const __m128 vTwo     = _mm_set1_ps(2.0f);
            const __m128 vOne     = _mm_set1_ps(1.0f);
            const __m128 vTexArea = _mm_set1_ps(texelArea);
            const __m128 vV       = _mm_set1_ps(v);
            const __m128 vSV      = _mm_set1_ps(sv);

            for (; x < simdEnd; x += 4) {
                // u = (x + 0.5) * invN for 4 consecutive x values
                const __m128 xBase = _mm_set_ps(
                    static_cast<float>(x + 3),
                    static_cast<float>(x + 2),
                    static_cast<float>(x + 1),
                    static_cast<float>(x + 0));
                const __m128 u4 = _mm_mul_ps(_mm_add_ps(xBase, vHalf), vInvN);

                // s = 2*u - 1, t = 2*v - 1 (cube-space coords)
                const __m128 s4 = _mm_sub_ps(_mm_mul_ps(vTwo, u4), vOne);
                const __m128 t4 = _mm_sub_ps(_mm_mul_ps(vTwo, vV), vOne);

                // Compute cubemap texel direction for 4 texels.
                // The face index is constant within this loop, so the switch
                // selects the mapping once and applies it to all 4 texels.
                __m128 dx4, dy4, dz4;
                switch (face) {
                    case 0: // +X: ox=1, oy=-t, oz=-s
                        dx4 = vOne;
                        dy4 = _mm_sub_ps(_mm_setzero_ps(), t4);
                        dz4 = _mm_sub_ps(_mm_setzero_ps(), s4);
                        break;
                    case 1: // -X: ox=-1, oy=-t, oz=s
                        dx4 = _mm_sub_ps(_mm_setzero_ps(), vOne);
                        dy4 = _mm_sub_ps(_mm_setzero_ps(), t4);
                        dz4 = s4;
                        break;
                    case 2: // +Y: ox=s, oy=1, oz=t
                        dx4 = s4;
                        dy4 = vOne;
                        dz4 = t4;
                        break;
                    case 3: // -Y: ox=s, oy=-1, oz=-t
                        dx4 = s4;
                        dy4 = _mm_sub_ps(_mm_setzero_ps(), vOne);
                        dz4 = _mm_sub_ps(_mm_setzero_ps(), t4);
                        break;
                    case 4: // +Z: ox=s, oy=-t, oz=1
                        dx4 = s4;
                        dy4 = _mm_sub_ps(_mm_setzero_ps(), t4);
                        dz4 = vOne;
                        break;
                    case 5: // -Z: ox=-s, oy=-t, oz=-1
                        dx4 = _mm_sub_ps(_mm_setzero_ps(), s4);
                        dy4 = _mm_sub_ps(_mm_setzero_ps(), t4);
                        dz4 = _mm_sub_ps(_mm_setzero_ps(), vOne);
                        break;
                    default:
                        dx4 = dy4 = dz4 = _mm_setzero_ps();
                        break;
                }

                // Normalize direction: invLen = rsqrt(dx*dx + dy*dy + dz*dz)
                // with one Newton-Raphson refinement for ~23-bit accuracy.
                const __m128 lenSq = _mm_add_ps(
                    _mm_add_ps(_mm_mul_ps(dx4, dx4), _mm_mul_ps(dy4, dy4)),
                    _mm_mul_ps(dz4, dz4));
                const __m128 rsqrtApprox = _mm_rsqrt_ps(lenSq);
                const __m128 halfLenSq = _mm_mul_ps(_mm_set1_ps(0.5f), lenSq);
                const __m128 threeHalf = _mm_set1_ps(1.5f);
                const __m128 invLen4 = _mm_mul_ps(rsqrtApprox,
                    _mm_sub_ps(threeHalf,
                        _mm_mul_ps(halfLenSq, _mm_mul_ps(rsqrtApprox, rsqrtApprox))));
                dx4 = _mm_mul_ps(dx4, invLen4);
                dy4 = _mm_mul_ps(dy4, invLen4);
                dz4 = _mm_mul_ps(dz4, invLen4);

                // Solid angle: texelArea / (r2 * sqrt(r2))
                // where r2 = 1 + su*su + sv*sv.
                // su varies per pixel (= s4), sv is constant for this row.
                // 1/(r2 * sqrt(r2)) = rsqrt(r2)^3
                const __m128 r2 = _mm_add_ps(vOne,
                    _mm_add_ps(_mm_mul_ps(s4, s4), _mm_mul_ps(vSV, vSV)));
                const __m128 rsqrtR2Approx = _mm_rsqrt_ps(r2);
                const __m128 halfR2 = _mm_mul_ps(_mm_set1_ps(0.5f), r2);
                const __m128 invSqrtR2 = _mm_mul_ps(rsqrtR2Approx,
                    _mm_sub_ps(threeHalf,
                        _mm_mul_ps(halfR2, _mm_mul_ps(rsqrtR2Approx, rsqrtR2Approx))));
                const __m128 invSqrtR2_sq = _mm_mul_ps(invSqrtR2, invSqrtR2);
                const __m128 invSqrtR2_cu = _mm_mul_ps(invSqrtR2_sq, invSqrtR2);
                const __m128 solidAngle4 = _mm_mul_ps(vTexArea, invSqrtR2_cu);

                // Load RGBA for 4 texels (interleaved: RGBARGBARGBARGBA)
                const uint32_t pixBase = (y * faceSize + x) * 4;
                const float* p0 = &pixels[pixBase];
                const float* p1 = &pixels[pixBase + 4];
                const float* p2 = &pixels[pixBase + 8];
                const float* p3 = &pixels[pixBase + 12];

                // Deinterleave: gather R, G, B channels from 4 texels
                const __m128 r4 = _mm_set_ps(p3[0], p2[0], p1[0], p0[0]);
                const __m128 g4 = _mm_set_ps(p3[1], p2[1], p1[1], p0[1]);
                const __m128 b4 = _mm_set_ps(p3[2], p2[2], p1[2], p0[2]);

                // Evaluate SH basis for 4 directions simultaneously
                __m128 basis4[9];
                EvaluateSHBasis4(dx4, dy4, dz4, basis4);

                // Accumulate: shR[i] += horizontal_sum(r * basis[i] * solidAngle)
                for (int i = 0; i < 9; ++i) {
                    const __m128 w = _mm_mul_ps(basis4[i], solidAngle4);
                    const __m128 wr = _mm_mul_ps(r4, w);
                    const __m128 wg = _mm_mul_ps(g4, w);
                    const __m128 wb = _mm_mul_ps(b4, w);

                    // Horizontal sum via two rounds of shuffle+add (SSE2 compatible)
                    __m128 tmp;

                    tmp = _mm_add_ps(wr, _mm_shuffle_ps(wr, wr, _MM_SHUFFLE(1, 0, 3, 2)));
                    tmp = _mm_add_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 3, 0, 1)));
                    shR[i] += _mm_cvtss_f32(tmp);

                    tmp = _mm_add_ps(wg, _mm_shuffle_ps(wg, wg, _MM_SHUFFLE(1, 0, 3, 2)));
                    tmp = _mm_add_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 3, 0, 1)));
                    shG[i] += _mm_cvtss_f32(tmp);

                    tmp = _mm_add_ps(wb, _mm_shuffle_ps(wb, wb, _MM_SHUFFLE(1, 0, 3, 2)));
                    tmp = _mm_add_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 3, 0, 1)));
                    shB[i] += _mm_cvtss_f32(tmp);
                }

                // Horizontal sum of solidAngle4 for totalWeight
                __m128 saSum = _mm_add_ps(solidAngle4,
                    _mm_shuffle_ps(solidAngle4, solidAngle4, _MM_SHUFFLE(1, 0, 3, 2)));
                saSum = _mm_add_ps(saSum,
                    _mm_shuffle_ps(saSum, saSum, _MM_SHUFFLE(2, 3, 0, 1)));
                totalWeight += _mm_cvtss_f32(saSum);
            }
#endif // UZ_SIMD_SSE2

            // Scalar remainder: handles the last (faceSize % 4) pixels, or
            // the entire row on platforms without SSE2.
            for (; x < faceSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) * invN;

                float dx, dy, dz;
                CubemapTexelDirection(face, u, v, dx, dy, dz);

                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float invLen = 1.0f / len;
                dx *= invLen;
                dy *= invLen;
                dz *= invLen;

                const float su = 2.0f * u - 1.0f;
                const float solidAngle = texelArea * CubemapTexelSolidAngle(su, sv);

                const uint32_t idx = (y * faceSize + x) * 4;
                const float r = pixels[idx + 0];
                const float g = pixels[idx + 1];
                const float b = pixels[idx + 2];

                float basis[9];
                EvaluateSHBasis(dx, dy, dz, basis);

                for (int i = 0; i < 9; ++i) {
                    const float w = basis[i] * solidAngle;
                    shR[i] += r * w;
                    shG[i] += g * w;
                    shB[i] += b * w;
                }

                totalWeight += solidAngle;
            }
        }
    }

    if (totalWeight > 0.0f) {
        const float normalization = (4.0f * kPI) / totalWeight;
        for (int i = 0; i < 9; ++i) {
            shR[i] *= normalization;
            shG[i] *= normalization;
            shB[i] *= normalization;
        }
    }

    SHCoefficients result;
    PackSHToCompactFormat(shR, shG, shB, result.coeffs);

    UZ_LOG(SHProjection, Info,
           "SH projection complete: faceSize={}, totalWeight={:.4f} (expected {:.4f})",
           faceSize, totalWeight, 4.0f * kPI);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Half-float conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a single IEEE 754 half-precision float (16-bit) to single-precision (32-bit).
///
/// Handles normalized values, denormals, zero, and infinity/NaN.
/// Uses bit manipulation rather than intrinsics for portability across all backends.
static float HalfToFloat(uint16_t h)
{
    const uint32_t sign     = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    const uint32_t exponent = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    const uint32_t mantissa =  static_cast<uint32_t>(h) & 0x03FFu;

    uint32_t result = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            result = sign;
        } else {
            uint32_t m = mantissa;
            uint32_t e = 0;
            while ((m & 0x0400u) == 0) {
                m <<= 1;
                ++e;
            }
            m &= 0x03FFu;
            result = sign | ((127u - 15u + 1u - e) << 23) | (m << 13);
        }
    } else if (exponent == 31) {
        result = sign | 0x7F800000u | (mantissa << 13);
    } else {
        result = sign | ((exponent + 127u - 15u) << 23) | (mantissa << 13);
    }

    float f;
    std::memcpy(&f, &result, sizeof(f));
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU Readback SH Projection
// ─────────────────────────────────────────────────────────────────────────────

SHCoefficients ProjectCubemapToSH(IRHIDevice* device, IRHICommandList* cmdList,
                                  RHITextureHandle cubemap, uint32_t faceSize)
{
    SHCoefficients zeroResult = {};

    if (!device || !cmdList || !cubemap.IsValid() || faceSize == 0) {
        UZ_LOG(SHProjection, Error,
               "ProjectCubemapToSH (GPU): invalid arguments");
        return zeroResult;
    }

    // RGBA16F = 8 bytes per texel
    constexpr uint32_t kBytesPerTexel = 8;
    const uint32_t faceRowBytes = faceSize * kBytesPerTexel;

    // DX12 requires 256-byte aligned row pitch for readback
    constexpr uint32_t kRowPitchAlignment = 256;
    const uint32_t alignedRowPitch = (faceRowBytes + kRowPitchAlignment - 1) & ~(kRowPitchAlignment - 1);

    const uint64_t faceBytes = static_cast<uint64_t>(alignedRowPitch) * faceSize;
    const uint64_t totalBytes = faceBytes * 6;

    // Create readback buffer large enough for all 6 faces
    RHIBufferDesc bufDesc = {};
    bufDesc.sizeBytes = totalBytes;
    bufDesc.usage = RHIBufferUsage::CopyDst;
    bufDesc.memory = RHIMemoryType::Readback;
    bufDesc.debugName = "SH_CubemapReadback";

    RHIBufferHandle readbackBuf = device->CreateBuffer(bufDesc);
    if (!readbackBuf.IsValid()) {
        UZ_LOG(SHProjection, Error,
               "ProjectCubemapToSH (GPU): failed to create readback buffer ({} bytes)", totalBytes);
        return zeroResult;
    }

    // Transition cubemap from UAV to CopySrc and copy each face.
    // The cubemap was last set to UnorderedAccess by the render graph's
    // sky capture compute pass. The UAV→CopySrc transition flushes
    // UAV writes so the copy reads correct data.
    cmdList->Begin();

    RHIBarrierDesc toCopySrc = {};
    toCopySrc.texture = cubemap;
    toCopySrc.stateBefore = RHIResourceState::UnorderedAccess;
    toCopySrc.stateAfter = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&toCopySrc, 1);

    for (uint32_t face = 0; face < 6; ++face) {
        const uint64_t offset = face * faceBytes;
        cmdList->CopyTextureToBuffer(cubemap, 0, readbackBuf, offset, alignedRowPitch, face);
    }

    // Transition cubemap back to UAV for next frame's sky capture.
    RHIBarrierDesc toUAV = {};
    toUAV.texture = cubemap;
    toUAV.stateBefore = RHIResourceState::CopySrc;
    toUAV.stateAfter = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(&toUAV, 1);

    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();

    // Map the readback buffer and convert RGBA16F → float
    const auto* mappedData = static_cast<const uint8_t*>(device->MapBuffer(readbackBuf));
    if (!mappedData) {
        UZ_LOG(SHProjection, Error, "ProjectCubemapToSH (GPU): failed to map readback buffer");
        device->DestroyBuffer(readbackBuf);
        return zeroResult;
    }

    // Convert half-float data to float and build face pointers for the CPU projection
    const uint32_t floatsPerFace = faceSize * faceSize * 4;
    std::vector<float> faceFloats(floatsPerFace * 6);
    const float* facePtrs[6] = {};

    for (uint32_t face = 0; face < 6; ++face) {
        float* dst = faceFloats.data() + face * floatsPerFace;
        facePtrs[face] = dst;

        const auto* faceBase = mappedData + face * faceBytes;
        for (uint32_t y = 0; y < faceSize; ++y) {
            const auto* rowHalf = reinterpret_cast<const uint16_t*>(faceBase + y * alignedRowPitch);
            float* rowFloat = dst + y * faceSize * 4;

            for (uint32_t x = 0; x < faceSize; ++x) {
                rowFloat[x * 4 + 0] = HalfToFloat(rowHalf[x * 4 + 0]);
                rowFloat[x * 4 + 1] = HalfToFloat(rowHalf[x * 4 + 1]);
                rowFloat[x * 4 + 2] = HalfToFloat(rowHalf[x * 4 + 2]);
                rowFloat[x * 4 + 3] = HalfToFloat(rowHalf[x * 4 + 3]);
            }
        }
    }

    device->UnmapBuffer(readbackBuf);
    device->DestroyBuffer(readbackBuf);

    // Run the CPU SH projection on the converted float data
    SHCoefficients result = ProjectCubemapToSH(facePtrs, faceSize);

    UZ_LOG(SHProjection, Info,
           "ProjectCubemapToSH (GPU): readback complete (faceSize={}, "
           "L0_R={:.4f}, L0_G={:.4f}, L0_B={:.4f})",
           faceSize, result.coeffs[0].w, result.coeffs[1].w, result.coeffs[2].w);

    return result;
}

} // namespace uz
