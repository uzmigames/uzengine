/// VFX Distortion Resolve Pass -- Fullscreen warp of HDR color by accumulated offsets.
///
/// Registers a fullscreen graphics pass in the render graph that reads the
/// RGBA16F distortion accumulation texture and a pre-distortion copy of the
/// HDR color buffer, warps each pixel's UV by the accumulated offset, and
/// writes the result to the color target.
///
/// # Warp Formula
/// ```
/// float2 offset    = distortion.Sample(uv).rg;
/// float  magnitude = distortion.Sample(uv).a;
/// float2 warpedUV  = saturate(uv + offset * magnitude);
/// output           = preDistortionColor.Sample(warpedUV);
/// ```
///
/// The resolve pass reads from a COPY of the pre-distortion color buffer
/// (not the live color target) to avoid read-write hazards. The caller is
/// responsible for issuing the copy pass before this resolve pass.
///
/// # Render Graph Integration
/// - Writes: `colorTarget` as `RenderTarget`.
/// - Reads: `distortionSource` as `ShaderResource`.
/// - Reads: `preDistortionCopy` as `ShaderResource`.
///
/// The pass is registered after the distortion accumulation pass and before
/// post-processing (tonemapping, SMAA, etc.).
///
/// # Root Signature Slot Layout
/// - Slot 0 (b0): Root constants (2 SRV descriptor indices).
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.

#pragma once

#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DistortionResolveConstants -- root constants for the resolve shader
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the distortion resolve pixel shader.
///
/// Contains bindless SRV descriptor indices for the pre-distortion HDR color
/// texture and the distortion accumulation texture. Passed via root constants
/// (slot 0, b0) for minimal overhead.
///
/// Total size: 16 bytes (4 x uint32).
struct DistortionResolveConstants {
    uint32_t colorTextureSrvIndex      = UINT32_MAX; ///< SRV index for pre-distortion HDR color.
    uint32_t distortionTextureSrvIndex = UINT32_MAX; ///< SRV index for RGBA16F distortion target.
    uint32_t _pad0                     = 0;          ///< Padding to 16-byte alignment.
    uint32_t _pad1                     = 0;          ///< Padding to 16-byte alignment.
};

// ─────────────────────────────────────────────────────────────────────────────
// DistortionResolveDesc -- full pass configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the distortion resolve render graph pass.
///
/// All fields must be set before calling `AddDistortionResolvePass`. The pass
/// does not take ownership of any resources -- the caller must ensure all
/// handles remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct DistortionResolveDesc {
    // -- Pipeline -------------------------------------------------------------

    /// Fullscreen triangle graphics PSO (fullscreen VS + distortion resolve PS).
    /// No blending -- the pass writes the warped color directly.
    RHIPipelineHandle resolvePipeline;

    // -- Render targets -------------------------------------------------------

    /// HDR color buffer to write the warped result into (render graph resource ID).
    rg::RgResourceId colorTarget;

    /// RGBA16F distortion accumulation texture (render graph resource ID).
    /// Read as shader resource.
    rg::RgResourceId distortionSource;

    /// Copy of the HDR color buffer taken before distortion (render graph resource ID).
    /// Read as shader resource. The caller must issue the copy pass before this pass.
    rg::RgResourceId preDistortionCopy;

    // -- Bindless descriptor indices ------------------------------------------

    /// SRV descriptor heap index for the pre-distortion HDR color texture.
    uint32_t colorTextureSrvIndex = UINT32_MAX;

    /// SRV descriptor heap index for the RGBA16F distortion accumulation texture.
    uint32_t distortionTextureSrvIndex = UINT32_MAX;
};

// ─────────────────────────────────────────────────────────────────────────────
// AddDistortionResolvePass
// ─────────────────────────────────────────────────────────────────────────────

/// Registers the distortion resolve pass in the render graph.
///
/// Draws a fullscreen triangle that:
/// 1. Samples the distortion accumulation texture at each pixel's UV.
/// 2. Computes a warped UV: `warpedUV = saturate(uv + offset.rg * offset.a)`.
/// 3. Samples the pre-distortion HDR color at the warped UV.
/// 4. Writes the result to the color target.
///
/// Pixels with zero distortion (offset.a == 0) sample the color at the
/// original UV, producing an identity transform -- no branching needed.
///
/// # Arguments
/// * `builder` -- Render graph builder.
/// * `desc` -- Pass configuration (pipeline, render targets, SRV indices).
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
///
/// # Panics
/// Asserts if `desc.resolvePipeline` is invalid.
void AddDistortionResolvePass(rg::RgBuilder& builder,
                              const DistortionResolveDesc& desc);

} // namespace uz
