/// UzEngine Volumetric Fog Pass — Froxel-based 3D fog.
///
/// Three-phase compute pipeline:
///   1. Inject: evaluate density + light at each froxel → write inscattering + extinction
///   2. Scatter: front-to-back accumulate inscattering + transmittance per column
///   3. Composite: fullscreen pass samples integrated volume at scene depth
///
/// Grid: 160×90×64 — inscattering (RGBA16F), density/extinction (R16F).
/// Depth distribution: exponential (more slices near camera).
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"

#pragma once

#include "render_graph_types.h"
#include "cb_ring_allocator.h"
#include "engine/rhi/rhi_types.h"
#include "mat4.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class PipelineLibrary;

namespace rg { class RgBuilder; }

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Hillaire 2015 — 160×90×64 grid is standard Frostbite resolution.
static constexpr uint32_t kFroxelGridX = 160;
static constexpr uint32_t kFroxelGridY = 90;
static constexpr uint32_t kFroxelGridZ = 64;

// ─────────────────────────────────────────────────────────────────────────────
// VolumetricFogCB — constant buffer for all 3 phases (256-byte aligned)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-dispatch parameters for froxel fog compute + composite shaders.
struct VolumetricFogCB {
    Mat4     invViewProj;          ///< Inverse view-projection (64 bytes).
    float    cameraPosX;           ///< Camera world position X (cm).
    float    cameraPosY;           ///< Camera world position Y (cm).
    float    cameraPosZ;           ///< Camera world position Z (cm).
    float    nearZ;                ///< Near clip plane (cm).
    float    farZ;                 ///< Far fog distance (cm, e.g. 640000 = 6.4km).
    float    globalDensity;        ///< Base fog density.
    float    heightFalloff;        ///< Exponential height falloff rate.
    float    heightOffset;         ///< Height offset for fog base (cm).
    float    albedoR;              ///< Fog albedo R (scattering color).
    float    albedoG;              ///< Fog albedo G.
    float    albedoB;              ///< Fog albedo B.
    float    phaseG;               ///< HG phase function asymmetry (-1..1, default 0.5).
    float    sunDirX;              ///< Sun direction X (normalised, toward sun).
    float    sunDirY;              ///< Sun direction Y.
    float    sunDirZ;              ///< Sun direction Z.
    float    sunIntensity;         ///< Sun illuminance scalar.
    float    sunColorR;            ///< Sun color R.
    float    sunColorG;            ///< Sun color G.
    float    sunColorB;            ///< Sun color B.
    float    temporalAlpha;        ///< Temporal blend weight (0.05 typical).
    uint32_t inscatteringUavIdx;   ///< UAV index for inscattering 3D volume (inject writes).
    uint32_t inscatteringSrvIdx;   ///< SRV index for inscattering 3D volume (scatter reads).
    uint32_t integratedUavIdx;     ///< UAV index for integrated volume (scatter writes).
    uint32_t integratedSrvIdx;     ///< SRV index for integrated volume (composite reads).
    uint32_t depthSrvIdx;          ///< SRV index for scene depth.
    uint32_t frameIndex;           ///< Frame counter for temporal jitter.
    uint32_t gridX;                ///< Froxel grid width (160).
    uint32_t gridY;                ///< Froxel grid height (90).
    uint32_t gridZ;                ///< Froxel grid depth (64).
    float    extinctionScale;      ///< Extinction = density * extinctionScale.
    float    emissiveR;            ///< Fog emissive R.
    float    emissiveG;            ///< Fog emissive G.
    float    emissiveB;            ///< Fog emissive B.
    float    _pad[15] = {};        ///< Pad to 256 bytes (196 data + 60 pad = 256).
};

static_assert(sizeof(VolumetricFogCB) == 256,
    "VolumetricFogCB must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// VolumetricFogPass — manages 3D textures and dispatches
// ─────────────────────────────────────────────────────────────────────────────

/// Owns the froxel grid textures and registers the three fog compute passes.
class VolumetricFogPass {
public:
    /// Initialize 3D textures (160×90×64). Returns true on success.
    bool Init(IRHIDevice* device);

    /// Release GPU resources.
    void Shutdown(IRHIDevice* device);

    bool IsInitialized() const { return m_initialized; }

    /// Register inject + scatter + composite passes in the render graph.
    void RegisterPasses(
        rg::RgBuilder& rgBuilder,
        rg::RgResourceId hdrRes,
        rg::RgResourceId depthRes,
        const CBAllocation& perFrameAlloc,
        const VolumetricFogCB& params,
        CBRingAllocator& cbRing,
        PipelineLibrary* pipelines,
        bool bindless,
        float vpWidth, float vpHeight);

    // ── Accessors ─────────────────────────────────────────────────────
    RHITextureHandle GetInscatteringTexture() const { return m_inscattering; }
    uint32_t GetInscatteringSrv() const { return m_inscatteringSrv; }
    uint32_t GetInscatteringUav() const { return m_inscatteringUav; }

    RHITextureHandle GetIntegratedTexture() const { return m_integrated; }
    uint32_t GetIntegratedSrv() const { return m_integratedSrv; }
    uint32_t GetIntegratedUav() const { return m_integratedUav; }

private:
    bool m_initialized = false;

    // Inscattering + extinction (inject output, scatter input)
    RHITextureHandle m_inscattering;
    uint32_t m_inscatteringSrv = UINT32_MAX;
    uint32_t m_inscatteringUav = UINT32_MAX;

    // Integrated inscattering + transmittance (scatter output, composite input)
    RHITextureHandle m_integrated;
    uint32_t m_integratedSrv = UINT32_MAX;
    uint32_t m_integratedUav = UINT32_MAX;
};

} // namespace uz
