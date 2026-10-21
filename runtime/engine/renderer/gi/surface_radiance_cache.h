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
/// time. Only the hardware ray-traced reflection pass reads and writes the cache in stage 1
/// (`RTReflectionRootConstants::surfaceCacheParamsSrv`, `kRTReflectionFlagSurfaceCache`).
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

/// The binding record every consumer reads (one element of a StructuredBuffer): the C++ mirror of
/// `SurfaceCacheParams` in `gi/surface_cache_contract.hlsli`. 64 bytes; the layout test
/// (scripts/rendering/test_cbuffer_layouts.py, Tier C) and the gtest pin the two equal.
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
    float    _pad0       = 0.0f;         ///< [11]
    uint32_t sdfLodBias  = 2;            ///< [12]
    uint32_t statsUav    = UINT32_MAX;   ///< [13] RWStructuredBuffer<uint> stats (sentinel = none).
    uint32_t generation  = 0;            ///< [14] Incremented by every clear.
    uint32_t refreshRate = 16;           ///< [15] One hit in refreshRate shades inline and updates its cell (0 = never).
};
static_assert(sizeof(SurfaceCacheParamsGPU) == 64, "SurfaceCacheParamsGPU mirrors SurfaceCacheParams (64 bytes)");

/// Root constants at b0 of `surface_cache_resolve_cs.hlsl` (12 DWORDs of the 20-DWORD budget).
struct SurfaceCacheResolveRootConstants {
    uint32_t keysUav     = UINT32_MAX;   ///< [0]
    uint32_t accumUav    = UINT32_MAX;   ///< [1]
    uint32_t resolvedUav = UINT32_MAX;   ///< [2]
    uint32_t statsUav    = UINT32_MAX;   ///< [3]
    uint32_t entriesLog2 = 20;           ///< [4]
    uint32_t accumFrames = 32;           ///< [5]  EMA horizon in frames.
    uint32_t staleFrames = 64;           ///< [6]  Eviction age in frames.
    uint32_t mode        = 0;            ///< [7]  surfacecache::kSurfaceCacheResolveMode*.
    float    cellSize    = 10.0f;        ///< [8]
    uint32_t regionCount = 0;            ///< [9]  Change boxes in the b2 block.
    uint32_t frame       = 0;            ///< [10]
    uint32_t _pad0       = 0;            ///< [11]
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
/// or the field is off); a lighting change clears (stages 1-3: the cells hold lit radiance).
struct SurfaceCacheInvalidation {
    bool     clear = false;        ///< Whole table cleared this frame.
    uint32_t regionCount = 0;      ///< Change boxes passed to the resolve (0 when clearing).
};
[[nodiscard]] SurfaceCacheInvalidation SurfaceCacheDecideInvalidation(
    bool sceneRevisionChanged, uint32_t changedRegionCount, bool geometryHashChanged,
    bool lightingHashChanged) noexcept;

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

    /// Last stats block read back (`.Stats`), for the log and the tests of the harness.
    struct Stats {
        uint32_t inserts = 0, failedInserts = 0, updates = 0, queries = 0, hits = 0, evictions = 0, live = 0, maxProbe = 0;
        uint32_t tombstones = 0, torn = 0, probeSum = 0;
        uint64_t totalInserts = 0, totalFailedInserts = 0;   ///< Since the table was created (every frame read).
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
    uint32_t m_keysUav = UINT32_MAX, m_accumUav = UINT32_MAX;
    uint32_t m_resolvedUav = UINT32_MAX, m_resolvedSrv = UINT32_MAX;
    uint32_t m_statsUav = UINT32_MAX;

    RHIBufferHandle m_params[kParamsRingFrames];
    uint32_t        m_paramsSrv[kParamsRingFrames] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
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
    Stats    m_lastStats;
};

} // namespace uz
