/// UzEngine Hardware Ray-Traced Reflections — stage 1: mirror rays that fill the off-screen holes
/// of the screen-space trace (opt-in: `r.RayTracing.Reflections`, default off).
///
/// One thread per pixel. Where this frame's screen trace found no confident hit
/// (`ReflectionNeedsRT`: eligible surface, SSR confidence <= 0.001) a single mirror ray is traced
/// against the scene TLAS with an inline `RayQuery` (DXR 1.1) and the hit is shaded exactly as the
/// reference path tracer shades a bounce vertex (`gi_trace_common.hlsli`, the substrate the DDGI
/// probe writer shares): the interpolated vertex normal and the material albedo from the per-mesh
/// table indexed by TLAS InstanceID, the global distance field's gradient and reflectance where a
/// mesh has no table entry — an inline RayQuery reports the hit distance but no attributes — then
/// the sun with a shadow ray. A ray that escapes the scene returns the sky the path tracer and the
/// probe writers see. The result replaces that texel of the SSR resolved buffer, which the deferred
/// lighting already blends before the BRDF (`radiance = lerp(radiance, ssr.rgb, ssr.a)`), so no
/// composite wiring changes.
///
/// Fallback ladder (Lumen's screen -> hardware trace -> probe): SSR where it hit on screen; this
/// pass where it did not (a hit with attributes, or the sky); otherwise the texel is left untouched
/// and the lighting keeps its prefiltered probe (`ReflectionHitUsable` false: a hit outside every
/// GDF cascade with no table geometry, or a hit without a usable normal).
///
/// Stage 1 is mirror / low roughness only (the `r.SSR.MaxRoughness` cutoff shared with SSR).
/// Deferred to stage 2: GGX-VNDF glossy rays, the radiance-cache SH indirect term at the hit,
/// denoise / temporal accumulation.
///
/// Bindings: root constants at b0 (`uz::RTReflectionRootConstants`, 13 DWORDs), PerFrameCB at b1,
/// the shared trace constants at b2 (`uz::RadianceCacheTraceConstants`, rows 1-11 as the DDGI
/// writer and the path tracer bind them). Dispatch ceil(w/8) x ceil(h/8).
///
/// @ref Wright et al. 2022 "Lumen: Real-time Global Illumination in Unreal Engine 5" — trace ladder
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Probes"
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>

#include "../common/constants.hlsli"     // kSentinelSrvIndex, PI
#include "../common/encoding.hlsli"      // OctDecode
#include "rt_reflection_contract.hlsli"  // ReflectionDirection / ReflectionEligible / ReflectionNeedsRT / ReflectionHitUsable

// ─── Root constants at b0 (mirrors `uz::RTReflectionRootConstants`) ────────────────────────

cbuffer RTReflectionRootCB : register(b0) {
    uint  depthSrvIdx;         // 0  GBuffer depth (reversed-Z device depth)
    uint  normalSrvIdx;        // 1  GBuffer normal: rg = octahedral normal, b = metallic, a = roughness
    uint  ssrTraceSrvIdx;      // 2  This frame's SSR trace output: a = screen confidence
    uint  outputUavIdx;        // 3  SSR resolved buffer: rgb = reflected radiance, a = blend weight
    uint  tlasSrvIdx;          // 4  Scene TLAS
    uint  outputWidth;         // 5
    uint  outputHeight;        // 6
    float maxRoughness;        // 7  r.SSR.MaxRoughness — the `ReflectionEligible` cutoff
    float rayMaxDistance;      // 8  cm — r.RayTracing.Reflections.MaxDistance (ray TMax)
    float intensity;           // 9  r.SSR.Intensity — the weight scale the screen trace uses
    float roughnessMaskScale;  // 10 -2 / MaxRoughness — the screen trace's roughness fade
    uint  materialTableSrv;    // 11 GIMeshMaterial table by TLAS InstanceID (sentinel = GDF attributes only)
    uint  skyCubemapSrv;       // 12 Sky cubemap for escaping rays (sentinel = flat skylight colour)
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

// ─── Trace constants at b2 + hit helpers (declares linearClamp s2, includes gdf_surface.hlsli) ──

#include "../gi/gi_trace_common.hlsli"

float3 ReconstructWorldPos(float2 uv, float deviceZ) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos  = float4(ndc, deviceZ, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    Texture2D<float>  depthTex    = ResourceDescriptorHeap[depthSrvIdx];
    Texture2D<float4> normalTex   = ResourceDescriptorHeap[normalSrvIdx];
    Texture2D<float4> screenTrace = ResourceDescriptorHeap[ssrTraceSrvIdx];

    const float  deviceZ          = depthTex.Load(int3(DTid.xy, 0)).r;
    const float4 normalData       = normalTex.Load(int3(DTid.xy, 0));
    const float  roughness        = normalData.a;
    // The current screen trace, not the temporally accumulated history: a reflection whose source
    // left the screen must hand over to the ray, not linger (contract comment).
    const float  screenConfidence = screenTrace.Load(int3(DTid.xy, 0)).a;
    if (!ReflectionNeedsRT(deviceZ, roughness, maxRoughness, screenConfidence)) return;

    // ── Mirror ray from the visible surface ──────────────────────────────────────────────
    const float2 uv       = (float2(DTid.xy) + 0.5) / float2(outputWidth, outputHeight);
    const float3 worldPos = ReconstructWorldPos(uv, deviceZ);
    const float3 N        = OctDecode(normalData.rg);
    const float3 incident = normalize(worldPos - cameraPos.xyz);
    const float3 R        = ReflectionDirection(incident, N);
    // A shading normal facing away from the camera reflects the ray into the surface; skip it
    // (the lighting keeps its probe) rather than trace from inside the geometry.
    if (dot(R, N) <= 0.0) return;

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
    if (!ReflectionHitUsable(true, surfaceDistance, dot(hitN, hitN))) return;

    // ── Direct sun with a shadow ray (the DDGI writer's / path tracer's NEE) ─────────────
    const float3 L   = normalize(float3(lightDirX, lightDirY, lightDirZ));
    const float  NdL = max(dot(hitN, L), 0.0);
    float visibility = 0.0;
    if (NdL > 0.0) {
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
    const float3 sunRad   = float3(lightColorR, lightColorG, lightColorB) * lightIntensity * NdL * visibility;
    const float3 radiance = albedo * (sunRad / PI);          // Lambert: the radiance the mirror sees

    output[DTid.xy] = float4(radiance, weight);
}
