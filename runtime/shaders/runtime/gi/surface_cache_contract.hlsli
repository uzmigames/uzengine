// Shared CPU/shader contract of the surface radiance cache (Phase D stage 1): the world-space
// spatial hash of outgoing diffuse radiance at ray hits — camera-distance level, cell and key
// packing, the 64 -> 32 bit slot hash, the fixed-point sample packing and the resolved-record
// metadata. Stage 4 appends the surface record's packing, the relight slice schedule, the
// relight decision and the cell-indirect gather / blend. Scalar math only, so the gtests
// (tests/engine/renderer/test_surface_cache.cpp) evaluate the same expressions the shaders run.
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
UZ_SC_FUNC float SurfaceCachePow(float v, float e) { return std::pow(v, e); }
UZ_SC_FUNC float SurfaceCacheSqrt(float v) { return std::sqrt(v); }
UZ_SC_FUNC float SurfaceCacheCos(float v) { return std::cos(v); }
UZ_SC_FUNC float SurfaceCacheSin(float v) { return std::sin(v); }
#else
#define UZ_SC_FUNC
UZ_SC_FUNC float SurfaceCacheFloor(float v) { return floor(v); }
UZ_SC_FUNC float SurfaceCacheAbs(float v) { return abs(v); }
UZ_SC_FUNC float SurfaceCachePow(float v, float e) { return pow(v, e); }
UZ_SC_FUNC float SurfaceCacheSqrt(float v) { return sqrt(v); }
UZ_SC_FUNC float SurfaceCacheCos(float v) { return cos(v); }
UZ_SC_FUNC float SurfaceCacheSin(float v) { return sin(v); }
#endif

UZ_SC_FUNC float SurfaceCacheSaturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

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
// Stage 3: the farthest an SDF hit is moved back to the raw surface (cm). A registered hit sits in
// front of the raw surface by the field's expansion (half a voxel plus the mesh bias, metres only
// in the coarsest cascade); a raw distance beyond this is not a surface hit (or the -65504 marker
// of a missing raw cascade) and snaps nothing.
static const float kSurfaceCacheMaxSnap     = 1000.0f;
// Stage 3: the SDF consumers' lift off the surface and the TLAS writers' (kReflectionSurfaceBias,
// the DDGI writer's kSurfaceBias): every writer keys its cell from the hit lifted 2 cm along the
// normal, so the readers must key from the same point.
static const float kSurfaceCacheSurfaceBias = 2.0f;

// Accumulator words (per entry, `kSurfaceCacheAccumStride` uints).
static const uint kSurfaceCacheAccumSunR   = 0u;
static const uint kSurfaceCacheAccumOtherR = 3u;
static const uint kSurfaceCacheAccumCount  = 6u;
static const uint kSurfaceCacheAccumTags   = 7u;

// Writer tags (`accum[7]`, a bit mask of the writers that updated the cell this frame) and the
// touch bit a reader sets on a hit: a cell that is read does not age out, only one nobody visits.
static const uint kSurfaceCacheWriterReflections = 1u;
static const uint kSurfaceCacheWriterFeed        = 2u;        // stage 3: the probe-grid feed trace (TLAS, exact material)
static const uint kSurfaceCacheWriterDdgi        = 4u;        // stage 3: the DDGI probe trace (TLAS)
static const uint kSurfaceCacheWriterSdf         = 8u;        // stage 3: an SDF consumer (SdfWrites, diagnostic)
static const uint kSurfaceCacheWriterIndirect    = 16u;       // stage 4: the cell-indirect gather's exact-material shading of a cold hit
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
static const uint kSurfaceCacheStatSdfQueries    = 11u;       // stage 3: queries by the SDF consumers (snapped hits)
static const uint kSurfaceCacheStatSdfHits       = 12u;       // stage 3: of those, readable cells (C3: TLAS-written cells found)
static const uint kSurfaceCacheStatFeedUpdates   = 13u;       // stage 3: samples the feed trace accumulated
static const uint kSurfaceCacheStatRelit         = 14u;       // stage 4: cells whose sun layer the relight pass REPLACED (a lighting change caught up)
static const uint kSurfaceCacheStatIndirectRays  = 15u;       // stage 4: cosine rays the cell-indirect pass traced
static const uint kSurfaceCacheStatRadianceSum   = 16u;       // stage 4: sum over the record cells of luminance(sun + other) x kSurfaceCacheStatRadianceScale (cell-indirect Finish)
static const uint kSurfaceCacheStatRadianceCells = 17u;       // stage 4: the cells of that sum (the cached-radiance convergence probe = sum / cells)
static const uint kSurfaceCacheStatCount         = 18u;
static const uint kSurfaceCacheStatWords         = 36u;       // two blocks
static const float kSurfaceCacheStatRadianceScale = 256.0f;

