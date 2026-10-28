/// UzEngine Surface Radiance Cache — the CPU side of the world-space spatial-hash cache of outgoing
/// diffuse radiance at ray hits (Lumen-parity Phase D, stage 1; SHaRC-style).
///
/// Owns the hash table (`keys` / `accum` / `resolved` / `stats`), the per-frame binding record
/// the consumers read (`SurfaceCacheParamsGPU`, one element of an upload-heap structured buffer
/// ringed over the frames in flight), the resolve dispatch (`surface_cache_resolve_cs.hlsl`) and
/// the invalidation policy of the spec's section 2.6: distance-field change boxes evict the cells
/// inside them, a scene-revision / geometry / lighting change clears the table.
///
/// Opt-in (`r.GI.SurfaceCache`, default 0): with the switch off nothing here is allocated, no pass
/// is registered and no consumer binds the record — the frame is byte-identical to the frame
/// before this class existed. Every number is a `r.GI.SurfaceCache.*` CVar read at execution
/// time. Only the hardware ray-traced reflection pass reads and writes the cache in stages 1-2
/// (`RTReflectionRootConstants::surfaceCacheParamsSrv`, `kRTReflectionFlagSurfaceCache`).
///
/// Stage 2 (lighting completeness at the hit): the frame's point / spot / rect lights are uploaded
/// as one `StructuredBuffer<SurfaceCacheLight>` per frame in flight (`SurfaceCacheBuildLights`, the
/// `rectLightsSrv` precedent) and named by the binding record together with K
/// (`r.GI.SurfaceCache.LocalLights`); a shaded hit adds its K strongest lights with a shadow ray
/// each to the cell's `sun` layer and the mesh's emission (`GIMeshMaterialGPU::emissive*`,
/// `r.GI.SurfaceCache.Emissive`) to its `other` layer.
///
/// Stage 3 (the SDF consumers and the probes read exact-material radiance): the binding record
/// carries the camera position (the level rule D3 for the consumers that bind no PerFrameCB) and
/// the stage-3 switches (`.SdfReads`, `.SdfWrites`, `.SdfLodBias`); the resolve runs before the
/// probe writers; the feed trace (`surface_cache_feed_cs.hlsl`, `.FeedRays` rays per radiance-cache
/// probe on the TLAS, exact material) fills the SDF consumers' coarser cells; `ShadeSdfReflectionHit`
/// (final gather, ReSTIR GI, deferred SDF reflections), the radiance-cache trace (the SH writer)
/// and the DDGI trace (`.DDGI`) read the cache through the SSGI root DWORDs, the LightingCB pad and
/// their own root DWORDs.
///
/// Stage 4 (the surface record, the scheduled relight and the cell-indirect gather): every TLAS
/// writer records its hit (position, normal, albedo, emission) in a 32-byte surface record beside
/// the cell (`surfaceUav`); the relight pass (`surface_cache_relight_cs.hlsl`, `.Relight`,
/// `.RelightSlices`) re-shades the direct layer of one slice of the live cells per frame from the
/// records, so a lighting change — which bumps `lightingGeneration` instead of clearing the table
/// — reaches every cell within `RelightSlices` frames with zero evictions; the cell-indirect pass
/// (`surface_cache_indirect_cs.hlsl`, `.MultiBounce`, `.IndirectRays`, `.IndirectFrames`) gathers
/// each cell's indirect light with finite-depth Jacobi iterations into the `other` layer.
/// The drained accumulator is temporary previous-iteration/previous-frame storage, restored
/// before consumers. No additional allocation; table storage remains 88 MiB at 2^20.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.
///
/// @ref docs/specs/rendering/surface-radiance-cache.md — sections 1.5, 2, 3.3, 4 (stage 1)
/// @ref NVIDIA SHaRC (RTXGI 2.0, 2024) — spatially hashed radiance cache

#pragma once

#include "rhi_types.h"
#include "vector3.h"
#include "gi/global_distance_field.h"   // GlobalDistanceField::ChangedRegion

#include <cstdint>
#include <vector>

