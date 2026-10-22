/// UzEngine Surface Radiance Cache — the consumer API (Phase D stage 1): `SurfaceCacheQuery` reads
/// the resolved outgoing radiance of the cell a hit falls in, `SurfaceCacheUpdate` accumulates a
/// hit's inline shading into that cell for the next resolve. World-space spatial hash keyed by
/// (camera-distance level, 6-direction normal, cell coords), open addressing with linear probing,
/// 32-bit atomics only (the two-word CAS insertion of the spec, section 2.3).
///
/// Every consumer binds one `StructuredBuffer<SurfaceCacheParams>` element (its SRV index is the
/// consumer's only new root DWORD) and reads `keys` / `accum` through the UAVs it names, the
/// resolved records through its SRV. A cell is readable when it holds at least `minSamples`
/// samples (D6); a miss means "shade inline as today, then update" (section 2.5's energy rule: a
/// valid cell replaces the WHOLE hit radiance, never adds to it).
///
/// The lookup jitter (+-0.5 cell) is seeded by (cell, frame) only, never by the pixel, so two
/// evaluations of one ray in a frame agree (the ReSTIR same-surface shortcut assumes a
/// deterministic trace).
///
/// @ref docs/specs/rendering/surface-radiance-cache.md sections 2.3, 2.5
/// @ref NVIDIA SHaRC (RTXGI 2.0) — camera-distance levels, CAS insertion, sample-threshold queries
#ifndef UZ_SURFACE_CACHE_HLSLI
#define UZ_SURFACE_CACHE_HLSLI

#include "surface_cache_contract.hlsli"

static const uint kSurfaceCacheNoSlot = 0xFFFFFFFFu;

struct SurfaceCacheCellId {
    uint2 key;
    uint  slot;      // home slot of the key (linear probing starts here)
    uint  level;
};

// The (jittered) cell of a hit for a consumer: the level from the CAMERA distance (D3, never the
// ray origin's), the cell of the jittered position, the key and its home slot.
SurfaceCacheCellId SurfaceCacheLocate(SurfaceCacheParams p, float3 pos, float3 n, float3 cameraPos,
                                      uint lodBias) {
    const float3 toCamera = pos - cameraPos;
    const uint   level    = min(SurfaceCacheLevel(sqrt(dot(toCamera, toCamera)), p.lodDistance) + lodBias,
                                kSurfaceCacheLevels - 1u);
    const float  cellSize = SurfaceCacheCellSize(p.cellSize, level);
    const int3   cell0    = SurfaceCacheCell(pos, cellSize);
    float3 q = pos;
    if ((p.flags & kSurfaceCacheFlagJitter) != 0u) {
        q = pos + SurfaceCacheJitter(cell0, p.frame) * cellSize;
    }
    SurfaceCacheCellId id;
    id.key   = SurfaceCacheKey(SurfaceCacheCell(q, cellSize), SurfaceCacheNormalIndex(n), level);
    id.slot  = SurfaceCacheSlot(id.key, p.entriesLog2);
    id.level = level;
    return id;
}

// Wave-reduced stat counter: one atomic per wave, only with the stats flag on (off by default,
// so the counters cost nothing on the default path).
void SurfaceCacheStat(SurfaceCacheParams p, uint index, bool count) {
    if ((p.flags & kSurfaceCacheFlagStats) == 0u || p.statsUav == kSurfaceCacheSentinel) return;
    const uint n = WaveActiveCountBits(count);
    if (WaveIsFirstLane() && n > 0u) {
        RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[p.statsUav];
        InterlockedAdd(stats[(p.frame & 1u) * kSurfaceCacheStatCount + index], n);
    }
}

void SurfaceCacheStatSum(SurfaceCacheParams p, uint index, uint value) {
    if ((p.flags & kSurfaceCacheFlagStats) == 0u || p.statsUav == kSurfaceCacheSentinel) return;
    const uint sum = WaveActiveSum(value);
    if (WaveIsFirstLane() && sum > 0u) {
        RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[p.statsUav];
        InterlockedAdd(stats[(p.frame & 1u) * kSurfaceCacheStatCount + index], sum);
    }
}

