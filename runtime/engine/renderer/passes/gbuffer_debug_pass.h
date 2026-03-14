/// UzEngine G-Buffer Debug Visualization Pass
///
/// Registers a fullscreen pass in the render graph that reads the three
/// G-Buffer textures and renders a single diagnostic channel into the
/// backbuffer.  The channel is selected at runtime via `GBufferDebugMode`.
///
/// Intended for editor/debug overlays only — never active in shipping builds.
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
/// - Debug mode uint pushed via root constants at slot 0 (b0 in the shader).
/// - Per-frame CB (with invViewProj) bound at root param 1 (b1 in the shader).
/// - SM6.6 bindless: G-Buffer SRV indices pushed via root constants at slot 0,
///   followed by the debug mode uint at offset 4.
/// - SM5 fallback: G-Buffer textures bound via SetFragmentTexture at t0-t3.

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
// GBufferDebugMode — channel selector enum
// ─────────────────────────────────────────────────────────────────────────────

/// G-Buffer debug visualization mode selector.
///
/// Each enumerant selects one channel or derived quantity to display as an
/// RGB diagnostic image.  The numeric values must stay in sync with the
/// `kMode*` constants in `gbuffer_debug_ps.hlsl`.
///
/// # Enumerants
/// - `Albedo`    — Display base color as stored in the G-Buffer (linear, no
///                 gamma correction applied).
/// - `Normal`    — Display world-space normals remapped from [-1,1] to [0,1]
///                 (R=X, G=Y, B=Z).
/// - `Metallic`  — Display metallic factor as grayscale (0=dielectric, 1=metal).
/// - `Roughness` — Display roughness factor as grayscale (0=mirror, 1=fully
///                 rough).
/// - `AO`        — Display ambient occlusion as grayscale (1=fully lit).
/// - `Depth`     — Display linearized depth as grayscale (white=near, black=far).
/// - `WorldPos`  — Display fractional world-space position as a color grid.
/// - `Velocity`  — Display screen-space motion vectors as color (RG=XY velocity).
/// - `Count`     — Sentinel: number of valid modes (not a displayable mode).
enum class GBufferDebugMode : uint32_t {
    Albedo    = 0, ///< Display base color.
    Normal    = 1, ///< Display world-space normals (remapped to [0,1]).
    Metallic  = 2, ///< Display metallic factor (grayscale).
    Roughness = 3, ///< Display roughness factor (grayscale).
    AO        = 4, ///< Display ambient occlusion (grayscale).
    Depth     = 5, ///< Display linearized depth (white=near, black=far).
    WorldPos  = 6, ///< Display fractional world position as a color grid.
    Velocity  = 7, ///< Display screen-space motion vectors (RG mapped to color).
    Count     = 8, ///< Number of valid debug modes (sentinel, not a displayable mode).
};

// ─────────────────────────────────────────────────────────────────────────────
// GBufferDebugPassDesc — configuration for the G-Buffer debug pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the G-Buffer debug visualization pass.
///
/// All fields must be set before calling `AddGBufferDebugPass`.  The pass
/// does not take ownership of any pointed-to objects — the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// Physical texture handles (`albedoTexture`, `normalTexture`, etc.) are only
/// required when `bindless` is false.  SRV index fields (`albedoSrvIndex`, etc.)
/// are only required when `bindless` is true.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
/// Only active in debug/editor builds; never compiled in shipping.
struct GBufferDebugPassDesc {
    rg::RgResourceId backbuffer;         ///< Final debug output (write, RenderTarget state).
    rg::RgResourceId albedoGBuffer;      ///< Albedo G-Buffer input (read, ShaderResource state).
    rg::RgResourceId normalGBuffer;      ///< Normal G-Buffer input (read, ShaderResource state).
    rg::RgResourceId depthGBuffer;       ///< Depth G-Buffer input (read as SRV, not DSV).
    rg::RgResourceId velocityGBuffer;    ///< Velocity G-Buffer input (read, ShaderResource state).

    CBAllocation      perFrameAlloc;     ///< Per-frame CB with invViewProj filled (bound at b1).
    CBAllocation      debugAlloc;        ///< Debug CB with {debugMode, pad[3]} (bound at b2, bindless only).

    RHIPipelineHandle pipeline;          ///< Debug PSO (fullscreen triangle, no depth write).

