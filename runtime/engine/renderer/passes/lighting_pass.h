/// UzEngine Deferred Lighting Pass
///
/// Registers a fullscreen lighting pass in the render graph that reads the
/// G-Buffer textures (albedo+AO, packed normal+metallic+roughness, depth) and
/// composites the final lit result into the backbuffer using a fullscreen
/// triangle (3 vertices from SV_VertexID, no vertex/index buffer).
///
/// # Render Graph Integration
/// The pass declares:
/// - Backbuffer as Write (RenderTarget state)
/// - Albedo G-Buffer as Read (ShaderResource state)
/// - Normal G-Buffer as Read (ShaderResource state)
/// - Depth G-Buffer as Read (ShaderResource state)
///
/// Barriers are scheduled automatically by the render graph compiler.
///
/// # Binding Model
/// - Per-frame CB (with invViewProj) bound at root param 1.
/// - Lighting CB (point light array) bound at root param 3.
/// - SM6.6 bindless: G-Buffer SRV indices pushed via root constants at slot 0.
/// - SM5 fallback: G-Buffer textures bound via SetFragmentTexture at t0-t2.

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
// LightingPassDesc — configuration for the deferred lighting pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the deferred lighting pass.
///
/// All fields must be set before calling `AddLightingPass`. The pass does
/// not take ownership of any pointed-to objects — the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct LightingPassDesc {
    rg::RgResourceId backbuffer;        ///< Final lit output (write, RenderTarget state).
    rg::RgResourceId albedoGBuffer;     ///< Albedo G-Buffer input (read, ShaderResource state).
    rg::RgResourceId normalGBuffer;     ///< Normal G-Buffer input (read, ShaderResource state).
    rg::RgResourceId depthGBuffer;      ///< Depth G-Buffer input (read as SRV, not DSV).
    CBAllocation      perFrameAlloc;    ///< Per-frame CB with invViewProj filled (bound at b1).
    CBAllocation      lightingAlloc;    ///< LightingCB with point light data (bound at b2).
    RHIPipelineHandle pipeline;         ///< Lighting PSO (fullscreen triangle, no depth).
    uint32_t          albedoSrvIndex;   ///< SRV descriptor index for albedo G-Buffer (SM6.6 bindless).
    uint32_t          normalSrvIndex;   ///< SRV descriptor index for normal G-Buffer (SM6.6 bindless).
    uint32_t          depthSrvIndex;    ///< SRV descriptor index for depth G-Buffer (SM6.6 bindless).
    RHITextureHandle  albedoTexture;    ///< Physical albedo texture handle (SM5 fallback binding).
    RHITextureHandle  normalTexture;    ///< Physical normal texture handle (SM5 fallback binding).
    RHITextureHandle  depthTextureForSrv;///< Physical depth texture handle (SM5 fallback binding, read as SRV).
    CBAllocation      shadowAlloc;      ///< ShadowDataCB allocation (bound at b4 via root param 3). Zero-init if no shadows.
    bool              bindless;         ///< True if SM6.6 bindless path is active.
    float             viewportWidth;    ///< Viewport width in pixels.
    float             viewportHeight;   ///< Viewport height in pixels.
};

/// Registers a deferred lighting pass in the render graph.
///
/// The pass reads the G-Buffer textures, binds per-frame and lighting
/// constant buffers, and draws a fullscreen triangle to composite the
/// final lit result into the backbuffer.
///
/// # Arguments
/// * `builder` — Render graph builder to register the pass with.
/// * `desc` — Pass configuration (resources, CB allocations, pipeline, etc.).
///   All referenced resources must have been created or imported in `builder`
///   before calling this function.
///
/// # Pass Execution
/// During execution the pass:
/// 1. Clears the backbuffer.
/// 2. Begins a render pass with one color attachment (no depth).
/// 3. Binds the pipeline, per-frame CB at b1, lighting CB at b3.
/// 4. For SM6.6: pushes G-Buffer SRV indices via root constants.
///    For SM5: binds G-Buffer textures at t0-t2 via SetFragmentTexture.
/// 5. Issues Draw(3) for the fullscreen triangle.
/// 6. Ends the render pass.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation (before `RgBuilder::Execute()`).
/// The callback captures state at call time and does not access `builder` after registration.
///
/// # Examples
/// ```cpp
/// LightingPassDesc desc{};
/// desc.backbuffer            = builder.ImportRenderTarget(backbufferTex);
/// desc.albedoGBuffer         = builder.ImportRenderTarget(albedoGBufTex);
/// desc.normalGBuffer         = builder.ImportRenderTarget(normalGBufTex);
/// desc.depthGBuffer          = builder.ImportDepthBuffer(depthGBufTex);
/// desc.perFrameAlloc         = perFrameCBAlloc;
/// desc.lightingAlloc         = lightingCBAlloc;
/// desc.pipeline              = lightingPSO;
/// desc.albedoSrvIndex        = 100;
/// desc.normalSrvIndex        = 101;
/// desc.depthSrvIndex         = 102;
/// desc.albedoTexture         = albedoGBufHandle;
/// desc.normalTexture         = normalGBufHandle;
/// desc.depthTextureForSrv    = depthGBufHandle;
/// desc.shadowAlloc           = shadowCBAlloc;
/// desc.bindless              = true;
/// desc.viewportWidth         = 1920.0f;
/// desc.viewportHeight        = 1080.0f;
/// AddLightingPass(builder, desc);
/// ```
void AddLightingPass(rg::RgBuilder& builder, const LightingPassDesc& desc);

} // namespace uz
