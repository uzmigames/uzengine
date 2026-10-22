// Finite Jacobi diffuse transport. Runs immediately after resolve/relight, before ALL consumers.
// The drained 32-byte accumulator becomes two uint4 scratch records per entry: a stable
// previous iteration and the previous frame's output. Gather never writes the snapshot,
// keys, surfaces, or accumulators. Separate dispatches with UAV barriers publish each iterate.
// Finish blends only the final finite-depth estimate, then zeros ALL scratch words.
// This lifetime alias keeps the table at 88 MiB without allocating another 16 MiB buffer.
//
// Bounces counts surface vertices including this cell: L[0] = direct + emission,
// L[n+1] = direct + emission + rho * mean(L[n](hit), sky(miss)).
// Cold hits use direct + emission only, never SH, and never insert during the iteration.
// rho <= .98, nonnegative normalized ray weights: ||T||inf <= .98. No ray sees next-iteration
// values. Finite sums and their temporal convex averages are bounded by ||source||inf/(1-.98).
#include "../common/constants.hlsli"
#include "../lighting/ibl.hlsli"
#include "rc_probe_guard.hlsli"

cbuffer SurfaceCacheRelightRootCB : register(b0) {
    uint tlasSrv;
    uint materialTableSrv;
    uint probeShSrv;
    uint surfaceCacheParamsSrv;
    uint resolvedUav;
    uint sliceIndex;
    uint sliceCount;
    uint indirectRays;
    uint skyCubemapSrv;
    uint transportMode;
    uint bounceIndex;
    uint bounces;
};

#include "gi_trace_common.hlsli"
#include "surface_cache.hlsli"
#include "surface_cache_lights.hlsli"
#include "surface_cache_hit.hlsli"

void IndirectBasis(float3 n, out float3 t, out float3 b) {
    if (n.z < -0.9999999) { t = float3(0, -1, 0); b = float3(-1, 0, 0); return; }
    const float a = 1.0 / (1.0 + n.z);
    const float c = -n.x * n.y * a;
    t = float3(1.0 - n.x * n.x * a, c, -n.x);
    b = float3(c, 1.0 - n.y * n.y * a, -n.y);
}

