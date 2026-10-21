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

void SurfaceCacheUpdate(SurfaceCacheParams p, float3 pos, float3 n, float3 cameraPos, uint lodBias,
                        float3 sun, float3 other, uint writerTag) {
    const SurfaceCacheCellId id = SurfaceCacheLocate(p, pos, n, cameraPos, lodBias);
    RWStructuredBuffer<uint2> keys  = ResourceDescriptorHeap[p.keysUav];
    RWStructuredBuffer<uint>  accum = ResourceDescriptorHeap[p.accumUav];
    const uint mask = (1u << p.entriesLog2) - 1u;
    uint slot = kSurfaceCacheNoSlot;
    bool inserted = false;
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
    if (slot == kSurfaceCacheNoSlot) return;

    const uint base = slot * kSurfaceCacheAccumStride;
    uint previousCount;
    InterlockedAdd(accum[base + kSurfaceCacheAccumCount], 1u, previousCount);
    const bool accepted = previousCount < kSurfaceCacheFrameCap;
    SurfaceCacheStat(p, kSurfaceCacheStatUpdates, accepted);
    if (!accepted) return;
    InterlockedAdd(accum[base + kSurfaceCacheAccumSunR + 0u],   SurfaceCachePackSample(sun.x,   p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumSunR + 1u],   SurfaceCachePackSample(sun.y,   p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumSunR + 2u],   SurfaceCachePackSample(sun.z,   p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumOtherR + 0u], SurfaceCachePackSample(other.x, p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumOtherR + 1u], SurfaceCachePackSample(other.y, p.maxRadiance));
    InterlockedAdd(accum[base + kSurfaceCacheAccumOtherR + 2u], SurfaceCachePackSample(other.z, p.maxRadiance));
    InterlockedOr(accum[base + kSurfaceCacheAccumTags], writerTag);
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
