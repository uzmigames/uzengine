/// VFX Trail Render Pass — Camera-facing ribbon trail rendering in the transparent phase.
///
/// Registers a graphics pass in the render graph that draws ribbon trails as
/// camera-facing quad strips (one quad per trail segment). Trails are sorted
/// back-to-front by distance to camera before submission, ensuring correct
/// alpha blending when trails overlap.
///
/// # Rendering Strategy
/// Each trail is drawn as a pre-built vertex/index buffer snapshot produced by
/// `TrailSystem::Update()`. The vertex buffer holds `TrailVertex` records
/// (position + UV + color + width factor) and the index buffer defines pairs
/// of triangles forming the ribbon quads. The pass binds one constant buffer
/// per trail containing camera-relative transform data, then issues one
/// `DrawIndexed` call per trail.
///
/// # Blend Mode
/// The PSO must be configured with alpha blending enabled and depth writes
/// disabled. The trail shader uses pre-multiplied alpha so that trails fade
/// cleanly at their tips without bright halos.
///
/// # Render Graph Integration
/// The pass is scheduled in the transparent phase, AFTER the particle render
/// pass and BEFORE any post-process passes that read the color buffer.
/// It declares:
/// - `colorTarget` as `RHIResourceState::RenderTarget` (write)
/// - `depthTarget` as `RHIResourceState::DepthStencilRead` (read-only)
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.

#pragma once

#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TrailPassConstants — CPU-side mirror of the trail ribbon shader CB
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side mirror of the trail vertex shader's constant buffer (register b1).
///
/// Contains the view-projection matrix for transforming pre-built ribbon
/// vertices from world space to clip space. Must be byte-identical to the
/// HLSL cbuffer layout.
///
/// Total size: 64 bytes (256-byte aligned by CBRingAllocator).
struct TrailPassConstants {
    float viewProjection[16] = {}; ///< 0: Combined view-projection matrix, column-major.
};

// ─────────────────────────────────────────────────────────────────────────────
// TrailPassRenderItem — per-trail data for one DrawIndexed call
// ─────────────────────────────────────────────────────────────────────────────

/// Per-trail rendering data for the trail render pass.
///
/// Each item corresponds to one active trail that has renderable ribbon
/// geometry (at least 2 points). The item carries the GPU buffer SRV indices,
/// draw counts, sort metadata, and the constant buffer allocation for the
/// per-trail shader constants.
///
/// # Ownership
/// Does NOT own any resources. The caller must ensure all handles and buffer
/// pointers remain valid until `rg::RgBuilder::Execute()` completes.
struct TrailPassRenderItem {
    // ── Draw Parameters ───────────────────────────────────────────────────────

    /// Graphics PSO for the trail ribbon shader (VS + PS).
    ///
    /// Typically the same pipeline for all trails, but items may differ when
    /// materials drive PSO selection (e.g., additive vs. alpha-blend trails).
    RHIPipelineHandle pipeline;

    /// SRV descriptor heap index for the ribbon vertex buffer.
    ///
    /// Bound at root slot t0. The vertex shader reads `TrailVertex` records
    /// from this structured buffer to build ribbon positions in clip space.
    uint32_t vertexBufferSrvIndex = UINT32_MAX;

    /// SRV descriptor heap index for the ribbon index buffer.
    ///
    /// Bound at root slot t1. Must be a raw/typed buffer of uint32_t indices.
    uint32_t indexBufferSrvIndex = UINT32_MAX;

    /// Number of vertices in the ribbon mesh. Must be > 0 for this item to
    /// be submitted.
    uint32_t vertexCount = 0;

    /// Number of indices in the ribbon mesh (DrawIndexed count argument).
    uint32_t indexCount = 0;

    /// Pre-filled CB allocation for the per-trail shader constants (b1).
    ///
    /// Contains the view-projection matrix and any per-trail parameters
    /// (e.g., time-based dissolve). Filled by the caller before passing
    /// the item to `AddTrailRenderPass`.
    CBAllocation constantBuffer = {};

    // ── Sorting Metadata ──────────────────────────────────────────────────────

    /// Unsigned distance from the trail centroid to the camera (metres).
    ///
    /// Larger values sort earlier (back-to-front draw order). Computed from
    /// the trail mesh centroid, not the emitter origin.
    float distanceToCamera = 0.0f;

    /// Rendering sort order from the TrailComponent (lower = drawn first).
    ///
    /// Used as a primary sort key: items with lower `sortOrder` are drawn
    /// before items with higher `sortOrder`. `distanceToCamera` is the
    /// secondary key within the same sort order.
    int sortOrder = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// TrailPassDesc — full pass configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the trail render graph pass.
///
/// Populated by the frame setup code before calling `AddTrailRenderPass`.
/// The pass does NOT take ownership of any pointed-to objects — the caller
/// must ensure all handles and allocations remain valid until
/// `rg::RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct TrailPassDesc {
    // ── Render Targets ────────────────────────────────────────────────────────

    /// The HDR color target to draw trails into.
    ///
    /// Declared as `RHIResourceState::RenderTarget` by the pass. Must be a
    /// valid render graph resource ID pointing to the frame's HDR color buffer.
    rg::RgResourceId colorTarget;

    /// The depth buffer for read-only depth testing.
    ///
    /// Declared as `RHIResourceState::DepthStencilRead` by the pass. Depth
    /// writes are disabled — the trail pass never modifies depth.
    rg::RgResourceId depthTarget;

    // ── Per-trail Items ───────────────────────────────────────────────────────

    /// Array of active trails to render this frame.
    ///
    /// The pass will sort these items by `(sortOrder, -distanceToCamera)`
    /// before drawing. Items with `indexCount == 0` are skipped silently.
    std::vector<TrailPassRenderItem> items;
};

// ─────────────────────────────────────────────────────────────────────────────
// AddTrailRenderPass
// ─────────────────────────────────────────────────────────────────────────────

/// Registers the ribbon trail rendering pass in the render graph.
///
/// Sorts `desc.items` back-to-front by `(sortOrder, distanceToCamera)` during
/// pass setup, then for each item with `indexCount > 0`:
/// 1. Binds the trail ribbon graphics pipeline.
/// 2. Sets the per-trail constant buffer (view-projection matrix, b1).
/// 3. Binds the SRV descriptor table for the vertex and index buffers (t0..t1).
/// 4. Issues `DrawIndexed(item.indexCount)` to rasterize the ribbon quads.
///
/// The pass is intended to be registered AFTER `AddParticleRenderPass` and
/// BEFORE any post-process pass that reads the HDR color buffer.
///
/// # Arguments
/// * `builder` -- Render graph builder used to declare the pass.
/// * `desc`    -- Pass configuration (render targets and per-trail items).
///
/// # Side Effects
/// - Logs `VFX/Info` on first execution with submitted trail and draw counts.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
///
/// # Preconditions
/// - At least one item in `desc.items` must have a valid `pipeline` handle.
///   (If all items are empty the pass is still registered but does no work.)
void AddTrailRenderPass(rg::RgBuilder& builder, const TrailPassDesc& desc);

} // namespace uz
