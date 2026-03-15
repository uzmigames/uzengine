/// UzEngine Height Fog Pass
///
/// Registers a fullscreen pass in the render graph that composites exponential
/// height fog over the lit scene using alpha blending. Reads depth to
/// reconstruct world position and computes analytical fog integration.
///
/// # Render Graph Integration
/// The pass declares:
/// - Backbuffer as Write (RenderTarget state) — fog is alpha-blended on top
/// - Depth G-Buffer as Read (ShaderResource state) — world position reconstruction
///
/// # Alpha Blending
/// The PSO uses SrcAlpha / InvSrcAlpha blending. The shader outputs
/// float4(fogColor, fogFactor). No need to read the backbuffer as SRV.
///
/// # Pass Ordering
/// Executes AFTER the sky atmosphere pass and BEFORE the overlay pass.

#pragma once

#include "constant_buffers.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogPassDesc
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the height fog compositing pass.
struct HeightFogPassDesc {
    rg::RgResourceId backbuffer;        ///< Scene output (write, RenderTarget — alpha-blended).
    rg::RgResourceId depthGBuffer;      ///< Depth input for world position (read, ShaderResource).

    CBAllocation      perFrameAlloc;    ///< PerFrameCB with invViewProj and cameraPos (bound at b1).
    CBAllocation      fogAlloc;         ///< HeightFogCB (bound at b2).
    RHIPipelineHandle pipeline;         ///< Height fog PSO (fullscreen triangle, alpha blend).

    uint32_t          depthSrvIndex;    ///< SRV heap index for depth (SM6.6 bindless).
    RHITextureHandle  depthTexture;     ///< Physical depth texture handle (SM5 fallback).

    bool              bindless;         ///< True if SM6.6 bindless path is active.
    float             viewportWidth;    ///< Viewport width in pixels.
    float             viewportHeight;   ///< Viewport height in pixels.
};

/// Registers a height fog compositing pass in the render graph.
///
/// Uses alpha blending to composite fog over the backbuffer. The shader
/// outputs float4(fogColor, fogFactor) and the PSO blends it with
/// SrcAlpha / InvSrcAlpha.
///
/// # Arguments
/// * `builder` — Render graph builder.
/// * `desc` — Pass configuration.
void AddHeightFogPass(rg::RgBuilder& builder, const HeightFogPassDesc& desc);

} // namespace uz