// `SurfaceCacheParams::flags`.
static const uint kSurfaceCacheFlagDirectInline = 1u;        // D7 DirectMode 1: inline sun, cached `other`
static const uint kSurfaceCacheFlagJitter       = 2u;        // +-0.5 cell lookup jitter
static const uint kSurfaceCacheFlagSdfWrites    = 4u;        // stage 3: SDF consumers write
static const uint kSurfaceCacheFlagStats        = 8u;        // count queries / hits / inserts
static const uint kSurfaceCacheFlagDebugShift   = 4u;        // debug view in bits 4-5 (0 off, 1 cell, 2 heat, 3 mask)
static const uint kSurfaceCacheFlagDebugMask    = 48u;
static const uint kSurfaceCacheFlagEmissive     = 64u;       // stage 2: the hit's emission joins the `other` layer
static const uint kSurfaceCacheFlagSdfReads     = 128u;      // stage 3: the SDF consumers read (r.GI.SurfaceCache.SdfReads)
static const uint kSurfaceCacheFlagRelight      = 256u;      // stage 4: the relight schedule runs (a lighting change bumps the generation, no clear)
static const uint kSurfaceCacheFlagMultiBounce  = 512u;      // stage 4: the cell-indirect gather owns the indirect part of `other`

// ─── Stage 2: the frame's local lights at a shaded hit ───────────────────────────────────────────
// One record per point / spot / rect light of the frame (`SurfaceCacheParams::lightsSrv`, at most
// kSurfaceCacheMaxLights = the LightingCB's 512 + 128 point / spot plus the 16 rect lights); the
// `localLights` (K) strongest at a hit, by their unshadowed Lambert radiance, each get a shadow ray.

static const uint  kSurfaceCacheMaxLights       = 656u;
static const uint  kSurfaceCacheMaxLocalLights  = 16u;       // K upper bound (r.GI.SurfaceCache.LocalLights)
static const uint  kSurfaceCacheLightTypePoint  = 0u;
static const uint  kSurfaceCacheLightTypeSpot   = 1u;
static const uint  kSurfaceCacheLightTypeRect   = 2u;
static const uint  kSurfaceCacheLightTypeMask   = 3u;
static const uint  kSurfaceCacheLightInverseSquared = 4u;    // point / spot: PointLightData::flags bit 0
static const uint  kSurfaceCacheLightTwoSided   = 8u;        // rect: RectLightData::flags bit 0
static const float kSurfaceCachePi              = 3.14159265358979323846f;

// 80 bytes; mirrored by `uz::SurfaceCacheLightGPU` (gi/surface_radiance_cache.h). The deferred
// lighting's per-light scalars with the receiver-independent factors folded in: `color` carries
// diffuseScale (point / spot: colour x intensity, rect: radiance), the rect's `right` / `up` carry
// its half extents, its `direction` is the emission normal cross(right, up).
struct SurfaceCacheLight {
    float3 position;  float radius;                // point / spot: attenuation radius; rect: range (window)
    float3 color;     uint  type;                  // kSurfaceCacheLightType* | kSurfaceCacheLight* flags
    float3 direction; float cosOuterAngle;         // spot: cone axis, cos of the outer half-angle; rect: emission normal
    float3 right;     float invCosConeDifference;  // rect: right axis x halfWidth; spot: 1 / (cosInner - cosOuter)
    float3 up;        float falloffExponent;       // rect: up axis x halfHeight; point / spot: custom falloff exponent
};

