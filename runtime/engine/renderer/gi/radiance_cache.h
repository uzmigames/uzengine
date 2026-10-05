/// UzEngine Radiance Cache — World-space probe grid for diffuse GI.
///
/// Maintains a uniform 3D grid of 16^3 = 4096 probes centered on the camera.
/// Each probe stores L2 spherical harmonics (9 coefficients, 3 channels = 27 floats,
/// packed into 7 float4s following the reference engine's SkyIrradianceEnvironmentMap layout).
///
/// The radiance cache is updated incrementally: each frame, a compute shader
/// traces 32 rays from each probe through the global distance field, accumulates
/// incoming radiance into SH, and blends with the previous frame's SH via
/// exponential moving average (alpha = 0.05).
///
/// # References
/// @ref "Real-Time Global Illumination using Precomputed Light Field Probes" (McGuire 2017)
/// World-space probe clipmap layout with SH radiance and temporal blending.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.

#pragma once

#include "rhi_types.h"
#include "vector3.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class GlobalDistanceField;

// ─────────────────────────────────────────────────────────────────────────────
// RadianceCache
// ─────────────────────────────────────────────────────────────────────────────

/// World-space probe grid for diffuse global illumination via SH.
///
/// # Ownership
/// Owns the SH buffer GPU resource. Does NOT own IRHIDevice or GlobalDistanceField.
///
/// # Lifecycle
/// ```cpp
/// RadianceCache rc;
/// rc.CreateResources(device, 5000.0f);
/// // Per frame:
/// rc.UpdateProbes(cmdList, gdf, tracePipeline, ...);
/// uint32_t shSrv = rc.GetProbeSHBufferSrv(); // bind in lighting shader
/// // Shutdown:
/// rc.DestroyResources();
/// ```
/// Change boxes the trace constants carry (block B.2 phase 4): probes inside replace their
/// history instead of blending. 16 four-float rows after the 13 shared ones.
inline constexpr uint32_t kMaxFieldRegions = 8;

class RadianceCache {
public:
    /// Number of probes per axis. 16^3 = 4096 total probes.
    /// 16^3 = 4096 probes — balanced between quality and trace budget.
    static constexpr uint32_t kProbesPerAxis = 16;

    /// Total number of probes in the grid.
    static constexpr uint32_t kTotalProbes = kProbesPerAxis * kProbesPerAxis * kProbesPerAxis;

    /// Number of L2 SH coefficients (per channel). 3 bands: 1 + 3 + 5 = 9.
    static constexpr uint32_t kSHCoeffs = 9;

    /// Number of float4 values per probe for packed SH storage.
    /// uses 7 float4s to pack 3-channel L2 SH coefficients:
    ///   [0..2]: R/G/B L0 + L1 terms (3 float4)
    ///   [3..5]: R/G/B L2 terms (3 float4)
    ///   [6]:    remaining L2 terms (1 float4)
    /// 7 float4s pack 3-channel L2 SH (27 coefficients) tightly.
    static constexpr uint32_t kSHFloat4PerProbe = 7;

    /// One float4 per probe after the SH records, at `kTotalProbes * kSHFloat4PerProbe + probe`:
    /// the guard of a probe the side classification kept although the biased field put it inside a
    /// surface's band (followup-01 candidate, `r.GI.RadianceCache.ProbeClassification` = 1).
    /// xyz = its nearest face's outward direction, w = its distance to that face; every reader of
    /// the cache rejects the probe for receivers behind that face. w = 0: no guard (always, under
    /// the current rule).
    static constexpr uint32_t kGuardFloat4PerProbe = 1;

    /// Diagnostic records per probe (r.GI.RadianceCache.Diagnostic): surface, trace, sky, path,
    /// classification (signed side distance, relocation distance, class code, 0).
    static constexpr uint32_t kDiagnosticFloat4PerProbe = 6;

    /// Number of rays traced per probe per frame.
    /// 32 rays is a balanced budget for 4096 probes (~131K rays/frame).
    static constexpr uint32_t kRaysPerProbe = 32;

