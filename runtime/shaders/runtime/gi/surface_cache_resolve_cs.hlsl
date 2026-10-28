/// UzEngine Surface Radiance Cache — per-frame resolve (Phase D stage 1). One thread per hash
/// entry: folds the frame's fixed-point accumulators into the resolved record's per-frame EMA
/// (D5), ages and evicts stale cells, evicts cells inside this frame's distance-field change boxes
/// (D11), clears the accumulators, counts the live cells, and (mode 1) clears the whole table.
/// Runs before every consumer of the frame, so they read the freshest mean (one frame of latency,
/// the `GetCompletedGridOrigin()` rule of the SH buffer). An eviction leaves a tombstone (the keys
/// inserted past the slot in their probe chains stay reachable); only the clear writes empties.
/// Stage 4: the surface record of a cleared or evicted slot is erased with it, and a cell the
/// cell-indirect pass owns (visited at least once, `kSurfaceCacheResolveFlagGatherOwnsOther`) folds
/// only its `sun` layer here — its `other` is the gather's EMA.
///
/// Bindings: root constants at b0 (`uz::SurfaceCacheResolveRootConstants`, 12 DWORDs), the change
/// boxes at b2 (`uz::SurfaceCacheResolveRegionsCB`, the `HistoryCB` region rows of the SSGI
/// temporal pass). Dispatch entries / 64 groups.
///
/// @ref docs/specs/rendering/surface-radiance-cache.md section 2.4 (the resolve), 2.6 (invalidation)

#include "surface_cache_contract.hlsli"

cbuffer SurfaceCacheResolveRootCB : register(b0) {
    uint  keysUav;        // 0  RWStructuredBuffer<uint2>
    uint  accumUav;       // 1  RWStructuredBuffer<uint>, kSurfaceCacheAccumStride per entry
    uint  resolvedUav;    // 2  RWStructuredBuffer<uint4>
    uint  statsUav;       // 3  RWStructuredBuffer<uint>, kSurfaceCacheStatWords (sentinel = no stats)
    uint  entriesLog2;    // 4
    uint  accumFrames;    // 5  EMA horizon (r.GI.SurfaceCache.AccumFrames)
    uint  staleFrames;    // 6  eviction age (r.GI.SurfaceCache.StaleFrames)
    uint  mode;           // 7  kSurfaceCacheResolveMode*
    float cellSize;       // 8  level-0 cell, cm (cell centres for the change-box test)
    uint  regionCount;    // 9  change boxes in the b2 block (<= kSurfaceCacheMaxRegions)
    uint  frame;          // 10 stats block = frame & 1
    uint  surfaceUav;     // 11 stage 4: RWStructuredBuffer<uint> surface records (sentinel = none)
};

cbuffer SurfaceCacheResolveRegionsCB : register(b2) {
    float4 regionLo[16];
    float4 regionHi[16];
};

static const uint kThreads = 64u;

bool InChangedRegion(float3 p, float halfCell) {
    [loop]
    for (uint r = 0u; r < min(regionCount, kSurfaceCacheMaxRegions); ++r) {
        if (all(p + halfCell >= regionLo[r].xyz) && all(p - halfCell <= regionHi[r].xyz)) return true;
    }
    return false;
}