namespace uz {

namespace rg { class RgStateRegistry; }

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class PipelineLibrary;
struct LightingCB;
struct RectLightData;

/// The binding record every consumer reads (one element of a StructuredBuffer): the C++ mirror of
/// `SurfaceCacheParams` in `gi/surface_cache_contract.hlsli`. 96 bytes (stage 2 appended the
/// lights row, stage 3 the camera row); the layout test (scripts/rendering/test_cbuffer_layouts.py,
/// Tier C) and the gtest pin the two equal.
struct SurfaceCacheParamsGPU {
    uint32_t keysUav     = UINT32_MAX;   ///< [0]  RWStructuredBuffer<uint2> keys.
    uint32_t accumUav    = UINT32_MAX;   ///< [1]  RWStructuredBuffer<uint> accumulators (8 per entry).
    uint32_t resolvedSrv = UINT32_MAX;   ///< [2]  StructuredBuffer<uint4> resolved records.
    uint32_t surfaceUav  = UINT32_MAX;   ///< [3]  Stage 4 surface record (sentinel before stage 4).
    uint32_t entriesLog2 = 20;           ///< [4]
    uint32_t minSamples  = 8;            ///< [5]  D6 validity threshold.
    uint32_t flags       = 0;            ///< [6]  surfacecache::kSurfaceCacheFlag* bits.
    uint32_t frame       = 0;            ///< [7]
    float    cellSize    = 10.0f;        ///< [8]  Level-0 cell, cm.
    float    lodDistance = 400.0f;       ///< [9]  Camera distance at which the cell first doubles, cm.
    float    maxRadiance = 4096.0f;      ///< [10] Per-sample clamp.
    uint32_t indirectHorizon = 8;        ///< [11] Stage 4: visits of the cell-indirect EMA (r.GI.SurfaceCache.IndirectFrames).
    uint32_t sdfLodBias  = 2;            ///< [12]
    uint32_t statsUav    = UINT32_MAX;   ///< [13] RWStructuredBuffer<uint> stats (sentinel = none).
    uint32_t generation  = 0;            ///< [14] Incremented by every clear.
    uint32_t refreshRate = 16;           ///< [15] One hit in refreshRate shades inline and updates its cell (0 = never).
    uint32_t lightsSrv   = UINT32_MAX;   ///< [16] Stage 2: StructuredBuffer<SurfaceCacheLight> of the frame (sentinel = none).
    uint32_t lightCount  = 0;            ///< [17] Records in it (<= surfacecache::kSurfaceCacheMaxLights).
    uint32_t localLights = 0;            ///< [18] K: lights with a shadow ray per shaded hit (0 = none).
    uint32_t lightingGeneration = 0;     ///< [19] Stage 4: bumped by every lighting change while the relight runs (u16; records behind it are relit).
    float    cameraPosX  = 0.0f;         ///< [20] Stage 3: the frame's camera position (the level rule D3 for every consumer).
    float    cameraPosY  = 0.0f;         ///< [21]
    float    cameraPosZ  = 0.0f;         ///< [22]
    float    _pad2       = 0.0f;         ///< [23]
};
static_assert(sizeof(SurfaceCacheParamsGPU) == 96, "SurfaceCacheParamsGPU mirrors SurfaceCacheParams (96 bytes)");

/// One local light of the frame as the cell lighting shades it: the C++ mirror of
/// `SurfaceCacheLight` (gi/surface_cache_contract.hlsli), 80 bytes. Built by
/// `SurfaceCacheBuildLights` from the frame's `LightingCB` (point / spot) and rect lights with the
/// receiver-independent factors folded in (see the shader struct's comment).
struct SurfaceCacheLightGPU {
    float    positionX = 0.0f, positionY = 0.0f, positionZ = 0.0f;   ///< [0..2]
    float    radius = 0.0f;                                           ///< [3]  point / spot: attenuation radius; rect: range.
    float    colorR = 0.0f, colorG = 0.0f, colorB = 0.0f;             ///< [4..6] colour x intensity x diffuseScale (rect: radiance x diffuseScale).
    uint32_t type = 0;                                                ///< [7]  surfacecache::kSurfaceCacheLightType* | flags.
    float    directionX = 0.0f, directionY = 0.0f, directionZ = 0.0f; ///< [8..10] spot cone axis / rect emission normal (unit).
    float    cosOuterAngle = -1.0f;                                   ///< [11] spot.
    float    rightX = 0.0f, rightY = 0.0f, rightZ = 0.0f;             ///< [12..14] rect: right x halfWidth.
    float    invCosConeDifference = 0.0f;                             ///< [15] spot.
    float    upX = 0.0f, upY = 0.0f, upZ = 0.0f;                      ///< [16..18] rect: up x halfHeight.
    float    falloffExponent = 0.0f;                                  ///< [19] point / spot custom falloff.
};
static_assert(sizeof(SurfaceCacheLightGPU) == 80, "SurfaceCacheLightGPU mirrors SurfaceCacheLight (80 bytes)");

/// Fills `out` (capacity records) with the frame's lights in the order the shader scans them: the
/// rect lights first (few, usually the strongest), then the LightingCB's point lights, then its
/// spot lights, each with its diffuse scale folded into the colour and the rect's half extents
/// into its axes. Lights that cannot contribute (zero colour or scale, no range) are skipped.
/// `lighting` / `rectLights` may be null (none of that kind). Returns the record count.
[[nodiscard]] uint32_t SurfaceCacheBuildLights(SurfaceCacheLightGPU* out, uint32_t capacity, const LightingCB* lighting,
                                               const RectLightData* rectLights, uint32_t rectLightCount) noexcept;

/// Root constants at b0 of `surface_cache_feed_cs.hlsl` (stage 3; 5 DWORDs of the 20-DWORD budget).
struct SurfaceCacheFeedRootConstants {
    uint32_t tlasSrv               = UINT32_MAX;   ///< [0] Scene TLAS.
    uint32_t materialTableSrv      = UINT32_MAX;   ///< [1] GIMeshMaterial table by TLAS InstanceID (sentinel = GDF attributes).
    uint32_t probeShSrv            = UINT32_MAX;   ///< [2] The completed radiance-cache SH buffer (sentinel = no probe term).
    uint32_t surfaceCacheParamsSrv = UINT32_MAX;   ///< [3] StructuredBuffer<SurfaceCacheParams> (1 element).
    uint32_t feedRays              = 0;            ///< [4] Rays per probe this frame.
};
static constexpr uint32_t kSurfaceCacheFeedRootDwords = sizeof(SurfaceCacheFeedRootConstants) / sizeof(uint32_t);
static_assert(sizeof(SurfaceCacheFeedRootConstants) == 20, "SurfaceCacheFeedRootConstants is 5 DWORDs");
static_assert(kSurfaceCacheFeedRootDwords <= 20, "bindless root constants are limited to 20 DWORDs");

/// Root constants at b0 of `surface_cache_relight_cs.hlsl` AND `surface_cache_indirect_cs.hlsl`
/// (stage 4; 12 DWORDs of the 20-DWORD budget, one block for the two passes).
struct SurfaceCacheRelightRootConstants {
    uint32_t tlasSrv               = UINT32_MAX;   ///< [0] Scene TLAS.
    uint32_t materialTableSrv      = UINT32_MAX;   ///< [1] GIMeshMaterial table by TLAS InstanceID (the indirect pass's cold hits).
    uint32_t probeShSrv            = UINT32_MAX;   ///< [2] The completed radiance-cache SH buffer (the indirect pass's cold hits); sentinel = none.
    uint32_t surfaceCacheParamsSrv = UINT32_MAX;   ///< [3] StructuredBuffer<SurfaceCacheParams> (1 element).
    uint32_t resolvedUav           = UINT32_MAX;   ///< [4] RWStructuredBuffer<uint4> resolved records.
    uint32_t sliceIndex            = 0;            ///< [5] frame mod sliceCount.
    uint32_t sliceCount            = 16;           ///< [6] r.GI.SurfaceCache.RelightSlices.
    uint32_t indirectRays          = 0;            ///< [7] r.GI.SurfaceCache.IndirectRays (0 in the relight dispatch).
    uint32_t skyCubemapSrv         = UINT32_MAX;   ///< [8] Sky for the indirect pass's escaping rays (sentinel = flat skylight).
    uint32_t transportMode         = 0;            ///< [9] Snapshot base / gather / snapshot next / finish.
    uint32_t bounceIndex           = 0;            ///< [10] Completed indirect iterations before this dispatch.
    uint32_t bounces               = 4;            ///< [11] Surface vertices, including the cached surface.
};
static constexpr uint32_t kSurfaceCacheRelightRootDwords = sizeof(SurfaceCacheRelightRootConstants) / sizeof(uint32_t);
static_assert(sizeof(SurfaceCacheRelightRootConstants) == 48, "SurfaceCacheRelightRootConstants is 12 DWORDs");
static_assert(kSurfaceCacheRelightRootDwords <= 20, "bindless root constants are limited to 20 DWORDs");

/// Root constants at b0 of `surface_cache_resolve_cs.hlsl` (12 DWORDs of the 20-DWORD budget).
struct SurfaceCacheResolveRootConstants {
    uint32_t keysUav     = UINT32_MAX;   ///< [0]
    uint32_t accumUav    = UINT32_MAX;   ///< [1]
    uint32_t resolvedUav = UINT32_MAX;   ///< [2]
    uint32_t statsUav    = UINT32_MAX;   ///< [3]
    uint32_t entriesLog2 = 20;           ///< [4]
    uint32_t accumFrames = 32;           ///< [5]  EMA horizon in frames.
    uint32_t staleFrames = 64;           ///< [6]  Eviction age in frames.
    uint32_t mode        = 0;            ///< [7]  surfacecache::kSurfaceCacheResolveMode* | kSurfaceCacheResolveFlag* bits.
    float    cellSize    = 10.0f;        ///< [8]
    uint32_t regionCount = 0;            ///< [9]  Change boxes in the b2 block.
    uint32_t frame       = 0;            ///< [10]
    uint32_t surfaceUav  = UINT32_MAX;   ///< [11] Stage 4: RWStructuredBuffer<uint> surface records (sentinel = none).
};
static constexpr uint32_t kSurfaceCacheResolveRootDwords = sizeof(SurfaceCacheResolveRootConstants) / sizeof(uint32_t);
static_assert(sizeof(SurfaceCacheResolveRootConstants) == 48, "SurfaceCacheResolveRootConstants is 12 DWORDs");
static_assert(kSurfaceCacheResolveRootDwords <= 20, "bindless root constants are limited to 20 DWORDs");

/// The change boxes at b2 of the resolve (the SSGI `HistoryCB` region rows): at most 16 boxes.
struct SurfaceCacheResolveRegionsCB {
    float regionLo[16][4];
    float regionHi[16][4];
};
static_assert(sizeof(SurfaceCacheResolveRegionsCB) == 512, "SurfaceCacheResolveRegionsCB mirrors surface_cache_resolve_cs.hlsl");

/// FNV-1a 64 over a byte range (the geometry and lighting hashes of the invalidation policy).
[[nodiscard]] uint64_t SurfaceCacheHashBytes(const void* data, size_t bytes, uint64_t hash = 14695981039346656037ull) noexcept;

/// The invalidation decision of one frame (spec section 2.6), as a pure function so the gtest can
/// pin it: a scene-revision bump clears; otherwise change boxes evict (the resolve pass handles
/// them); a geometry-hash change with no boxes clears (the TLAS moved without the field noticing,
/// or the field is off); a lighting change clears (stages 1-3: the cells hold lit radiance) —
/// unless the stage 4 relight runs (`relightEnabled`): then it only bumps the lighting generation
/// (`relight`) and the relight pass catches the cells up, nothing is evicted.
struct SurfaceCacheInvalidation {
    bool     clear = false;        ///< Whole table cleared this frame.
    uint32_t regionCount = 0;      ///< Change boxes passed to the resolve (0 when clearing).
    bool     relight = false;      ///< Stage 4: the lighting changed and the generation is bumped instead of a clear.
};
[[nodiscard]] SurfaceCacheInvalidation SurfaceCacheDecideInvalidation(
    bool sceneRevisionChanged, uint32_t changedRegionCount, bool geometryHashChanged,
    bool lightingHashChanged, bool relightEnabled = false) noexcept;

class SurfaceRadianceCache {
public:
    SurfaceRadianceCache() = default;
    ~SurfaceRadianceCache();