    /// Temporal blend factor (EMA alpha). Lower = more temporal stability.
    /// 0.12 averages over ~8 frames (fast convergence, reduced pulsing vs 0.05).
    static constexpr float kTemporalBlendAlpha = 0.12f;

    /// Probes updated per frame for temporal spreading. The trace cursor
    /// advances by this amount and wraps around the grid, so the full grid
    /// refreshes every `ceil(kTotalProbes / kProbesPerFrame)` frames. With
    /// 4096 total probes and 1024/frame the entire grid refreshes every 4
    /// frames — matches task 3.3 of the indoor lighting milestone.
    /// @ref Majercik 2019 "DDGI" — amortise probe updates over many frames.
    static constexpr uint32_t kProbesPerFrame = 1024;

    RadianceCache() = default;
    ~RadianceCache();

    RadianceCache(const RadianceCache&) = delete;
    RadianceCache& operator=(const RadianceCache&) = delete;
    RadianceCache(RadianceCache&&) noexcept = default;
    RadianceCache& operator=(RadianceCache&&) noexcept = default;

    /// Creates the SH buffer and initializes the probe grid.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation. Must outlive this object.
    /// * `worldExtent` — Half-size of the probe grid in world units (cm).
    ///   Default: 5000 cm (50 m). Probes cover a cube of this half-extent
    ///   centered on the camera.
    ///
    /// # Returns
    /// `true` if all resources were created successfully.
    [[nodiscard]] bool CreateResources(IRHIDevice* device, float worldExtent = 5000.0f);

    /// Destroys the SH buffer.
    void DestroyResources();

    /// Updates the probe grid center to track the camera. Marks the cache
    /// dirty when the grid shifts by one or more probe spacings.
    ///
    /// # Arguments
    /// * `cameraPos` — Current camera world-space position.
    void Update(const Vec3& cameraPos);

    /// Dispatches the radiance cache trace compute shader.
    ///
    /// Each thread traces rays from one probe through the global distance field,
    /// accumulates incoming radiance into L2 SH coefficients, and blends the
    /// new SH with the previous frame's history.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    /// * `gdf` — Global distance field for ray tracing.
    /// * `tracePipeline` — The radiance cache trace compute pipeline.
    /// * `cbRing` — CB ring allocator for uploading per-dispatch constants.
    /// * `perFrameBuffer` — PerFrameCB buffer handle.
    /// * `perFrameOffset` — Byte offset into the per-frame buffer.
    /// * `perFrameCpuPtr` — CPU pointer to the per-frame data.
    /// * `perFrameSize` — Size of the per-frame data in bytes.
    /// * `frameIndex` — Monotonic frame counter (for temporal jitter).
    void UpdateProbes(
        IRHICommandList* cmdList,
        const GlobalDistanceField& gdf,
        RHIPipelineHandle tracePipeline,
        CBRingAllocator* cbRing,
        RHIBufferHandle perFrameBuffer,
        uint32_t perFrameOffset,
        const void* perFrameCpuPtr,
        uint32_t perFrameSize,
        uint32_t frameIndex,
        RHIPipelineHandle sidePipeline = RHIPipelineHandle::Invalid());

    /// `r.GI.RadianceCache.ProbeClassification`: 0 = the current rule (a probe whose biased field
    /// distance is below the trace's 5 cm surface epsilon is inside geometry); 1 = the followup-01
    /// candidate (the side of the nearest face decides, probes kept in the band are traced from a
    /// relocated origin and carry a guard). The caller passes `sidePipeline` when this is non-zero.
    [[nodiscard]] static uint32_t ClassificationMode();

    /// Mode 3: the G-buffer world position (bindless SRV, cleared to 0 where nothing was drawn) whose
    /// visible points seed the flood fill when `r.GI.RadianceCache.ReachVisibleSeeds` is on. The caller
    /// declares the read in the render graph.
    void SetVisibleSeedSource(uint32_t positionSrv, uint32_t width, uint32_t height) noexcept {
        m_seedPositionSrv = positionSrv; m_seedWidth = width; m_seedHeight = height;
    }

