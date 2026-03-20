/// UzEngine Aerial Perspective Volume Pass
///
/// Registers a compute pass in the render graph that precomputes a 32x32x16
/// RGBA16F 3D texture containing per-voxel inscattering (RGB) and mean
/// transmittance (A) for the camera's aerial perspective.
///
/// @ref RenderCameraAerialPerspectiveVolumeCS
/// @ref GetAerialPerspectiveLuminanceTransmittance
///
/// # Render Graph Integration
/// The pass declares:
/// - volumeTarget as Write (UnorderedAccess state)
/// - transmittanceLutDep as Read (ShaderResource state)
///
/// # Dispatch
/// [numthreads(8,8,1)], dispatch (4, 4, 16) = 32x32x16 total threads.

#pragma once

#include "render_graph_types.h"
#include "cb_ring_allocator.h"
#include "engine/rhi/rhi_types.h"
#include "mat4.h"
#include "vector3.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// APVolumeRootConstants — 8 DWORDs (32 bytes) at root param 0 (b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the AP volume compute shader.
/// Matches the HLSL cbuffer AtmosphereParams at b0.
struct APVolumeRootConstants {
    float rayleighBetaX;     ///< Rayleigh scattering R (km^-1).
    float rayleighBetaY;     ///< Rayleigh scattering G (km^-1).
    float rayleighBetaZ;     ///< Rayleigh scattering B (km^-1).
    float rayleighScaleH;    ///< Rayleigh scale height (km).
    float mieBeta;           ///< Mie scattering coefficient (km^-1).
    float mieScaleH;         ///< Mie scale height (km).
    float bottomRadiusKm;    ///< Planet surface radius (km).
    float topRadiusKm;       ///< Top of atmosphere radius (km).
};

static_assert(sizeof(APVolumeRootConstants) == 32,
    "APVolumeRootConstants must be exactly 8 DWORDs (32 bytes)");

// ─────────────────────────────────────────────────────────────────────────────
// APVolumeCB — CBV at b2 (256-byte aligned)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-dispatch parameters for the AP volume compute shader.
/// Matches the HLSL cbuffer APVolumeCB at b2.
struct APVolumeCB {
    Mat4     invViewProj;             ///< Inverse view-projection (64 bytes).
    float    cameraPosX;              ///< Camera world position X (cm).
    float    cameraPosY;              ///< Camera world position Y (cm).
    float    cameraPosZ;              ///< Camera world position Z (cm).
    float    _camPadW;                ///< Pad to float4 (matches float4 cameraPosWorld).
    float    sunDirX;                 ///< Sun direction X (normalised, toward sun).
    float    sunDirY;                 ///< Sun direction Y.
    float    sunDirZ;                 ///< Sun direction Z.
    float    sunIntensity;            ///< Sun illuminance scalar.
    float    mieAnisotropy;           ///< HG anisotropy (0.8).
    float    rayleighScale;           ///< Artistic Rayleigh multiplier.
    float    mieScale;                ///< Artistic Mie multiplier.
    uint32_t transmittanceLutIdx;     ///< SRV index for transmittance LUT.
    uint32_t outputUavIdx;            ///< UAV index for output 3D texture.
    uint32_t depthSlices;             ///< Number of depth slices (16).
    float    totalDepthKm;            ///< Total depth range (100.0 km).
    float    _pad0;                   ///< Pad to 16-byte boundary.
    float    _pad1[32] = {};          ///< Pad to 256 bytes total.
};

static_assert(sizeof(APVolumeCB) == 256,
    "APVolumeCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// APVolumePassDesc
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the AP volume compute pass.
struct APVolumePassDesc {
    rg::RgResourceId  volumeTarget;          ///< Output 3D texture (write, UnorderedAccess).
    rg::RgResourceId  transmittanceLutDep;   ///< Transmittance LUT dependency (read, ShaderResource).
    RHIPipelineHandle pipeline;              ///< Compute PSO for aerial_perspective_volume_cs.hlsl.
    APVolumeRootConstants rootConstants;     ///< Atmosphere params (8 DWORDs at root param 0).
    CBAllocation      cbAlloc;               ///< APVolumeCB at root param 2 (b2).
    uint32_t          depthSlices = 16;      ///< Number of depth slices.
};

/// Registers the AP volume compute pass in the render graph.
///
/// Dispatches [4, 4, 16] thread groups of [8, 8, 1] threads each,
/// producing a 32x32x16 RGBA16F aerial perspective volume.
void AddAerialPerspectiveVolumePass(rg::RgBuilder& builder, const APVolumePassDesc& desc);

} // namespace uz
