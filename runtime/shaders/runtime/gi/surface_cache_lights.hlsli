/// UzEngine Surface Radiance Cache — local lights at a shaded hit (Phase D stage 2, lighting
/// completeness at the hit). The frame's point / spot / rect lights reach the cache's cell
/// lighting through one `StructuredBuffer<SurfaceCacheLight>` named by the binding record
/// (`SurfaceCacheParams::lightsSrv`, `lightCount`, `localLights` = K): a shaded hit scans the
/// records, keeps the K strongest by their unshadowed Lambert radiance, traces one shadow ray per
/// kept light on the scene TLAS and sums the visible ones. The result is the Lambert radiance the
/// deferred lighting would give the same surface from the same lights (`LocalLightAttenuation`,
/// the spot cone, the rect's clamped-cosine form factor with the identity LTC matrix and its
/// range window; `diffuseScale` folded into the record's colour on the CPU), so a cell holds what
/// the raster path shows on screen — with the shadows the raster's unshadowed rect lights lack.
///
/// A rect light's shadow ray goes to a point of the rectangle drawn from the caller's 2D sample
/// (one per hit and frame), so the cell's average over its samples converges to the area light's
/// partial visibility (soft shadows) instead of a hard centre-point shadow.
///
/// Stage 2 and the path tracer: the reference carries no point / spot lights (its light BVH holds
/// emissive triangles and the sun); a rect light is validated against the coincident emissive quad
/// the reference sees (scripts/rendering/surface-cache-locallight.uzscene, the area-light precedent).
///
/// @ref docs/specs/rendering/surface-radiance-cache.md section 2.5 (stage 2), 3.1
/// @ref Heitz, Dupuy, Hill, Neubelt 2016 "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines"
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — windowed inverse-square attenuation
#ifndef UZ_SURFACE_CACHE_LIGHTS_HLSLI
#define UZ_SURFACE_CACHE_LIGHTS_HLSLI

#include "surface_cache_contract.hlsli"
#include "../lighting/ltc.hlsli"          // LtcEvaluate (the clamped-cosine form factor of a quad)

// Unshadowed irradiance over pi of light `l` at (pos, n) — multiply by the albedo for the Lambert
// radiance — and the point its shadow ray targets (the centre; the rect's area sample is drawn by
// the caller). Zero outside the light's range, cone or emitting side, or below the horizon.
float3 SurfaceCacheLightIrradianceOverPi(SurfaceCacheLight l, float3 pos, float3 n) {
    const uint type = l.type & kSurfaceCacheLightTypeMask;
    if (type == kSurfaceCacheLightTypeRect) {
        const float3 toCenter = l.position - pos;
        const float  window   = SurfaceCacheRectWindow(dot(toCenter, toCenter), l.radius);
        if (window <= 0.0) return 0.0;
        const bool twoSided = (l.type & kSurfaceCacheLightTwoSided) != 0u;
        // One-sided: a receiver behind the emitting face gets nothing (ShadeRectLight's test).
        if (!twoSided && dot(l.direction, -toCenter) <= 0.0) return 0.0;
        const float3 p0 = l.position - l.right - l.up;
        const float3 p1 = l.position + l.right - l.up;
        const float3 p2 = l.position + l.right + l.up;
        const float3 p3 = l.position - l.right + l.up;
        const float3x3 identity = float3x3(1.0, 0.0, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0, 1.0);
        // The clamped cosine is view-independent: any tangent frame (V = n takes the fallback).
        const float form = LtcEvaluate(n, n, pos, identity, p0, p1, p2, p3, twoSided);
        return l.color * (form * window);
    }
    const float3 toLight = l.position - pos;
    const float  distSq  = dot(toLight, toLight);
    const float  atten   = SurfaceCacheLightAttenuation(distSq, l.radius, l.falloffExponent,
                                                        (l.type & kSurfaceCacheLightInverseSquared) != 0u);
    if (atten <= 0.0) return 0.0;
    const float3 L   = toLight * rsqrt(max(distSq, 1e-8));
    const float  NoL = dot(n, L);
    if (NoL <= 0.0) return 0.0;
    float cone = 1.0;
    if (type == kSurfaceCacheLightTypeSpot) {
        cone = SurfaceCacheSpotCone(dot(L, -l.direction), l.cosOuterAngle, l.invCosConeDifference);
        if (cone <= 0.0) return 0.0;
    }
    return l.color * (NoL * atten * cone / kSurfaceCachePi);
}