    /// Returns the SRV index for the probe SH buffer.
    /// Used by the deferred lighting shader to sample radiance cache probes.
    [[nodiscard]] uint32_t GetProbeSHBufferSrv() const noexcept {
        return HasProducedResult() ? m_shBufferSrv : UINT32_MAX;
    }

    /// Returns the UAV index for the probe SH buffer.
    [[nodiscard]] uint32_t GetProbeSHBufferUav() const noexcept { return m_shBufferUav; }

    /// Returns the world-space origin (corner) of the probe grid.
    bool m_identityLogged = false;   ///< Diagnostic: the sky-sample identity line is written once.
    [[nodiscard]] Vec3 GetGridOrigin() const noexcept { return m_gridOrigin; }

    /// Origin of the grid the last *completed* SH buffer (`GetProbeSHBufferSrv()`)
    /// was traced with. The per-frame constants are built before the GI passes,
    /// so deferred lighting samples last frame's buffer; on the frame the grid
    /// recentres, `GetGridOrigin()` is already the new origin while that buffer
    /// still holds the old grid — indexing it with the new origin shifts the
    /// whole indirect lighting by one cell for a frame. Consumers of the SH
    /// buffer must use this origin.
    [[nodiscard]] Vec3 GetCompletedGridOrigin() const noexcept { return m_completedGridOrigin; }

    /// Returns the half-extent of the probe grid.
    [[nodiscard]] float GetGridExtent() const noexcept { return m_gridExtent; }

    /// Returns the spacing between adjacent probes in world units (cm).
    [[nodiscard]] float GetProbeSpacing() const noexcept { return m_probeSpacing; }

    /// Returns the grid center (camera-tracked position).
    [[nodiscard]] Vec3 GetGridCenter() const noexcept { return m_gridCenter; }

    /// Returns true if resources have been created and are valid.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// `m_dirty` (grid recentred this frame) deliberately does NOT gate
    /// consumers: last frame's completed buffer stays valid for the recentre
    /// frame (indexed with `GetCompletedGridOrigin()`), and the recentre itself
    /// retraces every probe this frame while *keeping* the history of the probes
    /// that stayed inside the grid (`m_pendingHistoryShift`). Gating on it
    /// published `radianceCacheEnabled = 0` for the recentre frame, which also
    /// zeroed `sdfDiffuseSamples`/`sdfReflectionsEnabled`, dropped the world
    /// gather and reset its history — every ~probeSpacing (267 cm on Sponza) of
    /// camera travel the whole indirect lighting fell to the sky-SH fallback for
    /// one frame: "the whole scene flickers while flying".
    /// Diagnostic (followup-12): when set, `UpdateProbes` keeps the last converged SH buffer and
    /// skips the trace, so the diffuse GI is held still *without* falling back to another path.
    /// Skipping the whole call instead would leave `HasProducedResult()` false and silently switch
    /// the consumer to the sky-SH fallback, which is a different lighting path, not a frozen one.
    void SetFrozen(bool frozen) noexcept { m_frozen = frozen; }
    [[nodiscard]] bool IsFrozen() const noexcept { return m_frozen; }