// The deferred lighting's LocalLightAttenuation (deferred_lighting_ps.hlsl) with its range test:
// windowed inverse square (Karis 2013), or UE's custom falloff (no 1 / d^2) when the flag is
// clear. Zero beyond the radius and for a degenerate radius.
UZ_SC_FUNC float SurfaceCacheLightAttenuation(float distSq, float radius, float falloffExponent, bool inverseSquared) {
    if (!(radius > 0.0f) || !(distSq <= radius * radius)) return 0.0f;
    float f = distSq / (radius * radius);
    if (inverseSquared) {
        float s = SurfaceCacheSaturate(1.0f - f * f);
        return (s * s) / (distSq > 0.01f ? distSq : 0.01f);
    }
    return SurfaceCachePow(SurfaceCacheSaturate(1.0f - f), falloffExponent > 1e-3f ? falloffExponent : 1e-3f);
}

// The spot cone of ShadeSpotLight: squared smoothstep-free ramp between the outer and inner angles.
UZ_SC_FUNC float SurfaceCacheSpotCone(float cosAngle, float cosOuterAngle, float invCosConeDifference) {
    float t = SurfaceCacheSaturate((cosAngle - cosOuterAngle) * invCosConeDifference);
    return t * t;
}

// The rect light's range window of ShadeRectLight (squared; the 1 / d^2 is inside the form factor).
UZ_SC_FUNC float SurfaceCacheRectWindow(float distSq, float range) {
    float r2 = range * range > 1e-4f ? range * range : 1e-4f;
    float f = distSq / r2;
    float w = SurfaceCacheSaturate(1.0f - f * f);
    return w * w;
}

