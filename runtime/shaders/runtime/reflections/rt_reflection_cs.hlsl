/// UzEngine Hardware Ray-Traced Reflections — rays that fill the off-screen holes of the screen-space
/// trace (opt-in: `r.RayTracing.Reflections`, default off). Stage 1: mirror rays, sun + sky at the
/// hit. Stage 2 (each opt-in, default = the stage 1 behaviour): a GGX-VNDF sampled glossy ray, the
/// radiance-cache probe irradiance at the hit, and the SSR temporal + spatial denoise over the output.
///
/// One thread per pixel. Where this frame's screen trace found no confident hit
/// (`ReflectionNeedsRT`: eligible surface, SSR confidence <= 0.001) a single ray is traced against
/// the scene TLAS with an inline `RayQuery` (DXR 1.1) and the hit is shaded exactly as the reference
/// path tracer shades a bounce vertex (`gi_trace_common.hlsli`, the substrate the DDGI probe writer
/// shares): the interpolated vertex normal and the material albedo from the per-mesh table indexed
/// by TLAS InstanceID, the global distance field's gradient and reflectance where a mesh has no
/// table entry — an inline RayQuery reports the hit distance but no attributes — then the sun with
/// a shadow ray. A ray that escapes the scene returns the sky the path tracer and the probe writers
/// see. The result replaces that texel of the SSR resolved buffer, which the deferred lighting
/// already blends before the BRDF (`radiance = lerp(radiance, ssr.rgb, ssr.a)`), so no composite
/// wiring changes.
///
/// Fallback ladder (Lumen's screen -> hardware trace -> probe): SSR where it hit on screen; this
/// pass where it did not (a hit with attributes, or the sky); otherwise the texel is left untouched
/// and the lighting keeps its prefiltered probe (`ReflectionHitUsable` false: a hit outside every
/// GDF cascade with no table geometry, or a hit without a usable normal).
///
/// Stage 2 extensions (flags in the root constants):
///  - Glossy (`kReflectionFlagGlossy`): for roughness above the mirror threshold the ray is drawn
///    from the GGX distribution of visible normals — the path tracer's sampler (`SampleGGX_VNDF`)
///    on the deferred BRDF's alpha — so the texel is one sample of the lobe the lighting's
///    split-sum integrates. The ratio estimator at one sample per pixel is the hit radiance
///    itself (the BRDF x cos / pdf weight cancels in the normalisation), so the texel stays
///    "radiance before the BRDF" as the screen trace writes it. A sample below the horizon is
///    redrawn (up to 4 attempts), then the mirror direction (the lobe's mode) stands in, so the
///    texel always carries a valid sample for the temporal accumulation.
///  - Indirect (`kReflectionFlagIndirect`): the radiance cache's probe SH (E / pi) evaluated at the
///    hit with the lighting's trilinear, facing-weighted, guard-checked read (the grid rows of the
///    shared b2 constants) adds the diffuse bounce the reflected surface receives, so a reflected
///    surface in the sun's shadow is lit as the path tracer's multi-bounce lights it.
///  - Hit distance (`kReflectionFlagHitDistance`, with `r.RayTracing.Reflections.Denoise`): the
///    ray t is written to the SSR hit-distance texture for the SSR temporal pass's virtual-hit
///    reprojection; on that route the pass runs between SSR_Resolve and SSR_Temporal.
///
/// Stage 3 (`kReflectionFlagNoScreenTrace`): no screen trace this frame — SSR off (`r.SSR` 0), or
/// its trace pass skipped (`r.SSR.Trace` 0). The screen confidence is zero everywhere, so every
/// eligible texel is traced, and the pass owns the whole reflection buffer (and, on the denoise
/// route, the hit-distance texture): a texel it does not trace is written as "no reflection" (zero
/// weight, the lighting keeps its prefiltered probe / sky) instead of being left to a resolve that
/// did not run. The lighting reads the buffer through the same `PerFrameCB::ssrSrvIndex`, so with
/// SSR off the ladder is ray -> probe / sky. With the flag clear nothing here changes.
///
/// Phase D stage 1 (`kReflectionFlagSurfaceCache`, `r.GI.SurfaceCache`): the surface radiance cache
/// (gi/surface_cache.hlsli) at the hit. A readable cell replaces the WHOLE hit radiance — its
/// `sun` + `other` layers (`DirectMode 0`), or the inline sun with the shadow ray plus the cell's
/// `other` layer (`DirectMode 1`) — so the texel carries the cell's cell-and-time average of the
/// very estimator below; a miss shades inline exactly as before and feeds the two terms
/// (`albedo * sunRad / pi`, `albedo * E / pi`) into the cell. With the flag clear nothing changes.
///
/// Phase D stage 2 (lighting completeness at the hit, same flag): a shaded hit also adds the K
/// strongest point / spot / rect lights of the frame, each with a shadow ray, to the `sun` layer
/// (gi/surface_cache_lights.hlsli; `r.GI.SurfaceCache.LocalLights`) and the mesh's emission from the
/// material table to the `other` layer (`GITrace_HitEmissive`; `r.GI.SurfaceCache.Emissive`), so a
/// cell holds the lamp and the emitter the reference path tracer sees and stage 1 left black.
///
/// Bindings: root constants at b0 (`uz::RTReflectionRootConstants`, 17 DWORDs), PerFrameCB at b1,
/// the shared trace constants at b2 (`uz::RadianceCacheTraceConstants`, rows 1-11 as the DDGI
/// writer and the path tracer bind them; rows 1-2 carry the probe grid + frame index here).
/// Dispatch ceil(w/8) x ceil(h/8).
///
/// @ref Wright et al. 2022 "Lumen: Real-time Global Illumination in Unreal Engine 5" — trace ladder
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Probes"
/// @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
/// @ref Dupuy & Benyoub 2023 "Sampling Visible GGX Normals with Spherical Caps"
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections" — ratio estimator, IGN seeds
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>