    [[nodiscard]] bool HasProducedResult() const noexcept {
        return m_initialized && !m_firstWritePending;
    }
    /// The scene under the probes changed (GDF revision): the next UpdateProbes
    /// retraces every probe and replaces its history instead of blending.
    void InvalidateHistory() noexcept { m_dirty = true; m_historyInvalid = true; }
    /// A world box where the distance field changed (block B.2).
    struct FieldRegion { Vec3 lo; Vec3 hi; };
    /// The field changed inside `boxes` (block B.2): retrace every probe next UpdateProbes, keep
    /// the history everywhere except inside the boxes, where the probes replace it. More boxes
    /// than kMaxFieldRegions pending at once fall back to a full invalidation.
    void RetraceKeepingHistory(const FieldRegion* boxes, size_t count) {
        m_dirty = true;
        if (m_pendingRegions.size() + count > kMaxFieldRegions) {
            m_historyInvalid = true;
            m_pendingRegions.clear();
            return;
        }
        m_pendingRegions.insert(m_pendingRegions.end(), boxes, boxes + count);
    }
    /// True between a grid recentre / invalidation and the UpdateProbes that consumes it.
    [[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }
    /// True when InvalidateHistory() was called since the last UpdateProbes (scene changed).
    [[nodiscard]] bool IsHistoryInvalid() const noexcept { return m_historyInvalid; }
    /// Grid shift (whole probes) accumulated by Update() since the last UpdateProbes; non-zero = recentre.
    [[nodiscard]] const int32_t* GetPendingHistoryShift() const noexcept { return m_pendingHistoryShift; }

private:
    void QueueDiagnosticReadback(IRHICommandList* cmdList, uint32_t frameIndex);
    void ReadDiagnosticResults(uint32_t frameIndex);
    void DispatchProbeSide(IRHICommandList* cmdList, const GlobalDistanceField& gdf,
                           RHIPipelineHandle sidePipeline, CBRingAllocator* cbRing);
    /// Mode 3: air grid, flood fill from the camera, per-probe decision into the probe side buffer.
    /// Binds the trace constants (`traceConstants*`, already uploaded) at b2. False when the passes
    /// could not run; the caller then keeps the current rule for this frame.
    bool DispatchProbeReach(IRHICommandList* cmdList, RHIPipelineHandle reachPipeline,
                            RHIBufferHandle traceConstants, uint64_t traceConstantsOffset,
                            const void* traceConstantsCpu, uint32_t traceConstantsSize);

    IRHIDevice* m_device = nullptr;

    /// StructuredBuffer<float4> — kTotalProbes * kSHFloat4PerProbe float4 elements.
    /// Total: 4096 * 7 * 16 = 458,752 bytes (~449 KB).
    RHIBufferHandle m_shBuffer;
    uint32_t        m_shBufferSrv = UINT32_MAX;
    uint32_t        m_shBufferUav = UINT32_MAX;
    RHIBufferHandle m_historyShBuffer;
    uint32_t        m_historyShBufferSrv = UINT32_MAX;
    uint32_t        m_historyShBufferUav = UINT32_MAX;
    RHIResourceState m_shBufferState = RHIResourceState::Common;
    RHIResourceState m_historyShBufferState = RHIResourceState::Common;
    RHIBufferHandle m_diagnosticReadback;
    RHIBufferHandle m_diagnosticStats;
    uint32_t m_diagnosticStatsUav = UINT32_MAX;
    RHIResourceState m_diagnosticStatsState = RHIResourceState::Common;
    uint32_t m_diagnosticCopyFrame = UINT32_MAX;
    bool m_diagnosticComplete = false;
    Vec3 m_diagnosticOrigin;
    float m_diagnosticSpacing = 0.0f;

    /// Per probe, two float4 (`probe_side_cs.hlsl`): nearest face (direction to the probe, distance)
    /// and the fixed rays (back-face fraction, hits, side of the nearest face). Recomputed when the
    /// grid moves or the instances the field was built from change.
    RHIBufferHandle m_probeSideBuffer;
    uint32_t m_probeSideSrv = UINT32_MAX;
    uint32_t m_probeSideUav = UINT32_MAX;
    RHIResourceState m_probeSideState = RHIResourceState::Common;
    bool     m_probeSideValid = false;
    uint64_t m_probeSideHash = 0;
    Vec3     m_probeSideOrigin;
    bool     m_probeSideTooManyLogged = false;
    uint32_t m_probeSideMode = 0;   ///< Classification mode whose layout the side buffer holds.

    /// Mode 3 (`probe_reach_cs.hlsl`): one uint per voxel of a 128^3 grid over the probe grid
    /// (0 solid, 1 air, 2 reached from the camera) and a seed counter. Created on first use (8 MiB).
    static constexpr uint32_t kReachResolution = 128;
    RHIBufferHandle m_reachAirBuffer;
    uint32_t m_reachAirUav = UINT32_MAX;
    RHIBufferHandle m_reachStatsBuffer;
    uint32_t m_reachStatsUav = UINT32_MAX;
    RHIResourceState m_reachAirState = RHIResourceState::Common;
    RHIResourceState m_reachStatsState = RHIResourceState::Common;
    /// Camera position of the last Update() — the flood fill's seed.
    Vec3 m_cameraPos;
    /// G-buffer world position the visible-surface seeds read (`r.GI.RadianceCache.ReachVisibleSeeds`).
    uint32_t m_seedPositionSrv = UINT32_MAX;
    uint32_t m_seedWidth = 0, m_seedHeight = 0;
    /// Diagnostic (`r.GI.RadianceCache.ReachDump`): the air grid copied once at frame >= 120 and
    /// logged four frames later, one line per (z, y) row.
    RHIBufferHandle m_reachReadback;
    uint32_t m_reachDumpFrame = UINT32_MAX;
    bool m_reachDumped = false;
    Vec3 m_reachDumpGridOrigin;
    void HandleReachDump(IRHICommandList* cmdList, uint32_t frameIndex);

    /// World-space grid origin (minimum corner = center - extent).
    Vec3   m_gridOrigin;

    /// World-space grid center (tracks camera).
    Vec3   m_gridCenter;

    /// Half-size of the probe grid in world units (cm).
    float  m_gridExtent   = 5000.0f;

    /// Distance between adjacent probes in world units (cm).
    float  m_probeSpacing = 0.0f;

    /// Monotonic counter for temporal variation.
    uint32_t m_frameCounter = 0;
    uint32_t m_probeUpdateCursor = 0;  ///< Round-robin offset for temporal probe spreading.

    bool   m_initialized = false;
    bool   m_dirty       = true;       ///< Retrace every probe next UpdateProbes (recentre or invalidation).
    bool   m_firstWritePending = true; ///< Set at CreateResources; consumed by the first UpdateProbes.
    bool   m_frozen = false;           ///< Diagnostic: hold the converged probes (SetFrozen).
    bool   m_historyInvalid    = false; ///< Set by InvalidateHistory(); the next trace replaces instead of blends.

    /// Grid shift in whole probes (new − old) accumulated by Update() since the
    /// last UpdateProbes(). The trace shader reads probe c's history at
    /// c + shift, so a recentre keeps the temporal history of the 15/16 probes
    /// that stayed inside the grid; only the slab that entered starts from
    /// scratch. Without it every recentre was a first write: no history, no
    /// multi-bounce feedback for that frame — a dark blink that took ~20 frames
    /// (alpha 0.12) to recover, once per 267 cm of travel on any axis.
    int32_t m_pendingHistoryShift[3] = {0, 0, 0};
    /// Change boxes accumulated by RetraceKeepingHistory() until UpdateProbes consumes them.
    std::vector<FieldRegion> m_pendingRegions;

    /// Grid origin the last completed SH buffer was traced with (see
    /// GetCompletedGridOrigin()).
    Vec3   m_completedGridOrigin;
};

// ─────────────────────────────────────────────────────────────────────────────
// Radiance Cache Shader Constants — CPU mirror of compute shader cbuffer
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the radiance cache trace compute shader.
struct RadianceCacheTraceConstants {
    float    gridOriginX;        ///< Grid origin X (minimum corner).
    float    gridOriginY;        ///< Grid origin Y.
    float    gridOriginZ;        ///< Grid origin Z.
    float    probeSpacing;       ///< Distance between probes (cm).

