// Shared CPU/shader contract of the surface radiance cache (Phase D stage 1): the world-space
// spatial hash of outgoing diffuse radiance at ray hits — camera-distance level, cell and key
// packing, the 64 -> 32 bit slot hash, the fixed-point sample packing and the resolved-record
// metadata. Scalar math only, so the gtests (tests/engine/renderer/test_surface_cache.cpp)
// evaluate the same expressions the shaders run.
//
// @ref docs/specs/rendering/surface-radiance-cache.md — decisions D1-D7 (storage, key, level rule,
//      stored quantity, accumulation, validity, reader policy)
// @ref NVIDIA SHaRC (RTXGI 2.0, 2024) — hash-grid radiance cache with camera-distance levels
// @ref Binder, Fricke, Keller 2018 "Fast Path Space Filtering by Jittered Spatial Hashing"
#ifndef UZ_SURFACE_CACHE_CONTRACT
#define UZ_SURFACE_CACHE_CONTRACT

#ifdef __cplusplus
#include "vector3.h"
#include <cmath>
#include <cstdint>
namespace uz::surfacecache {
using float3 = Vec3;
using uint = uint32_t;
struct uint2 { uint x; uint y; };
struct int3 { int x; int y; int z; };
// The header is included by more than one C++ translation unit; HLSL has no linkage.
#define UZ_SC_FUNC inline
UZ_SC_FUNC float SurfaceCacheFloor(float v) { return std::floor(v); }
UZ_SC_FUNC float SurfaceCacheAbs(float v) { return std::fabs(v); }
#else
#define UZ_SC_FUNC
UZ_SC_FUNC float SurfaceCacheFloor(float v) { return floor(v); }
UZ_SC_FUNC float SurfaceCacheAbs(float v) { return abs(v); }
#endif

// ─── Sizes and limits (D1, D2, D5) ────────────────────────────────────────────────────────────

static const uint  kSurfaceCacheLevels      = 16u;            // 4 bits of level
static const uint  kSurfaceCacheCoordBits   = 18u;            // signed per axis: +-131072 cells
static const uint  kSurfaceCacheCoordOffset = 131072u;        // 2^17: signed -> unsigned coordinate
static const uint  kSurfaceCacheCoordMask   = 262143u;        // 2^18 - 1
static const uint  kSurfaceCacheProbeSteps  = 8u;             // linear probing length
static const float kSurfaceCacheScale       = 1024.0f;        // fixed-point accumulator scale (2^10)
static const uint  kSurfaceCacheFrameCap    = 1024u;          // samples per cell per frame
// Largest packed sample: kSurfaceCacheFrameCap of them sum below 2^32 (overflow-proof by construction).
static const uint  kSurfaceCachePackMax     = 4194303u;       // floor((2^32 - 1) / kSurfaceCacheFrameCap)
static const uint  kSurfaceCacheAccumStride = 8u;             // uints per accumulator entry
static const uint  kSurfaceCacheSentinel    = 0xFFFFFFFFu;    // "not bound" descriptor index
static const uint  kSurfaceCacheMaxRegions  = 16u;            // change boxes per resolve

// Accumulator words (per entry, `kSurfaceCacheAccumStride` uints).
static const uint kSurfaceCacheAccumSunR   = 0u;
static const uint kSurfaceCacheAccumOtherR = 3u;
static const uint kSurfaceCacheAccumCount  = 6u;
static const uint kSurfaceCacheAccumTags   = 7u;

// Writer tags (`accum[7]`, a bit mask of the writers that updated the cell this frame) and the
// touch bit a reader sets on a hit: a cell that is read does not age out, only one nobody visits.
static const uint kSurfaceCacheWriterReflections = 1u;
static const uint kSurfaceCacheTagTouched        = 0x80000000u;

// Stats words. Two blocks of kSurfaceCacheStatCount words, block `frame & 1` for frame `frame`;
// the resolve pass zeroes the other block for the next frame.
static const uint kSurfaceCacheStatInserts       = 0u;
static const uint kSurfaceCacheStatFailedInserts = 1u;
static const uint kSurfaceCacheStatUpdates       = 2u;
static const uint kSurfaceCacheStatQueries       = 3u;
static const uint kSurfaceCacheStatHits          = 4u;
static const uint kSurfaceCacheStatEvictions     = 5u;
static const uint kSurfaceCacheStatLive          = 6u;
static const uint kSurfaceCacheStatMaxProbe      = 7u;
static const uint kSurfaceCacheStatTombstones    = 8u;        // evicted slots not yet reused (resolve)
static const uint kSurfaceCacheStatTorn          = 9u;        // updates that gave the frame up on a torn insert
static const uint kSurfaceCacheStatProbeSum      = 10u;       // probe steps summed over the updates
static const uint kSurfaceCacheStatCount         = 16u;
static const uint kSurfaceCacheStatWords         = 32u;       // two blocks

// `SurfaceCacheParams::flags`.
static const uint kSurfaceCacheFlagDirectInline = 1u;        // D7 DirectMode 1: inline sun, cached `other`
static const uint kSurfaceCacheFlagJitter       = 2u;        // +-0.5 cell lookup jitter
static const uint kSurfaceCacheFlagSdfWrites    = 4u;        // stage 3: SDF consumers write
static const uint kSurfaceCacheFlagStats        = 8u;        // count queries / hits / inserts
static const uint kSurfaceCacheFlagDebugShift   = 4u;        // debug view in bits 4-5 (0 off, 1 cell, 2 heat, 3 mask)
static const uint kSurfaceCacheFlagDebugMask    = 48u;

// Resolve pass modes (`SurfaceCacheResolveRootConstants::mode`).
static const uint kSurfaceCacheResolveModeResolve = 0u;
static const uint kSurfaceCacheResolveModeClear   = 1u;

// ─── The binding record (D10): one element of a StructuredBuffer, 64 bytes ──────────────────
// Mirrored by `uz::SurfaceCacheParamsGPU` (gi/surface_radiance_cache.h); the layout test and the
// gtest pin the two equal.

struct SurfaceCacheParams {
    uint  keysUav;          // RWStructuredBuffer<uint2>  keys (0,0 = empty)
    uint  accumUav;         // RWStructuredBuffer<uint>   accum, kSurfaceCacheAccumStride per entry
    uint  resolvedSrv;      // StructuredBuffer<uint4>    resolved records
    uint  surfaceUav;       // stage 4 surface record (sentinel before stage 4)
    uint  entriesLog2;
    uint  minSamples;       // D6 validity threshold
    uint  flags;            // kSurfaceCacheFlag*
    uint  frame;
    float cellSize;         // level-0 cell, cm
    float lodDistance;      // camera distance at which the cell first doubles, cm
    float maxRadiance;      // per-sample clamp
    float _pad0;
    uint  sdfLodBias;
    uint  statsUav;         // RWStructuredBuffer<uint> stats (sentinel = no stats)
    uint  generation;
    uint  refreshRate;      // a hit shades inline and updates its cell once in refreshRate rays (0 = never)
};

// ─── Level and cell (D2, D3) ─────────────────────────────────────────────────────────────────

// level = clamp(floor(log2(max(dist / lodDistance, 1))), 0, 15), computed on the integer part of
// the ratio so the C++ mirror and the shader agree bit for bit at the power-of-two boundaries.
UZ_SC_FUNC uint SurfaceCacheLevel(float cameraDistance, float lodDistance) {
    if (!(lodDistance > 0.0f)) return 0u;
    float ratio = cameraDistance / lodDistance;
    if (!(ratio >= 2.0f)) return 0u;
    if (ratio > 65535.0f) ratio = 65535.0f;
    uint r = (uint)ratio;
    uint level = 0u;
    while (r > 1u) { r >>= 1u; ++level; }
    return level;
}

UZ_SC_FUNC float SurfaceCacheCellSize(float cellSize, uint level) {
    return cellSize * (float)(1u << level);
}

// 0..5 = +X, -X, +Y, -Y, +Z, -Z by the largest |component|; ties go to X, then Y.
UZ_SC_FUNC uint SurfaceCacheNormalIndex(float3 n) {
    float ax = SurfaceCacheAbs(n.x);
    float ay = SurfaceCacheAbs(n.y);
    float az = SurfaceCacheAbs(n.z);
    if (ax >= ay && ax >= az) return n.x >= 0.0f ? 0u : 1u;
    if (ay >= az) return n.y >= 0.0f ? 2u : 3u;
    return n.z >= 0.0f ? 4u : 5u;
}

UZ_SC_FUNC int SurfaceCacheCellCoord(float p, float cellSize) {
    return (int)SurfaceCacheFloor(p / cellSize);
}

UZ_SC_FUNC int3 SurfaceCacheCell(float3 p, float cellSize) {
    int3 cell;
    cell.x = SurfaceCacheCellCoord(p.x, cellSize);
    cell.y = SurfaceCacheCellCoord(p.y, cellSize);
    cell.z = SurfaceCacheCellCoord(p.z, cellSize);
    return cell;
}

// ─── Key packing (D2): lo = valid(31) | x(18) | y low 13; hi = valid(31) | y high 5 | z(18) |
//     normal(3) | level(4) | spare(1). Both words carry the valid bit, so a key is never (0,0).

UZ_SC_FUNC uint SurfaceCacheClampCoord(int c) {
    int u = c + (int)kSurfaceCacheCoordOffset;
    if (u < 0) u = 0;
    if (u > (int)kSurfaceCacheCoordMask) u = (int)kSurfaceCacheCoordMask;
    return (uint)u;
}

UZ_SC_FUNC uint2 SurfaceCacheKey(int3 cell, uint normalIndex, uint level) {
    uint ux = SurfaceCacheClampCoord(cell.x);
    uint uy = SurfaceCacheClampCoord(cell.y);
    uint uz = SurfaceCacheClampCoord(cell.z);
    uint2 key;
    key.x = 0x80000000u | (ux << 13) | (uy & 0x1FFFu);
    key.y = 0x80000000u | (((uy >> 13) & 0x1Fu) << 26) | (uz << 8) | ((normalIndex & 7u) << 5)
          | ((level & 15u) << 1);
    return key;
}

UZ_SC_FUNC int3 SurfaceCacheKeyCell(uint2 key) {
    uint ux = (key.x >> 13) & kSurfaceCacheCoordMask;
    uint uy = (key.x & 0x1FFFu) | (((key.y >> 26) & 0x1Fu) << 13);
    uint uz = (key.y >> 8) & kSurfaceCacheCoordMask;
    int3 cell;
    cell.x = (int)ux - (int)kSurfaceCacheCoordOffset;
    cell.y = (int)uy - (int)kSurfaceCacheCoordOffset;
    cell.z = (int)uz - (int)kSurfaceCacheCoordOffset;
    return cell;
}

UZ_SC_FUNC uint SurfaceCacheKeyNormal(uint2 key) { return (key.y >> 5) & 7u; }
UZ_SC_FUNC uint SurfaceCacheKeyLevel(uint2 key)  { return (key.y >> 1) & 15u; }

// Slot states. An evicted slot becomes a TOMBSTONE (x = 0, y = kSurfaceCacheTombstoneY), not an
// empty slot: readers probe past it (the keys inserted beyond it in the chain stay reachable) and
// writers may reuse it (the CAS on x = 0 claims empties and tombstones alike). Only a clear writes
// (0, 0). The second word of a torn insert of this frame is 0 or the marker, so a reader that sees
// the first word match and the second differ keeps probing.
static const uint kSurfaceCacheTombstoneY = 1u;
UZ_SC_FUNC bool SurfaceCacheKeyEmpty(uint2 key) { return key.x == 0u && key.y == 0u; }   // end of chain
UZ_SC_FUNC bool SurfaceCacheKeyFree(uint2 key)  { return key.x == 0u; }                   // insertable
UZ_SC_FUNC bool SurfaceCacheKeyLive(uint2 key)  { return key.x != 0u; }

// ─── Slot hash: XXH32 of the two key words (seed 0), masked to the table ────────────────────

UZ_SC_FUNC uint SurfaceCacheRotl(uint x, uint r) { return (x << r) | (x >> (32u - r)); }

UZ_SC_FUNC uint SurfaceCacheHash(uint2 key) {
    uint h = 374761393u + 8u;                                    // PRIME32_5 + length
    h += key.x * 3266489917u; h = SurfaceCacheRotl(h, 17u) * 668265263u;
    h += key.y * 3266489917u; h = SurfaceCacheRotl(h, 17u) * 668265263u;
    h ^= h >> 15; h *= 2246822519u;
    h ^= h >> 13; h *= 3266489917u;
    h ^= h >> 16;
    return h;
}

UZ_SC_FUNC uint SurfaceCacheSlot(uint2 key, uint entriesLog2) {
    return SurfaceCacheHash(key) & ((1u << entriesLog2) - 1u);
}

// ─── Lookup jitter: +-0.5 cell, seeded by (cell, frame) only — deterministic within a frame ──

UZ_SC_FUNC float SurfaceCacheJitterComponent(uint h) {
    return (float)(h & 1023u) * (1.0f / 1024.0f) - 0.5f;
}

UZ_SC_FUNC float3 SurfaceCacheJitter(int3 cell, uint frame) {
    uint2 seed;
    seed.x = ((uint)cell.x * 73856093u) ^ ((uint)cell.y * 19349663u);
    seed.y = ((uint)cell.z * 83492791u) ^ (frame * 2654435761u);
    uint h = SurfaceCacheHash(seed);
    float3 j;
    j.x = SurfaceCacheJitterComponent(h);
    j.y = SurfaceCacheJitterComponent(h >> 10);
    j.z = SurfaceCacheJitterComponent(h >> 20);
    return j;
}

// ─── Refresh: the subset of hits that keep feeding a readable cell ───────────────────────────
// Deterministic per (ray id, frame): one hit in `refreshRate` shades inline as a miss does and
// updates its cell, so a readable cell goes on converging toward the cell-and-time average
// (hundreds of samples) instead of freezing on the first `minSamples` ones. 0 = never.
UZ_SC_FUNC bool SurfaceCacheRefresh(uint rayId, uint frame, uint refreshRate) {
    if (refreshRate == 0u) return false;
    if (refreshRate == 1u) return true;
    uint2 seed;
    seed.x = rayId;
    seed.y = frame * 2654435761u;
    return (SurfaceCacheHash(seed) % refreshRate) == 0u;
}

// ─── Fixed-point samples (D5) ─────────────────────────────────────────────────────────────────

// clamp to [0, maxRadiance], scale by 2^10, round; capped so kSurfaceCacheFrameCap samples never
// overflow a 32-bit sum. NaN and negative radiance pack to 0.
UZ_SC_FUNC uint SurfaceCachePackSample(float v, float maxRadiance) {
    if (!(v > 0.0f)) return 0u;
    if (v > maxRadiance) v = maxRadiance;
    uint packed = (uint)(v * kSurfaceCacheScale + 0.5f);
    return packed > kSurfaceCachePackMax ? kSurfaceCachePackMax : packed;
}

UZ_SC_FUNC float SurfaceCacheUnpackMean(uint sum, uint count) {
    return (float)sum / (kSurfaceCacheScale * (float)count);
}

// ─── Resolved record metadata: samples u16 | frames u8 | age u8 ─────────────────────────────

UZ_SC_FUNC uint SurfaceCachePackMeta(uint samples, uint frames, uint age) {
    return (samples & 0xFFFFu) | ((frames & 0xFFu) << 16) | ((age & 0xFFu) << 24);
}
UZ_SC_FUNC uint SurfaceCacheMetaSamples(uint meta) { return meta & 0xFFFFu; }
UZ_SC_FUNC uint SurfaceCacheMetaFrames(uint meta)  { return (meta >> 16) & 0xFFu; }
UZ_SC_FUNC uint SurfaceCacheMetaAge(uint meta)     { return (meta >> 24) & 0xFFu; }

UZ_SC_FUNC bool SurfaceCacheReadable(uint meta, uint minSamples) {
    return SurfaceCacheMetaSamples(meta) >= minSamples;
}

// ─── The per-frame EMA (D5) ───────────────────────────────────────────────────────────────────

// frames after this frame's fold: 1 on the first frame with samples, then up to accumFrames.
UZ_SC_FUNC uint SurfaceCacheNextFrames(uint frames, uint accumFrames) {
    uint next = frames + 1u;
    return next < accumFrames ? next : accumFrames;
}

// mean <- lerp(mean, frameMean, 1 / frames): the running mean while frames < accumFrames, an
// exponential moving average with horizon accumFrames afterwards.
UZ_SC_FUNC float SurfaceCacheEma(float mean, float frameMean, uint frames) {
    return mean + (frameMean - mean) / (float)frames;
}

UZ_SC_FUNC uint SurfaceCacheNextSamples(uint samples, uint count) {
    uint next = samples + count;
    return next > 65535u ? 65535u : next;
}

#ifdef __cplusplus
} // namespace uz::surfacecache
#endif
#endif
