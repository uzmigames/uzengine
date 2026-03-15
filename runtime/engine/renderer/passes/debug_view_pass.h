/// UzEngine Debug View Pass
///
/// Registers a fullscreen pass in the render graph that reads the G-Buffer
/// textures and renders a selected debug channel into the backbuffer.
/// Replaces the lighting pass output when a non-Lit debug view mode is active.
///
/// # Render Graph Integration
/// The pass declares:
/// - Backbuffer as Write (RenderTarget state)
/// - Albedo G-Buffer as Read (ShaderResource state)
/// - Normal G-Buffer as Read (ShaderResource state)
/// - Depth G-Buffer as Read (ShaderResource state)
/// - Position G-Buffer as Read (ShaderResource state)
///
/// Barriers are scheduled automatically by the render graph compiler.
///
/// # Binding Model
/// - Per-frame CB (with nearPlane/farPlane for depth linearization) bound at root param 1.
/// - SM6.6 bindless: mode + G-Buffer SRV indices pushed via root constants at slot 0.
/// - SM5 fallback: G-Buffer textures bound via SetFragmentTexture at t0-t3.

#pragma once

#include "debug_view_mode.h"
#include "constant_buffers.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DebugViewPassDesc — configuration for the debug view pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the debug view visualization pass.
///
/// All fields must be set before calling `AddDebugViewPass`. The pass does
/// not take ownership of any pointed-to objects — the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct DebugViewPassDesc {
    rg::RgResourceId backbuffer;        ///< Final debug output (write, RenderTarget state).
    rg::RgResourceId albedoGBuffer;     ///< Albedo G-Buffer input (read, ShaderResource state).
    rg::RgResourceId normalGBuffer;     ///< Normal G-Buffer input (read, ShaderResource state).
    rg::RgResourceId depthGBuffer;      ///< Depth G-Buffer input (read as SRV, not DSV).
    rg::RgResourceId positionGBuffer;   ///< World-space position G-Buffer input (read, ShaderResource state).

    CBAllocation      perFrameAlloc;    ///< Per-frame CB with nearPlane/farPlane (bound at b1).
    RHIPipelineHandle pipeline;         ///< Debug view PSO (fullscreen triangle, no depth).
    DebugViewMode     mode;             ///< Channel to visualize at runtime.

    // ── SM5 physical texture handles (used when bindless == false) ────────────

    RHITextureHandle  albedoTexture;        ///< Physical albedo texture handle.
    RHITextureHandle  normalTexture;        ///< Physical normal texture handle.
    RHITextureHandle  depthTexture;         ///< Physical depth texture handle (bound as SRV).
    RHITextureHandle  positionTexture;      ///< Physical world-space position texture handle.

    // ── SM6.6 bindless SRV descriptor indices (used when bindless == true) ───

    uint32_t          albedoSrvIndex;   ///< SRV heap index for albedo G-Buffer.
    uint32_t          normalSrvIndex;   ///< SRV heap index for normal G-Buffer.
    uint32_t          depthSrvIndex;    ///< SRV heap index for depth G-Buffer.
    uint32_t          positionSrvIndex; ///< SRV heap index for position G-Buffer.

    bool              bindless;         ///< True if SM6.6 bindless descriptor model is active.
    float             viewportWidth;    ///< Viewport width in pixels.
    float             viewportHeight;   ///< Viewport height in pixels.
};

/// Registers a debug view visualization pass in the render graph.
///
/// The pass reads the G-Buffer textures, selects a diagnostic channel based
/// on `desc.mode`, and draws a fullscreen triangle to write the result to
/// the backbuffer. No depth buffer is attached or written.
///
/// # Arguments
/// * `builder` — Render graph builder to register the pass with.
/// * `desc` — Pass configuration (resources, pipeline, mode, etc.).
///   All referenced render graph resources must have been created or imported
///   in `builder` before this call.
///
/// # Pass Execution
/// During execution the pass:
/// 1. Clears the backbuffer to black.
/// 2. Begins a render pass with one color attachment (no depth).
/// 3. Binds the debug view PSO.
/// 4. Binds per-frame CB at root param 1 (b1) for depth linearization.
/// 5. For SM6.6: pushes mode + G-Buffer SRV indices via root constants at slot 0.
///    For SM5: pushes mode via root constants, then binds textures at t0-t3.
/// 6. Sets the viewport and scissor rect.
/// 7. Issues Draw(3) for the fullscreen triangle.
/// 8. Ends the render pass.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation
/// (before `RgBuilder::Execute()`).
///
/// # Preconditions
/// - `desc.mode` must not be `DebugViewMode::Lit` (caller should skip the pass).
/// - `desc.mode` must be less than `DebugViewMode::Count`.
/// - `desc.pipeline` must be a valid fullscreen PSO.
void AddDebugViewPass(rg::RgBuilder& builder, const DebugViewPassDesc& desc);

} // namespace uz
