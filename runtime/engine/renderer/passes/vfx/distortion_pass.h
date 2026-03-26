/// VFX Distortion Accumulation Pass -- Render graph pass for screen-space distortion.
///
/// Registers a graphics pass in the render graph that draws distortion-emitting
/// particles into an RGBA16F accumulation texture. Each pixel outputs a screen-space
/// UV offset vector (RG channels) and distortion strength (A channel). The
/// accumulated offsets are consumed by the distortion resolve pass to warp the
/// HDR color buffer.
///
/// # Rendering Strategy
/// Each emitter is drawn with a single `DrawIndexedIndirect` call using the
/// indirect argument buffer filled by the simulation pass. Emitters are sorted
/// back-to-front by distance to camera to ensure correct alpha blending of
/// overlapping distortion volumes.
///
/// # Blend State
/// The PSO must use additive blending (SrcAlpha / One) so that overlapping
/// distortion sources combine their UV offsets rather than replacing them.
/// Depth writes are disabled; depth testing is read-only against the scene
/// depth buffer.
///
/// # Render Graph Integration
/// The pass declares write access to the RGBA16F distortion target and read
/// access to the scene depth buffer. Particle attribute buffers and indirect
/// args buffers are external persistent resources expected to already be in
/// the correct state from the preceding simulation pass.
///
/// # Root Signature Slot Layout
/// Matches the global engine root signature:
/// - Slot 0 (b0): Root CBV for per-emitter distortion constants.
/// - Slot 1 (b1): Root CBV for per-emitter billboard constants (view/proj).
/// - Slot 5 (t0..tN): SRV descriptor table for particle attributes + normal map.
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
// DistortionConstants -- CPU-side mirror of distortion PS constant buffer
// ---------------------------------------------------------------------------

/// CPU-side mirror of the distortion pixel shader's constant buffer (register b0).
///
/// Contains the distortion strength multiplier and the bindless SRV index for
/// the normal map texture. Must be byte-identical to the HLSL cbuffer layout.
///
/// Total size: 16 bytes (256-byte aligned by CBRingAllocator).
///
/// @ref Vlachos 2015 "Advanced VR Rendering" -- screen-space distortion technique
struct DistortionConstants {
    float    distortionStrength = 1.0f;       ///< Multiplier for UV offset magnitude.
    uint32_t normalMapSrvIndex  = UINT32_MAX; ///< SRV index for normal map texture.
    float    _pad0              = 0.0f;       ///< Padding to 16-byte alignment.
    float    _pad1              = 0.0f;       ///< Padding to 16-byte alignment.
};

// ---------------------------------------------------------------------------
// DistortionEmitterData -- per-emitter data for the distortion pass
// ---------------------------------------------------------------------------

/// Per-emitter rendering data for the distortion accumulation pass.
///
/// Contains the pipeline, constant buffer allocations, and GPU buffer
/// references needed to draw one emitter's particles as distortion sources.
///
/// # Ownership
/// Does NOT own any pointed-to resources. The caller must ensure all handles
/// and pointers remain valid until `RgBuilder::Execute()` completes.
struct DistortionEmitterData {
    /// Graphics PSO for the distortion shader (billboard VS + distortion PS).
    /// Must be configured with additive blending, depth read-only, and no depth writes.
    RHIPipelineHandle pipeline;

    /// Pre-filled CB allocation for per-emitter billboard constants (b1).
    /// Contains view/proj matrices and viewport dimensions.
    CBAllocation billboardCB = {};

    /// Pre-filled CB allocation for the distortion PS constants (b0).
    /// Contains distortion strength and normal map SRV index.
    CBAllocation distortionCB = {};

    /// SRV descriptor heap index for the read-set attribute buffers.
    uint32_t readSetSrvIndex = UINT32_MAX;

    /// The emitter's GPU particle storage (used for indirect draw args).
    const GPUParticleBuffer* buffer = nullptr;

    /// Signed distance from camera to emitter origin for back-to-front sorting.
    float distanceToCamera = 0.0f;

    // -- Render graph resource IDs (for automatic barrier tracking) -----------

    /// RgResourceIds for the read-set attribute buffers.
    rg::RgResourceId rgReadPositions;
    rg::RgResourceId rgReadVelocities;
    rg::RgResourceId rgReadAges;
    rg::RgResourceId rgReadLifespans;
    rg::RgResourceId rgReadSizes;
    rg::RgResourceId rgReadColors;
    rg::RgResourceId rgReadRotations;

    /// Shared buffers needed for render (alive count and indirect draw args).
    rg::RgResourceId rgAliveCount;
    rg::RgResourceId rgIndirectDrawArgs;

    /// Sorted indices buffer produced by the bitonic sort pass.
    /// Invalid if the sort pass was not dispatched for this emitter.
    rg::RgResourceId rgSortIndices;
};

// ---------------------------------------------------------------------------
// DistortionPassDesc -- full pass configuration
// ---------------------------------------------------------------------------

/// Configuration for the distortion accumulation render graph pass.
///
/// All fields must be set before calling `AddDistortionAccumulationPass`.
/// The pass does not take ownership of any pointed-to objects -- the caller
/// must ensure they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct DistortionPassDesc {
    // -- Render targets -------------------------------------------------------

    /// RGBA16F distortion accumulation texture (render graph resource ID).
    /// RG = UV offset vector, A = distortion strength.
    rg::RgResourceId distortionTarget;

    /// Scene depth buffer for depth testing (render graph resource ID).
    /// Read-only -- no depth writes.
    rg::RgResourceId depthTarget;

    // -- Per-emitter render data ----------------------------------------------

    /// Array of active distortion emitters to render this frame.
    /// Will be sorted back-to-front by `distanceToCamera` during pass setup.
    std::vector<DistortionEmitterData> emitters;
};

// ---------------------------------------------------------------------------
// AddDistortionAccumulationPass
// ---------------------------------------------------------------------------

/// Registers the distortion accumulation pass in the render graph.
///
/// Sorts emitters back-to-front by `distanceToCamera`, then for each emitter:
/// 1. Binds the distortion graphics pipeline (billboard VS + distortion PS).
/// 2. Sets the per-emitter distortion constant buffer (b0) with strength
///    and normal map SRV index.
/// 3. Sets the per-emitter billboard constant buffer (b1) with view/proj
///    matrices and viewport dimensions.
/// 4. Binds the SRV descriptor table for particle attributes and normal map.
/// 5. Issues `DrawIndexedIndirect` using the emitter's indirect draw args
///    buffer (populated by the preceding simulation pass).
///
/// The pass renders into the transparent phase after opaque geometry.
/// The output RGBA16F target is consumed by the distortion resolve pass.
///
/// # Arguments
/// * `builder` -- Render graph builder.
/// * `desc` -- Pass configuration (distortion target, depth target,
///   per-emitter data).
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
///
/// # Panics
/// Asserts if any emitter's `buffer` is null or not allocated.
void AddDistortionAccumulationPass(rg::RgBuilder& builder,
                                   const DistortionPassDesc& desc);

} // namespace uz