void SurfaceCacheStatMax(SurfaceCacheParams p, uint index, uint value) {
    if ((p.flags & kSurfaceCacheFlagStats) == 0u || p.statsUav == kSurfaceCacheSentinel) return;
    const uint m = WaveActiveMax(value);
    if (WaveIsFirstLane()) {
        RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[p.statsUav];
        InterlockedMax(stats[(p.frame & 1u) * kSurfaceCacheStatCount + index], m);
    }
}

// The slot holding `id.key`, or kSurfaceCacheNoSlot. Readers stop at the first EMPTY slot and
// probe past tombstones; a slot whose first word matches and whose second does not is a foreign
// key (or a torn insert of this frame) and is skipped.
uint SurfaceCacheFind(SurfaceCacheParams p, SurfaceCacheCellId id) {
    RWStructuredBuffer<uint2> keys = ResourceDescriptorHeap[p.keysUav];
    const uint mask = (1u << p.entriesLog2) - 1u;
    [loop]
    for (uint i = 0u; i < kSurfaceCacheProbeSteps; ++i) {
        const uint  s = (id.slot + i) & mask;
        const uint2 k = keys[s];
        if (SurfaceCacheKeyEmpty(k)) return kSurfaceCacheNoSlot;
        if (k.x == id.key.x && k.y == id.key.y) return s;
    }
    return kSurfaceCacheNoSlot;
}

// Reads the cell of (pos, n): true with the two radiance layers when the cell is readable (D6).
// `samples` reports the cell's resolved sample count (0 when there is no cell) for the debug views.
bool SurfaceCacheQuery(SurfaceCacheParams p, float3 pos, float3 n, float3 cameraPos, uint lodBias,
                       out float3 sun, out float3 other, out uint samples) {
    sun = 0.0; other = 0.0; samples = 0u;
    const SurfaceCacheCellId id = SurfaceCacheLocate(p, pos, n, cameraPos, lodBias);
    const uint slot = SurfaceCacheFind(p, id);
    bool readable = false;
    if (slot != kSurfaceCacheNoSlot) {
        StructuredBuffer<uint4> resolved = ResourceDescriptorHeap[p.resolvedSrv];
        const uint4 rec = resolved[slot];
        samples  = SurfaceCacheMetaSamples(rec.w);
        readable = SurfaceCacheReadable(rec.w, p.minSamples);
        if (readable) {
            sun   = float3(f16tof32(rec.x), f16tof32(rec.x >> 16), f16tof32(rec.y));
            other = float3(f16tof32(rec.y >> 16), f16tof32(rec.z), f16tof32(rec.z >> 16));
            // Touch: a read cell does not age out. One atomic per cell per frame (the plain load
            // skips it once set), not one per ray.
            RWStructuredBuffer<uint> accum = ResourceDescriptorHeap[p.accumUav];
            const uint tags = slot * kSurfaceCacheAccumStride + kSurfaceCacheAccumTags;
            if ((accum[tags] & kSurfaceCacheTagTouched) == 0u) InterlockedOr(accum[tags], kSurfaceCacheTagTouched);
        }
    }
    SurfaceCacheStat(p, kSurfaceCacheStatQueries, true);
    SurfaceCacheStat(p, kSurfaceCacheStatHits, readable);
    return readable;
}

// Stage 3: the SDF consumers' read (D8). `pos` is the hit already snapped to the raw surface
// (SurfaceCacheSnapDistance along the hit normal); the cell is `p.sdfLodBias` levels coarser than a
// TLAS hit's at the same camera distance, and the camera position is the record's (the SDF
// consumers that bind no PerFrameCB use the same one every writer used). Counted apart from the
// TLAS consumers' queries: with the SDF consumers read-only (SdfWrites off) every readable cell
// they find was filled by a TLAS writer, so hits / queries is the spec's C3 fraction.
bool SurfaceCacheQuerySdf(SurfaceCacheParams p, float3 pos, float3 n, out float3 sun, out float3 other) {
    uint samples;
    const bool readable = SurfaceCacheQuery(p, pos, n, SurfaceCacheCameraPos(p), p.sdfLodBias, sun, other, samples);
    SurfaceCacheStat(p, kSurfaceCacheStatSdfQueries, true);
    SurfaceCacheStat(p, kSurfaceCacheStatSdfHits, readable);
    return readable;
}