    uint32_t probesPerAxis;      ///< Probes per axis (16).
    uint32_t totalProbes;        ///< Total probes (4096).
    uint32_t raysPerProbe;       ///< Rays per probe per frame (32).
    uint32_t frameIndex;         ///< Frame counter for temporal jitter.

    float    maxTraceDistance;    ///< Maximum ray trace distance (cm).
    float    temporalBlendAlpha; ///< EMA blend factor (0.05).
    uint32_t shBufferUav;        ///< UAV index for SH buffer read/write.
    uint32_t firstWrite;         ///< 1 = write SH without temporal blend (buffer never written).

    // GDF clipmap parameters for the trace shader
    float    gdfCenter0X, gdfCenter0Y, gdfCenter0Z, gdfExtent0;
    float    gdfCenter1X, gdfCenter1Y, gdfCenter1Z, gdfExtent1;
    float    gdfCenter2X, gdfCenter2Y, gdfCenter2Z, gdfExtent2;
    float    gdfCenter3X, gdfCenter3Y, gdfCenter3Z, gdfExtent3;

    uint32_t gdfClipmapSrv0;     ///< SRV indices for GDF clipmap textures.
    uint32_t gdfClipmapSrv1;
    uint32_t gdfClipmapSrv2;
    uint32_t gdfClipmapSrv3;