    GBufferDebugMode  mode;              ///< Channel to visualize at runtime.

    // ── SM5 physical texture handles (used when bindless == false) ────────────

    RHITextureHandle  albedoTexture;         ///< Physical albedo texture handle.
    RHITextureHandle  normalTexture;         ///< Physical normal texture handle.
    RHITextureHandle  depthTextureForSrv;    ///< Physical depth texture handle (bound as SRV).
    RHITextureHandle  velocityTexture;       ///< Physical velocity texture handle.

    // ── SM6.6 bindless SRV descriptor indices (used when bindless == true) ───

    uint32_t          albedoSrvIndex;    ///< SRV heap index for albedo G-Buffer.
    uint32_t          normalSrvIndex;    ///< SRV heap index for normal G-Buffer.
    uint32_t          depthSrvIndex;     ///< SRV heap index for depth G-Buffer (read as SRV).
    uint32_t          velocitySrvIndex;  ///< SRV heap index for velocity G-Buffer.

    bool              bindless;          ///< True if SM6.6 bindless descriptor model is active.

    float             viewportWidth;     ///< Viewport width in pixels.
    float             viewportHeight;    ///< Viewport height in pixels.
};

// ─────────────────────────────────────────────────────────────────────────────
// AddGBufferDebugPass — render graph registration entry point
// ─────────────────────────────────────────────────────────────────────────────

/// Registers a G-Buffer debug visualization pass in the render graph.
///
/// The pass reads the three G-Buffer textures, selects a diagnostic channel
/// based on `desc.mode`, and draws a fullscreen triangle to write the result
/// to the backbuffer.  No depth buffer is attached or written.
///
/// # Arguments
/// * `builder` — Render graph builder to register the pass with.
/// * `desc`    — Pass configuration (resources, CB allocation, pipeline, mode).
///   All referenced render graph resources must have been created or imported
///   in `builder` before this call.
///
/// # Pass Execution
/// During execution the pass:
/// 1. Begins a render pass with one color attachment (no depth).
/// 2. Binds the debug PSO.
/// 3. Pushes the `mode` value as a root constant at slot 0.
/// 4. Binds the per-frame CB at root param 1 (b1) for invViewProj.
/// 5. For SM6.6: pushes G-Buffer SRV indices (albedo, normal, depth, velocity)
///    + debug mode via root constants.
///    For SM5: binds G-Buffer textures at t0-t3 via SetFragmentTexture.
/// 6. Sets the viewport and scissor rect.
/// 7. Issues Draw(3) for the fullscreen triangle.
/// 8. Ends the render pass.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation (before `RgBuilder::Execute()`).
/// The callback captures state at call time. Debug pass is a no-op if disabled at compile time.
///
/// # Examples
/// ```cpp
/// GBufferDebugPassDesc desc{};
/// desc.backbuffer            = builder.ImportRenderTarget(backbufferTex);
/// desc.albedoGBuffer         = builder.ImportRenderTarget(albedoGBufTex);
/// desc.normalGBuffer         = builder.ImportRenderTarget(normalGBufTex);
/// desc.depthGBuffer          = builder.ImportDepthBuffer(depthGBufTex);
/// desc.perFrameAlloc         = perFrameCBAlloc;
/// desc.debugAlloc            = debugCBAlloc;  // Contains GBufferDebugMode value
/// desc.pipeline              = debugPSO;
/// desc.mode                  = GBufferDebugMode::Normal;  // Show normals
/// desc.albedoTexture         = albedoGBufHandle;
/// desc.normalTexture         = normalGBufHandle;
/// desc.depthTextureForSrv    = depthGBufHandle;
/// desc.albedoSrvIndex        = 100;
/// desc.normalSrvIndex        = 101;
/// desc.depthSrvIndex         = 102;
/// desc.bindless              = true;
/// desc.viewportWidth         = 1920.0f;
/// desc.viewportHeight        = 1080.0f;
/// AddGBufferDebugPass(builder, desc);
/// ```
///
/// # Preconditions
/// - `desc.pipeline` must have been compiled from `gbuffer_debug_ps.hlsl`
///   with `fullscreen_vs.hlsl` as its vertex shader.
/// - `desc.mode` must be less than `GBufferDebugMode::Count`.
void AddGBufferDebugPass(rg::RgBuilder& builder, const GBufferDebugPassDesc& desc);

} // namespace uz