// Accumulates one sample of the two layers into the cell of (pos, n), inserting the key when the
// cell is new. Portable insertion (32-bit atomics): a CAS on the first key word claims an empty or
// tombstone slot, an atomic exchange then publishes the second word. The window between the two
// is the torn insert: a lane that finds the first word equal to its own and the second word
// unwritten waits for it with a few atomic re-reads (bounded, so a wave whose winner lane is
// parked behind the divergent branch cannot hang) and, if it is still unwritten, drops its sample
// for this frame rather than inserting a duplicate of the key down the chain — on the first frame
// of a fill every lane of a cell inserts at once, and duplicates would fill whole probe chains and
// lock other cells out until they age out. A failed probe sequence counts a failed insert and
// drops the sample (the consumer already shaded inline, so nothing is lost but the cache fill).
// Per-cell per-frame cap of kSurfaceCacheFrameCap samples: the count is incremented first and
// only the first cap winners add their channels, so the channel sums stay below 2^32 (the resolve
// clamps the count the same way).
static const uint kSurfaceCacheTornSpins = 16u;

// The second key word of slot `s`, re-read atomically while it is unwritten (0 or the tombstone
// marker the insert overwrites).
uint SurfaceCacheWaitSecondWord(RWStructuredBuffer<uint2> keys, uint s, uint y) {
    [loop]
    for (uint spin = 0u; spin < kSurfaceCacheTornSpins && (y == 0u || y == kSurfaceCacheTombstoneY); ++spin) {
        InterlockedCompareExchange(keys[s].y, 0u, 0u, y);
    }
    return y;
}

// The insertion + accumulation behind `SurfaceCacheUpdate` / `SurfaceCacheUpdateSurface`: returns
// the slot of the cell (kSurfaceCacheNoSlot when the probe sequence or a torn insert gave the frame
// up), whether this lane inserted the key, and whether its sample was accepted under the per-frame
// cap (the surface record is written for accepted samples only, so a hot cell's record stores stay
// bounded like its atomics).
uint SurfaceCacheUpdateSlot(SurfaceCacheParams p, float3 pos, float3 n, float3 cameraPos, uint lodBias,
                            float3 sun, float3 other, uint writerTag, out bool inserted, out bool accepted) {
    inserted = false;
    accepted = false;
    const SurfaceCacheCellId id = SurfaceCacheLocate(p, pos, n, cameraPos, lodBias);
    RWStructuredBuffer<uint2> keys  = ResourceDescriptorHeap[p.keysUav];
    RWStructuredBuffer<uint>  accum = ResourceDescriptorHeap[p.accumUav];
    const uint mask = (1u << p.entriesLog2) - 1u;
    uint slot = kSurfaceCacheNoSlot;
    bool torn = false;      // gave the frame up on a torn insert of this key (not a failed insert)
    uint probes = 0u;
    [loop]
    for (uint i = 0u; i < kSurfaceCacheProbeSteps; ++i) {
        const uint  s = (id.slot + i) & mask;
        const uint2 k = keys[s];
        probes = i + 1u;
        if (k.x == id.key.x) {
            // Our first word: the cell, a torn insert of it, or a foreign key sharing the word.
            const uint y = SurfaceCacheWaitSecondWord(keys, s, k.y);
            if (y == id.key.y) { slot = s; break; }
            if (y == 0u || y == kSurfaceCacheTombstoneY) { torn = true; break; }
            continue;
        }
        if (SurfaceCacheKeyFree(k)) {
            uint previous;
            InterlockedCompareExchange(keys[s].x, 0u, id.key.x, previous);
            if (previous == 0u) {
                uint old;
                InterlockedExchange(keys[s].y, id.key.y, old);   // publishes the key
                slot = s;
                inserted = true;
                break;
            }
            if (previous == id.key.x) {
                const uint y = SurfaceCacheWaitSecondWord(keys, s, 0u);
                if (y == id.key.y) { slot = s; break; }
                if (y == 0u || y == kSurfaceCacheTombstoneY) { torn = true; break; }
            }
            // Taken by a foreign key this instant: probe on.
        }
    }
    SurfaceCacheStat(p, kSurfaceCacheStatFailedInserts, slot == kSurfaceCacheNoSlot && !torn);
    SurfaceCacheStat(p, kSurfaceCacheStatTorn, torn);
    SurfaceCacheStatSum(p, kSurfaceCacheStatProbeSum, probes);

    SurfaceCacheStat(p, kSurfaceCacheStatInserts, inserted);
    SurfaceCacheStatMax(p, kSurfaceCacheStatMaxProbe, probes);
    if (slot == kSurfaceCacheNoSlot) return kSurfaceCacheNoSlot;

    const uint base = slot * kSurfaceCacheAccumStride;
    uint previousCount;
    InterlockedAdd(accum[base + kSurfaceCacheAccumCount], 1u, previousCount);
    accepted = previousCount < kSurfaceCacheFrameCap;
    SurfaceCacheStat(p, kSurfaceCacheStatUpdates, accepted);
    SurfaceCacheStat(p, kSurfaceCacheStatFeedUpdates, accepted && writerTag == kSurfaceCacheWriterFeed);
    if (!accepted) return slot;
    InterlockedAdd(accum[base + kSurfaceCacheAccumSunR + 0u],   SurfaceCachePackSample(sun.x,   p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumSunR + 1u],   SurfaceCachePackSample(sun.y,   p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumSunR + 2u],   SurfaceCachePackSample(sun.z,   p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumOtherR + 0u], SurfaceCachePackSample(other.x, p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumOtherR + 1u], SurfaceCachePackSample(other.y, p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumOtherR + 2u], SurfaceCachePackSample(other.z, p.maxRadiance));
    InterlockedOr(accum[base + kSurfaceCacheAccumTags], writerTag);
    return slot;
}

