/// UzEngine Surface Radiance Cache — the TLAS hit shading the cache's own writers share (Phase D
/// stage 3 feed, stage 4 relight and cell-indirect): the exact-material attributes of a committed
/// `RayQuery` hit (the reflection pass's sources: the interpolated vertex normal and the triangle-UV
/// albedo from the per-mesh material table, the global distance field's gradient and per-mesh-
/// average reflectance where a mesh has no entry), the direct sun with a shadow ray, the
/// radiance-cache probe irradiance (E / pi) at a point, and the two cell layers of a hit
/// (`sun` = albedo x sun / pi + the K local lights, `other` = albedo x E / pi + the mesh's emission).
///
/// # Requirements
/// The including shader must include, BEFORE this file: `gi_trace_common.hlsli` (the b2 trace
/// constants, `linearClamp`, the material table and `gdf_surface.hlsli`), `rc_probe_guard.hlsli`,
/// `../lighting/ibl.hlsli` (EvalSH_L2), `surface_cache.hlsli` and `surface_cache_lights.hlsli`.
///
/// @ref docs/specs/rendering/surface-radiance-cache.md sections 2.5, 3.1
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Probes"
#ifndef UZ_SURFACE_CACHE_HIT_HLSLI
#define UZ_SURFACE_CACHE_HIT_HLSLI

static const float kSurfaceCacheRayTMin = 1.0;   // cm — the reflection pass's kReflectionRayTMin

// ─── Hit attributes: the reflection pass's sources ───────────────────────────────────────────
// The shading normal (facing against `dir`) and the albedo of a committed triangle hit: the
// material table's interpolated vertex normal under the instance's inverse transpose (row-vector
// multiplication by WorldToObject) and its triangle-UV albedo; without table geometry the global
// distance field's gradient and reflectance. False where neither gives a normal (outside every
// cascade): the hit has no attributes.
bool SurfaceCacheHitAttributes(uint meshId, uint primIndex, float2 bary, float3x4 worldToObject,
                               float3 hitP, float3 dir, uint materialTableSrv,
                               out float3 hitN, out float3 albedo) {
    hitN = 0.0;
    albedo = 0.0;
    float3 tableAlbedo, objectNormal;
    if (GITrace_HitSurface(meshId, primIndex, bary, materialTableSrv, tableAlbedo, objectNormal)) {
        const float3 n = mul(objectNormal, (float3x3)worldToObject);
        if (dot(n, n) > 1e-12) {
            hitN = normalize(n);
            if (dot(hitN, dir) > 0.0) hitN = -hitN;
            albedo = tableAlbedo;
            return true;
        }
    }
    const float4 surface = SampleGdfSurface(hitP);
    if (surface.x >= 1e8) return false;                 // outside every cascade: no attributes
    const float3 g = GdfNormal(hitP);
    if (dot(g, g) < 0.5) return false;
    hitN   = (dot(g, dir) > 0.0) ? -g : g;
    albedo = GITrace_HitAlbedo(meshId, hitP, materialTableSrv);
    return true;
}

// ─── Radiance-cache probe irradiance (E / pi) at a world point: the reflection pass's read ─────
// (trilinear over the eight surrounding probes, each weighted by its facing, skipped when invalid
// or guarded behind a face; zero outside the grid or without a buffer).
float3 SurfaceCacheProbeIrradianceOverPi(uint probeShSrv, float3 position, float3 normal) {
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

// ─── Direct sun with a shadow ray from a lifted surface point ─────────────────────────────────
// `sunRad` = colour x intensity x N.L x visibility (the DDGI writer's / path tracer's NEE term).
float3 SurfaceCacheSunRadiance(RaytracingAccelerationStructure tlas, float3 origin, float3 n) {
    const float3 L   = normalize(float3(lightDirX, lightDirY, lightDirZ));
    const float  NdL = max(dot(n, L), 0.0);
    float visibility = 0.0;
    if (NdL > 0.0) {
        RayDesc shadowRay;
        shadowRay.Origin    = origin;
        shadowRay.Direction = L;
        shadowRay.TMin      = kSurfaceCacheRayTMin;
        shadowRay.TMax      = maxTraceDistance;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
        sq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, shadowRay);
        sq.Proceed();
        visibility = (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
    }
    return float3(lightColorR, lightColorG, lightColorB) * lightIntensity * NdL * visibility;
}

// The cell's direct layer at a lifted surface point: albedo x sun / pi plus the K strongest local
// lights of the frame, each with a shadow ray (the rect light's area sample from `areaSample`).
float3 SurfaceCacheDirectLayer(SurfaceCacheParams cache, RaytracingAccelerationStructure tlas, float3 origin,
                               float3 n, float3 albedo, float2 areaSample) {
    float3 sunTerm = albedo * (SurfaceCacheSunRadiance(tlas, origin, n) / PI);
    sunTerm += SurfaceCacheLocalLightRadiance(cache, origin, n, albedo, tlas, kSurfaceCacheRayTMin,
                                              kSurfaceCacheSurfaceBias, areaSample);
    return sunTerm;
}

// ─── The two layers of a committed hit (the feed's and the cell-indirect fallback's shading) ──
// `origin` is the hit lifted by kSurfaceCacheSurfaceBias (the point the cell is keyed from).
void SurfaceCacheShadeHit(SurfaceCacheParams cache, RaytracingAccelerationStructure tlas, float3 origin, float3 hitN,
                          float3 albedo, uint meshId, uint primIndex, float2 bary, uint materialTableSrv,
                          uint probeShSrv, float2 areaSample,
                          out float3 sunTerm, out float3 otherTerm, out float3 emissive) {
    sunTerm   = SurfaceCacheDirectLayer(cache, tlas, origin, hitN, albedo, areaSample);
    otherTerm = albedo * SurfaceCacheProbeIrradianceOverPi(probeShSrv, origin, hitN);
    emissive  = 0.0;
    if ((cache.flags & kSurfaceCacheFlagEmissive) != 0u) {
        emissive   = GITrace_HitEmissive(meshId, primIndex, bary, materialTableSrv);
        otherTerm += emissive;
    }
}

#endif // UZ_SURFACE_CACHE_HIT_HLSLI
