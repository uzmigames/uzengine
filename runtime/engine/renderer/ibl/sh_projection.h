/// UzEngine SH Projection — L2 Spherical Harmonics for Sky Irradiance.
///
/// Projects a cubemap into 9 SH coefficients (L2 / order 3), then packs
/// them into 7 float4 values matching the reference engine's SkyIrradianceEnvironmentMap[0..6].
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
/// - @ref Sloan 2008 "Stupid Spherical Harmonics (SH) Tricks" — L2 SH projection and packing
/// - @ref L2 SH sky irradiance environment map packing technique — cosine-lobe baked coefficients

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
/// 7 float4 values matching reference SkyIrradianceEnvironmentMap[0..6].
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
class IRHICommandList;

/// Projects a GPU cubemap texture into L2 SH by reading back the texture data.
///
/// Creates a readback buffer, copies each cubemap face from GPU to CPU via
/// `CopyTextureToBuffer`, converts RGBA16F half-float data to float, then
/// calls the CPU SH projection.
///
/// The GPU must be idle before calling this (after `WaitIdle()`). The command
/// list must NOT be in recording state — this function calls Begin/End/Execute
/// internally.
///
/// # Arguments
/// * `device` — RHI device for buffer creation and mapping.
/// * `cmdList` — Command list for recording copy commands (must be idle).
/// * `cubemap` — GPU cubemap texture handle (RGBA16F, 6 faces).
/// * `faceSize` — Width/height per face.
///
/// # Returns
/// `SHCoefficients` ready for GPU upload, or zero-initialized coefficients on failure.
SHCoefficients ProjectCubemapToSH(IRHIDevice* device, IRHICommandList* cmdList,
                                  RHITextureHandle cubemap, uint32_t faceSize);

} // namespace uz