void SurfaceCacheUpdate(SurfaceCacheParams p, float3 pos, float3 n, float3 cameraPos, uint lodBias,
                        float3 sun, float3 other, uint writerTag) {
    bool inserted, accepted;
    SurfaceCacheUpdateSlot(p, pos, n, cameraPos, lodBias, sun, other, writerTag, inserted, accepted);
}

// Stage 4: the TLAS writers' update, which also records the hit in the cell's surface record
// (`SurfaceCacheParams::surfaceUav`; nothing more than the stage 1-3 update while the record is
// unbound): the lifted hit position the cell was keyed from, the shading normal, the exact-material
// albedo and the emission — what the relight pass re-shades and the cell-indirect pass gathers from.
// One writer claims the persistent representative. Mixing the coordinates/material/normal
// of competing points is not a valid surface record. Relight runs before writers, and its
// next dispatch sees the complete record after the pass UAV barrier.
void SurfaceCacheUpdateSurface(SurfaceCacheParams p, float3 pos, float3 n, float3 cameraPos, uint lodBias,
                               float3 sun, float3 other, uint writerTag, float3 albedo, float3 emissive) {
    bool inserted, accepted;
    const uint slot = SurfaceCacheUpdateSlot(p, pos, n, cameraPos, lodBias, sun, other, writerTag, inserted, accepted);
    if (slot == kSurfaceCacheNoSlot || !accepted || p.surfaceUav == kSurfaceCacheSentinel) return;
    RWStructuredBuffer<uint> surface = ResourceDescriptorHeap[p.surfaceUav];
    const uint base = slot * kSurfaceCacheSurfaceStride;
    uint previous;
    InterlockedCompareExchange(surface[base + kSurfaceCacheSurfaceAlbedo], 0u, 0xFFFFFFFFu, previous);
    if (previous != 0u) return;
    surface[base + kSurfaceCacheSurfacePosition + 0u] = asuint(pos.x);
    surface[base + kSurfaceCacheSurfacePosition + 1u] = asuint(pos.y);
    surface[base + kSurfaceCacheSurfacePosition + 2u] = asuint(pos.z);
    surface[base + kSurfaceCacheSurfaceNormal]        = SurfaceCacheEncodeNormal(n);
    surface[base + kSurfaceCacheSurfaceAlbedo]        = SurfaceCachePackAlbedoTag(albedo, writerTag);
    surface[base + kSurfaceCacheSurfaceEmissive + 0u] = f32tof16(max(emissive.x, 0.0)) | (f32tof16(max(emissive.y, 0.0)) << 16);
    surface[base + kSurfaceCacheSurfaceEmissive + 1u] = f32tof16(max(emissive.z, 0.0));
    surface[base + kSurfaceCacheSurfaceMeta] = SurfaceCachePackSurfaceMeta(p.lightingGeneration, 0u);
}

