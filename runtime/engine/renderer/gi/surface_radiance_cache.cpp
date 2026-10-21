/// UzEngine Surface Radiance Cache — implementation (Phase D stage 1).
///
/// Per frame (`Resolve`, the body of the `SurfaceCache_Resolve` pass registered right after the
/// distance-field composite): create the table on first use, decide the invalidation (clear or
/// change boxes), write the binding record of the frame, fold last frame's accumulators and age
/// the cells with one dispatch of `surface_cache_resolve_cs.hlsl`, then leave the buffers in the
/// states the consumers bind them in. The stats readback follows the `RadianceCache`
/// diagnostic pattern (a copy recorded now, mapped `kReadbackFrameLag` frames later).
///
/// @ref docs/specs/rendering/surface-radiance-cache.md

#include "surface_radiance_cache.h"
#include "../render_graph/render_graph_states.h"
#include "../../../shaders/runtime/gi/surface_cache_contract.hlsli"

#include "pipeline_library.h"
#include "cb_ring_allocator.h"
#include "constant_buffers.h"     // LightingCB / RectLightData (the stage 2 light records)
#include "root_slots.h"
#include "log.h"
#include "rhi.h"
#include "cvar.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars (spec section 3.4, stage 1; every number read at execution time) ───────────────────
static auto* cv_SurfaceCache = UZ_CVAR_BOOL("r.GI.SurfaceCache", false,
    "Surface radiance cache (Lumen-parity Phase D stage 1): a world-space spatial hash of the outgoing "
    "diffuse radiance at ray hits, read and written by the hardware ray-traced reflection pass. Off: "
    "nothing allocated, no pass registered, the frame unchanged");
static auto* cv_SCEntriesLog2 = UZ_CVAR_INT("r.GI.SurfaceCache.EntriesLog2", 20,
    "Surface cache: log2 of the hash table size (2^20 = 56 MB). Read when the table is created", 16, 22);
static auto* cv_SCCellSize = UZ_CVAR_FLOAT("r.GI.SurfaceCache.CellSize", 10.0f,
    "Surface cache: level-0 cell size in cm (doubles with every camera-distance level)", 2.0f, 100.0f);
static auto* cv_SCLodDistance = UZ_CVAR_FLOAT("r.GI.SurfaceCache.LodDistance", 400.0f,
    "Surface cache: camera distance in cm at which the cell size first doubles", 50.0f, 5000.0f);
static auto* cv_SCMinSamples = UZ_CVAR_INT("r.GI.SurfaceCache.MinSamples", 8,
    "Surface cache: resolved samples a cell needs before a consumer reads it (below: inline shading)", 1, 256);
static auto* cv_SCAccumFrames = UZ_CVAR_INT("r.GI.SurfaceCache.AccumFrames", 32,
    "Surface cache: horizon in frames of the per-frame exponential moving average", 1, 255);
static auto* cv_SCStaleFrames = UZ_CVAR_INT("r.GI.SurfaceCache.StaleFrames", 64,
    "Surface cache: frames without a sample after which a cell is evicted", 1, 255);
static auto* cv_SCMaxRadiance = UZ_CVAR_FLOAT("r.GI.SurfaceCache.MaxRadiance", 4096.0f,
    "Surface cache: per-sample radiance clamp (firefly cap and the accumulator overflow guard)", 1.0f, 4096.0f);
static auto* cv_SCJitter = UZ_CVAR_BOOL("r.GI.SurfaceCache.Jitter", true,
    "Surface cache: +-0.5 cell lookup jitter seeded by (cell, frame), turning cell blockiness into noise");
static auto* cv_SCDirectMode = UZ_CVAR_INT("r.GI.SurfaceCache.DirectMode", 0,
    "Surface cache reader policy (D7): 0 = the cell's sun + other layers replace the hit radiance; "
    "1 = inline sun with a shadow ray, the cell's other layer (sharp sun shadows)", 0, 1);
static auto* cv_SCRefresh = UZ_CVAR_INT("r.GI.SurfaceCache.Refresh", 16,
    "Surface cache: one hit in this many rays shades inline and updates its cell, so a readable cell keeps "
    "converging toward the cell-and-time average instead of freezing on its first MinSamples samples "
    "(0 = never refresh, 1 = every ray)", 0, 1024);
