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

namespace rg { class RgStateRegistry; }

class GlobalDistanceField;

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

    /// Capacity of the per-ray buffers (block B.6): `r.GI.RadianceCache.RaysPerProbe` is clamped
    /// to this in both DDGI shaders (`kMaxRaysPerProbe` in ddgi_probe_common.hlsli).
    static constexpr uint32_t kMaxRaysPerProbe = 64;

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

    void SetStateRegistry(rg::RgStateRegistry* states) { m_rgStates = states; }

    /// Releases the atlas textures and bindless descriptors.
    void DestroyResources();

    /// Updates the probe grid origin (minimum corner) tracked by DDGI. The
    /// renderer mirrors this into the deferred lighting CB so the sampling
    /// path uses the same world-space layout as the probe-update CS.
    void SetGridOrigin(const Vec3& origin) noexcept { m_gridOrigin = origin; }

    /// Sets the per-axis spacing (cm). Mirrors `RadianceCache::probeSpacing`.
    void SetProbeSpacing(float spacing) noexcept { m_probeSpacing = spacing; }

    // ── Accessors ────────────────────────────────────────────────────────

    /// Atlas texture handles — for diagnostics/readback registration only.
    /// Lighting binds the bindless SRVs below, never these handles.
    [[nodiscard]] RHITextureHandle GetIrradianceTexture() const noexcept { return m_irradAtlas; }
    [[nodiscard]] RHITextureHandle GetDepthTexture() const noexcept      { return m_depthAtlas; }

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

    /// True once DispatchUpdate() has completed a full atlas write. Until then
    /// the atlases have never been written and must not be bound for lighting.
    // Diagnostic: what the last dispatch sampled the sky with, for the identity log line.
    uint32_t m_lastSkyRadianceSrv = UINT32_MAX;
    uint32_t m_lastRaysPerProbe = 0;
    float    m_lastSkyColor[3] = {0.0f, 0.0f, 0.0f};
    float    m_lastLightColor[3] = {0.0f, 0.0f, 0.0f};
    float    m_lastLightDir[3] = {0.0f, 0.0f, 0.0f};
    float    m_lastLightIntensity = 0.0f;
    [[nodiscard]] bool  HasCompletedFirstUpdate() const noexcept {
        return m_firstUpdateComplete;
    }

    /// Atlas SRV/UAV pair to bind to the DDGI compute shader's root constants.
    /// Uses the bindless heap — both indices must be valid before dispatch.
    [[nodiscard]] bool HasValidUavs() const noexcept {
        return m_irradUav != UINT32_MAX && m_depthUav != UINT32_MAX;
    }

    /// Traces every probe's rays once (`ddgi_probe_trace_cs.hlsl`, block B.6) into the two
    /// ray buffers, then dispatches the DDGI atlas-writing compute shader twice — once for the
    /// irradiance atlas and once for the depth atlas — with the appropriate
    /// root constants and resource barriers.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    /// * `ddgiPipeline` — Compute PSO created from `ddgi_probe_update_cs.hlsl`.
    /// * `tracePipeline` — Compute PSO created from `ddgi_probe_trace_cs.hlsl`. Invalid (or
    ///   `r.GI.DDGI.RayBuffers 0`, or no ray buffers) keeps the per-texel trace of the
    ///   previous design: the update shader traces the rays itself, 292 times each.
    /// * `tlasSrvIndex` — Bindless SRV index of the scene TLAS (0xFFFFFFFF if none).
    /// * `cbRing` — CB ring allocator for trace constants.
    /// * `perFrameBuffer` / `perFrameOffset` / `perFrameCpuPtr` / `perFrameSize` —
    ///   The current frame's PerFrameCB allocation. The same struct is bound
    ///   at b1 by the renderer; we re-bind it as the b2 trace constants
    ///   because the DDGI shader reuses the radiance cache CB layout.
    /// * `vfxEmissiveSrvIndex` — Bindless SRV of the `VfxDDGIEmissiveFeed`
    ///   StructuredBuffer (one `VfxEmissiveSample` per active emitter).
    ///   Pass `UINT32_MAX` to disable the VFX emissive injection branch in
    ///   the probe-update CS — the shader gates on
    ///   `vfxEmissiveSampleCount > 0 && srv != 0xFFFFFFFF`.
    /// * `vfxEmissiveSampleCount` — Number of valid samples in the buffer.
    ///   `0` is the natural disable signal; the shader skips its loop.
    /// * `gdf` — Global distance field, sampled at ray hit points for the
    ///   surface normal and albedo. Inline `RayQuery` reports a hit distance
    ///   but no attributes, so without it the writer had to assume every hit
    ///   faced the ray and reflected a constant 18% grey.
    /// * `perFrameCpuPtr` — Mapped `PerFrameCB`. Supplies the sun direction,
    ///   colour and sky radiance. Until 2026-09-08 this method hardcoded a sun
    ///   pointing straight up at intensity 1 and a (0.6, 0.7, 0.9) sky, so the
    ///   probes were lit by a placeholder environment instead of the scene.
    void DispatchUpdate(IRHICommandList* cmdList,
                        RHIPipelineHandle ddgiPipeline,
                        RHIPipelineHandle tracePipeline,
                        RHIPipelineHandle borderPipeline,
                        uint32_t          tlasSrvIndex,
                        CBRingAllocator*  cbRing,
                        uint32_t          frameIndex,
                        const GlobalDistanceField* gdf            = nullptr,
                        const void*       perFrameCpuPtr          = nullptr,
                        uint32_t          vfxEmissiveSrvIndex     = UINT32_MAX,
                        uint32_t          vfxEmissiveSampleCount  = 0);

