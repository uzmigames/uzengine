/// UzEngine SH Projection — Implementation.
///
/// Projects cubemap faces into L2 Spherical Harmonics coefficients using
/// the standard real SH basis, then packs into UE5-compatible 7-float4 format.
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
/// These are the standard constants used by UE5 (SHMath.cpp InitSHTables):
/// K_lm = sqrt((2l+1)/(4pi) * (l-|m|)!/(l+|m|)!) * (m!=0 ? sqrt(2) : 1)
///
/// Precomputed for L0..L2:
/// V[0] = Y00:  1/(2*sqrt(pi))                    = 0.282095
/// V[1] = Y1-1: sqrt(3/(4*pi)) * sqrt(2) * sin(phi) * (-sqrt(1-z^2))
///             → but the combined constant = sqrt(3/(4*pi)) = 0.488603
///               (the sqrt(2) cancels with the 1/sqrt(2) from the complex->real conversion)
/// ... and so on. The point is: UE5 uses the STANDARD real SH basis.

// ─────────────────────────────────────────────────────────────────────────────
// SH Basis Evaluation (order 3 / L2, 9 coefficients)
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates all 9 real SH basis functions at the given unit direction.
///
/// Uses the standard real spherical harmonics (same as UE5 SHBasisFunction).
/// Index ordering follows UE5: V[L*(L+1)+M] for L=0..2, M=-L..+L.
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
    // Y2,-2 = sqrt(15/(4*pi)) * x*y / sqrt(2) ... actually:
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
    // The solid angle of a cube face texel centered at (u,v) on the unit cube
    // is: dA / r^3 where dA is the texel area on the cube face and r is the
    // distance from center to the texel direction.
    //
    // For a texel at (u, v) in [-1, 1]:
    // r^2 = 1 + u^2 + v^2  (the face is at distance 1 from center)
    // dA = (2/N)^2 on the cube face
    // solid angle = dA / r^3 = 4/(N^2 * r^3)
    //
    // But we compute this per-texel with the area factored out in the caller,
    // so we return just 1/r^3 here. The caller multiplies by 4/N^2.
    const float r2 = 1.0f + u * u + v * v;
    const float r = std::sqrt(r2);
    return 1.0f / (r2 * r);
}

// ─────────────────────────────────────────────────────────────────────────────
// Packing into UE5 Format
// ─────────────────────────────────────────────────────────────────────────────