// Stage 4: the record of slot `slot` for the relight / cell-indirect passes. False without a record.
bool SurfaceCacheReadSurface(SurfaceCacheParams p, uint slot, out float3 pos, out float3 n, out float3 albedo,
                             out float3 emissive, out uint meta) {
    pos = 0.0; n = float3(0.0, 0.0, 1.0); albedo = 0.0; emissive = 0.0; meta = 0u;
    if (p.surfaceUav == kSurfaceCacheSentinel) return false;
    RWStructuredBuffer<uint> surface = ResourceDescriptorHeap[p.surfaceUav];
    const uint base = slot * kSurfaceCacheSurfaceStride;
    const uint albedoTag = surface[base + kSurfaceCacheSurfaceAlbedo];
    if (!SurfaceCacheRecordExists(albedoTag)) return false;
    pos = float3(asfloat(surface[base + kSurfaceCacheSurfacePosition + 0u]),
                 asfloat(surface[base + kSurfaceCacheSurfacePosition + 1u]),
                 asfloat(surface[base + kSurfaceCacheSurfacePosition + 2u]));
    n        = SurfaceCacheDecodeNormal(surface[base + kSurfaceCacheSurfaceNormal]);
    albedo   = SurfaceCacheUnpackAlbedo(albedoTag);
    const uint e0 = surface[base + kSurfaceCacheSurfaceEmissive + 0u];
    const uint e1 = surface[base + kSurfaceCacheSurfaceEmissive + 1u];
    emissive = float3(f16tof32(e0), f16tof32(e0 >> 16), f16tof32(e1));
    meta     = surface[base + kSurfaceCacheSurfaceMeta];
    return all(isfinite(pos)) && all(isfinite(emissive));
}

// Stage 4: the resolved record of slot `slot` unpacked (the relight / indirect passes rewrite one
// layer and keep the other and the metadata), and its packing.
void SurfaceCacheUnpackResolved(uint4 rec, out float3 sun, out float3 other) {
    sun   = float3(f16tof32(rec.x), f16tof32(rec.x >> 16), f16tof32(rec.y));
    other = float3(f16tof32(rec.y >> 16), f16tof32(rec.z), f16tof32(rec.z >> 16));
}
uint4 SurfaceCachePackResolved(float3 sun, float3 other, uint meta) {
    uint4 rec;
    rec.x = f32tof16(sun.x)   | (f32tof16(sun.y)   << 16);
    rec.y = f32tof16(sun.z)   | (f32tof16(other.x) << 16);
    rec.z = f32tof16(other.y) | (f32tof16(other.z) << 16);
    rec.w = meta;
    return rec;
}

// ─── Debug views (`r.GI.SurfaceCache.Debug`, bits 4-5 of the flags) ──────────────────────────

uint SurfaceCacheDebugMode(SurfaceCacheParams p) {
    return (p.flags & kSurfaceCacheFlagDebugMask) >> kSurfaceCacheFlagDebugShift;
}

// 1: the cell's hash as a colour (cell structure and level boundaries); 2: sample-count heat
// (black = no cell, blue -> green -> red over 0..4096 samples); 3: hit mask (green = readable
// cell, yellow = cell below minSamples, red = no cell).
float3 SurfaceCacheDebugColor(SurfaceCacheParams p, float3 pos, float3 n, float3 cameraPos, uint lodBias,
                              bool readable, uint samples) {
    const uint mode = SurfaceCacheDebugMode(p);
    if (mode == 1u) {
        const SurfaceCacheCellId id = SurfaceCacheLocate(p, pos, n, cameraPos, lodBias);
        const uint h = SurfaceCacheHash(id.key);
        return float3((h & 255u) / 255.0, ((h >> 8) & 255u) / 255.0, ((h >> 16) & 255u) / 255.0);
    }
    if (mode == 2u) {
        const float t = saturate(float(samples) / 4096.0);
        return samples == 0u ? float3(0.0, 0.0, 0.0)
             : (t < 0.5 ? lerp(float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 0.0), t * 2.0)
                        : lerp(float3(0.0, 1.0, 0.0), float3(1.0, 0.0, 0.0), t * 2.0 - 1.0));
    }
    return readable ? float3(0.0, 1.0, 0.0) : (samples > 0u ? float3(1.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0));
}

#endif