// Lambert radiance (albedo x E / pi) of the K strongest local lights at a hit, each with a shadow
// ray from `origin` (the hit point already lifted off the surface by the caller's bias) to the
// light: the centre of a point / spot light, a point of a rect light drawn from `areaSample`. A
// shadow ray stops `bias` short of the target so an emitter mesh coincident with its rect light
// (the raster / path-tracer pairing) does not occlude it. Nothing is traced when the record names
// no lights or K is 0. The K are summed in the order they were kept (ties and the sum's rounding
// order are not part of the contract).
float3 SurfaceCacheLocalLightRadiance(SurfaceCacheParams p, float3 origin, float3 n, float3 albedo,
                                      RaytracingAccelerationStructure tlas, float tmin, float bias,
                                      float2 areaSample) {
    if (p.lightsSrv == kSurfaceCacheSentinel || p.lightCount == 0u || p.localLights == 0u) return 0.0;
    StructuredBuffer<SurfaceCacheLight> lights = ResourceDescriptorHeap[p.lightsSrv];
    const uint k     = min(p.localLights, kSurfaceCacheMaxLocalLights);
    const uint count = min(p.lightCount, kSurfaceCacheMaxLights);

    // The K strongest by relevance, unsorted: every access to the two arrays below has a
    // compile-time index (the [unroll] loops over the fixed capacity, predicated on the live slot),
    // so the selection compiles to selects and never needs indexable temporaries. (Measured on the
    // Sponza tour with the cache on: this form, the sorted dynamically indexed form and a capacity
    // of 4 all cost the same; the pass's +0.1 ms over stage 1 there is the code this header adds to
    // the reflection shader as a whole, paid on frames without a local light too.)
    float topWeight[kSurfaceCacheMaxLocalLights];
    uint  topIndex[kSurfaceCacheMaxLocalLights];
    [unroll]
    for (uint c = 0u; c < kSurfaceCacheMaxLocalLights; ++c) { topWeight[c] = 0.0; topIndex[c] = 0u; }
    uint  kept          = 0u;
    uint  weakest       = 0u;    // the kept slot with the smallest weight (valid once kept == k)
    float weakestWeight = 0.0;
    [loop]
    for (uint i = 0u; i < count; ++i) {
        const float w = SurfaceCacheLuminance(SurfaceCacheLightIrradianceOverPi(lights[i], origin, n));
        if (!(w > 0.0)) continue;
        uint slot;
        if (kept < k) {
            slot = kept++;
        } else if (w > weakestWeight) {
            slot = weakest;
        } else {
            continue;
        }
        [unroll]
        for (uint s = 0u; s < kSurfaceCacheMaxLocalLights; ++s) {
            if (s == slot) { topWeight[s] = w; topIndex[s] = i; }
        }
        if (kept == k) {
            weakest = 0u;
            weakestWeight = topWeight[0];
            [unroll]
            for (uint s = 1u; s < kSurfaceCacheMaxLocalLights; ++s) {
                if (s < k && topWeight[s] < weakestWeight) { weakest = s; weakestWeight = topWeight[s]; }
            }
        }
    }

    float3 sum = 0.0;
    [loop]
    for (uint s = 0u; s < kept; ++s) {
        uint lightIndex = 0u;
        [unroll]
        for (uint c = 0u; c < kSurfaceCacheMaxLocalLights; ++c) { if (c == s) lightIndex = topIndex[c]; }
        const SurfaceCacheLight l = lights[lightIndex];
        float3 target = l.position;
        if ((l.type & kSurfaceCacheLightTypeMask) == kSurfaceCacheLightTypeRect) {
            target += l.right * (2.0 * areaSample.x - 1.0) + l.up * (2.0 * areaSample.y - 1.0);
        }
        const float3 toTarget = target - origin;
        const float  dist     = length(toTarget);
        if (dist <= tmin + bias) continue;
        RayDesc shadowRay;
        shadowRay.Origin    = origin;
        shadowRay.Direction = toTarget / dist;
        shadowRay.TMin      = tmin;
        shadowRay.TMax      = dist - bias;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
        sq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, shadowRay);
        sq.Proceed();
        if (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) continue;
        sum += SurfaceCacheLightIrradianceOverPi(l, origin, n);
    }
    return albedo * sum;
}

#endif
