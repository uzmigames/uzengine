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
/// - Slot 0 (b0): Root constants for VFXBillboardIndices (bindless SRV descriptor indices).
/// - Slot 1 (b1): PerFrameCB (viewProj, camera — inherited from previous passes, NOT set here).
/// - Slot 2 (b2): Root CBV for per-emitter VFXBillboardCB (invView, emitter params, rendering params).
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
#include <limits>
#include <vector>

namespace uz {

struct GPUParticleBuffer;

} // namespace uz

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ---------------------------------------------------------------------------
// ParticleBillboardIndices / VFXBillboardCB -- CPU-side mirrors of billboard shader CBs
// ---------------------------------------------------------------------------

/// CPU-side mirror of VFXBillboardConstants at register(b0) -- root constants.
///
/// Contains bindless descriptor indices for particle attribute buffers (VS)
/// and texture resources (PS). Both VS and PS share the same b0 layout.
///
/// Total size: 48 bytes (12 x uint32, matching HLSL exactly).
struct ParticleBillboardIndices {
    // VS indices (bytes 0-31)
    uint32_t positionBufferIdx      = 0;
    uint32_t sizeBufferIdx          = 0;
    uint32_t colorBufferIdx         = 0;
    uint32_t rotationBufferIdx      = 0;
    uint32_t sortedIndicesSrvIndex  = UINT32_MAX; ///< UINT32_MAX = no sorted indices.
    uint32_t velocityBufferIdx      = 0;
    uint32_t agesBufferIdx          = 0;
    uint32_t lifespansBufferIdx     = 0;
    // PS indices (bytes 32-47)
    uint32_t spriteTextureIdx       = UINT32_MAX; ///< Reserved (no sprite texture).
    uint32_t depthBufferIdx         = 0;          ///< SRV: Texture2D<float> scene depth.
    uint32_t dynamicParamsBufferIdx = 0;          ///< SRV: StructuredBuffer<float4> dynamic params.
    uint32_t _vfxPad1               = 0;
};
static_assert(sizeof(ParticleBillboardIndices) == 48,
              "ParticleBillboardIndices must be 48 bytes to match HLSL VFXBillboardConstants");

/// CPU-side mirror of the unified billboard constant buffer at register(b2).
///
/// Layout MUST match the HLSL cbuffer VFXBillboardCB byte-for-byte.
/// VS reads invView + emitter params; PS reads rendering params.
/// Uses float arrays instead of Mat4 for portability.
/// Total: 304 bytes (19 x float4).
struct VFXBillboardCB {
    // ── VS data: emitter parameters (bytes 0-159) ───────────────────────
    float invView[16]              = {};    ///<  0: Inverse view matrix (rows 0,1 = camera right/up).
    float emitterPositionX         = 0.0f;  ///< 64: World-space emitter origin X.
    float emitterPositionY         = 0.0f;  ///< 68: World-space emitter origin Y.
    float emitterPositionZ         = 0.0f;  ///< 72: World-space emitter origin Z.
    float globalSizeMultiplier     = 1.0f;  ///< 76: Multiplied with per-particle size.
    float globalColorTintR         = 1.0f;  ///< 80: Global color tint RGBA.
    float globalColorTintG         = 1.0f;  ///< 84
    float globalColorTintB         = 1.0f;  ///< 88
    float globalColorTintA         = 1.0f;  ///< 92
    float prevViewProj[16]         = {};    ///< 96: Previous frame viewProj for TAA motion vectors.

    // ── PS data: rendering parameters (bytes 160-207) ───────────────────
    float softFadeDistance         = 50.0f; ///< 160: Soft particle depth fade distance (world units).
    float alphaErosion             = 0.0f;  ///< 164: Subtractive alpha erosion [0, 1].
    float emissiveIntensity        = 0.0f;  ///< 168: Emissive multiplier.
    uint32_t litMode               = 0;     ///< 172: 0=Unlit, 1=Lit.
    float cameraOffset             = 0.0f;  ///< 176: Push/pull along camera forward.
    float pivotOffsetX             = 0.0f;  ///< 180: Sprite pivot X [-1,1].
    float pivotOffsetY             = 0.0f;  ///< 184: Sprite pivot Y [-1,1].
    float screenSpacePixelSize     = 0.0f;  ///< 188: Screen-space pixel diameter (0=disabled).