void StatAdd(uint index, bool count) {
    if (statsUav == kSurfaceCacheSentinel) return;
    const uint n = WaveActiveCountBits(count);
    if (WaveIsFirstLane() && n > 0u) {
        RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[statsUav];
        InterlockedAdd(stats[(frame & 1u) * kSurfaceCacheStatCount + index], n);
    }
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint entry = DTid.x;
    if (entry >= (1u << entriesLog2)) return;

    RWStructuredBuffer<uint2> keys     = ResourceDescriptorHeap[keysUav];
    RWStructuredBuffer<uint>  accum    = ResourceDescriptorHeap[accumUav];
    RWStructuredBuffer<uint4> resolved = ResourceDescriptorHeap[resolvedUav];
    const uint base = entry * kSurfaceCacheAccumStride;

    // The next frame's stats block starts at zero (this frame's block is being filled by the
    // consumers after this dispatch and is read back before the next resolve).
    if (entry < kSurfaceCacheStatCount && statsUav != kSurfaceCacheSentinel) {
        RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[statsUav];
        stats[((frame + 1u) & 1u) * kSurfaceCacheStatCount + entry] = 0u;
    }

    const bool haveSurface = surfaceUav != kSurfaceCacheSentinel;
    const uint surfaceBase = entry * kSurfaceCacheSurfaceStride;
    if ((mode & kSurfaceCacheResolveModeClear) != 0u) {
        keys[entry]     = uint2(0u, 0u);
        resolved[entry] = uint4(0u, 0u, 0u, 0u);
        [unroll]
        for (uint w = 0u; w < kSurfaceCacheAccumStride; ++w) accum[base + w] = 0u;
        if (haveSurface) {
            RWStructuredBuffer<uint> surface = ResourceDescriptorHeap[surfaceUav];
            [unroll]
            for (uint s = 0u; s < kSurfaceCacheSurfaceStride; ++s) surface[surfaceBase + s] = 0u;
        }
        return;
    }

    const uint2 key = keys[entry];
    bool live  = SurfaceCacheKeyLive(key);          // neither empty nor a tombstone
    bool evict = false;
    uint4 rec  = uint4(0u, 0u, 0u, 0u);

    if (live) {
        // Change-box eviction: the cell centre from the key (level + coords) against the boxes.
        if (regionCount > 0u) {
            const float  size   = SurfaceCacheCellSize(cellSize, SurfaceCacheKeyLevel(key));
            const int3   cell   = SurfaceCacheKeyCell(key);
            const float3 centre = (float3(cell) + 0.5) * size;
            evict = InChangedRegion(centre, 0.5 * size);
        }
        if (!evict) {
            rec = resolved[entry];
            const uint count = min(accum[base + kSurfaceCacheAccumCount], kSurfaceCacheFrameCap);
            uint samples = SurfaceCacheMetaSamples(rec.w);
            uint frames  = SurfaceCacheMetaFrames(rec.w);
            uint age     = SurfaceCacheMetaAge(rec.w);
            if (count > 0u) {
                frames = SurfaceCacheNextFrames(frames, max(accumFrames, 1u));
                float3 sun   = float3(f16tof32(rec.x), f16tof32(rec.x >> 16), f16tof32(rec.y));
                float3 other = float3(f16tof32(rec.y >> 16), f16tof32(rec.z), f16tof32(rec.z >> 16));
                // Stage 4: a cell the cell-indirect pass has visited keeps the gather's `other`
                // (the inline samples seeded it and now feed only `sun`).
                bool foldOther = true;
                bool foldSun = true;
                if ((mode & kSurfaceCacheResolveFlagRelightOwnsSun) != 0u && haveSurface && samples > 0u) {
                    RWStructuredBuffer<uint> surface = ResourceDescriptorHeap[surfaceUav];
                    foldSun = !SurfaceCacheRecordExists(surface[surfaceBase + kSurfaceCacheSurfaceAlbedo]);
                }
                if ((mode & kSurfaceCacheResolveFlagGatherOwnsOther) != 0u && haveSurface) {
                    RWStructuredBuffer<uint> surface = ResourceDescriptorHeap[surfaceUav];
                    foldOther = SurfaceCacheSurfaceVisits(surface[surfaceBase + kSurfaceCacheSurfaceMeta]) == 0u;
                }
                [unroll]
                for (uint c = 0u; c < 3u; ++c) {
                    if (foldSun) sun[c] = SurfaceCacheEma(sun[c], SurfaceCacheUnpackMean(accum[base + kSurfaceCacheAccumSunR + c], count), frames);
                    if (foldOther) {
                        other[c] = SurfaceCacheEma(other[c], SurfaceCacheUnpackMean(accum[base + kSurfaceCacheAccumOtherR + c], count), frames);
                    }
                }
                samples = SurfaceCacheNextSamples(samples, count);
                age     = 0u;
                rec.x = f32tof16(sun.x)   | (f32tof16(sun.y)   << 16);
                rec.y = f32tof16(sun.z)   | (f32tof16(other.x) << 16);
                rec.z = f32tof16(other.y) | (f32tof16(other.z) << 16);
            } else if ((accum[base + kSurfaceCacheAccumTags] & kSurfaceCacheTagTouched) != 0u) {
                age = 0u;                                    // read this frame: not stale
            } else {
                age = min(age + 1u, 255u);
                evict = age >= staleFrames;
            }
            rec.w = SurfaceCachePackMeta(samples, frames, age);
        }
    }

    if (evict) {
        // A tombstone, not an empty slot: the probe chains through this slot stay intact.
        keys[entry] = uint2(0u, kSurfaceCacheTombstoneY);
        rec = uint4(0u, 0u, 0u, 0u);
        live = false;
        if (haveSurface) {
            // Stage 4: no record (tag 0) and no stale bookkeeping for the slot's next tenant.
            RWStructuredBuffer<uint> surface = ResourceDescriptorHeap[surfaceUav];
            surface[surfaceBase + kSurfaceCacheSurfaceAlbedo] = 0u;
            surface[surfaceBase + kSurfaceCacheSurfaceMeta]   = 0u;
        }
    }
    if (live || evict) {
        resolved[entry] = rec;
        [unroll]
        for (uint w = 0u; w < kSurfaceCacheAccumStride; ++w) accum[base + w] = 0u;
    }
    StatAdd(kSurfaceCacheStatEvictions, evict);
    StatAdd(kSurfaceCacheStatLive, live);
    StatAdd(kSurfaceCacheStatTombstones, !live && !SurfaceCacheKeyEmpty(key) && !evict);
}