private:
    IRHIDevice*      m_device       = nullptr;
    rg::RgStateRegistry* m_rgStates = nullptr;
    RHITextureHandle m_irradAtlas{};
    RHITextureHandle m_depthAtlas{};
    uint32_t         m_irradSrv     = UINT32_MAX;
    uint32_t         m_irradUav     = UINT32_MAX;
    uint32_t         m_depthSrv     = UINT32_MAX;
    uint32_t         m_depthUav     = UINT32_MAX;

    Vec3             m_gridOrigin{};
    float            m_probeSpacing = 0.0f;
    bool             m_initialized  = false;
    bool             m_firstUpdateComplete = false;
    bool m_atlasCleared = false;   ///< Both atlases cleared once (border fill pass, H-55).

    // ── Per-ray buffers (block B.6) ────────────────────────────────────────
    // `ddgi_probe_trace_cs.hlsl` writes (radiance, hitT) and the hit info of every probe ray
    // once per frame; the atlas writer reads them per texel instead of tracing. Sized for
    // kTotalProbes * kMaxRaysPerProbe float4 each.
    RHIBufferHandle  m_rayRadiance{};
    RHIBufferHandle  m_rayInfo{};
    uint32_t         m_rayRadianceSrv = UINT32_MAX;
    uint32_t         m_rayRadianceUav = UINT32_MAX;
    uint32_t         m_rayInfoSrv     = UINT32_MAX;
    uint32_t         m_rayInfoUav     = UINT32_MAX;
    bool             m_lastRayBuffers  = false;   ///< identity log line
    bool             m_lastIndirectTerm = false;  ///< identity log line

    // ── Transport diagnostic (r.GI.DDGI.Diagnostic) ────────────────────────
    // One float2 per irradiance-atlas texel: mean ray radiance luminance and
    // sky fraction, written by the irradiance pass. Copied to a readback
    // buffer once (frame >= 120) and logged as "DDGI transport: ..." lines that
    // pair with the radiance cache's "RC transport: ..." lines — the GPU-side
    // DDGI/SDF energy comparison (phase0_sponza-followup-08, item 2).
    RHIBufferHandle  m_diagnosticStats{};
    RHIBufferHandle  m_diagnosticReadback{};
    uint32_t         m_diagnosticStatsUav = UINT32_MAX;
    uint32_t         m_diagnosticCopyFrame = UINT32_MAX;
    bool             m_diagnosticComplete = false;
    void QueueDiagnosticReadback(IRHICommandList* cmdList, uint32_t frameIndex);
    void ReadDiagnosticResults(uint32_t frameIndex);
};

} // namespace uz