#include "../common/constants.hlsli"     // kSentinelSrvIndex, PI, kMinGGXAlpha
#include "../common/encoding.hlsli"      // OctDecode
#include "../common/sampling.hlsli"      // SampleGGX_VNDF (+ math.hlsli: BuildOrthonormalBasis)
#include "../lighting/ibl.hlsli"         // EvalSH_L2 (the lighting's packed-SH evaluation)
#include "../gi/rc_probe_guard.hlsli"    // ProbeGuardIndex / ProbeEffectivePosition / ProbeGuardRejects
#include "rt_reflection_contract.hlsli"  // ReflectionDirection / ReflectionNeedsRT / glossy lobe / hit radiance

// ─── Root constants at b0 (mirrors `uz::RTReflectionRootConstants`) ────────────────────────

cbuffer RTReflectionRootCB : register(b0) {
    uint  depthSrvIdx;         // 0  GBuffer depth (reversed-Z device depth)
    uint  normalSrvIdx;        // 1  GBuffer normal: rg = octahedral normal, b = metallic, a = roughness
    uint  ssrTraceSrvIdx;      // 2  This frame's SSR trace output: a = screen confidence (unread, sentinel, with kReflectionFlagNoScreenTrace)
    uint  outputUavIdx;        // 3  SSR resolved buffer: rgb = reflected radiance, a = blend weight
    uint  tlasSrvIdx;          // 4  Scene TLAS
    uint  outputWidth;         // 5
    uint  outputHeight;        // 6
    float maxRoughness;        // 7  The `ReflectionEligible` cutoff (r.RayTracing.Reflections.MaxRoughness, else r.SSR.MaxRoughness)
    float rayMaxDistance;      // 8  cm — r.RayTracing.Reflections.MaxDistance (ray TMax)
    float intensity;           // 9  r.SSR.Intensity — the weight scale the screen trace uses
    float roughnessMaskScale;  // 10 -2 / maxRoughness — the screen trace's roughness fade
    uint  materialTableSrv;    // 11 GIMeshMaterial table by TLAS InstanceID (sentinel = GDF attributes only)
    uint  skyCubemapSrv;       // 12 Sky cubemap for escaping rays (sentinel = flat skylight colour)
    uint  probeShSrv;          // 13 Radiance-cache probe SH buffer (sentinel = no indirect term)
    uint  hitDistUavIdx;       // 14 SSR hit-distance texture (written with kReflectionFlagHitDistance)
    uint  flags;               // 15 kReflectionFlag* bits
    uint  surfaceCacheParamsSrv; // 16 Phase D: StructuredBuffer<SurfaceCacheParams> (1 element); sentinel = no cache
};

