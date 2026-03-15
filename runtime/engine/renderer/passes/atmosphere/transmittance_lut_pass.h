/// UzEngine Transmittance LUT Pass
///
/// Registers a compute pass in the render graph that precomputes a 256x64
/// transmittance lookup table for the atmosphere. The LUT encodes RGB
/// optical transmittance from any (height, zenith_angle) to the top of
/// the atmosphere, using Bruneton 2017 non-linear UV parameterization.
///
/// This pass replaces the per-pixel secondary ray march toward the sun
/// in the sky atmosphere shader with a single texture sample, cutting
/// atmosphere ALU cost significantly.
///
/// # Render Graph Integration
/// The pass declares:
/// - lutTarget as Write (UnorderedAccess state)
///
/// Barriers are scheduled automatically by the render graph compiler.
/// The sky atmosphere pass should declare a Read on the same resource
/// (ShaderResource state) so the graph inserts a UAV->SRV barrier.
///
/// # Pass Ordering
/// Executes BEFORE the sky atmosphere pass. Should be placed early in
/// the frame (e.g., alongside shadow map passes) since it has no
/// dependencies on scene rendering.

#pragma once

#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"
#include "vector3.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TransmittanceLutRootConstants — 8 DWORDs (32 bytes) at root param 0 (b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the transmittance LUT compute shader.
///
/// Matches the HLSL cbuffer AtmosphereLutParams at b0.
/// Uses root constants (not a CBV) because the global root signature
/// defines slot 0 as 8x 32-bit root constants — not a CBV descriptor.
/// This is the same pattern used by the IBL baker compute shaders.
struct TransmittanceLutRootConstants {
    float rayleighBetaX;     ///< Rayleigh scattering R (km^-1).
    float rayleighBetaY;     ///< Rayleigh scattering G (km^-1).
    float rayleighBetaZ;     ///< Rayleigh scattering B (km^-1).
    float rayleighScaleH;    ///< Rayleigh scale height (km).
    float mieBeta;           ///< Mie scattering coefficient (km^-1).
    float mieScaleH;         ///< Mie scale height (km).
    float bottomRadiusKm;    ///< Planet surface radius (km).
    float topRadiusKm;       ///< Top of atmosphere radius (km).
};

static_assert(sizeof(TransmittanceLutRootConstants) == 32,
    "TransmittanceLutRootConstants must be exactly 8 DWORDs (32 bytes)");

// ─────────────────────────────────────────────────────────────────────────────
// TransmittanceLutPassDesc
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the transmittance LUT compute pass.
///
/// All fields must be set before calling `AddTransmittanceLutPass`. The pass
/// does not take ownership of any pointed-to objects.
struct TransmittanceLutPassDesc {
    rg::RgResourceId  lutTarget;       ///< Output texture (write, UnorderedAccess state). RGBA16F 256x64.
    RHIPipelineHandle pipeline;        ///< Compute PSO for transmittance_lut_cs.hlsl.
    TransmittanceLutRootConstants rootConstants; ///< Atmosphere params (8 DWORDs at root param 0).
    uint32_t          lutUavIndex;     ///< UAV descriptor heap index for the LUT texture.
};

/// Registers a transmittance LUT compute pass in the render graph.
///
/// Dispatches [32, 8, 1] thread groups of [8, 8, 1] threads each,
/// producing a 256x64 RGBA16F transmittance lookup table.
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration.
void AddTransmittanceLutPass(rg::RgBuilder& builder, const TransmittanceLutPassDesc& desc);

} // namespace uz