    SurfaceRadianceCache(const SurfaceRadianceCache&) = delete;
    SurfaceRadianceCache& operator=(const SurfaceRadianceCache&) = delete;
    SurfaceRadianceCache(SurfaceRadianceCache&&) noexcept = default;
    SurfaceRadianceCache& operator=(SurfaceRadianceCache&&) noexcept = default;

    /// `r.GI.SurfaceCache` (read every frame; the default is off).
    [[nodiscard]] static bool Enabled();
    /// `r.GI.SurfaceCache.Stats`: readback + a log line every 60 frames.
    [[nodiscard]] static bool StatsEnabled();
    /// `r.GI.SurfaceCache.LocalLights` (K, clamped to [0, kSurfaceCacheMaxLocalLights]) and
    /// `r.GI.SurfaceCache.Emissive` (stage 2), read every frame.
    [[nodiscard]] static uint32_t LocalLights();
    [[nodiscard]] static bool EmissiveEnabled();
    /// Stage 3 switches, read every frame: `r.GI.SurfaceCache.SdfReads` (the SDF consumers read),
    /// `.SdfWrites` (they write, diagnostic), `.SdfLodBias` (their coarser levels), `.DDGI` (the
    /// DDGI trace reads / writes) and `.FeedRays` (rays per probe of the feed trace, 0 = no feed).
    [[nodiscard]] static bool SdfReadsEnabled();
    [[nodiscard]] static bool SdfWritesEnabled();
    [[nodiscard]] static uint32_t SdfLodBias();
    [[nodiscard]] static bool DdgiEnabled();
    [[nodiscard]] static uint32_t FeedRays();
    /// True when any SDF consumer binds the record this frame (reads or writes on).
    [[nodiscard]] static bool SdfConsumersBound() { return SdfReadsEnabled() || SdfWritesEnabled(); }
    /// Stage 4 switches, read every frame: `r.GI.SurfaceCache.Relight` (the scheduled relight),
    /// `.RelightSlices` (frames to relight every live cell once, clamped to [1, 64]), `.MultiBounce`
    /// (the cell-indirect gather), `.IndirectRays` (cosine rays per cell per visit, [0, 16]; 0 = no
    /// gather) and `.IndirectFrames` (visits of the gather's EMA, [1, 64]).
    [[nodiscard]] static bool RelightEnabled();
    [[nodiscard]] static uint32_t RelightSlices();
    [[nodiscard]] static bool MultiBounceEnabled();
    [[nodiscard]] static uint32_t IndirectRays();
    [[nodiscard]] static uint32_t IndirectFrames();
    [[nodiscard]] static uint32_t Bounces();
    /// True when the cell-indirect pass runs this frame (`.MultiBounce` with rays).
    [[nodiscard]] static bool IndirectActive() { return MultiBounceEnabled() && IndirectRays() > 0u; }
    /// True when the surface record is bound this frame (either stage 4 pass runs).
    [[nodiscard]] static bool SurfaceRecordActive() { return RelightEnabled() || IndirectActive(); }