// ─── PerFrameCB at b1 (the prefix the SSR passes declare; invViewProj + cameraPos are read) ──

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float  nearPlane, farPlane;
};

float3 ReconstructWorldPos(float2 uv, float deviceZ) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos  = float4(ndc, deviceZ, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// ─── Trace constants at b2 + hit helpers (declares linearClamp s2, includes gdf_surface.hlsli).
// The helpers below read its grid rows (gridOrigin*, probeSpacing, probesPerAxis, frameIndex). ──

#include "../gi/gi_trace_common.hlsli"
#include "../gi/surface_cache.hlsli"      // Phase D: SurfaceCacheQuery / SurfaceCacheUpdate at the hit
#include "../gi/surface_cache_lights.hlsli" // Phase D stage 2: the frame's local lights at the hit

// ─── Glossy sample seeds: the screen trace's interleaved gradient noise with its temporal
// rotation (ssr_trace_cs.hlsl), so the ray texels carry the noise pattern the SSR temporal and
// spatial passes were built for. `attempt` offsets the pattern for a redraw.
// @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty"
static const uint kReflectionGlossyAttempts = 4u;

float ReflectionIGN(float2 p, uint frame) {
    p += float(frame % 64u) * float2(47.0, 17.0) * 0.695;
    return frac(52.9829189 * frac(p.x * 0.06711056 + p.y * 0.00583715));
}

float2 ReflectionSample2D(uint2 pixel, uint frame, uint attempt) {
    const float2 p = float2(pixel) + float(attempt) * float2(23.0, 41.0);
    return float2(ReflectionIGN(p, frame), ReflectionIGN(p + float2(37.0, 59.0), frame));
}

// ─── Radiance-cache probe irradiance (E / pi) at a world point: the SDF reflections' read
// (sdf_reflections.hlsli SdfReflectionIrradiance) on the grid rows of the b2 constants —
// trilinear over the eight surrounding probes, each weighted by its facing, skipped when invalid
// (record 6.w) or guarded behind a face; zero outside the grid.
float3 ReflectionProbeIrradianceOverPi(float3 position, float3 normal) {
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
        weight *= ReflectionProbeFacingWeight(normal, ProbeEffectivePosition(guard, gridPosition) - position);
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

void WriteHitDistance(uint2 pixel, float t) {
    if ((flags & kReflectionFlagHitDistance) == 0u) return;
    RWTexture2D<float> hitDistance = ResourceDescriptorHeap[hitDistUavIdx];
    hitDistance[pixel] = t;
}

// Stage 3: with no screen trace this frame the pass owns the whole buffer, so a texel it does not
// trace is written as "no reflection" (zero weight: the lighting keeps its prefiltered probe / sky)
// and its hit distance as "no hit", instead of being left to a screen trace that did not run. With
// the screen trace on (flag clear) the texel keeps the resolve's value, as in stages 1 and 2.
void WriteNoReflection(uint2 pixel) {
    if (!ReflectionOwnsOutput(flags)) return;
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputUavIdx];
    output[pixel] = float4(0.0, 0.0, 0.0, 0.0);
    WriteHitDistance(pixel, 0.0);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    Texture2D<float>  depthTex    = ResourceDescriptorHeap[depthSrvIdx];
    Texture2D<float4> normalTex   = ResourceDescriptorHeap[normalSrvIdx];

    const float  deviceZ          = depthTex.Load(int3(DTid.xy, 0)).r;
    const float4 normalData       = normalTex.Load(int3(DTid.xy, 0));
    const float  roughness        = normalData.a;
    // The current screen trace, not the temporally accumulated history: a reflection whose source
    // left the screen must hand over to the ray, not linger (contract comment). Stage 3: with no
    // screen trace this frame the confidence is zero everywhere (every eligible texel is traced)
    // and the trace texture is not read.
    float screenConfidence = 0.0;
    if (!ReflectionOwnsOutput(flags)) {
        Texture2D<float4> screenTrace = ResourceDescriptorHeap[ssrTraceSrvIdx];
        screenConfidence = screenTrace.Load(int3(DTid.xy, 0)).a;
    }
    if (!ReflectionNeedsRT(deviceZ, roughness, maxRoughness, screenConfidence)) {
        WriteNoReflection(DTid.xy);
        return;
    }

    // ── The ray from the visible surface: the mirror vector, or a glossy lobe sample ───────
    const float2 uv       = (float2(DTid.xy) + 0.5) / float2(outputWidth, outputHeight);
    const float3 worldPos = ReconstructWorldPos(uv, deviceZ);
    const float3 N        = OctDecode(normalData.rg);
    const float3 incident = normalize(worldPos - cameraPos.xyz);
    float3 R = ReflectionDirection(incident, N);
    if (ReflectionUsesGlossyLobe(flags, roughness)) {
        // A visible microfacet normal from the view direction (tangent space, Z along N), the ray
        // reflected about it — path_tracer_ps.hlsl's specular lobe on the same sampler and alpha.
        const float  alpha = ReflectionGlossyAlpha(roughness);
        const float3 V     = -incident;
        float3 T, B;
        BuildOrthonormalBasis(N, T, B);
        const float3 Vt = float3(dot(V, T), dot(V, B), dot(V, N));
        [loop]
        for (uint attempt = 0u; attempt < kReflectionGlossyAttempts; ++attempt) {
            const float3 Ht = SampleGGX_VNDF(alpha, Vt, ReflectionSample2D(DTid.xy, frameIndex, attempt));
            const float3 H  = normalize(T * Ht.x + B * Ht.y + N * Ht.z);
            const float3 L  = ReflectionDirection(incident, H);
            if (dot(L, N) > 0.0) { R = L; break; }
        }
    }
    // A shading normal facing away from the camera reflects the ray into the surface; skip it
    // (the lighting keeps its probe) rather than trace from inside the geometry.
    if (dot(R, N) <= 0.0) {
        WriteNoReflection(DTid.xy);
        return;
    }

    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[tlasSrvIdx];

    RayDesc ray;
    ray.Origin    = worldPos + N * kReflectionSurfaceBias;
    ray.Direction = R;
    ray.TMin      = kReflectionRayTMin;
    ray.TMax      = rayMaxDistance;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();

    // Same blend weight the screen trace would have carried for this surface (its roughness fade
    // times r.SSR.Intensity), so SSR and ray texels are weighted alike by the lighting.
    const float weight = saturate(roughness * roughnessMaskScale + 2.0) * intensity;
    RWTexture2D<float4> output = ResourceDescriptorHeap[outputUavIdx];

    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        // Escaped the scene: the sky the reference path tracer and the probe writers return for
        // a miss (the atmosphere capture / HDRI scaled by the skylight colour x intensity).
        output[DTid.xy] = float4(GITrace_SkyRadiance(R, skyCubemapSrv), weight);
        WriteHitDistance(DTid.xy, 0.0);   // the SSR convention: 0 = no hit, surface reprojection
        return;
    }

    // ── Surface attributes at the hit: the path tracer's bounce-vertex sources ─────────────
    const float3 hitP   = ray.Origin + R * q.CommittedRayT();
    const uint   meshId = q.CommittedInstanceID();
    float3 hitN            = float3(0.0, 0.0, 0.0);
    float3 albedo          = float3(0.0, 0.0, 0.0);
    float  surfaceDistance = 1e9;   // GDF distance sentinel: "no attributes here"
    float3 tableAlbedo, objectNormal;
    bool   haveGeometry = false;
    if (GITrace_HitSurface(meshId, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                           materialTableSrv, tableAlbedo, objectNormal)) {
        // Row-vector multiplication by WorldToObject applies the inverse transpose to the
        // object-space normal (nonuniform instance scale included), as path_tracer_ps.hlsl.
        float3 n = mul(objectNormal, (float3x3)q.CommittedWorldToObject3x4());
        if (dot(n, n) > 1e-12) {
            hitN = normalize(n);
            if (dot(hitN, R) > 0.0) hitN = -hitN;
            albedo          = tableAlbedo;
            surfaceDistance = 0.0;
            haveGeometry    = true;
        }
    }
    if (!haveGeometry) {
        // No table geometry for this mesh: the global distance field, as the DDGI writer — its
        // gradient for the normal (zero outside every cascade) and the reflectance stored beside
        // the distance (the table's tint when the mesh has an entry).
        const float4 surface = SampleGdfSurface(hitP);
        const float3 g = GdfNormal(hitP);
        hitN            = (dot(g, R) > 0.0) ? -g : g;
        surfaceDistance = surface.x;
        albedo          = GITrace_HitAlbedo(meshId, hitP, materialTableSrv);
    }
    // A hit without attributes is left to the lighting's prefiltered probe.
    if (!ReflectionHitUsable(true, surfaceDistance, dot(hitN, hitN))) {
        WriteNoReflection(DTid.xy);
        return;
    }

    // ── Phase D stage 1: the surface radiance cache at the hit ────────────────────────
    // The cell of the biased hit point (the point the probe term is read at) and the hit normal,
    // at the level of the CAMERA distance (D3). A readable cell replaces the whole hit radiance
    // (section 2.5's energy rule); a miss shades inline below and feeds the cell.
    const bool   cacheBound = (flags & kReflectionFlagSurfaceCache) != 0u
                           && surfaceCacheParamsSrv != kGITraceSentinelSrv;
    const float3 cachePos   = hitP + hitN * kReflectionSurfaceBias;
    SurfaceCacheParams cache = (SurfaceCacheParams)0;
    float3 cacheSun = 0.0, cacheOther = 0.0;
    uint   cacheSamples = 0u;
    bool   cacheHit = false;
    if (cacheBound) {
        StructuredBuffer<SurfaceCacheParams> cacheParams = ResourceDescriptorHeap[surfaceCacheParamsSrv];
        cache    = cacheParams[0];
        cacheHit = SurfaceCacheQuery(cache, cachePos, hitN, cameraPos.xyz, 0u, cacheSun, cacheOther, cacheSamples);
    }
    // One hit in Refresh rays shades inline as a miss does and feeds the cell (the output stays the
    // cell's), so readable cells keep converging; DirectMode 1 keeps the inline sun (sharp shadows)
    // and takes only `other` from the cell.
    const bool cacheRefresh = cacheHit && SurfaceCacheRefresh(DTid.x | (DTid.y << 16), frameIndex, cache.refreshRate);
    const bool inlineSun = !cacheHit || cacheRefresh || (cache.flags & kSurfaceCacheFlagDirectInline) != 0u;

    // ── Direct sun with a shadow ray (the DDGI writer's / path tracer's NEE) ─────────────
    const float3 L   = normalize(float3(lightDirX, lightDirY, lightDirZ));
    const float  NdL = max(dot(hitN, L), 0.0);
    float visibility = 0.0;
    if (inlineSun && NdL > 0.0) {
        RayDesc shadowRay;
        shadowRay.Origin    = hitP + hitN * kReflectionSurfaceBias;
        shadowRay.Direction = L;
        shadowRay.TMin      = kReflectionRayTMin;
        shadowRay.TMax      = rayMaxDistance;

        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
        sq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, shadowRay);
        sq.Proceed();
        visibility = (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
    }
    const float3 sunRad = float3(lightColorR, lightColorG, lightColorB) * lightIntensity * NdL * visibility;
    float3 radiance = albedo * (sunRad / PI);          // Lambert: the radiance the mirror sees
    float3 sunTerm = radiance;                         // the cell's `sun` layer (direct lighting)
    float3 otherTerm = 0.0;                            // the cell's `other` layer (the probe bounce)
    // Phase D stage 2: the frame's local lights join the direct layer wherever the sun is shaded
    // inline (a miss, a refresh, DirectMode 1), each of the K strongest with a shadow ray; the
    // rect light's area sample rotates with the glossy sequence (one draw per hit and frame).
    float3 localTerm = 0.0;
    if (cacheBound && inlineSun) {
        localTerm = SurfaceCacheLocalLightRadiance(cache, cachePos, hitN, albedo, tlas, kReflectionRayTMin,
                                                   kReflectionSurfaceBias,
                                                   ReflectionSample2D(DTid.xy, frameIndex, kReflectionGlossyAttempts));
        sunTerm += localTerm;
    }
    if ((!cacheHit || cacheRefresh) && (flags & kReflectionFlagIndirect) != 0u) {
        // The diffuse bounce the reflected surface receives (the radiance cache's E / pi at the
        // hit, read from the biased hit point as the SDF reflections do): the term the path
        // tracer's multi-bounce carries and stage 1 left out.
        const float3 probeIrradianceOverPi = ReflectionProbeIrradianceOverPi(cachePos, hitN);
        radiance  = ReflectionHitRadiance(albedo, sunRad, probeIrradianceOverPi);
        otherTerm = albedo * probeIrradianceOverPi;
    }
    if (cacheBound && (!cacheHit || cacheRefresh)) {
        // Stage 2: the mesh's emission (the table's emissive colour x intensity and texture) is
        // part of what leaves the hit; it lives in the `other` layer beside the bounce.
        float3 emissiveTerm = 0.0;
        if ((cache.flags & kSurfaceCacheFlagEmissive) != 0u) {
            emissiveTerm = GITrace_HitEmissive(meshId, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                                               materialTableSrv);
            otherTerm += emissiveTerm;
        }
        // The inline radiance gains the stage 2 terms and nothing else (added to the stage 1 value,
        // so `.LocalLights 0` + `.Emissive 0` reproduces the stage 1 texel bit for bit).
        radiance += localTerm + emissiveTerm;
        SurfaceCacheUpdate(cache, cachePos, hitN, cameraPos.xyz, 0u, sunTerm, otherTerm,
                           kSurfaceCacheWriterReflections);
    }
    if (cacheHit) {
        // The cell replaces the hit radiance: both layers, or the inline sun with the cell's other.
        const bool cellSun = (cache.flags & kSurfaceCacheFlagDirectInline) == 0u;
        radiance = (cellSun ? cacheSun : sunTerm) + cacheOther;
    }
    if (cacheBound && SurfaceCacheDebugMode(cache) != 0u) {
        radiance = SurfaceCacheDebugColor(cache, cachePos, hitN, cameraPos.xyz, 0u, cacheHit, cacheSamples);
    }

    output[DTid.xy] = float4(radiance, weight);
    WriteHitDistance(DTid.xy, q.CommittedRayT());
}