    uint32_t numGdfClipmaps;     ///< Number of active GDF clipmaps (1-4).
    float    skyColorR;          ///< Sky color for rays that miss all geometry.
    float    skyColorG;
    float    skyColorB;

    // Sun/light parameters for single-bounce GI at hit points
    float    lightDirX;          ///< Primary directional light direction X (toward light).
    float    lightDirY;
    float    lightDirZ;
    float    lightIntensity;     ///< Light color intensity (pre-multiplied RGB luminance).
    float    lightColorR;        ///< Primary light color R.
    float    lightColorG;
    float    lightColorB;
    float    groundAlbedo;       ///< Estimated average scene albedo for bounce (0.18 typical).

    // ── VFX → DDGI emissive injection (phase16 hardware-validation) ───────────
    // Bindless SRV index of the `VfxDDGIEmissiveFeed` StructuredBuffer plus the
    // number of valid samples in it. Both default to UINT32_MAX / 0 so the
    // in-shader gate (`vfxEmissiveSampleCount > 0`) short-circuits when:
    //   * the `r.VFX.DDGIInjection` CVar is off (the feed Resets to 0), or
    //   * no emitters were alive this frame, or
    //   * the host hasn't built a feed (e.g. the radiance-cache trace shader
    //     which shares this CB layout but never reads the VFX fields).
    //
    // Two trailing floats keep the struct on a 16-byte boundary so DX12 root
    // CBV reads and HLSL cbuffer packing stay in lock-step.
    uint32_t vfxEmissiveSrvIndex   = UINT32_MAX; ///< Bindless SRV of the VFX emissive feed buffer.
    uint32_t vfxEmissiveSampleCount = 0;          ///< Valid sample count in the buffer.
    float    regionBlendAlpha      = 1.0f;        ///< Blend weight of a probe inside a change box (block B.2 phase 4): 1 = replace its history. Was a pad; the DDGI writer ignores it.
    float    _vfxEmissivePad1      = 0.0f;        ///< 16-byte alignment pad.

    // ── Grid recentre (radiance-cache trace only) ─────────────────────────────
    // Shift of the probe grid since the history buffer was written, in whole
    // probes: history coordinate = probe coordinate + shift. Zero unless the
    // grid recentred this frame. The DDGI writer mirrors the row and ignores it.
    int32_t  historyShiftX = 0;
    int32_t  historyShiftY = 0;
    int32_t  historyShiftZ = 0;
    uint32_t regionCount = 0;         ///< Change boxes below; probes inside replace their history.
    /// World boxes where the distance field changed this frame (lo.xyz, pad / hi.xyz, pad). The
    /// DDGI writer and probe_reach declare the 13 rows before them and ignore these.
    float    regionLo[kMaxFieldRegions][4] = {};
    float    regionHi[kMaxFieldRegions][4] = {};
};

// 176 bytes of shared trace constants, the 16-byte VFX emissive tail and the
// 16-byte grid-recentre row. Every shader that binds this CB —
// `radiance_cache_trace_cs.hlsl` and `ddgi_probe_update_cs.hlsl` — must declare
// all 13 four-field rows, scalars only, or HLSL register packing shifts every
// field past the mismatch.
static_assert(sizeof(RadianceCacheTraceConstants) == 464, "RadianceCacheTraceConstants must be 464 bytes (13 shared rows + 16 change-box rows)");
static_assert(offsetof(RadianceCacheTraceConstants, regionCount) == 204);
static_assert(offsetof(RadianceCacheTraceConstants, regionLo) == 208);

} // namespace uz
