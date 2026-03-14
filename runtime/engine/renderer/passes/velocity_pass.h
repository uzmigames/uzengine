/// UzEngine Velocity Buffer Generation Pass
///
/// Registers a fullscreen pass in the render graph that reconstructs world
/// positions from the depth buffer and reprojects them using the previous
/// frame's view-projection matrix to produce screen-space motion vectors.
///
/// The velocity buffer (RG16Float) is consumed by TAA and motion blur passes.
///
/// # Render Graph Integration
/// The pass declares:
/// - Velocity RT as Write (RenderTarget state)
/// - Depth G-Buffer as Read (ShaderResource state)
///
/// Barriers are scheduled automatically by the render graph compiler.
///
/// # Binding Model
/// - Root constants at slot 0 (b0): [depthSrvIndex, screenWidth, screenHeight]
/// - VelocityCB at root param 2 (b2): invViewProj + prevViewProj (128 bytes)
/// - SM5 fallback: depth texture bound via SetFragmentTexture at t0.

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
// VelocityCB — constant buffer for the velocity generation pass
// ─────────────────────────────────────────────────────────────────────────────

/// Constant buffer for the velocity generation pass.
///
/// Contains the current frame's inverse view-projection for reconstructing
/// world positions from depth, and the previous frame's view-projection for
/// reprojecting those positions to compute screen-space motion vectors.
///
/// Padded to 256-byte boundary as required by D3D12 constant buffer alignment.
struct VelocityCB {
    Mat4 invViewProj;      ///< Current frame inverse view-projection matrix.
    Mat4 prevViewProj;     ///< Previous frame view-projection matrix.
    float _pad[32] = {};   ///< Padding to 256-byte alignment (128 + 128 = 256).
};

static_assert(sizeof(VelocityCB) == 256, "VelocityCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// VelocityPassDesc — configuration for the velocity pass
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the velocity buffer generation pass.
///
/// All fields must be set before calling `AddVelocityPass`. The pass does
/// not take ownership of any pointed-to objects -- the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct VelocityPassDesc {
    // ── Render graph resource IDs ─────────────────────────────────────────
    rg::RgResourceId velocityTarget;   ///< Write: velocity RT (RG16Float).
    rg::RgResourceId depthBuffer;      ///< Read: depth buffer (ShaderResource state).

    // ── Physical handles (SM5 fallback) ───────────────────────────────────
    RHITextureHandle velocityTexture;  ///< Physical velocity texture handle.
    RHITextureHandle depthTexture;     ///< Physical depth texture handle (bound as SRV).

    // ── Bindless SRV indices ──────────────────────────────────────────────
    uint32_t depthSrvIndex;            ///< SRV heap index for depth buffer.

    // ── CB allocation ─────────────────────────────────────────────────────
    CBAllocation velocityCBAlloc;      ///< VelocityCB allocation (bound at b2).

    // ── Pipeline ──────────────────────────────────────────────────────────
    RHIPipelineHandle pipeline;        ///< Velocity PSO (fullscreen triangle, no depth).

    // ── Viewport ──────────────────────────────────────────────────────────
    float viewportWidth;               ///< Viewport width in pixels.
    float viewportHeight;              ///< Viewport height in pixels.

    bool bindless;                     ///< True if SM6.6 bindless path is active.
};

// ─────────────────────────────────────────────────────────────────────────────
// AddVelocityPass — render graph registration entry point
// ─────────────────────────────────────────────────────────────────────────────

/// Registers a velocity buffer generation pass in the render graph.
///
/// The pass reads the depth buffer, reconstructs world-space positions using
/// `invViewProj`, reprojects them to the previous frame via `prevViewProj`,
/// and writes screen-space motion vectors (current - previous NDC) to the
/// velocity render target.
///
/// # Arguments
/// * `builder` -- Render graph builder to register the pass with.
/// * `desc`    -- Pass configuration (resources, CB allocation, pipeline).
///   All referenced render graph resources must have been created or imported
///   in `builder` before this call.
///
/// # Pass Execution
/// During execution the pass:
/// 1. Begins a render pass with 1 color attachment (velocity RT), no depth.
/// 2. Binds the velocity PSO.
/// 3. Pushes root constants at slot 0: [depthSrvIndex, screenWidth, screenHeight].
/// 4. Binds VelocityCB at root param 2 (b2).
/// 5. For SM5: binds depth texture at t0 via SetFragmentTexture.
/// 6. Sets the viewport and scissor rect.
/// 7. Issues Draw(3) for the fullscreen triangle.
/// 8. Ends the render pass.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation
/// (before `RgBuilder::Execute()`).
void AddVelocityPass(rg::RgBuilder& builder, const VelocityPassDesc& desc);

} // namespace uz
