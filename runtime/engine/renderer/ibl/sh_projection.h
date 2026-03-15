/// UzEngine SH Projection — L2 Spherical Harmonics for Sky Irradiance.
///
/// Projects a cubemap into 9 SH coefficients (L2 / order 3), then packs
/// them into 7 float4 values matching UE5's SkyIrradianceEnvironmentMap[0..6].
/// The packed format bakes in diffuse convolution (cosine lobe) so the
/// shader only evaluates basis functions — no per-pixel convolution needed.
///
/// # Usage
/// ```cpp
/// SHCoefficients sh = ProjectCubemapToSH(faceData, 128);
/// // Upload sh.coeffs[0..6] to GPU constant buffer
/// // Evaluate in shader with GetSkySHDiffuse(normal, sh.coeffs)
/// ```
///
/// # Reference
/// - UE5 ReflectionEnvironmentCapture.cpp: SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance
/// - UE5 ReflectionEnvironmentShared.ush: GetSkySHDiffuse
/// - Peter-Pike Sloan, "Stupid Spherical Harmonics (SH) Tricks"

#pragma once

#include "math/vector4.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SHCoefficients — Packed L2 SH for GPU upload
// ─────────────────────────────────────────────────────────────────────────────

/// L2 Spherical Harmonics coefficients for sky irradiance, packed for GPU.
///
/// 7 float4 values matching UE5 SkyIrradianceEnvironmentMap[0..6].
/// The diffuse convolution (cosine lobe) coefficients are baked in,
/// so the shader only needs to evaluate basis functions via dot products.
///
/// # Memory Layout
/// ```
/// coeffs[0] = R channel: L1 + L0 terms
/// coeffs[1] = G channel: L1 + L0 terms
/// coeffs[2] = B channel: L1 + L0 terms
/// coeffs[3] = R channel: L2 terms
/// coeffs[4] = G channel: L2 terms
/// coeffs[5] = B channel: L2 terms
/// coeffs[6] = (R, G, B) Y22 terms, w=1
/// ```
/// Total: 112 bytes (7 * 16).
struct SHCoefficients {
    Vec4 coeffs[7];
};

// ─────────────────────────────────────────────────────────────────────────────
// Projection API
// ─────────────────────────────────────────────────────────────────────────────

/// Projects a cubemap into L2 Spherical Harmonics and packs for GPU upload.
///
/// Integrates each cubemap texel against the 9 real SH basis functions (L0, L1, L2),
/// applies diffuse convolution (cosine lobe) scaling, and packs into the
/// 7-float4 format consumed by `GetSkySHDiffuse()` in the shader.
///
/// # Arguments
/// * `faceData` — Array of 6 pointers, each pointing to `faceSize * faceSize * 4`
///   floats (RGBA per pixel). Face order: +X, -X, +Y, -Y, +Z, -Z (DX cubemap convention).
/// * `faceSize` — Width and height of each cubemap face in pixels.
///
/// # Returns
/// `SHCoefficients` ready to upload to a GPU constant buffer.
///
/// # Notes
/// - Input is expected in linear HDR color space (no sRGB gamma).
/// - Alpha channel is ignored.
/// - Solid angle weighting accounts for cubemap texel distortion.
SHCoefficients ProjectCubemapToSH(const float* faceData[6], uint32_t faceSize);

class IRHIDevice;

/// Projects a GPU cubemap texture into L2 SH by reading back the texture data.
///
/// This performs a GPU→CPU readback of the cubemap, then calls the CPU projection.
/// Should only be called when the GPU is idle (after WaitIdle).
///
/// # Arguments
/// * `device` — RHI device for texture readback.
/// * `cubemap` — GPU cubemap texture handle (RGBA16F, 6 faces).
/// * `faceSize` — Width/height per face.
SHCoefficients ProjectCubemapToSH(IRHIDevice* device, RHITextureHandle cubemap, uint32_t faceSize);

} // namespace uz