    void SetStateRegistry(rg::RgStateRegistry* states) noexcept { m_rgStates = states; }

    /// True once the table exists (created by the first `Resolve` with the switch on). Kept until
    /// `DestroyResources` even if the switch is turned off again: the device destroys buffers
    /// immediately and the previous frame may still read them.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// Destroys every buffer (D3D12MA asserts on leaked allocations at exit).
    void DestroyResources();

    /// Marks the table for a clear on the next `Resolve`.
    void InvalidateAll() noexcept { m_clearPending = true; }

    /// What the per-frame resolve needs from the frame.
    struct FrameInputs {
        IRHIDevice*      device = nullptr;
        CBRingAllocator* cbRing = nullptr;
        PipelineLibrary* pipelines = nullptr;
        uint32_t         frameIndex = 0;
        /// This frame's distance-field change boxes (read at execution, after the GDF composite)
        /// and the field's revision; null / 0 when the field is off.
        const std::vector<GlobalDistanceField::ChangedRegion>* changedRegions = nullptr;
        uint64_t         sceneRevision = 0;
        /// FNV-1a of the frame's GI-occluder instances (mesh + transform) and of its lights.
        uint64_t         geometryHash = 0;
        uint64_t         lightingHash = 0;
        /// Stage 2: the frame's local lights (the LightingCB of the frame for point / spot, the
        /// rect lights of `LightFrameData`); null / 0 = none of that kind.
        const LightingCB*    lighting = nullptr;
        const RectLightData* rectLights = nullptr;
        uint32_t             rectLightCount = 0;
        /// Stage 3: the frame's camera position (PerFrameCB::cameraPos), the level rule's reference
        /// for every consumer through the record.
        Vec3                 cameraPos{};
    };