/// Packs raw L2 SH coefficients (9 per channel) into UE5's 7-float4 format.
///
/// The packing bakes in diffuse convolution (cosine lobe) scaling so that
/// the shader only needs dot products with the normal to reconstruct irradiance.
///
/// Matches UE5 SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance()
/// exactly (ReflectionEnvironment.cpp).
///
/// # Arguments
/// * `shR`, `shG`, `shB` — Raw SH coefficients per color channel (9 each).
/// * `out` — Output 7 float4 values.
static void PackSHToUE5Format(const float shR[9], const float shG[9],
                              const float shB[9], Vec4 out[7])
{
    // These coefficients match UE5 exactly (ReflectionEnvironment.cpp:625-630).
    // They incorporate the SH normalization, diffuse convolution (Al), and
    // the 1/pi factor needed for irradiance-to-exitance conversion.
    //
    // From "Stupid Spherical Harmonics Tricks" (Sloan):
    // The cosine-lobe convolution in SH is a per-band multiplication:
    //   A0 = pi,  A1 = 2*pi/3,  A2 = pi/4
    //
    // The UE5 packing factors combine these with SH normalization constants
    // to produce coefficients that, when dotted with the normal vector products,
    // give the correct irradiance directly.
    const float sqrtPI = std::sqrt(kPI);

    const float c0 = 1.0f / (2.0f * sqrtPI);                 // 0.282095
    const float c1 = std::sqrt(3.0f) / (3.0f * sqrtPI);      // 0.325735
    const float c2 = std::sqrt(15.0f) / (8.0f * sqrtPI);     // 0.273137
    const float c3 = std::sqrt(5.0f) / (16.0f * sqrtPI);     // 0.078848
    const float c4 = 0.5f * c2;                               // 0.136569

    // coeffs[0..2]: L0 + L1 terms per color channel (R, G, B).
    // The shader evaluates: dot(coeffs[i], float4(nx, ny, nz, 1))
    //
    // Mapping to SH basis (UE5 index ordering V[L*(L+1)+M]):
    //   .x = -c1 * V[3]   (Y11 ~ x, negated for shader convention)
    //   .y = -c1 * V[1]   (Y1,-1 ~ y, negated)
    //   .z =  c1 * V[2]   (Y10 ~ z)
    //   .w =  c0 * V[0] - c3 * V[6]  (Y00 constant, minus Y20 correction)

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

    // coeffs[3..5]: L2 terms per color channel (R, G, B).
    // The shader evaluates: dot(coeffs[i], float4(x*y, y*z, z*z, x*z))
    //
    // Mapping:
    //   .x =  c2 * V[4]       (Y2,-2 ~ x*y)
    //   .y = -c2 * V[5]       (Y2,-1 ~ y*z, negated)
    //   .z = 3*c3 * V[6]      (Y2,0 ~ 3z^2 - 1, the z^2 part)
    //   .w = -c2 * V[7]       (Y2,1 ~ x*z, negated)

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

    // coeffs[6]: Y2,2 term (x^2 - y^2), one float per channel.
    // The shader evaluates: coeffs[6].xyz * (nx*nx - ny*ny)
    out[6].x = c4 * shR[8];
    out[6].y = c4 * shG[8];
    out[6].z = c4 * shB[8];
    out[6].w = 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

SHCoefficients ProjectCubemapToSH(const float* faceData[6], uint32_t faceSize)
{
    // Raw SH coefficients per color channel (9 each, L0..L2).
    float shR[9] = {};
    float shG[9] = {};
    float shB[9] = {};

    float totalWeight = 0.0f;

    // Texel size on the cube face in [-1, 1] space.
    const float invN = 1.0f / static_cast<float>(faceSize);
    // Area of one texel on the cube face: (2/N)^2 = 4/N^2.
    const float texelArea = 4.0f * invN * invN;

    for (uint32_t face = 0; face < 6; ++face) {
        const float* pixels = faceData[face];
        if (!pixels) {
            UZ_LOG(SHProjection, Warning, "Face {} is null, skipping", face);
            continue;
        }

        for (uint32_t y = 0; y < faceSize; ++y) {
            for (uint32_t x = 0; x < faceSize; ++x) {
                // Texel center in [0, 1]
                const float u = (static_cast<float>(x) + 0.5f) * invN;
                const float v = (static_cast<float>(y) + 0.5f) * invN;

                // Direction (unnormalized) from cube center to texel
                float dx, dy, dz;
                CubemapTexelDirection(face, u, v, dx, dy, dz);

                // Normalize direction
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float invLen = 1.0f / len;
                dx *= invLen;
                dy *= invLen;
                dz *= invLen;

                // Solid angle for this texel.
                // Map u,v from [0,1] to [-1,1] for the solid angle computation.
                const float su = 2.0f * u - 1.0f;
                const float sv = 2.0f * v - 1.0f;
                const float solidAngle = texelArea * CubemapTexelSolidAngle(su, sv);

                // Read texel color (RGBA, 4 floats per pixel)
                const uint32_t idx = (y * faceSize + x) * 4;
                const float r = pixels[idx + 0];
                const float g = pixels[idx + 1];
                const float b = pixels[idx + 2];
                // Alpha (idx+3) ignored

                // Evaluate SH basis at this direction
                float basis[9];
                EvaluateSHBasis(dx, dy, dz, basis);

                // Accumulate weighted SH projection:
                // coeff[i] += color * Y_i(dir) * solidAngle
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

    // The total solid angle of a sphere is 4*pi.
    // Due to cubemap texel discretization, the sum won't be exactly 4*pi.
    // Normalize to correct for this numerical error.
    if (totalWeight > 0.0f) {
        const float normalization = (4.0f * kPI) / totalWeight;
        for (int i = 0; i < 9; ++i) {
            shR[i] *= normalization;
            shG[i] *= normalization;
            shB[i] *= normalization;
        }
    }

    // Pack into UE5-compatible 7-float4 format
    SHCoefficients result;
    PackSHToUE5Format(shR, shG, shB, result.coeffs);

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
            // Zero (positive or negative)
            result = sign;
        } else {
            // Denormalized half → normalized float
            // Count leading zeros in mantissa to find the implicit 1 position
            uint32_t m = mantissa;
            uint32_t e = 0;
            while ((m & 0x0400u) == 0) {
                m <<= 1;
                ++e;
            }
            m &= 0x03FFu; // Remove the implicit leading 1
            result = sign | ((127u - 15u + 1u - e) << 23) | (m << 13);
        }
    } else if (exponent == 31) {
        // Inf or NaN — preserve mantissa bits for NaN distinction
        result = sign | 0x7F800000u | (mantissa << 13);
    } else {
        // Normalized value: rebias exponent from half (bias 15) to float (bias 127)
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
               "ProjectCubemapToSH (GPU): invalid arguments (device={}, cmdList={}, cubemap={}, faceSize={})",
               device != nullptr, cmdList != nullptr, cubemap.IsValid(), faceSize);
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
               "ProjectCubemapToSH (GPU): failed to create readback buffer ({}  bytes)", totalBytes);
        return zeroResult;
    }

    // Transition cubemap from UAV to CopySrc. The cubemap was last explicitly
    // set to UnorderedAccess by the render graph's sky capture pass. BakeEnvironment
    // reads it via SRV implicit promotion but does not change the tracked state.
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

    // Transition cubemap back to UAV — the render graph imports it as
    // UnorderedAccess for the next frame's sky capture compute pass.
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
    const uint32_t floatsPerFace = faceSize * faceSize * 4; // RGBA
    std::vector<float> faceFloats(floatsPerFace * 6);
    const float* facePtrs[6] = {};

    for (uint32_t face = 0; face < 6; ++face) {
        float* dst = faceFloats.data() + face * floatsPerFace;
        facePtrs[face] = dst;

        const auto* faceBase = mappedData + face * faceBytes;
        for (uint32_t y = 0; y < faceSize; ++y) {
            // Row data starts at aligned row pitch offset
            const auto* rowHalf = reinterpret_cast<const uint16_t*>(faceBase + y * alignedRowPitch);
            float* rowFloat = dst + y * faceSize * 4;

            for (uint32_t x = 0; x < faceSize; ++x) {
                rowFloat[x * 4 + 0] = HalfToFloat(rowHalf[x * 4 + 0]); // R
                rowFloat[x * 4 + 1] = HalfToFloat(rowHalf[x * 4 + 1]); // G
                rowFloat[x * 4 + 2] = HalfToFloat(rowHalf[x * 4 + 2]); // B
                rowFloat[x * 4 + 3] = HalfToFloat(rowHalf[x * 4 + 3]); // A (ignored by SH)
            }
        }
    }

    // Diagnostic: log a few sample pixels to verify readback data is non-zero
    {
        float maxVal = 0.0f;
        uint32_t nonZeroCount = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            const float* fp = facePtrs[face];
            for (uint32_t i = 0; i < floatsPerFace; i += 4) {
                const float r = fp[i], g = fp[i + 1], b = fp[i + 2];
                if (r > 0.0f || g > 0.0f || b > 0.0f) ++nonZeroCount;
                if (r > maxVal) maxVal = r;
                if (g > maxVal) maxVal = g;
                if (b > maxVal) maxVal = b;
            }
        }
        // Also log raw half-float bytes from the first face center pixel
        const auto* firstFaceCenter = mappedData + (faceSize / 2) * alignedRowPitch
                                    + (faceSize / 2) * kBytesPerTexel;
        const auto* halfPixel = reinterpret_cast<const uint16_t*>(firstFaceCenter);
        UZ_LOG(SHProjection, Info,
               "Readback diagnostic: nonZero={}/{}, maxVal={:.6f}, "
               "face0_center_half=[0x{:04X}, 0x{:04X}, 0x{:04X}, 0x{:04X}]",
               nonZeroCount, faceSize * faceSize * 6, maxVal,
               halfPixel[0], halfPixel[1], halfPixel[2], halfPixel[3]);
    }

    device->UnmapBuffer(readbackBuf);
    device->DestroyBuffer(readbackBuf);

    // Run the CPU SH projection on the converted float data
    SHCoefficients result = ProjectCubemapToSH(facePtrs, faceSize);

    UZ_LOG(SHProjection, Info,
           "ProjectCubemapToSH (GPU): readback + projection complete (faceSize={}, "
           "L0_R={:.4f}, L0_G={:.4f}, L0_B={:.4f})",
           faceSize, result.coeffs[0].w, result.coeffs[1].w, result.coeffs[2].w);

    return result;
}

} // namespace uz
