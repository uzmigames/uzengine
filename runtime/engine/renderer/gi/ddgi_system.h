/// UzEngine DDGISystem — Octahedral irradiance + depth probe atlases.
///
/// Owns the two GPU 2D textures consumed by the DDGI compute shader and the
/// deferred lighting Chebyshev visibility sampler. Per probe we store:
///
/// * `kIrradianceProbeSize` × `kIrradianceProbeSize` octahedral RGBA16F
///   irradiance values, with a 1-texel border for bilinear edge wrap.
/// * `kDepthProbeSize` × `kDepthProbeSize` octahedral RG16F (mean distance,
///   mean distance²) values for the Majercik 2019 Chebyshev visibility test.
///
/// The atlases are 2D textures laid out as `probesPerAxis × (probesPerAxis*probesPerAxis)`
/// probe tiles (a XY slice per Z layer of the probe grid). The DDGI compute
/// shader writes both atlases via UAV; deferred lighting samples them via SRV
/// using the bindless heap.
///
/// # References
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
///                     Irradiance Probes" — atlas layout, Chebyshev visibility
/// @ref Cigolle et al. 2014 "A Survey of Efficient Representations for
///                           Independent Unit Vectors" — octahedral encoding
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.

#pragma once

#include "rhi_types.h"
#include "vector3.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class RTManager;

// ─────────────────────────────────────────────────────────────────────────────
// DDGISystem
// ─────────────────────────────────────────────────────────────────────────────

/// Owns the per-probe octahedral irradiance + depth atlases used by DDGI.
///
/// # Ownership
/// Owns both atlas textures. Does NOT own `IRHIDevice`.
///
/// # Lifecycle
/// ```cpp
/// DDGISystem ddgi;
/// ddgi.CreateResources(device);
/// // Per frame:
/// ddgi.SetGridOrigin(probeGridMin);
/// // ...DDGI compute shader binds GetIrradianceUav() / GetDepthUav()...
/// // ...deferred lighting samples GetIrradianceSrv() / GetDepthSrv()...
/// ddgi.DestroyResources();
/// ```
class DDGISystem {
public:
    /// Probes per axis — must match `RadianceCache::kProbesPerAxis` so the
    /// DDGI shader and the SH path can share the same probe positions.
    static constexpr uint32_t kProbesPerAxis = 16;
    static constexpr uint32_t kTotalProbes   = kProbesPerAxis
                                              * kProbesPerAxis
                                              * kProbesPerAxis;

    /// Octahedral resolution per probe (Majercik 2019 §4.1 — 6×6 irradiance
    /// is enough because cosine-weighted irradiance varies slowly).
    static constexpr uint32_t kIrradianceProbeSize = 6;

    /// Higher resolution for depth — needed because depth has hard edges.
    static constexpr uint32_t kDepthProbeSize = 16;

    DDGISystem() = default;
    ~DDGISystem();

    DDGISystem(const DDGISystem&)            = delete;
    DDGISystem& operator=(const DDGISystem&) = delete;
    DDGISystem(DDGISystem&&) noexcept        = default;
    DDGISystem& operator=(DDGISystem&&) noexcept = default;

    /// Allocates the two atlas textures and registers SRV/UAV bindless slots.
    ///
    /// # Arguments
    /// * `device` — RHI device. Must outlive this object.
    ///
    /// # Returns
    /// `true` on success.
    [[nodiscard]] bool CreateResources(IRHIDevice* device);

    /// Releases the atlas textures and bindless descriptors.
    void DestroyResources();

    /// Updates the probe grid origin (minimum corner) tracked by DDGI. The
    /// renderer mirrors this into the deferred lighting CB so the sampling
    /// path uses the same world-space layout as the probe-update CS.
    void SetGridOrigin(const Vec3& origin) noexcept { m_gridOrigin = origin; }

    /// Sets the per-axis spacing (cm). Mirrors `RadianceCache::probeSpacing`.
    void SetProbeSpacing(float spacing) noexcept { m_probeSpacing = spacing; }

    // ── Accessors ────────────────────────────────────────────────────────

    [[nodiscard]] uint32_t GetIrradianceSrv() const noexcept { return m_irradSrv; }
    [[nodiscard]] uint32_t GetIrradianceUav() const noexcept { return m_irradUav; }
    [[nodiscard]] uint32_t GetDepthSrv() const noexcept       { return m_depthSrv; }
    [[nodiscard]] uint32_t GetDepthUav() const noexcept       { return m_depthUav; }

    [[nodiscard]] uint32_t GetIrradianceAtlasWidth() const noexcept {
        return kProbesPerAxis * kProbesPerAxis * (kIrradianceProbeSize + 2);
    }
    [[nodiscard]] uint32_t GetIrradianceAtlasHeight() const noexcept {
        return kProbesPerAxis * (kIrradianceProbeSize + 2);
    }
    [[nodiscard]] uint32_t GetDepthAtlasWidth() const noexcept {
        return kProbesPerAxis * kProbesPerAxis * (kDepthProbeSize + 2);
    }
    [[nodiscard]] uint32_t GetDepthAtlasHeight() const noexcept {
        return kProbesPerAxis * (kDepthProbeSize + 2);
    }

    [[nodiscard]] Vec3  GetGridOrigin() const noexcept   { return m_gridOrigin; }
    [[nodiscard]] float GetProbeSpacing() const noexcept { return m_probeSpacing; }
    [[nodiscard]] bool  IsInitialized() const noexcept   { return m_initialized; }

    /// Atlas SRV/UAV pair to bind to the DDGI compute shader's root constants.
    /// Uses the bindless heap — both indices must be valid before dispatch.
    [[nodiscard]] bool HasValidUavs() const noexcept {
        return m_irradUav != UINT32_MAX && m_depthUav != UINT32_MAX;
    }

    /// Dispatches the DDGI atlas-writing compute shader twice — once for the
    /// irradiance atlas and once for the depth atlas — with the appropriate
    /// root constants and resource barriers.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    /// * `ddgiPipeline` — Compute PSO created from `ddgi_probe_update_cs.hlsl`.
    /// * `tlasSrvIndex` — Bindless SRV index of the scene TLAS (0xFFFFFFFF if none).
    /// * `cbRing` — CB ring allocator for trace constants.
    /// * `perFrameBuffer` / `perFrameOffset` / `perFrameCpuPtr` / `perFrameSize` —
    ///   The current frame's PerFrameCB allocation. The same struct is bound
    ///   at b1 by the renderer; we re-bind it as the b2 trace constants
    ///   because the DDGI shader reuses the radiance cache CB layout.
    void DispatchUpdate(IRHICommandList* cmdList,
                        RHIPipelineHandle ddgiPipeline,
                        uint32_t          tlasSrvIndex,
                        CBRingAllocator*  cbRing,
                        uint32_t          frameIndex);

private:
    IRHIDevice*      m_device       = nullptr;
    RHITextureHandle m_irradAtlas{};
    RHITextureHandle m_depthAtlas{};
    uint32_t         m_irradSrv     = UINT32_MAX;
    uint32_t         m_irradUav     = UINT32_MAX;
    uint32_t         m_depthSrv     = UINT32_MAX;
    uint32_t         m_depthUav     = UINT32_MAX;

    Vec3             m_gridOrigin{};
    float            m_probeSpacing = 0.0f;
    bool             m_initialized  = false;
    bool             m_firstDispatch = true; ///< Atlases start in Common state.
};

} // namespace uz
