/// UzEngine Surface Radiance Cache — the feed trace (Phase D stage 3). Populates the cells the
/// SDF consumers read with EXACT-material radiance, independently of where the reflection rays go.
///
/// One thread per (radiance-cache probe, ray): `feedRays` full-sphere rays per probe per frame from
/// the probe grid the SDF radiance cache traces (its relocated origins where the side classifier
/// moved a probe, its inside probes skipped), each an inline `RayQuery` on the scene TLAS. A hit is
/// shaded exactly as the reflection pass shades its hits — the interpolated vertex normal and the
/// triangle-UV albedo from the per-mesh material table (the field's gradient and per-mesh-average
/// reflectance where a mesh has no entry), the sun with a shadow ray, the K strongest local lights
/// with shadow rays, the radiance-cache probe irradiance (E / pi) at the hit and the mesh's emission
/// — and the two layers (`sun` = albedo x sun / pi + local lights, `other` = albedo x E / pi +
/// emission) are accumulated into the hit's cell at the SDF consumers' level (`sdfLodBias` levels
/// coarser than the camera-distance level, D8), where `ShadeSdfReflectionHit` and the radiance-cache
/// trace look them up. Update only: nothing is read, nothing is output. A miss does nothing.
/// Stage 4: the hit is also recorded in the cell's surface record (`SurfaceCacheUpdateSurface`), so
/// the relight pass re-shades it and the cell-indirect pass gathers from it.
///
/// Bindings: root constants at b0 (`uz::SurfaceCacheFeedRootConstants`, 5 DWORDs), the shared
/// trace constants at b2 (`uz::RadianceCacheTraceConstants`: the probe grid rows of the completed
/// SH buffer, the sun, the skylight, the GDF clipmaps). Dispatch ceil(totalProbes x feedRays / 64).
///
/// @ref docs/specs/rendering/surface-radiance-cache.md sections 2.4, 2.5, 3.1 (the feed)
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Probes"
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>

#include "../common/constants.hlsli"     // kSentinelSrvIndex, PI
#include "../lighting/ibl.hlsli"         // EvalSH_L2 (the lighting's packed-SH evaluation)
#include "rc_probe_guard.hlsli"          // ProbeGuardIndex / ProbeEffectivePosition / ProbeGuardRejects

// ─── Root constants at b0 (mirrors `uz::SurfaceCacheFeedRootConstants`) ───────────────────────

cbuffer SurfaceCacheFeedRootCB : register(b0) {
    uint tlasSrv;                 // 0  Scene TLAS
    uint materialTableSrv;        // 1  GIMeshMaterial table by TLAS InstanceID (sentinel = GDF attributes only)
    uint probeShSrv;              // 2  The completed radiance-cache SH buffer (E / pi at the hit, guards, validity); sentinel = no probe term
    uint surfaceCacheParamsSrv;   // 3  StructuredBuffer<SurfaceCacheParams> (1 element)
    uint feedRays;                // 4  Rays per probe this frame (r.GI.SurfaceCache.FeedRays)
};

// ─── Trace constants at b2 + hit helpers (declares linearClamp s2, the material table, gdf_surface) ──

#include "gi_trace_common.hlsli"
#include "surface_cache.hlsli"
#include "surface_cache_lights.hlsli"
#include "surface_cache_hit.hlsli"     // the hit attributes, the sun, the probe E / pi, the two layers

static const float kFeedRayTMin = kSurfaceCacheRayTMin;   // cm — the reflection pass's kReflectionRayTMin

// ─── The probe's rays: the radiance-cache writer's stratified set with its own temporal rotation ──
// (the same Hammersley + Cranley-Patterson construction as radiance_cache_trace_cs.hlsl, rotated
// by a different frame seed so the feed and the SH writer do not revisit one another's directions).

float FeedRadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float3 FeedRayDirection(uint i, uint nRays, uint probeIdx, uint frameIdx) {
    const uint   frameJitter = ((frameIdx + 7919u) * 1664525u + 1013904223u);
    const float2 xi = frac(float2(float(i) / float(nRays), FeedRadicalInverseVdC(i))
                           + float2(FeedRadicalInverseVdC(frameJitter), FeedRadicalInverseVdC(frameJitter ^ probeIdx)));
    const float z   = 1.0 - 2.0 * xi.x;
    const float r   = sqrt(max(0.0, 1.0 - z * z));
    const float phi = 2.0 * PI * xi.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

// ─── Entry point — one thread per probe ray ──────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint nRays = min(feedRays, 64u);
    if (nRays == 0u || DTid.x >= totalProbes * nRays) return;
    if (surfaceCacheParamsSrv == kGITraceSentinelSrv || tlasSrv == kGITraceSentinelSrv) return;
    const uint probeIdx = DTid.x / nRays;
    const uint i        = DTid.x - probeIdx * nRays;

    StructuredBuffer<SurfaceCacheParams> cacheParams = ResourceDescriptorHeap[surfaceCacheParamsSrv];
    const SurfaceCacheParams cache = cacheParams[0];

    // The probe: its grid position, moved where the classifier relocated it; a probe inside
    // geometry (validity 0) shoots nothing.
    const uint3 c = uint3(probeIdx % probesPerAxis, (probeIdx / probesPerAxis) % probesPerAxis,
                          probeIdx / (probesPerAxis * probesPerAxis));
    float3 origin = float3(gridOriginX, gridOriginY, gridOriginZ) + float3(c) * probeSpacing;
    if (probeShSrv != kGITraceSentinelSrv) {
        StructuredBuffer<float4> probes = ResourceDescriptorHeap[probeShSrv];
        if (probes[probeIdx * 7u + 6u].w <= 0.0) return;
        origin = ProbeEffectivePosition(probes[ProbeGuardIndex(probesPerAxis, probeIdx)], origin);
    }
    const float3 dir = FeedRayDirection(i, nRays, probeIdx, frameIndex);

    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[tlasSrv];
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = kFeedRayTMin;
    ray.TMax      = maxTraceDistance;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return;

    // ── Surface attributes at the hit: the reflection pass's sources ───────────────────────
    const float3 hitP      = origin + dir * q.CommittedRayT();
    const uint   meshId    = q.CommittedInstanceID();
    const uint   primIndex = q.CommittedPrimitiveIndex();
    const float2 bary      = q.CommittedTriangleBarycentrics();
    float3 hitN, albedo;
    if (!SurfaceCacheHitAttributes(meshId, primIndex, bary, q.CommittedWorldToObject3x4(), hitP, dir,
                                   materialTableSrv, hitN, albedo)) {
        return;                                       // outside every cascade: no attributes, nothing to feed
    }
    const float3 cachePos = hitP + hitN * kSurfaceCacheSurfaceBias;

    // ── The two layers (the reflection pass's), accumulated at the SDF consumers' level ────
    // (the direct sun with a shadow ray, the K local lights, the probe E / pi, the emission).
    const float2 areaSample = float2(FeedRadicalInverseVdC(DTid.x + frameIndex * 7919u),
                                     FeedRadicalInverseVdC((DTid.x ^ 0x9e3779b9u) + frameIndex));
    float3 sunTerm, otherTerm, emissive;
    SurfaceCacheShadeHit(cache, tlas, cachePos, hitN, albedo, meshId, primIndex, bary, materialTableSrv, probeShSrv,
                         areaSample, sunTerm, otherTerm, emissive);
    SurfaceCacheUpdateSurface(cache, cachePos, hitN, SurfaceCacheCameraPos(cache), cache.sdfLodBias, sunTerm, otherTerm,
                              kSurfaceCacheWriterFeed, albedo, emissive);
}