    /// The resolve pass's body: creates the table on first use, applies the invalidation policy,
    /// writes this frame's binding record, folds last frame's accumulators (one dispatch over
    /// every entry) and leaves `keys` / `accum` in UnorderedAccess and `resolved` in
    /// ShaderResource for the consumers. Returns false (and binds nothing) when the table could
    /// not be created or the pipeline is unavailable.
    bool Resolve(IRHICommandList* cmd, const FrameInputs& in);

    /// The binding record SRV of frame `frameIndex` (UINT32_MAX unless `Resolve` ran this frame).
    [[nodiscard]] uint32_t GetParamsSrv(uint32_t frameIndex) const noexcept {
        return (m_initialized && m_resolvedFrame == frameIndex) ? m_paramsSrv[frameIndex % kParamsRingFrames] : UINT32_MAX;
    }

    /// Before a consumer dispatch: UAV barriers on the buffers the resolve wrote (they stay in
    /// UnorderedAccess between the dispatches, so the barrier is what orders the writes).
    void PrepareConsumer(IRHICommandList* cmd);

    /// Stage 4: order each relight/snapshot/gather/finish dispatch with UAV barriers.
    /// Gather reads the immutable accumulator snapshot, not the resolved SRV.
    /// `PrepareConsumer` returns resolved to ShaderResource after scratch has been cleared.
    void PrepareRelight(IRHICommandList* cmd);
    /// The relight / indirect passes' bindings of the frame the resolve ran (sentinels otherwise).
    [[nodiscard]] uint32_t GetResolvedUav(uint32_t frameIndex) const noexcept {
        return (m_initialized && m_resolvedFrame == frameIndex) ? m_resolvedUav : UINT32_MAX;
    }
    [[nodiscard]] uint32_t GetSurfaceUav() const noexcept { return m_initialized ? m_surfaceUav : UINT32_MAX; }
    /// The lighting generation the relight pass compares the records against (u16, wraps).
    [[nodiscard]] uint32_t GetLightingGeneration() const noexcept { return m_lightingGeneration; }
    /// This frame's slice of the relight / cell-indirect schedule (the contract's `SurfaceCacheSlice*`
    /// math over the table: frame mod `count` selects ceil(entries / count) consecutive entries, so
    /// every entry is visited once in `count` frames); `groups` x 64 threads cover it (0 before the
    /// table exists).
    struct RelightSlice { uint32_t index = 0, count = 1, groups = 0; };
    [[nodiscard]] RelightSlice GetRelightSlice(uint32_t frameIndex) const noexcept;