static auto* cv_SCStats = UZ_CVAR_BOOL("r.GI.SurfaceCache.Stats", false,
    "Surface cache: count inserts / failed inserts / updates / queries / hits / evictions / live cells on the "
    "GPU, read them back and log them every 60 frames");
static auto* cv_SCDebug = UZ_CVAR_INT("r.GI.SurfaceCache.Debug", 0,
    "Surface cache debug view written into the reflection buffer by the RT pass: 1 = cell hash colour, "
    "2 = sample-count heat, 3 = hit mask (green readable, yellow below MinSamples, red no cell)", 0, 3);
// Stage 2 (lighting completeness at the hit).
static auto* cv_SCLocalLights = UZ_CVAR_INT("r.GI.SurfaceCache.LocalLights", 4,
    "Surface cache (stage 2): the K strongest point / spot / rect lights of the frame at a shaded hit (by their "
    "unshadowed Lambert radiance), each with a shadow ray, added to the cell's sun layer (0 = none)", 0, 16);
static auto* cv_SCEmissive = UZ_CVAR_BOOL("r.GI.SurfaceCache.Emissive", true,
    "Surface cache (stage 2): the hit mesh's emission (the GI material table's emissive colour x intensity and "
    "texture) joins the cell's other layer");

namespace uz {

namespace {

constexpr uint32_t kStatsBytes = surfacecache::kSurfaceCacheStatWords * sizeof(uint32_t);

} // namespace

uint64_t SurfaceCacheHashBytes(const void* data, size_t bytes, uint64_t hash) noexcept {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

SurfaceCacheInvalidation SurfaceCacheDecideInvalidation(bool sceneRevisionChanged, uint32_t changedRegionCount,
                                                        bool geometryHashChanged, bool lightingHashChanged) noexcept {
    SurfaceCacheInvalidation out{};
    if (sceneRevisionChanged || lightingHashChanged) {
        out.clear = true;
    } else if (changedRegionCount > 0) {
        if (changedRegionCount > surfacecache::kSurfaceCacheMaxRegions) {
            out.clear = true;                        // more boxes than the resolve carries: clear
        } else {
            out.regionCount = changedRegionCount;    // the boxes evict their cells
        }
    } else if (geometryHashChanged) {
        out.clear = true;                            // geometry moved with no box to evict by
    }
    return out;
}

bool SurfaceRadianceCache::Enabled() { return cv_SurfaceCache->value != 0.0f; }
bool SurfaceRadianceCache::StatsEnabled() { return cv_SCStats->value != 0.0f; }
uint32_t SurfaceRadianceCache::LocalLights() {
    return static_cast<uint32_t>(std::clamp(static_cast<int>(cv_SCLocalLights->value), 0,
                                            static_cast<int>(surfacecache::kSurfaceCacheMaxLocalLights)));
}
bool SurfaceRadianceCache::EmissiveEnabled() { return cv_SCEmissive->value != 0.0f; }

uint32_t SurfaceCacheBuildLights(SurfaceCacheLightGPU* out, uint32_t capacity, const LightingCB* lighting,
                                 const RectLightData* rectLights, uint32_t rectLightCount) noexcept {
    if (!out || capacity == 0) return 0;
    uint32_t n = 0;
    const auto positive = [](float r, float g, float b) { return r > 0.0f || g > 0.0f || b > 0.0f; };
    // Rect lights first: few, and usually the strongest at any hit.
    const uint32_t rects = rectLights ? std::min(rectLightCount, kMaxRectLights) : 0u;
    for (uint32_t i = 0; i < rects && n < capacity; ++i) {
        const RectLightData& r = rectLights[i];
        if (r.diffuseScale <= 0.0f || r.range <= 0.0f || r.halfWidth <= 0.0f || r.halfHeight <= 0.0f
            || !positive(r.color.x, r.color.y, r.color.z)) continue;
        const Vec3 normal = r.right.Cross(r.up);
        const float len = normal.Length();
        if (!(len > 1e-8f)) continue;
        const Vec3 direction = normal * (1.0f / len);
        SurfaceCacheLightGPU& l = out[n++];
        l = SurfaceCacheLightGPU{};
        l.positionX = r.position.x; l.positionY = r.position.y; l.positionZ = r.position.z;
        l.radius = r.range;
        l.colorR = r.color.x * r.diffuseScale; l.colorG = r.color.y * r.diffuseScale; l.colorB = r.color.z * r.diffuseScale;
        l.type = surfacecache::kSurfaceCacheLightTypeRect
               | ((r.flags & kRectLightFlag_TwoSided) ? surfacecache::kSurfaceCacheLightTwoSided : 0u);
        l.directionX = direction.x; l.directionY = direction.y; l.directionZ = direction.z;
        l.rightX = r.right.x * r.halfWidth; l.rightY = r.right.y * r.halfWidth; l.rightZ = r.right.z * r.halfWidth;
        l.upX = r.up.x * r.halfHeight; l.upY = r.up.y * r.halfHeight; l.upZ = r.up.z * r.halfHeight;
    }
    if (!lighting) return n;
    const uint32_t points = std::min(lighting->pointLightCount, kMaxPointLights);
    for (uint32_t i = 0; i < points && n < capacity; ++i) {
        const PointLightData& p = lighting->pointLights[i];
        if (p.diffuseScale <= 0.0f || p.radius <= 0.0f || !positive(p.color.x, p.color.y, p.color.z)) continue;
        SurfaceCacheLightGPU& l = out[n++];
        l = SurfaceCacheLightGPU{};
        l.positionX = p.position.x; l.positionY = p.position.y; l.positionZ = p.position.z;
        l.radius = p.radius;
        l.colorR = p.color.x * p.diffuseScale; l.colorG = p.color.y * p.diffuseScale; l.colorB = p.color.z * p.diffuseScale;
        l.type = surfacecache::kSurfaceCacheLightTypePoint
               | ((p.flags & kLightFlag_InverseSquared) ? surfacecache::kSurfaceCacheLightInverseSquared : 0u);
        l.falloffExponent = p.falloffExponent;
    }
    const uint32_t spots = std::min(lighting->spotLightCount, kMaxSpotLights);
    for (uint32_t i = 0; i < spots && n < capacity; ++i) {
        const SpotLightData& s = lighting->spotLights[i];
        if (s.diffuseScale <= 0.0f || s.radius <= 0.0f || !positive(s.color.x, s.color.y, s.color.z)) continue;
        SurfaceCacheLightGPU& l = out[n++];
        l = SurfaceCacheLightGPU{};
        l.positionX = s.position.x; l.positionY = s.position.y; l.positionZ = s.position.z;
        l.radius = s.radius;
        l.colorR = s.color.x * s.diffuseScale; l.colorG = s.color.y * s.diffuseScale; l.colorB = s.color.z * s.diffuseScale;
        l.type = surfacecache::kSurfaceCacheLightTypeSpot
               | ((s.flags & kLightFlag_InverseSquared) ? surfacecache::kSurfaceCacheLightInverseSquared : 0u);
        l.directionX = s.direction.x; l.directionY = s.direction.y; l.directionZ = s.direction.z;
        l.cosOuterAngle = s.cosOuterAngle;
        l.invCosConeDifference = s.invCosConeDifference;
        l.falloffExponent = s.falloffExponent;
    }
    return n;
}

SurfaceRadianceCache::~SurfaceRadianceCache() {
    DestroyResources();
}

void SurfaceRadianceCache::DestroyResources() {
    if (m_device) {
        for (auto* handle : {&m_keys, &m_accum, &m_resolved, &m_stats}) {
            if (handle->IsValid()) m_device->DestroyBuffer(*handle);
            *handle = {};
        }
        for (uint32_t i = 0; i < kParamsRingFrames; ++i) {
            if (m_params[i].IsValid()) m_device->DestroyBuffer(m_params[i]);
            m_params[i] = {};
            if (m_lights[i].IsValid()) m_device->DestroyBuffer(m_lights[i]);
            m_lights[i] = {};
        }
        for (uint32_t i = 0; i < kReadbackRingFrames; ++i) {
            if (m_readback[i].IsValid()) m_device->DestroyBuffer(m_readback[i]);
            m_readback[i] = {};
        }
    }
    for (uint32_t i = 0; i < kParamsRingFrames; ++i) { m_paramsSrv[i] = UINT32_MAX; m_lightsSrv[i] = UINT32_MAX; }
    for (uint32_t i = 0; i < kReadbackRingFrames; ++i) m_readbackFrame[i] = UINT32_MAX;
    m_device = nullptr;
    m_keysUav = m_accumUav = m_resolvedUav = m_resolvedSrv = m_statsUav = UINT32_MAX;
    m_resolvedFrame = UINT32_MAX;
    m_initialized = false;
    m_clearPending = true;
    m_createFailed = false;
    m_lastStats = Stats{};
}

bool SurfaceRadianceCache::EnsureResources(IRHIDevice* device) {
    if (m_initialized) return true;
    if (m_createFailed || !device || !m_rgStates) return false;
    m_device = device;
    m_entriesLog2 = static_cast<uint32_t>(std::clamp(static_cast<int>(cv_SCEntriesLog2->value), 16, 22));
    const uint64_t entries = 1ull << m_entriesLog2;

    RHIBufferDesc desc{};
    desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
    desc.sizeBytes = entries * 2 * sizeof(uint32_t);
    desc.stride    = 2 * sizeof(uint32_t);
    desc.debugName = "SurfaceCache_Keys";
    m_keys = device->CreateBuffer(desc);
    desc.sizeBytes = entries * surfacecache::kSurfaceCacheAccumStride * sizeof(uint32_t);
    desc.stride    = sizeof(uint32_t);
    desc.debugName = "SurfaceCache_Accum";
    m_accum = device->CreateBuffer(desc);
    desc.sizeBytes = entries * 4 * sizeof(uint32_t);
    desc.stride    = 4 * sizeof(uint32_t);
    desc.debugName = "SurfaceCache_Resolved";
    m_resolved = device->CreateBuffer(desc);
    desc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySrc;
    desc.sizeBytes = kStatsBytes;
    desc.stride    = sizeof(uint32_t);
    desc.debugName = "SurfaceCache_Stats";
    m_stats = device->CreateBuffer(desc);

    RHIBufferDesc paramsDesc{};
    paramsDesc.sizeBytes = sizeof(SurfaceCacheParamsGPU);
    paramsDesc.stride    = sizeof(SurfaceCacheParamsGPU);
    paramsDesc.usage     = RHIBufferUsage::StructuredBuffer;
    paramsDesc.memory    = RHIMemoryType::Upload;
    paramsDesc.debugName = "SurfaceCache_Params";
    RHIBufferDesc readbackDesc{};
    readbackDesc.sizeBytes = kStatsBytes;
    readbackDesc.memory    = RHIMemoryType::Readback;
    readbackDesc.debugName = "SurfaceCache_StatsReadback";
    // Stage 2: the frame's light records, ringed like the params (an upload-heap structured buffer).
    RHIBufferDesc lightsDesc{};
    lightsDesc.sizeBytes = uint64_t(surfacecache::kSurfaceCacheMaxLights) * sizeof(SurfaceCacheLightGPU);
    lightsDesc.stride    = sizeof(SurfaceCacheLightGPU);
    lightsDesc.usage     = RHIBufferUsage::StructuredBuffer;
    lightsDesc.memory    = RHIMemoryType::Upload;
    lightsDesc.debugName = "SurfaceCache_Lights";
    bool ok = m_keys.IsValid() && m_accum.IsValid() && m_resolved.IsValid() && m_stats.IsValid();
    for (uint32_t i = 0; i < kParamsRingFrames && ok; ++i) {
        m_params[i] = device->CreateBuffer(paramsDesc);
        ok = m_params[i].IsValid();
        if (ok) m_paramsSrv[i] = device->GetBufferSRVIndex(m_params[i]);
        ok = ok && m_paramsSrv[i] != UINT32_MAX;
        if (ok) {
            m_lights[i] = device->CreateBuffer(lightsDesc);
            ok = m_lights[i].IsValid();
            if (ok) m_lightsSrv[i] = device->GetBufferSRVIndex(m_lights[i]);
            ok = ok && m_lightsSrv[i] != UINT32_MAX;
        }
    }
    for (uint32_t i = 0; i < kReadbackRingFrames && ok; ++i) {
        m_readback[i] = device->CreateBuffer(readbackDesc);
        ok = m_readback[i].IsValid();
    }
    if (ok) {
        m_keysUav     = device->GetBufferUAVIndex(m_keys);
        m_accumUav    = device->GetBufferUAVIndex(m_accum);
        m_resolvedUav = device->GetBufferUAVIndex(m_resolved);
        m_resolvedSrv = device->GetBufferSRVIndex(m_resolved);
        m_statsUav    = device->GetBufferUAVIndex(m_stats);
        ok = m_keysUav != UINT32_MAX && m_accumUav != UINT32_MAX && m_resolvedUav != UINT32_MAX
          && m_resolvedSrv != UINT32_MAX && m_statsUav != UINT32_MAX;
    }
    if (!ok) {
        UZ_LOG(Renderer, Error, "SurfaceCache: buffer creation failed (entriesLog2={}); the cache stays off", m_entriesLog2);
        m_createFailed = true;
        m_initialized = true;      // so DestroyResources releases what was created
        DestroyResources();
        m_createFailed = true;
        return false;
    }
    const uint64_t bytes = entries * (2 + surfacecache::kSurfaceCacheAccumStride + 4) * sizeof(uint32_t);
    UZ_LOG(Renderer, Info, "SurfaceCache: created {} entries ({} MB: keys {} MB, accum {} MB, resolved {} MB) keysUav={} accumUav={} resolvedSrv={} statsUav={}",
           entries, bytes >> 20, (entries * 8) >> 20, (entries * 32) >> 20, (entries * 16) >> 20,
           m_keysUav, m_accumUav, m_resolvedSrv, m_statsUav);
    m_initialized = true;
    m_clearPending = true;
    m_statsValidFrom = UINT32_MAX;     // set by the first Resolve
    m_lastCellSize = cv_SCCellSize->value;
    m_lastLodDistance = cv_SCLodDistance->value;
    return true;
}

void SurfaceRadianceCache::QueueStatsReadback(IRHICommandList* cmd, uint32_t frameIndex) {
    const uint32_t slot = frameIndex % kReadbackRingFrames;
    if (!m_readback[slot].IsValid()) return;
    // The previous frame's block is complete (its consumers ran before this list); the copy takes
    // the whole 16-word buffer and the reader picks the block of the frame it was recorded for.
    m_rgStates->Transition(cmd, m_stats, RHIResourceState::CopySrc);
    cmd->CopyBuffer(m_stats, 0, m_readback[slot], 0, kStatsBytes);
    m_rgStates->Transition(cmd, m_stats, RHIResourceState::UnorderedAccess);
    m_readbackFrame[slot] = frameIndex;
}

void SurfaceRadianceCache::ReadStats(uint32_t frameIndex) {
    // The copy recorded at frame N holds the stats of frame N - 1; map it once its fence passed.
    for (uint32_t slot = 0; slot < kReadbackRingFrames; ++slot) {
        const uint32_t recorded = m_readbackFrame[slot];
        if (recorded == UINT32_MAX || frameIndex < recorded + kReadbackFrameLag) continue;
        const uint32_t statsFrame = recorded - 1u;
        // The block of a frame before the first resolve was never zeroed.
        const auto* words = statsFrame >= m_statsValidFrom
            ? static_cast<const uint32_t*>(m_device->MapBuffer(m_readback[slot])) : nullptr;
        if (words) {
            const uint32_t* block = words + (statsFrame & 1u) * surfacecache::kSurfaceCacheStatCount;
            m_lastStats.inserts       = block[surfacecache::kSurfaceCacheStatInserts];
            m_lastStats.failedInserts = block[surfacecache::kSurfaceCacheStatFailedInserts];
            m_lastStats.updates       = block[surfacecache::kSurfaceCacheStatUpdates];
            m_lastStats.queries       = block[surfacecache::kSurfaceCacheStatQueries];
            m_lastStats.hits          = block[surfacecache::kSurfaceCacheStatHits];
            m_lastStats.evictions     = block[surfacecache::kSurfaceCacheStatEvictions];
            m_lastStats.live          = block[surfacecache::kSurfaceCacheStatLive];
            m_lastStats.maxProbe      = block[surfacecache::kSurfaceCacheStatMaxProbe];
            m_lastStats.tombstones    = block[surfacecache::kSurfaceCacheStatTombstones];
            m_lastStats.torn          = block[surfacecache::kSurfaceCacheStatTorn];
            m_lastStats.probeSum      = block[surfacecache::kSurfaceCacheStatProbeSum];
            m_lastStats.totalInserts       += m_lastStats.inserts;
            m_lastStats.totalFailedInserts += m_lastStats.failedInserts;
            m_lastStats.valid = true;
            m_device->UnmapBuffer(m_readback[slot]);
            if (statsFrame % 60u == 0u) {
                const double failedPct = m_lastStats.inserts + m_lastStats.failedInserts > 0
                    ? 100.0 * m_lastStats.failedInserts / double(m_lastStats.inserts + m_lastStats.failedInserts) : 0.0;
                const double totalPct = m_lastStats.totalInserts + m_lastStats.totalFailedInserts > 0
                    ? 100.0 * double(m_lastStats.totalFailedInserts) / double(m_lastStats.totalInserts + m_lastStats.totalFailedInserts) : 0.0;
                const double avgProbe = m_lastStats.updates + m_lastStats.failedInserts + m_lastStats.torn > 0
                    ? double(m_lastStats.probeSum) / double(m_lastStats.updates + m_lastStats.failedInserts + m_lastStats.torn) : 0.0;
                UZ_LOG(Renderer, Info, "SurfaceCache stats: frame={} live={} tombstones={} inserts={} failedInserts={} ({:.3f} %) torn={} updates={} queries={} hits={} evictions={} maxProbe={} avgProbe={:.2f} totalInserts={} totalFailedInserts={} ({:.3f} %) entries=2^{} generation={} lights={} K={}",
                       statsFrame, m_lastStats.live, m_lastStats.tombstones, m_lastStats.inserts, m_lastStats.failedInserts, failedPct,
                       m_lastStats.torn, m_lastStats.updates, m_lastStats.queries, m_lastStats.hits, m_lastStats.evictions,
                       m_lastStats.maxProbe, avgProbe, m_lastStats.totalInserts, m_lastStats.totalFailedInserts, totalPct,
                       m_entriesLog2, m_generation, m_lastLightCount, m_lastLocalLights);
            }
        }
        m_readbackFrame[slot] = UINT32_MAX;
    }
}

bool SurfaceRadianceCache::Resolve(IRHICommandList* cmd, const FrameInputs& in) {
    if (!cmd || !in.device || !in.cbRing || !in.pipelines || !m_rgStates) return false;
    if (!EnsureResources(in.device)) return false;
    const RHIPipelineHandle pso = in.pipelines->GetSurfaceCacheResolve();
    if (!pso.IsValid()) return false;
    if (m_statsValidFrom == UINT32_MAX) m_statsValidFrom = in.frameIndex + 1u;

    // The table size is fixed at creation (the device destroys buffers immediately, so a live
    // resize would race the previous frame); say so once instead of silently ignoring the CVar.
    const auto wantedLog2 = static_cast<uint32_t>(std::clamp(static_cast<int>(cv_SCEntriesLog2->value), 16, 22));
    if (wantedLog2 != m_entriesLog2 && !m_entriesChangeLogged) {
        m_entriesChangeLogged = true;
        UZ_LOG(Renderer, Warning, "SurfaceCache: r.GI.SurfaceCache.EntriesLog2 {} takes effect at the next start (table created at 2^{})",
               wantedLog2, m_entriesLog2);
    }

    // ── Invalidation (section 2.6) ──────────────────────────────────────────────────────────
    const uint32_t regionCount = in.changedRegions ? static_cast<uint32_t>(in.changedRegions->size()) : 0u;
    const bool revisionChanged = in.sceneRevision != m_lastSceneRevision;
    const bool geometryChanged = in.geometryHash != m_lastGeometryHash;
    const bool lightingChanged = in.lightingHash != m_lastLightingHash;
    const bool cellChanged     = cv_SCCellSize->value != m_lastCellSize || cv_SCLodDistance->value != m_lastLodDistance;
    SurfaceCacheInvalidation decision = SurfaceCacheDecideInvalidation(revisionChanged, regionCount, geometryChanged, lightingChanged);
    if (m_clearPending || cellChanged) decision = SurfaceCacheInvalidation{true, 0};
    m_lastSceneRevision = in.sceneRevision;
    m_lastGeometryHash  = in.geometryHash;
    m_lastLightingHash  = in.lightingHash;
    m_lastCellSize      = cv_SCCellSize->value;
    m_lastLodDistance   = cv_SCLodDistance->value;
    if (decision.clear) {
        ++m_generation;
        m_clearPending = false;
        if (StatsEnabled()) {
            UZ_LOG(Renderer, Info, "SurfaceCache: clear frame={} generation={} (revision={} regions={} geometry={} lighting={} cell={})",
                   in.frameIndex, m_generation, revisionChanged, regionCount, geometryChanged, lightingChanged, cellChanged);
        }
    }

    // ── This frame's binding record ─────────────────────────────────────────────────────────
    const bool stats = StatsEnabled();
    const uint32_t slot = in.frameIndex % kParamsRingFrames;
    SurfaceCacheParamsGPU params{};
    params.keysUav     = m_keysUav;
    params.accumUav    = m_accumUav;
    params.resolvedSrv = m_resolvedSrv;
    params.surfaceUav  = UINT32_MAX;
    params.entriesLog2 = m_entriesLog2;
    params.minSamples  = static_cast<uint32_t>(std::clamp(static_cast<int>(cv_SCMinSamples->value), 1, 256));
    params.flags       = (cv_SCDirectMode->value != 0.0f ? surfacecache::kSurfaceCacheFlagDirectInline : 0u)
                       | (cv_SCJitter->value != 0.0f ? surfacecache::kSurfaceCacheFlagJitter : 0u)
                       | (stats ? surfacecache::kSurfaceCacheFlagStats : 0u)
                       | ((static_cast<uint32_t>(std::clamp(static_cast<int>(cv_SCDebug->value), 0, 3)) << surfacecache::kSurfaceCacheFlagDebugShift)
                          & surfacecache::kSurfaceCacheFlagDebugMask);
    params.frame       = in.frameIndex;
    params.cellSize    = cv_SCCellSize->value;
    params.lodDistance = cv_SCLodDistance->value;
    params.maxRadiance = cv_SCMaxRadiance->value;
    params.sdfLodBias  = 2u;
    params.statsUav    = stats ? m_statsUav : UINT32_MAX;
    params.generation  = m_generation;
    params.refreshRate = static_cast<uint32_t>(std::clamp(static_cast<int>(cv_SCRefresh->value), 0, 1024));
    // Stage 2: the frame's light records (rebuilt every frame while the cache is on: the records are
    // small and the LightingCB is already hashed here) and the hit's emission.
    const uint32_t localLights = LocalLights();
    uint32_t lightCount = 0;
    if (localLights > 0u) {
        if (auto* records = static_cast<SurfaceCacheLightGPU*>(in.device->MapBuffer(m_lights[slot]))) {
            lightCount = SurfaceCacheBuildLights(records, surfacecache::kSurfaceCacheMaxLights, in.lighting,
                                                 in.rectLights, in.rectLightCount);
            in.device->UnmapBuffer(m_lights[slot]);
        }
    }
    params.lightsSrv   = lightCount > 0u ? m_lightsSrv[slot] : UINT32_MAX;
    params.lightCount  = lightCount;
    params.localLights = lightCount > 0u ? localLights : 0u;
    m_lastLightCount   = lightCount;
    m_lastLocalLights  = params.localLights;
    if (EmissiveEnabled()) params.flags |= surfacecache::kSurfaceCacheFlagEmissive;
    if (void* mapped = in.device->MapBuffer(m_params[slot])) {
        std::memcpy(mapped, &params, sizeof(params));
        in.device->UnmapBuffer(m_params[slot]);
    } else {
        UZ_LOG(Renderer, Error, "SurfaceCache: params map failed (frame {})", in.frameIndex);
        return false;
    }

    // ── Stats: read what is ready, record the copy of last frame's block ──────────────────
    if (stats) {
        ReadStats(in.frameIndex);
        QueueStatsReadback(cmd, in.frameIndex);
    }

    // ── The resolve dispatch ───────────────────────────────────────────────────────────────
    for (auto buffer : {m_keys, m_accum, m_resolved, m_stats}) {
        if (m_rgStates->Get(buffer) == RHIResourceState::UnorderedAccess)
            m_rgStates->UavBarrier(cmd, buffer);
        else
            m_rgStates->Transition(cmd, buffer, RHIResourceState::UnorderedAccess);
    }

    SurfaceCacheResolveRegionsCB regions{};
    for (uint32_t r = 0; r < decision.regionCount; ++r) {
        const auto& box = (*in.changedRegions)[r];
        regions.regionLo[r][0] = box.lo.x; regions.regionLo[r][1] = box.lo.y; regions.regionLo[r][2] = box.lo.z;
        regions.regionHi[r][0] = box.hi.x; regions.regionHi[r][1] = box.hi.y; regions.regionHi[r][2] = box.hi.z;
    }
    auto regionsAlloc = in.cbRing->Allocate(sizeof(regions));
    if (!regionsAlloc.cpuPtr) return false;
    std::memcpy(regionsAlloc.cpuPtr, &regions, sizeof(regions));

    SurfaceCacheResolveRootConstants rc{};
    rc.keysUav     = m_keysUav;
    rc.accumUav    = m_accumUav;
    rc.resolvedUav = m_resolvedUav;
    rc.statsUav    = stats ? m_statsUav : UINT32_MAX;
    rc.entriesLog2 = m_entriesLog2;
    rc.accumFrames = static_cast<uint32_t>(std::clamp(static_cast<int>(cv_SCAccumFrames->value), 1, 255));
    rc.staleFrames = static_cast<uint32_t>(std::clamp(static_cast<int>(cv_SCStaleFrames->value), 1, 255));
    rc.mode        = decision.clear ? surfacecache::kSurfaceCacheResolveModeClear : surfacecache::kSurfaceCacheResolveModeResolve;
    rc.cellSize    = cv_SCCellSize->value;
    rc.regionCount = decision.regionCount;
    rc.frame       = in.frameIndex;

    cmd->SetPipeline(pso);
    cmd->SetRootConstants(kSlotRootConstants, reinterpret_cast<const uint32_t*>(&rc), kSurfaceCacheResolveRootDwords);
    cmd->SetRootCBV(kSlotPerObject, regionsAlloc.bufferHandle, regionsAlloc.offsetBytes, regionsAlloc.cpuPtr, sizeof(regions));
    const uint32_t groups = static_cast<uint32_t>((1ull << m_entriesLog2) / 64ull);
    cmd->Dispatch(groups, 1, 1);

    // The consumers read the resolved records through the SRV and keep writing keys / accum.
    m_rgStates->UavBarrier(cmd, m_keys);
    m_rgStates->UavBarrier(cmd, m_accum);
    m_rgStates->UavBarrier(cmd, m_stats);
    m_rgStates->Transition(cmd, m_resolved, RHIResourceState::ShaderResource);
    m_resolvedFrame = in.frameIndex;
    return true;
}

void SurfaceRadianceCache::PrepareConsumer(IRHICommandList* cmd) {
    if (!m_initialized || !cmd || !m_rgStates) return;
    for (auto buffer : {m_keys, m_accum, m_stats}) {
        if (m_rgStates->Get(buffer) == RHIResourceState::UnorderedAccess)
            m_rgStates->UavBarrier(cmd, buffer);
        else
            m_rgStates->Transition(cmd, buffer, RHIResourceState::UnorderedAccess);
    }
    if (m_rgStates->Get(m_resolved) != RHIResourceState::ShaderResource)
        m_rgStates->Transition(cmd, m_resolved, RHIResourceState::ShaderResource);
}

} // namespace uz
