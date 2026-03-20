/// UzEngine SH Projection — Implementation.
///
/// Projects cubemap faces into L2 Spherical Harmonics coefficients using
/// the standard real SH basis, then packs into standard 7-float4 format.
/// Includes a GPU readback path that copies RGBA16F cubemap data to CPU,
/// converts half-float to float, and runs the CPU SH projection.

#include "sh_projection.h"

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
            for (uint32_t x = 0; x < faceSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) * invN;
                const float v = (static_cast<float>(y) + 0.5f) * invN;

                float dx, dy, dz;
                CubemapTexelDirection(face, u, v, dx, dy, dz);

                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float invLen = 1.0f / len;
                dx *= invLen;
                dy *= invLen;
                dz *= invLen;

                const float su = 2.0f * u - 1.0f;
                const float sv = 2.0f * v - 1.0f;
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