    /// Last stats block read back (`.Stats`), for the log and the tests of the harness.
    struct Stats {
        uint32_t inserts = 0, failedInserts = 0, updates = 0, queries = 0, hits = 0, evictions = 0, live = 0, maxProbe = 0;
        uint32_t tombstones = 0, torn = 0, probeSum = 0;
        uint32_t sdfQueries = 0, sdfHits = 0, feedUpdates = 0;   ///< Stage 3: the SDF consumers' lookups and their readable cells (C3), the feed's samples.
        uint32_t relit = 0, indirectRays = 0;                     ///< Stage 4: cells whose sun layer the relight replaced, cosine rays the gather traced.
        uint32_t radianceSum = 0, radianceCells = 0;              ///< Stage 4: the cached-radiance probe (luminance x kSurfaceCacheStatRadianceScale summed over the record cells, and their count).
        uint64_t totalInserts = 0, totalFailedInserts = 0;   ///< Since the table was created (every frame read).
        uint64_t totalEvictions = 0, totalRelit = 0;         ///< Stage 4: the same running sums (a lighting change must evict nothing).
        bool valid = false;
    };
    [[nodiscard]] const Stats& GetLastStats() const noexcept { return m_lastStats; }
    [[nodiscard]] uint32_t GetEntriesLog2() const noexcept { return m_entriesLog2; }
    [[nodiscard]] uint32_t GetGeneration() const noexcept { return m_generation; }

