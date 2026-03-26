/// VFX Particle Render Pass -- Render graph graphics pass for billboard particle drawing.
///
/// Registers a graphics pass in the render graph that draws all active particle
/// emitters as camera-facing billboards in the transparent rendering phase
/// (after opaque geometry and lighting).
///
/// # Rendering Strategy
/// Each emitter is drawn with a single `DrawIndexedIndirect` call using the
/// indirect argument buffer filled by the simulation pass's indirect args CS.
/// Emitters are sorted back-to-front by distance to the camera to ensure
/// correct alpha blending order.
///
/// # Soft Particles
/// The pass reads the scene depth texture as an SRV to implement soft particle
/// fading near opaque surfaces (depth-based alpha modulation).
///
/// # Render Graph Integration
/// The pass imports the scene depth texture and declares it as a shader resource
/// read. Particle attribute buffers and indirect args buffers are external
/// persistent resources with no explicit graph tracking -- they are expected
/// to already be in `ShaderResource` / `IndirectArgument` state from the
/// preceding simulation pass.
///
/// # Pipeline Root Signature Layout
/// Matches the global engine root signature:
/// - Slot 0 (b0): Root CBV for VFXBillboardIndices (bindless SRV descriptor indices).
/// - Slot 1 (b1): Root CBV for per-emitter billboard constants.
/// - Slot 4 (u0..u3): UAV descriptor table (unused by this pass).
/// - Slot 5 (t0..tN): SRV descriptor table for particle attributes + sprite texture.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.

#pragma once

#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace uz {

struct GPUParticleBuffer;

} // namespace uz

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ---------------------------------------------------------------------------
// ParticleBillboardConstants -- CPU-side mirror of billboard shader CB
// ---------------------------------------------------------------------------

/// CPU-side mirror of the billboard vertex shader's constant buffer (register b1).
///
/// Contains the matrices needed to orient billboards toward the camera and
/// project them to clip space, plus the viewport dimensions for soft particle
/// depth comparison.
///
/// Total size: 144 bytes (256-byte aligned by CBRingAllocator).
struct ParticleBillboardConstants {
    float viewMatrix[16]       = {};  ///< 0:   View matrix (world -> view), column-major.
    float projMatrix[16]       = {};  ///< 64:  Projection matrix (view -> clip), column-major.
    float viewportWidth        = 0.0f; ///< 128: Viewport width in pixels (for soft particles).
    float viewportHeight       = 0.0f; ///< 132: Viewport height in pixels.
    float softParticleFadeRange = 1.0f; ///< 136: Depth range for soft particle fade (world units).
    float _pad0                = 0.0f; ///< 140: Padding to 144 bytes (16-byte aligned).
};

// ---------------------------------------------------------------------------
// EmitterRenderData -- per-emitter data for the render pass
// ---------------------------------------------------------------------------

/// Per-emitter rendering data for the particle render pass.
///
/// Contains the GPU buffer references, constant buffer allocation, sprite
/// texture, and sorting distance needed to draw one emitter's particles
/// as camera-facing billboards.
///
/// # Ownership
/// Does NOT own any pointed-to resources. The caller must ensure all handles
/// and pointers remain valid until `RgBuilder::Execute()` completes.
struct EmitterRenderData {
    /// The emitter's GPU particle storage (read set is used for SRV binding).
    GPUParticleBuffer* buffer = nullptr;

    /// Pre-filled CB allocation for the billboard shader constants.
    CBAllocation renderCB = {};

    /// CB allocation for the VFXBillboardIndices root constants (b0).
    /// Contains 4 SRV descriptor indices for the read-set attribute buffers.
    /// Only used when bindless rendering is active.
    CBAllocation indicesCB = {};

    /// Sprite texture for this emitter's particles.
    /// Bound as an SRV in the pixel shader.
    RHITextureHandle spriteTexture;

    /// Signed distance from camera to emitter origin, used for back-to-front sorting.
    /// Larger values are drawn first (farther from camera).
    float distanceToCamera = 0.0f;

    // -- Descriptor heap indices for SRV binding (SM5 path) -------------------

