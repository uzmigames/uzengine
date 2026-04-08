/// UzEngine Path Tracer Pass
///
/// Runtime mode toggle (`r.PathTracer.Enable`) that replaces the deferred
/// lighting / GI / shadow chain with a Monte Carlo path tracer. The pass
/// writes the lit result directly to `hdrSceneColor` so all downstream
/// post-process (bloom, exposure, tonemap, AA) keeps working unchanged.
///
/// # Step 1 (current)
/// Solid magenta fullscreen — verifies pass wiring.
///
/// # Future Steps (see docs/analysis/path-tracing/04-incremental-plan.md)
/// 2. GBuffer first hit visualization
/// 3. One-bounce diffuse + accumulation
/// 4. Multi-bounce + Russian roulette + direct sun
/// 5. GGX VNDF specular via material/brdf.hlsli
/// 6. Camera-cut accumulation reset
/// 7. Analytic primitives from PtPrimitiveComponent
/// 8. Falcor PSNR parity (CLAUDE.md RULE 0)
///
/// @ref Kajiya 1986 "The Rendering Equation"

#pragma once

#include "constant_buffers.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

/// Configuration for the path tracer pass.
struct PathTracerPassDesc {
    rg::RgResourceId  hdrTarget;        ///< HDR scene color RT[1] (write, RenderTarget).
    rg::RgResourceId  accumCurr;        ///< RGBA32F accumulation curr write target RT[0].
    rg::RgResourceId  normalGBuffer;    ///< GBuffer normal (read).
    rg::RgResourceId  depthGBuffer;     ///< GBuffer depth  (read).
    rg::RgResourceId  albedoGBuffer;    ///< GBuffer albedo+AO (read).
    rg::RgResourceId  positionGBuffer;  ///< GBuffer worldPos (read).
    rg::RgResourceId  emissiveGBuffer;  ///< GBuffer emissive HDR (read).
    CBAllocation      perFrameAlloc;    ///< PerFrame CB at b1.
    RHIPipelineHandle pipeline;         ///< Path tracer fullscreen PSO (2 RTs).
    uint32_t          normalSrvIndex;
    uint32_t          depthSrvIndex;
    uint32_t          albedoSrvIndex;
    uint32_t          positionSrvIndex;
    uint32_t          emissiveSrvIndex;   ///< GBuffer emissive HDR SRV index.
    uint32_t          skyCubeSrvIndex;
    uint32_t          accumPrevSrvIndex;  ///< SRV of the OTHER ping-pong accum (history read).
    uint32_t          sampleCount;        ///< Accumulated sample count this frame (0=fresh).
    uint32_t          frameIndex;
    CBAllocation      shadowAlloc;        ///< ShadowDataCB allocation (b4) — for CSM sun shadows.
    bool              bindless;
    float             viewportWidth;
    float             viewportHeight;
};

/// Registers the path tracer pass in the render graph.
///
/// The pass clears the HDR scene color and runs a fullscreen pixel shader
/// that writes the path-traced result. Called from the lighting registration
/// path when `r.PathTracer.Enable=1` (which also skips deferred lighting,
/// GI, and shadow passes).
void AddPathTracerPass(rg::RgBuilder& builder, const PathTracerPassDesc& desc);

} // namespace uz
