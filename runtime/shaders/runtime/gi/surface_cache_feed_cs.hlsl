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

static const float kFeedRayTMin = 1.0;   // cm — the reflection pass's kReflectionRayTMin

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

// ─── Radiance-cache probe irradiance (E / pi) at a world point: the reflection pass's read ─────
// (trilinear over the eight surrounding probes, each weighted by its facing, skipped when invalid
// or guarded behind a face; zero outside the grid or without a buffer).

float3 FeedProbeIrradianceOverPi(float3 position, float3 normal) {
    if (probeShSrv == kGITraceSentinelSrv || probesPerAxis < 2u || probeSpacing <= 0.0) return 0.0;
    const float3 origin = float3(gridOriginX, gridOriginY, gridOriginZ);
    const float3 cell = (position - origin) / probeSpacing;
    if (any(cell < 0.0) || any(cell > float(probesPerAxis - 1u))) return 0.0;
    const uint3  lower    = uint3(floor(cell));
    const uint3  upper    = min(lower + 1u, probesPerAxis - 1u);
    const float3 fraction = frac(cell);
    StructuredBuffer<float4> probes = ResourceDescriptorHeap[probeShSrv];
    float3 irradiance = 0.0;
    float  totalWeight = 0.0;
    [unroll]
    for (uint corner = 0; corner < 8; ++corner) {
        const uint3 index = uint3((corner & 1) ? upper.x : lower.x,
                                  (corner & 2) ? upper.y : lower.y,
                                  (corner & 4) ? upper.z : lower.z);
        float weight = ((corner & 1) ? fraction.x : 1.0 - fraction.x)
                     * ((corner & 2) ? fraction.y : 1.0 - fraction.y)
                     * ((corner & 4) ? fraction.z : 1.0 - fraction.z);
        const uint   probeLinear  = index.x + index.y * probesPerAxis + index.z * probesPerAxis * probesPerAxis;
        const float3 gridPosition = origin + float3(index) * probeSpacing;
        const float4 guard        = probes[ProbeGuardIndex(probesPerAxis, probeLinear)];
        const float3 toProbe      = ProbeEffectivePosition(guard, gridPosition) - position;
        const float  d2           = dot(toProbe, toProbe);
        const float  facing       = d2 > 0.0 ? saturate(dot(normal, toProbe) * rsqrt(d2)) : 1.0;
        weight *= facing * facing;
        const uint base = probeLinear * 7u;
        if (probes[base + 6].w <= 0.0) continue;
        if (ProbeGuardRejects(guard, gridPosition, position)) continue;
        float4 sh[7];
        [unroll]
        for (uint k = 0; k < 7; ++k) sh[k] = probes[base + k];
        irradiance  += weight * EvalSH_L2(normal, sh);
        totalWeight += weight;
    }
    return totalWeight > 0.0 ? irradiance / totalWeight : 0.0;
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
    float3 hitN   = 0.0;
    float3 albedo = 0.0;
    bool   haveGeometry = false;
    float3 tableAlbedo, objectNormal;
    if (GITrace_HitSurface(meshId, primIndex, bary, materialTableSrv, tableAlbedo, objectNormal)) {
        // Row-vector multiplication by WorldToObject applies the inverse transpose to the
        // object-space normal (nonuniform instance scale included), as the reflection pass.
        const float3 n = mul(objectNormal, (float3x3)q.CommittedWorldToObject3x4());
        if (dot(n, n) > 1e-12) {
            hitN = normalize(n);
            if (dot(hitN, dir) > 0.0) hitN = -hitN;
            albedo = tableAlbedo;
            haveGeometry = true;
        }
    }
    if (!haveGeometry) {
        // No table geometry for this mesh: the global distance field's gradient and reflectance.
        const float4 surface = SampleGdfSurface(hitP);
        if (surface.x >= 1e8) return;                 // outside every cascade: no attributes, nothing to feed
        const float3 g = GdfNormal(hitP);
        if (dot(g, g) < 0.5) return;
        hitN   = (dot(g, dir) > 0.0) ? -g : g;
        albedo = GITrace_HitAlbedo(meshId, hitP, materialTableSrv);
    }
    const float3 cachePos = hitP + hitN * kSurfaceCacheSurfaceBias;

    // ── Direct sun with a shadow ray ───────────────────────────────────────────────────────
    const float3 L   = normalize(float3(lightDirX, lightDirY, lightDirZ));
    const float  NdL = max(dot(hitN, L), 0.0);
    float visibility = 0.0;
    if (NdL > 0.0) {
        RayDesc shadowRay;
        shadowRay.Origin    = cachePos;
        shadowRay.Direction = L;
        shadowRay.TMin      = kFeedRayTMin;
        shadowRay.TMax      = maxTraceDistance;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
        sq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, shadowRay);
        sq.Proceed();
        visibility = (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
    }
    const float3 sunRad = float3(lightColorR, lightColorG, lightColorB) * lightIntensity * NdL * visibility;

    // ── The two layers (the reflection pass's), accumulated at the SDF consumers' level ────
    const float2 areaSample = float2(FeedRadicalInverseVdC(DTid.x + frameIndex * 7919u),
                                     FeedRadicalInverseVdC((DTid.x ^ 0x9e3779b9u) + frameIndex));
    float3 sunTerm = albedo * (sunRad / PI);
    sunTerm += SurfaceCacheLocalLightRadiance(cache, cachePos, hitN, albedo, tlas, kFeedRayTMin,
                                              kSurfaceCacheSurfaceBias, areaSample);
    float3 otherTerm = albedo * FeedProbeIrradianceOverPi(cachePos, hitN);
    if ((cache.flags & kSurfaceCacheFlagEmissive) != 0u) {
        otherTerm += GITrace_HitEmissive(meshId, primIndex, bary, materialTableSrv);
    }
    SurfaceCacheUpdate(cache, cachePos, hitN, SurfaceCacheCameraPos(cache), cache.sdfLodBias, sunTerm, otherTerm,
                       kSurfaceCacheWriterFeed);
}