// Rec. 709 luminance: the relevance of a light at a hit (its unshadowed Lambert radiance).
UZ_SC_FUNC float SurfaceCacheLuminance(float3 c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

// Resolve pass modes (`SurfaceCacheResolveRootConstants::mode`): bit 0 clears the table; bit 1
// (stage 4) leaves the `other` layer of a cell the cell-indirect pass has visited to that pass
// (the inline writers' `other` samples seed a cell and then stop folding into it).
static const uint kSurfaceCacheResolveModeResolve        = 0u;
static const uint kSurfaceCacheResolveModeClear          = 1u;
static const uint kSurfaceCacheResolveFlagGatherOwnsOther = 2u;
static const uint kSurfaceCacheResolveFlagRelightOwnsSun = 4u;

// ─── The binding record (D10): one element of a StructuredBuffer, 96 bytes ──────────────────
// Mirrored by `uz::SurfaceCacheParamsGPU` (gi/surface_radiance_cache.h); the layout test and the
// gtest pin the two equal. Stage 2 appended the fifth row (the frame's lights and K); stage 3 the
// sixth (the camera position of the level rule D3 for the consumers that bind no PerFrameCB: the
// radiance-cache trace, the DDGI trace and the feed); stage 4 fills two pads (the cell-indirect
// EMA horizon, the lighting generation the relight pass compares a record against) and binds
// `surfaceUav`.

struct SurfaceCacheParams {
    uint  keysUav;          // RWStructuredBuffer<uint2>  keys (0,0 = empty)
    uint  accumUav;         // RWStructuredBuffer<uint>   accum, kSurfaceCacheAccumStride per entry
    uint  resolvedSrv;      // StructuredBuffer<uint4>    resolved records
    uint  surfaceUav;       // stage 4: RWStructuredBuffer<uint> surface records, kSurfaceCacheSurfaceStride per entry (sentinel = no record)
    uint  entriesLog2;
    uint  minSamples;       // D6 validity threshold
    uint  flags;            // kSurfaceCacheFlag*
    uint  frame;
    float cellSize;         // level-0 cell, cm
    float lodDistance;      // camera distance at which the cell first doubles, cm
    float maxRadiance;      // per-sample clamp
    uint  indirectHorizon;  // stage 4: visits of the cell-indirect EMA (r.GI.SurfaceCache.IndirectFrames)
    uint  sdfLodBias;
    uint  statsUav;         // RWStructuredBuffer<uint> stats (sentinel = no stats)
    uint  generation;
    uint  refreshRate;      // a hit shades inline and updates its cell once in refreshRate rays (0 = never)
    uint  lightsSrv;        // stage 2: StructuredBuffer<SurfaceCacheLight> of the frame (sentinel = no local lights)
    uint  lightCount;       // records in it (<= kSurfaceCacheMaxLights)
    uint  localLights;      // K: lights with a shadow ray per shaded hit (0 = none)
    uint  lightingGeneration; // stage 4: bumped by every lighting change while the relight runs (u16; records behind it are relit)
    float cameraPosX;       // stage 3: the frame's camera position (the level rule D3 for every consumer)
    float cameraPosY;
    float cameraPosZ;
    float _pad2;
};

#ifndef __cplusplus
float3 SurfaceCacheCameraPos(SurfaceCacheParams p) {
    return float3(p.cameraPosX, p.cameraPosY, p.cameraPosZ);
}
#endif

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

// ─── Stage 3: the SDF consumers' snap and the reader policy (D7, D8) ─────────────────────────

// The level an SDF-traced hit reads at: `sdfLodBias` levels coarser than a TLAS hit at the same
// camera distance (D8), clamped to the last level. The feed writes every hit at this level too
// (beside the base level), so the coarser cells the SDF consumers key exist.
UZ_SC_FUNC uint SurfaceCacheSdfLevel(float cameraDistance, float lodDistance, uint sdfLodBias) {
    uint level = SurfaceCacheLevel(cameraDistance, lodDistance) + sdfLodBias;
    return level < kSurfaceCacheLevels - 1u ? level : kSurfaceCacheLevels - 1u;
}

// The distance an SDF hit is moved back along its normal before the lookup: the field's raw
// (unexpanded) distance at the hit, which is how far in front of the raw surface the expanded-
// surface tracer registered it. A distance beyond `maxSnap` (a missing raw cascade reports
// -65504, a hit far from any surface is not a surface) or a non-finite one snaps nothing.
UZ_SC_FUNC float SurfaceCacheSnapDistance(float rawDistance, float maxSnap) {
    if (!(SurfaceCacheAbs(rawDistance) <= maxSnap)) return 0.0f;
    return rawDistance;
}

// The point an SDF consumer keys its cell from: the hit moved back along its normal to the raw
// surface (by the raw distance, when it is a surface distance), then lifted by the writers' bias
// (kSurfaceCacheSurfaceBias) - the point a TLAS writer keyed the same surface from.
UZ_SC_FUNC float3 SurfaceCacheSdfReadPosition(float3 hit, float3 n, float rawDistance) {
    float shift = SurfaceCacheSnapDistance(rawDistance, kSurfaceCacheMaxSnap) - kSurfaceCacheSurfaceBias;
    float3 p;
    p.x = hit.x - n.x * shift;
    p.y = hit.y - n.y * shift;
    p.z = hit.z - n.z * shift;
    return p;
}

// Reader policy (D7) as one expression: a readable cell replaces the WHOLE hit radiance - both
// layers, or the inline sun with the cell's `other` under DirectMode 1 - and a miss is the
// inline shading (sun + the rest) unchanged.
UZ_SC_FUNC float3 SurfaceCacheHitRadiance(bool readable, bool directInline, float3 cellSun, float3 cellOther,
                                          float3 inlineSun, float3 inlineRest) {
    if (!readable) return inlineSun + inlineRest;
    return (directInline ? inlineSun : cellSun) + cellOther;
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

// ─── Stage 4: the surface record ─────────────────────────────────────────────────────────────
// kSurfaceCacheSurfaceStride uints (32 bytes) per entry (`SurfaceCacheParams::surfaceUav`): the
// last TLAS writer's hit — a representative point of the cell the relight pass re-shades and the
// cell-indirect pass gathers from — plus the stage-4 bookkeeping. Plain stores, last writer wins;
// a record whose writer tag is 0 does not exist (the cell stays a lazy stage 1-3 cell).

static const uint kSurfaceCacheSurfaceStride   = 8u;
static const uint kSurfaceCacheSurfacePosition = 0u;   // [0..2] world position (float bits): the writer's lifted hit
static const uint kSurfaceCacheSurfaceNormal   = 3u;   // [3]    octahedral shading normal, 2 x unorm16
static const uint kSurfaceCacheSurfaceAlbedo   = 4u;   // [4]    albedo RGB8 (unorm) | writer tag in bits 24-31 (0 = no record)
static const uint kSurfaceCacheSurfaceEmissive = 5u;   // [5]    emissive r | g (halves); [6] emissive b | 0
static const uint kSurfaceCacheSurfaceMeta     = 7u;   // [7]    lit generation u16 | cell-indirect visits u8 | spare u8

// Octahedral normal, two unorm16 (Cigolle 2014): round trip within 1e-4 on a unit vector.
UZ_SC_FUNC uint SurfaceCachePackUnorm16(float v) {
    float s = SurfaceCacheSaturate(v) * 65535.0f + 0.5f;
    return (uint)s;
}
UZ_SC_FUNC float SurfaceCacheUnpackUnorm16(uint v) { return (float)(v & 0xFFFFu) * (1.0f / 65535.0f); }

UZ_SC_FUNC uint SurfaceCacheEncodeNormal(float3 n) {
    float l1 = SurfaceCacheAbs(n.x) + SurfaceCacheAbs(n.y) + SurfaceCacheAbs(n.z);
    if (!(l1 > 0.0f)) l1 = 1.0f;
    float px = n.x / l1;
    float py = n.y / l1;
    if (n.z < 0.0f) {
        float qx = (1.0f - SurfaceCacheAbs(py)) * (px >= 0.0f ? 1.0f : -1.0f);
        float qy = (1.0f - SurfaceCacheAbs(px)) * (py >= 0.0f ? 1.0f : -1.0f);
        px = qx; py = qy;
    }
    return SurfaceCachePackUnorm16(px * 0.5f + 0.5f) | (SurfaceCachePackUnorm16(py * 0.5f + 0.5f) << 16);
}

UZ_SC_FUNC float3 SurfaceCacheDecodeNormal(uint packed) {
    float px = SurfaceCacheUnpackUnorm16(packed) * 2.0f - 1.0f;
    float py = SurfaceCacheUnpackUnorm16(packed >> 16) * 2.0f - 1.0f;
    float3 n;
    n.x = px; n.y = py; n.z = 1.0f - SurfaceCacheAbs(px) - SurfaceCacheAbs(py);
    if (n.z < 0.0f) {
        float qx = (1.0f - SurfaceCacheAbs(py)) * (px >= 0.0f ? 1.0f : -1.0f);
        float qy = (1.0f - SurfaceCacheAbs(px)) * (py >= 0.0f ? 1.0f : -1.0f);
        n.x = qx; n.y = qy;
    }
    float len = SurfaceCacheSqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (!(len > 0.0f)) { n.x = 0.0f; n.y = 0.0f; n.z = 1.0f; return n; }
    n.x /= len; n.y /= len; n.z /= len;
    return n;
}

// Albedo as three unorm8 (a tint above 1 clamps: the record is a reflectance) beside the writer tag.
UZ_SC_FUNC uint SurfaceCachePackUnorm8(float v) { return (uint)(SurfaceCacheSaturate(v) * 255.0f + 0.5f); }
UZ_SC_FUNC uint SurfaceCachePackAlbedoTag(float3 albedo, uint writerTag) {
    return SurfaceCachePackUnorm8(albedo.x) | (SurfaceCachePackUnorm8(albedo.y) << 8) | (SurfaceCachePackUnorm8(albedo.z) << 16)
         | ((writerTag & 0xFFu) << 24);
}
UZ_SC_FUNC float3 SurfaceCacheUnpackAlbedo(uint packed) {
    float3 a;
    a.x = (float)(packed & 0xFFu) * (1.0f / 255.0f);
    a.y = (float)((packed >> 8) & 0xFFu) * (1.0f / 255.0f);
    a.z = (float)((packed >> 16) & 0xFFu) * (1.0f / 255.0f);
    return a;
}
UZ_SC_FUNC uint SurfaceCacheRecordTag(uint packed) { return packed >> 24; }
UZ_SC_FUNC bool SurfaceCacheRecordExists(uint packed) { return SurfaceCacheRecordTag(packed) != 0u; }

// The record's bookkeeping: the lighting generation its sun layer was last computed against and the
// visits of the cell-indirect EMA.
UZ_SC_FUNC uint SurfaceCachePackSurfaceMeta(uint litGeneration, uint visits) {
    return (litGeneration & 0xFFFFu) | ((visits & 0xFFu) << 16);
}
UZ_SC_FUNC uint SurfaceCacheSurfaceGeneration(uint meta) { return meta & 0xFFFFu; }
UZ_SC_FUNC uint SurfaceCacheSurfaceVisits(uint meta)     { return (meta >> 16) & 0xFFu; }

// ─── Stage 4: the relight schedule ───────────────────────────────────────────────────────────
// Frame f walks slice f mod sliceCount of the table: ceil(entries / sliceCount) consecutive entries
// from slice x size, so every entry is visited exactly once in sliceCount consecutive frames (the
// last slice is short when the division is not exact). sliceCount 0 is read as 1.

UZ_SC_FUNC uint SurfaceCacheSliceSize(uint entriesLog2, uint sliceCount) {
    uint entries = 1u << entriesLog2;
    uint slices  = sliceCount > 0u ? sliceCount : 1u;
    return (entries + slices - 1u) / slices;
}
UZ_SC_FUNC uint SurfaceCacheSliceIndex(uint frame, uint sliceCount) {
    return sliceCount > 1u ? frame % sliceCount : 0u;
}
UZ_SC_FUNC uint SurfaceCacheSliceFirst(uint entriesLog2, uint sliceIndex, uint sliceCount) {
    return sliceIndex * SurfaceCacheSliceSize(entriesLog2, sliceCount);
}
// Entries of the slice (the last slice is short; a slice past the table is empty).
UZ_SC_FUNC uint SurfaceCacheSliceCount(uint entriesLog2, uint sliceIndex, uint sliceCount) {
    uint entries = 1u << entriesLog2;
    uint first   = SurfaceCacheSliceFirst(entriesLog2, sliceIndex, sliceCount);
    uint size    = SurfaceCacheSliceSize(entriesLog2, sliceCount);
    if (first >= entries) return 0u;
    return (entries - first) < size ? (entries - first) : size;
}

// The relight decision: a record computed against another lighting generation takes the relit sun
// outright (the change propagates in one visit, no eviction); a record of the current generation
// folds the relit point sample at the cell's EMA rate (one more sample of the cell average, so a
// static scene does not flicker between the cell mean and a point sample every schedule).
UZ_SC_FUNC bool SurfaceCacheRelightReplaces(uint recordGeneration, uint lightingGeneration) {
    return (recordGeneration & 0xFFFFu) != (lightingGeneration & 0xFFFFu);
}
UZ_SC_FUNC float SurfaceCacheRelightSun(bool replaces, float sun, float relit, uint frames) {
    if (replaces) return relit;
    return SurfaceCacheEma(sun, relit, frames > 0u ? frames : 1u);
}
// Visits kept by a replaced record: the next cell-indirect visit blends at 1/2, so the indirect
// part follows the new lighting in a few visits instead of a full horizon.
static const uint kSurfaceCacheRelightVisitsKept = 1u;
UZ_SC_FUNC uint SurfaceCacheRelightVisits(bool replaces, uint visits) {
    return replaces && visits > kSurfaceCacheRelightVisitsKept ? kSurfaceCacheRelightVisitsKept : visits;
}

// ─── Stage 4: the cell-indirect gather ───────────────────────────────────────────────────────
// Cosine-weighted rays from the record: the estimator of E / pi is the plain mean of the ray
// radiances (pdf = cos / pi cancels the cosine), and the indirect radiance leaving the cell is the
// albedo times that mean. A constant environment L gives albedo x L exactly (the furnace), and the
// gather never exceeds albedo x the largest ray radiance (no energy is created).

UZ_SC_FUNC float SurfaceCacheRadicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return (float)bits * 2.3283064365386963e-10f;
}

// Cosine-weighted direction in the local frame (z = the normal) from a 2D sample in [0, 1)^2.
UZ_SC_FUNC float3 SurfaceCacheCosineDirection(float xi0, float xi1) {
    float r   = SurfaceCacheSqrt(SurfaceCacheSaturate(xi0));
    float phi = 2.0f * kSurfaceCachePi * xi1;
    float3 d;
    d.x = r * SurfaceCacheCos(phi);
    d.y = r * SurfaceCacheSin(phi);
    float z2 = 1.0f - SurfaceCacheSaturate(xi0);
    d.z = SurfaceCacheSqrt(z2 > 0.0f ? z2 : 0.0f);
    return d;
}

// albedo x (sum of the ray radiances / rays): the indirect radiance leaving the cell. No rays: 0.
UZ_SC_FUNC float SurfaceCacheGatherIndirect(float albedo, float raySum, uint rayCount) {
    if (rayCount == 0u) return 0.0f;
    return albedo * (raySum / (float)rayCount);
}

// A strictly subunit diffuse reflectance bounds every row sum of the discrete cosine
// transport operator. The 0.98 ceiling affects only opt-in cell transport, not the base BRDF.
// SH cold-hit gain is exactly zero: unconstrained signed SH reconstruction is not a
// substochastic operator, so multiplying it by an arbitrary "small" weight proves nothing.
static const float kSurfaceCacheTransportGain = 0.98f;
UZ_SC_FUNC float SurfaceCacheTransportReflectance(float albedo) {
    float a = SurfaceCacheSaturate(albedo);
    return a < kSurfaceCacheTransportGain ? a : kSurfaceCacheTransportGain;
}
UZ_SC_FUNC float SurfaceCacheTransportStep(float source, float albedo, float raySum, uint rays) {
    return source + SurfaceCacheGatherIndirect(SurfaceCacheTransportReflectance(albedo), raySum, rays);
}
static const uint kSurfaceCacheTransportBase = 0u;
static const uint kSurfaceCacheTransportGather = 1u;
static const uint kSurfaceCacheTransportSnapshot = 2u;
static const uint kSurfaceCacheTransportFinish = 3u;

// The indirect part of a cell's `other` layer (the emission the record keeps apart is subtracted).
UZ_SC_FUNC float SurfaceCacheIndirectOf(float other, float emissive) {
    float v = other - emissive;
    return v > 0.0f ? v : 0.0f;
}

// The per-visit EMA of the gather: the running mean over the first `horizon` visits, an exponential
// average with that horizon afterwards. Returns the visits after this one.
UZ_SC_FUNC uint SurfaceCacheNextVisits(uint visits, uint horizon) {
    uint h = horizon > 0u ? horizon : 1u;
    uint next = visits + 1u;
    return next < h ? next : h;
}
UZ_SC_FUNC float SurfaceCacheIndirectBlend(float indirect, float gather, uint nextVisits) {
    return SurfaceCacheEma(indirect, gather, nextVisits > 0u ? nextVisits : 1u);
}

#ifdef __cplusplus
} // namespace uz::surfacecache
#endif
#endif