    // ── Size by speed (bytes 192-207) ───────────────────────────────────
    uint32_t sizeBySpeedEnabled    = 0;     ///< 192
    float sizeBySpeedMin           = 1.0f;  ///< 196
    float sizeBySpeedMax           = 2.0f;  ///< 200
    float speedRangeMax            = 100.0f;///< 204

    // ── Size over lifetime (bytes 208-223) ──────────────────────────────
    float sizeOverLifeStart        = 1.0f;  ///< 208
    float sizeOverLifePeak         = 1.0f;  ///< 212
    float sizeOverLifeEnd          = 0.0f;  ///< 216
    float sizeOverLifePeakTime     = 0.3f;  ///< 220

    // ── Color over lifetime (bytes 224-255) ─────────────────────────────
    float colorLifeStartR          = 1.0f;  ///< 224
    float colorLifeStartG          = 1.0f;  ///< 228
    float colorLifeStartB          = 1.0f;  ///< 232
    float colorLifeStartA          = 1.0f;  ///< 236
    float colorLifeEndR            = 1.0f;  ///< 240
    float colorLifeEndG            = 1.0f;  ///< 244
    float colorLifeEndB            = 1.0f;  ///< 248
    float colorLifeEndA            = 0.0f;  ///< 252

    // ── Billboard facing + stretching (bytes 256-287) ───────────────────
    uint32_t facingMode            = 0;     ///< 256: 0=FaceCamera.
    float customFacingX            = 0.0f;  ///< 260
    float customFacingY            = 0.0f;  ///< 264
    float customFacingZ            = 1.0f;  ///< 268
    float stretchFactor            = 0.0f;  ///< 272
    uint32_t blendModeHint         = 0;     ///< 276: 0=AlphaBlend, 1=Additive.
    uint32_t colorRampLutSrvIndex  = 0xFFFFFFFF; ///< 280
    uint32_t screenSpaceSizeEnabled = 0;    ///< 284

    // ── Color by speed (bytes 288-303) ──────────────────────────────────
    uint32_t colorBySpeedEnabled   = 0;     ///< 288
    uint32_t _cbsPad0              = 0;     ///< 292
    uint32_t _cbsPad1              = 0;     ///< 296
    uint32_t _cbsPad2              = 0;     ///< 300
};
static_assert(sizeof(VFXBillboardCB) == 304, "VFXBillboardCB must be 304 bytes to match HLSL layout");

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

    /// CPU-side alive particle count for non-indirect draw fallback.
    uint32_t cpuAliveCount = 0;

    /// Signed distance from camera to emitter origin, used for back-to-front sorting.
    /// Larger values are drawn first (farther from camera).
    float distanceToCamera = 0.0f;

    /// CPU-side vertex count for non-indexed draw (6 vertices per alive particle).
    /// Used as fallback until DrawIndirect command signature is added.
    uint32_t cpuVertexCount = 0;

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

    /// Physical texture handle for the HDR color target (for BeginRenderPass).
    RHITextureHandle colorTexture;

    /// Physical texture handle for the depth buffer (for BeginRenderPass).
    RHITextureHandle depthTexture;

    /// Pre-filled per-frame CB allocation (b1) — viewProj, camera pos, etc.
    /// Must be re-bound after SetPipeline resets root signature state.
    CBAllocation perFrameCB = {};

    /// Render viewport dimensions for SetViewport/SetScissorRect.
    uint32_t renderWidth  = 0;
    uint32_t renderHeight = 0;

    /// The depth buffer for depth testing (read-only, no depth write).
    rg::RgResourceId depthTarget;

    /// Viewport dimensions for setting viewport/scissor in the render pass.
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;

    // -- Per-frame constant buffer (b1) ----------------------------------------

    /// Per-frame CB allocation (viewProj, camera, time, lighting).
    /// Must be set so the billboard VS can read viewProj at register(b1).
    /// SetPipeline resets root signature bindings, so each pass must re-bind.
    CBAllocation perFrameAlloc = {};

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