uint4 ReadScratch(RWStructuredBuffer<uint> scratch, uint base) {
    return uint4(scratch[base], scratch[base + 1u], scratch[base + 2u], scratch[base + 3u]);
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (surfaceCacheParamsSrv == kGITraceSentinelSrv || resolvedUav == kGITraceSentinelSrv) return;
    StructuredBuffer<SurfaceCacheParams> params = ResourceDescriptorHeap[surfaceCacheParamsSrv];
    const SurfaceCacheParams cache = params[0];
    const uint entry = DTid.x;
    if (entry >= (1u << cache.entriesLog2)) return;
    const uint base = entry * kSurfaceCacheAccumStride;
    RWStructuredBuffer<uint> scratch = ResourceDescriptorHeap[cache.accumUav];
    RWStructuredBuffer<uint4> resolved = ResourceDescriptorHeap[resolvedUav];
    RWStructuredBuffer<uint2> keys = ResourceDescriptorHeap[cache.keysUav];
    float3 pos, n, albedo, emissive;
    uint meta;
    const bool live = SurfaceCacheKeyLive(keys[entry])
        && SurfaceCacheReadSurface(cache, entry, pos, n, albedo, emissive, meta);

    if (transportMode == kSurfaceCacheTransportFinish) {
        if (live) {
            float3 sun, other, oldSun, oldOther;
            const uint4 rec = resolved[entry];
            SurfaceCacheUnpackResolved(rec, sun, other);
            SurfaceCacheUnpackResolved(ReadScratch(scratch, base + 4u), oldSun, oldOther);
            const uint visits = SurfaceCacheNextVisits(SurfaceCacheSurfaceVisits(meta), cache.indirectHorizon);
            // Depth 1 is exactly direct + emission, never a blend of old higher-depth output.
            if (bounces > 1u) other = lerp(oldOther, other, 1.0 / float(visits));
            resolved[entry] = SurfaceCachePackResolved(sun, other, rec.w);
            // The cached-radiance probe (Stats): the mean luminance of the record cells.
            const float lum = SurfaceCacheLuminance(sun + other);
            SurfaceCacheStatSum(cache, kSurfaceCacheStatRadianceSum,
                                (uint)min(max(lum, 0.0) * kSurfaceCacheStatRadianceScale, 65535.0));
            SurfaceCacheStat(cache, kSurfaceCacheStatRadianceCells, true);
            RWStructuredBuffer<uint> surface = ResourceDescriptorHeap[cache.surfaceUav];
            surface[entry * kSurfaceCacheSurfaceStride + kSurfaceCacheSurfaceMeta] =
                SurfaceCachePackSurfaceMeta(SurfaceCacheSurfaceGeneration(meta), visits);
        }
        [unroll] for (uint w = 0u; w < kSurfaceCacheAccumStride; ++w) scratch[base + w] = 0u;
        return;
    }
    if (transportMode == kSurfaceCacheTransportBase || transportMode == kSurfaceCacheTransportSnapshot) {
        uint4 rec = live ? resolved[entry] : uint4(0, 0, 0, 0);
        if (transportMode == kSurfaceCacheTransportBase) {
            [unroll] for (uint w = 0u; w < 4u; ++w) scratch[base + 4u + w] = rec[w];
            if (live) {
                float3 sun, other;
                SurfaceCacheUnpackResolved(rec, sun, other);
                rec = SurfaceCachePackResolved(sun, emissive, rec.w);
                resolved[entry] = rec;
            }
        }
        [unroll] for (uint w = 0u; w < 4u; ++w) scratch[base + w] = rec[w];
        return;
    }
    if (!live || tlasSrv == kGITraceSentinelSrv) return;
    const uint rays = min(indirectRays, 16u);
    if (rays == 0u) return;
    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[tlasSrv];
    float3 t, b;
    IndirectBasis(n, t, b);
    // The same per-frame sample operator for every iteration; only its source changes.
    const uint seed = (entry * 2654435761u) ^ (frameIndex * 1664525u + 1013904223u);
    const float2 rotation = float2(SurfaceCacheRadicalInverse(seed), SurfaceCacheRadicalInverse(seed ^ 0x9e3779b9u));
    float3 sum = 0.0;
    [loop] for (uint i = 0u; i < rays; ++i) {
        const float2 xi = frac(float2(float(i) / float(rays), SurfaceCacheRadicalInverse(i)) + rotation);
        const float3 local = SurfaceCacheCosineDirection(xi.x, xi.y);
        const float3 dir = normalize(t * local.x + b * local.y + n * local.z);
        RayDesc ray;
        ray.Origin = pos;
        ray.Direction = dir;
        ray.TMin = kSurfaceCacheRayTMin;
        ray.TMax = maxTraceDistance;
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
        q.Proceed();
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            sum += max(GITrace_SkyRadiance(dir, skyCubemapSrv), 0.0);
            continue;
        }
        const float3 hitP = pos + dir * q.CommittedRayT();
        const uint meshId = q.CommittedInstanceID();
        const uint prim = q.CommittedPrimitiveIndex();
        const float2 bary = q.CommittedTriangleBarycentrics();
        float3 hitN, hitAlbedo;
        if (!SurfaceCacheHitAttributes(meshId, prim, bary, q.CommittedWorldToObject3x4(), hitP, dir,
                                       materialTableSrv, hitN, hitAlbedo)) continue;
        const float3 hitPos = hitP + hitN * kSurfaceCacheSurfaceBias;
        // Side-effect-free lookup: no SurfaceCacheQuery (it reads the resolved SRV and touches
        // accumulators). Every source is the immutable previous iteration, in UAV state.
        bool found = false;
        [unroll] for (uint level = 0u; level < 2u; ++level) {
            const uint bias = level == 0u ? 0u : cache.sdfLodBias;
            const SurfaceCacheCellId id = SurfaceCacheLocate(cache, hitPos, hitN, SurfaceCacheCameraPos(cache), bias);
            const uint slot = SurfaceCacheFind(cache, id);
            if (!found && slot != kSurfaceCacheNoSlot) {
                const uint4 previous = ReadScratch(scratch, slot * kSurfaceCacheAccumStride);
                // A cell with a surface record has a defined iterate (its direct layer is the
                // relight's / the folded samples', its `other` the previous iteration): readable
                // from its first folded sample. The consumers' MinSamples gate guards sampled
                // means only; the snapshot of a record-less cell is zero (samples 0) = cold.
                if (SurfaceCacheMetaSamples(previous.w) > 0u) {
                    float3 hitSun, hitOther;
                    SurfaceCacheUnpackResolved(previous, hitSun, hitOther);
                    sum += hitSun + hitOther;
                    found = true;
                }
            }
        }
        if (found) continue;
        // SH gain = 0, including cold hits: direct + emission is an external source, not
        // feedback. Do not feed a cold cell here (that would mutate the iteration's domain).
        const float2 areaSample = float2(SurfaceCacheRadicalInverse((entry + i) * 7919u + frameIndex),
            SurfaceCacheRadicalInverse(((entry + i) ^ 0x9e3779b9u) + frameIndex));
        float3 sunTerm, otherTerm, hitEmissive;
        SurfaceCacheShadeHit(cache, tlas, hitPos, hitN, saturate(hitAlbedo), meshId, prim, bary,
            materialTableSrv, kGITraceSentinelSrv, areaSample, sunTerm, otherTerm, hitEmissive);
        sum += max(sunTerm + otherTerm, 0.0);
    }
    SurfaceCacheStatSum(cache, kSurfaceCacheStatIndirectRays, rays);
    const uint4 rec = resolved[entry]; // only this thread writes its destination
    float3 sun, other;
    SurfaceCacheUnpackResolved(rec, sun, other);
    [unroll] for (uint c = 0u; c < 3u; ++c)
        other[c] = min(SurfaceCacheTransportStep(emissive[c], albedo[c], sum[c], rays), cache.maxRadiance);
    resolved[entry] = SurfaceCachePackResolved(sun, other, rec.w);
}