    /// Frames in flight the binding record is ringed over.
    static constexpr uint32_t kParamsRingFrames = 3;
    /// The readback copy recorded at frame N is mapped at frame N + kReadbackFrameLag (the frame
    /// fence covers N - 3 when frame N is recorded); the ring holds one more slot than the lag so
    /// a copy is read before its slot is reused.
    static constexpr uint32_t kReadbackFrameLag = 3;
    static constexpr uint32_t kReadbackRingFrames = kReadbackFrameLag + 1;

private:
    bool EnsureResources(IRHIDevice* device);
    void QueueStatsReadback(IRHICommandList* cmd, uint32_t frameIndex);
    void ReadStats(uint32_t frameIndex);

    IRHIDevice*          m_device = nullptr;
    rg::RgStateRegistry* m_rgStates = nullptr;

    RHIBufferHandle m_keys, m_accum, m_resolved, m_stats;
    RHIBufferHandle m_surface;                        ///< Stage 4: the surface records (kSurfaceCacheSurfaceStride uints per entry).
    uint32_t m_keysUav = UINT32_MAX, m_accumUav = UINT32_MAX;
    uint32_t m_resolvedUav = UINT32_MAX, m_resolvedSrv = UINT32_MAX;
    uint32_t m_statsUav = UINT32_MAX;
    uint32_t m_surfaceUav = UINT32_MAX;
    uint32_t m_lightingGeneration = 0;                ///< Stage 4: bumped per lighting change while the relight runs.

    RHIBufferHandle m_params[kParamsRingFrames];
    uint32_t        m_paramsSrv[kParamsRingFrames] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    RHIBufferHandle m_lights[kParamsRingFrames];     ///< Stage 2: the frame's light records (upload heap, ringed like the params).
    uint32_t        m_lightsSrv[kParamsRingFrames] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t        m_lastLightCount  = 0;           ///< Records uploaded by the last resolve (the stats line).
    uint32_t        m_lastLocalLights = 0;           ///< K bound by the last resolve (0 with no records).
    RHIBufferHandle m_readback[kReadbackRingFrames];
    uint32_t        m_readbackFrame[kReadbackRingFrames] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};

    uint32_t m_entriesLog2 = 20;
    uint32_t m_generation = 0;
    uint32_t m_resolvedFrame = UINT32_MAX;
    uint32_t m_statsValidFrom = 0;         ///< First frame whose stats block the resolve zeroed.
    uint64_t m_lastSceneRevision = 0;
    uint64_t m_lastGeometryHash = 0;
    uint64_t m_lastLightingHash = 0;
    float    m_lastCellSize = 0.0f;
    float    m_lastLodDistance = 0.0f;
    bool     m_initialized = false;
    bool     m_clearPending = true;
    bool     m_createFailed = false;
    bool     m_entriesChangeLogged = false;
    bool     m_lastRelight = false;
    bool     m_lastIndirect = false;
    uint32_t m_lastBounces = 4;
    Stats    m_lastStats;
};

} // namespace uz