    /// SRV index for the read attribute set (positions, sizes, colors, rotations).
    uint32_t readSetSrvIndex = UINT32_MAX;

    /// SRV index for the sprite texture.
    uint32_t spriteSrvIndex = UINT32_MAX;

    /// SRV index for the scene depth texture (soft particles).
    uint32_t sceneDepthSrvIndex = UINT32_MAX;

    // -- Render graph resource IDs (for automatic barrier tracking) -----------

    /// RgResourceIds for the 7 read-set attribute buffers.
    rg::RgResourceId rgReadPositions;
    rg::RgResourceId rgReadVelocities;
    rg::RgResourceId rgReadAges;
    rg::RgResourceId rgReadLifespans;
    rg::RgResourceId rgReadSizes;
    rg::RgResourceId rgReadColors;
    rg::RgResourceId rgReadRotations;

    /// Shared buffers needed by render (alive count for structured buffer read,
    /// indirect draw args for DrawIndexedIndirect).
    rg::RgResourceId rgAliveCount;
    rg::RgResourceId rgIndirectDrawArgs;

    /// Render graph resource ID for the sorted indices buffer produced by the
    /// bitonic sort pass. The billboard vertex shader reads this to draw
    /// particles in back-to-front order for correct alpha blending.
    /// Invalid if the sort pass was not dispatched for this emitter.
    rg::RgResourceId rgSortIndices;
};

// ---------------------------------------------------------------------------
// ParticleRenderPassDesc -- full pass configuration
// ---------------------------------------------------------------------------

/// Configuration for the GPU particle rendering render graph pass.
///
/// All fields must be set before calling `AddParticleRenderPass`. The pass
/// does not take ownership of any pointed-to objects -- the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct ParticleRenderPassDesc {
    // -- Pipeline -------------------------------------------------------------

    /// Graphics PSO for the billboard particle shader (VS + PS).
    /// Must be configured with alpha blending enabled and depth write disabled.
    RHIPipelineHandle billboardPipeline;

    // -- Scene depth for soft particles ----------------------------------------

    /// Scene depth texture handle for soft particle fading.
    /// Expected to be in `RHIResourceState::ShaderResource` (or transitioned
    /// by the render graph) before this pass executes.
    RHITextureHandle sceneDepthTexture;

    // -- Render target ---------------------------------------------------------

    /// The HDR color target to draw particles into.
    /// Must already be in `RenderTarget` state (set up by the render graph).
    rg::RgResourceId colorTarget;

    /// The depth buffer for depth testing (read-only, no depth write).
    rg::RgResourceId depthTarget;

    // -- Per-emitter render data -----------------------------------------------

    /// Array of active emitters to render this frame.
    /// Will be sorted back-to-front by `distanceToCamera` during pass setup.
    std::vector<EmitterRenderData> emitters;
};

// ---------------------------------------------------------------------------
// AddParticleRenderPass
// ---------------------------------------------------------------------------

/// Registers the GPU particle billboard rendering pass in the render graph.
///
/// Sorts emitters back-to-front by `distanceToCamera`, then for each emitter:
/// 1. Binds the billboard graphics pipeline.
/// 2. Sets the per-emitter billboard constant buffer (view/proj matrices,
///    viewport dimensions, soft particle fade range).
/// 3. Binds SRV descriptor table for particle attributes, sprite texture,
///    and scene depth.
/// 4. Issues `DrawIndexedIndirect` using the emitter's indirect draw args
///    buffer (populated by the preceding simulation pass).
///
/// The pass renders into the transparent phase after opaque geometry and
/// lighting. Depth testing is enabled but depth writes are disabled to
/// allow correct alpha blending of overlapping particles.
///
/// # Arguments
/// * `builder` -- Render graph builder.
/// * `desc` -- Pass configuration (pipeline, depth texture, per-emitter data).
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
///
/// # Panics
/// Asserts if `desc.billboardPipeline` is invalid or if any emitter's
/// `buffer` is null or not allocated.
void AddParticleRenderPass(rg::RgBuilder& builder,
                           const ParticleRenderPassDesc& desc);

} // namespace uz
