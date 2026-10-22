/// UzEngine Surface Radiance Cache — the scheduled relight (Phase D stage 4). Re-shades the direct
/// layer of the live cells from their surface records, one slice of the table per frame, so a sun
/// or local-light change reaches every cell within `RelightSlices` frames WITHOUT evicting
/// anything — the one thing a lazily populated cache cannot do (stages 1-3 cleared the table on a
/// lighting change and fell back to inline shading until the rays refilled it).
///
/// One thread per entry of this frame's slice (`SurfaceCacheSliceFirst/Count`: frame mod
/// RelightSlices selects ceil(entries / RelightSlices) consecutive entries, every entry once per
/// RelightSlices frames). A live cell with a surface record (a TLAS writer's hit: position, normal,
/// albedo) gets its `sun` layer recomputed exactly as the writers compute it — the sun with a
/// shadow ray on the TLAS, the K strongest local lights with a shadow ray each — at the record's
/// point. A record lit against another lighting generation (the CPU bumps
/// `SurfaceCacheParams::lightingGeneration` on every lighting-hash change) takes the relit value
/// outright and is stamped current; a record of the current generation folds the relit point
/// sample at the cell's EMA rate (one more sample of the cell average: a static scene does not
/// flicker between the cell mean and a point sample every schedule). A replaced record also drops
/// its cell-indirect visits to one, so the indirect layer follows the new lighting in a few visits.
///
/// Bindings: root constants at b0 (`uz::SurfaceCacheRelightRootConstants`, 12 DWORDs; the cell-
/// indirect pass binds the same block), the shared b2 trace constants (the sun, the GDF clipmaps,
/// the trace range). Dispatch ceil(sliceSize / 64) groups.
///
/// @ref docs/specs/rendering/surface-radiance-cache.md sections 2.4-2.6, 3.1 (stage 4)
/// @ref Wright 2022 "Lumen" — surface cache direct lighting updated on a schedule

#include "../common/constants.hlsli"     // kSentinelSrvIndex, PI
#include "../lighting/ibl.hlsli"         // EvalSH_L2 (surface_cache_hit.hlsli)
#include "rc_probe_guard.hlsli"          // ProbeGuardIndex / ProbeEffectivePosition / ProbeGuardRejects

// ─── Root constants at b0 (mirrors `uz::SurfaceCacheRelightRootConstants`) ────────────────────

cbuffer SurfaceCacheRelightRootCB : register(b0) {
    uint tlasSrv;                 // 0  Scene TLAS
    uint materialTableSrv;        // 1  GIMeshMaterial table by TLAS InstanceID (the indirect pass's cold hits)
    uint probeShSrv;              // 2  The completed radiance-cache SH buffer (the indirect pass's cold hits); sentinel = none
    uint surfaceCacheParamsSrv;   // 3  StructuredBuffer<SurfaceCacheParams> (1 element)
    uint resolvedUav;             // 4  RWStructuredBuffer<uint4> resolved records (the SRV of the record is read-only)
    uint sliceIndex;              // 5  frame mod sliceCount
    uint sliceCount;              // 6  r.GI.SurfaceCache.RelightSlices
    uint indirectRays;            // 7  r.GI.SurfaceCache.IndirectRays (the indirect pass; 0 here)
    uint skyCubemapSrv;           // 8  Sky for the indirect pass's escaping rays (sentinel = flat skylight)
    uint transportMode;
    uint bounceIndex;
    uint bounces;
};

#include "gi_trace_common.hlsli"
#include "surface_cache.hlsli"
#include "surface_cache_lights.hlsli"
#include "surface_cache_hit.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (surfaceCacheParamsSrv == kGITraceSentinelSrv || tlasSrv == kGITraceSentinelSrv
        || resolvedUav == kGITraceSentinelSrv) return;
    StructuredBuffer<SurfaceCacheParams> cacheParams = ResourceDescriptorHeap[surfaceCacheParamsSrv];
    const SurfaceCacheParams cache = cacheParams[0];
    if (cache.surfaceUav == kSurfaceCacheSentinel) return;
    if (DTid.x >= SurfaceCacheSliceCount(cache.entriesLog2, sliceIndex, sliceCount)) return;
    const uint entry = SurfaceCacheSliceFirst(cache.entriesLog2, sliceIndex, sliceCount) + DTid.x;

    RWStructuredBuffer<uint2> keys = ResourceDescriptorHeap[cache.keysUav];
    if (!SurfaceCacheKeyLive(keys[entry])) return;
    float3 pos, n, albedo, emissive;
    uint   meta;
    if (!SurfaceCacheReadSurface(cache, entry, pos, n, albedo, emissive, meta)) return;

    // ── The direct layer at the record's point: the writers' shading, one shadow ray per light ──
    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[tlasSrv];
    const float2 areaSample = float2(SurfaceCacheRadicalInverse(entry + frameIndex * 7919u),
                                     SurfaceCacheRadicalInverse((entry ^ 0x9e3779b9u) + frameIndex));
    const float3 relit = SurfaceCacheDirectLayer(cache, tlas, pos, n, albedo, areaSample);

    // ── Replace (a lighting change) or fold (the current generation) ─────────────────────────
    RWStructuredBuffer<uint4> resolved = ResourceDescriptorHeap[resolvedUav];
    const uint4 rec = resolved[entry];
    float3 sun, other;
    SurfaceCacheUnpackResolved(rec, sun, other);
    const bool replaces = SurfaceCacheRelightReplaces(SurfaceCacheSurfaceGeneration(meta), cache.lightingGeneration);
    const uint frames   = SurfaceCacheMetaFrames(rec.w);
    [unroll]
    for (uint c = 0u; c < 3u; ++c) sun[c] = SurfaceCacheRelightSun(replaces, sun[c], relit[c], frames);
    resolved[entry] = SurfaceCachePackResolved(sun, other, rec.w);

    RWStructuredBuffer<uint> surface = ResourceDescriptorHeap[cache.surfaceUav];
    surface[entry * kSurfaceCacheSurfaceStride + kSurfaceCacheSurfaceMeta] =
        SurfaceCachePackSurfaceMeta(cache.lightingGeneration,
                                    SurfaceCacheRelightVisits(replaces, SurfaceCacheSurfaceVisits(meta)));
    SurfaceCacheStat(cache, kSurfaceCacheStatRelit, replaces);
}
