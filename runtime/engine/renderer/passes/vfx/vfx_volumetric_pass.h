#pragma once

/// VFX volumetric render passes — density splat (CS) + raymarch (PS).
///
/// Phase 9.10.0 — render-graph wiring for the phase 8.15.0 shaders.
///
/// Two passes are registered:
///
/// 1. **VfxVolumetricSplat** (compute): for each alive particle, scatter
///    its density into a transient 3D R32Float volume (128³ default)
///    using a 3×3×3 tent filter. The volume is allocated via
///    `RgBuilder::CreateTransient3D` (phase 10.6.0).
///
/// 2. **VfxVolumetricRaymarch** (fullscreen PS): march rays from the
///    camera through the volume AABB, accumulating in-scattering
///    (HG phase × sun colour × transmittance) and reducing transmittance
///    (Beer–Lambert). Output is premultiplied alpha blended onto the
///    existing HDR scene colour target.
///
/// # Integration order
///
/// The splat pass runs after particle simulation (needs alive positions).
/// The raymarch pass runs after deferred lighting but before fog
/// (default), or after fog when `afterFog == true`.
///
/// # Root signature
///
/// Both passes use the global engine root signature:
/// - Splat CS: b0 = VolumetricSplatCB, UAV table for density volume,
///   SRV table for particle positions.
/// - Raymarch PS: b0 = VolumetricRaymarchCB, SRV table for density volume.
///
/// @ref engine/vfx/modules/vfx_volumetric.h (config + CPU math)
/// @ref shaders/runtime/vfx/vfx_volumetric_splat_cs.hlsl
/// @ref shaders/runtime/vfx/vfx_volumetric_raymarch_ps.hlsl
/// @ref docs/engine/25-vfx/32-volumetric-renderer.md

#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"
#include "engine/vfx/modules/vfx_volumetric.h"

#include <cstdint>
#include <vector>

namespace uz {

struct GPUParticleBuffer;

} // namespace uz

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CPU-side CB mirrors
// ─────────────────────────────────────────────────────────────────────────────

/// CPU mirror of `VolumetricSplatCB` at register(b0) in the splat CS.
struct VfxVolumetricSplatCB {
    float gridCenterX, gridCenterY, gridCenterZ;
    float halfExtent;
    uint32_t gridResolution;
    float densityPerParticle;
    uint32_t aliveCount;
    uint32_t _pad0;
};
static_assert(sizeof(VfxVolumetricSplatCB) == 32,
              "VolumetricSplatCB must be 32 bytes (match HLSL)");

/// CPU mirror of `VolumetricRaymarchCB` at register(b0) in the raymarch PS.
struct VfxVolumetricRaymarchCB {
    float invViewProj[16]; ///< row_major float4x4
    float cameraPosX, cameraPosY, cameraPosZ;
    float halfExtent;
    float gridCenterX, gridCenterY, gridCenterZ;
    uint32_t gridResolution;
    float sunDirX, sunDirY, sunDirZ;
    float extinctionCoeff;
    float scatteringColorR, scatteringColorG, scatteringColorB;
    float hgAnisotropy;
    uint32_t raymarchSteps;
    uint32_t _pad0, _pad1, _pad2;
};
static_assert(sizeof(VfxVolumetricRaymarchCB) == 144,
              "VolumetricRaymarchCB must be 144 bytes (match HLSL)");

// ─────────────────────────────────────────────────────────────────────────────
// Per-emitter volumetric data
// ─────────────────────────────────────────────────────────────────────────────

/// Per-emitter data needed by the volumetric splat pass.
struct VolumetricEmitterData {
    /// Alive particle count (for dispatch sizing + CB).
    uint32_t aliveCount = 0;

    /// SRV index for the position SoA buffer (float4, xyz.w=alive flag).
    uint32_t positionSrvIndex = UINT32_MAX;

    /// Volumetric configuration for this emitter.
    VfxVolumetricConfig config{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Splat pass descriptor
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the volumetric density splat compute pass.
struct VfxVolumetricSplatPassDesc {
    /// Compute pipeline handle for the splat shader.
    RHIPipelineHandle splatPipeline;

    /// The render-graph resource for the transient 3D density volume.
    /// Allocated via `RgBuilder::CreateTransient3D(...)`.
    rg::RgResourceId densityVolume;

    /// UAV descriptor index for the density volume RWTexture3D<float>.
    uint32_t densityVolumeUavIndex = UINT32_MAX;

    /// CB ring allocator for per-dispatch constant buffers.
    CBRingAllocator* cbRing = nullptr;

    /// Emitters with volumetric rendering enabled.
    std::vector<VolumetricEmitterData> emitters;
};

// ─────────────────────────────────────────────────────────────────────────────
// Raymarch pass descriptor
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the volumetric raymarch fullscreen pass.
struct VfxVolumetricRaymarchPassDesc {
    /// Graphics pipeline handle for the fullscreen raymarch shader.
    /// Blend: SrcAlpha / InvSrcAlpha (premultiplied alpha).
    RHIPipelineHandle raymarchPipeline;

    /// The render-graph resource for the density volume (SRV read).
    rg::RgResourceId densityVolume;

    /// SRV descriptor index for the density volume Texture3D<float>.
    uint32_t densityVolumeSrvIndex = UINT32_MAX;

    /// HDR scene colour target (read-modify-write via alpha blend).
    rg::RgResourceId colorTarget;

    /// Physical handle for the HDR color target.
    RHITextureHandle colorTexture;

    /// CB ring allocator for per-dispatch constant buffers.
    CBRingAllocator* cbRing = nullptr;

    /// Viewport dimensions for SetViewport/SetScissor.
    uint32_t renderWidth  = 0;
    uint32_t renderHeight = 0;

    /// Merged volumetric config (from the emitter or a scene-wide override).
    VfxVolumetricConfig config{};

    /// Inverse view-projection matrix (row-major, for world-space ray
    /// reconstruction from screen UV).
    float invViewProj[16] = {};

    /// Camera world-space position.
    float cameraPosX = 0, cameraPosY = 0, cameraPosZ = 0;

    /// World-space direction toward the sun (normalised).
    float sunDirX = 0, sunDirY = -1.0f, sunDirZ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pass registration
// ─────────────────────────────────────────────────────────────────────────────

/// Registers the volumetric density splat compute pass.
///
/// One dispatch per emitter with `[numthreads(64,1,1)]`, sized by
/// `ceil(aliveCount / 64)`.
void AddVfxVolumetricSplatPass(rg::RgBuilder& builder,
                                const VfxVolumetricSplatPassDesc& desc);

/// Registers the volumetric raymarch fullscreen pass.
///
/// Single fullscreen triangle draw, alpha-blended onto the HDR scene
/// colour target.
void AddVfxVolumetricRaymarchPass(rg::RgBuilder& builder,
                                    const VfxVolumetricRaymarchPassDesc& desc);

} // namespace uz
