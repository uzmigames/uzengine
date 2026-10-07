/// UzEngine DDGI probe writers — shared constants, helpers and the probe ray trace.
///
/// Included by `ddgi_probe_trace_cs.hlsl` (one thread per probe ray: traces, shades, writes the
/// ray buffers) and `ddgi_probe_update_cs.hlsl` (one thread per atlas texel: integrates the probe's
/// rays from the buffers into the irradiance / depth atlases). Until block B.6 (2026-10-07) the
/// update shader traced every probe's rays once per atlas texel: 36 irradiance + 256 depth texels
/// per probe, each tracing the same 32 rays — 292 traces of every ray.
///
/// The including shader declares, before this include:
///   SamplerState linearClamp : register(s2);
///   a `RootConstants` cbuffer at b0 holding at least `g_tlasSrvIndex`, `g_probesPerAxis` and
///   `g_skyRadianceSrv` (the two shaders share the first eight root constants).
///
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Probes"
/// @ref Cigolle et al. 2014 "A Survey of Efficient Representations for Independent Unit Vectors"
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>

#ifndef UZ_DDGI_PROBE_COMMON_HLSLI
#define UZ_DDGI_PROBE_COMMON_HLSLI

#include "../common/constants.hlsli"   // kSentinelSrvIndex

// ─── Trace constants at b2 (matches `uz::RadianceCacheTraceConstants`) ─────────────────────

cbuffer TraceConstants : register(b2) {
    float gridOriginX;
    float gridOriginY;
    float gridOriginZ;
    float probeSpacing;

    uint  probesPerAxis;
    uint  totalProbes;
    uint  raysPerProbe;
    uint  frameIndex;

    float maxTraceDistance;
    float temporalBlendAlpha;
    uint  ddgiRayRadianceSrv;   ///< C++ `shBufferUav` (radiance cache only): the DDGI writers reuse the
                                ///< slot for the per-ray (radiance, hitT) buffer written by the trace pass.
    uint  ddgiRayInfoSrv;       ///< C++ `firstWrite` (radiance cache only): per-ray hit info buffer.

    // GDF clipmap parameters. They MUST stay scalar to mirror RadianceCacheTraceConstants: HLSL
    // gives every array element its own 16-byte register, so `float x[4]` would occupy 64 bytes
    // instead of 16 and shift every field below by 192 bytes.
    // @ref Microsoft HLSL "Packing Rules for Constant Variables"
    float gdfCenter0X, gdfCenter0Y, gdfCenter0Z, gdfExtent0;
    float gdfCenter1X, gdfCenter1Y, gdfCenter1Z, gdfExtent1;
    float gdfCenter2X, gdfCenter2Y, gdfCenter2Z, gdfExtent2;
    float gdfCenter3X, gdfCenter3Y, gdfCenter3Z, gdfExtent3;
    uint  gdfClipmapSrv0;
    uint  gdfClipmapSrv1;
    uint  gdfClipmapSrv2;
    uint  gdfClipmapSrv3;

    uint  numGdfClipmaps;
    float skyColorR;
    float skyColorG;
    float skyColorB;

    float lightDirX;
    float lightDirY;
    float lightDirZ;
    float lightIntensity;

    float lightColorR;
    float lightColorG;
    float lightColorB;
    float groundAlbedo;

    // VFX → DDGI emissive injection. Mirrors the trailing fields of
    // `uz::RadianceCacheTraceConstants` (engine/renderer/gi/radiance_cache.h).
    uint  vfxEmissiveSrvIndex;
    uint  vfxEmissiveSampleCount;
    float regionBlendAlpha;    ///< radiance cache only (block B.2 phase 4); layout mirror
    float _vfxEmissivePad1;

    // Radiance-cache grid-recentre row (history scroll); layout mirror, not consumed here.
    int   historyShiftX;
    int   historyShiftY;
    int   historyShiftZ;
    uint  _historyShiftPad;
};

// ─── VFX emissive sample (mirrors `uz::VfxEmissiveSample`, 28 bytes, scalars only) ────────────

struct VfxDDGIEmissiveSample {
    float positionX;        // offset 0
    float positionY;        // offset 4
    float positionZ;        // offset 8
    float emissiveX;        // offset 12
    float emissiveY;        // offset 16
    float emissiveZ;        // offset 20
    float influenceRadius;  // offset 24
};

// ─── Constants ────────────────────────────────────────────────────────────

static const float kPI            = 3.14159265358979323846;
static const float kRayTMin       = 1.0;          // cm — avoid self-hit
static const float kSurfaceBias   = 2.0;          // cm — shadow ray origin offset
static const uint  kIrradProbeSize = 6;           // matches DDGISystem::kIrradianceProbeSize
static const uint  kDepthProbeSize = 16;          // matches DDGISystem::kDepthProbeSize
static const uint  kMaxRaysPerProbe = 64;         // matches DDGISystem::kMaxRaysPerProbe (ray buffers)
static const float kSurfaceEpsilon = 5.0;         // cm — GDF normal central-difference offset

// Shared GDF surface sampling — supplies the hit normal and albedo that an
// inline RayQuery cannot report. Needs linearClamp + the trace cbuffer above.
#include "gdf_surface.hlsli"

// ─── Hammersley + spherical sample (shared with the SDF shader) ──────────────

float RadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverseVdC(i));
}

float3 SphericalSample(float2 xi) {
    float z = 1.0 - 2.0 * xi.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * kPI * xi.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

/// Ray `i` of `nRays` for a probe this frame: a full stratified set rotated per frame and per
/// probe (Cranley-Patterson), the same construction as radiance_cache_trace_cs.hlsl. The trace
/// pass and the update pass call this with the same arguments, so the buffers line up.
float3 ProbeRayDirection(uint i, uint nRays, uint probeIdx, uint frameIdx) {
    const uint frameJitter = (frameIdx * 1664525u + 1013904223u);
    const float2 xi = frac(Hammersley(i, nRays)
                           + float2(RadicalInverseVdC(frameJitter),
                                    RadicalInverseVdC(frameJitter ^ probeIdx)));
    return SphericalSample(xi);
}

// ─── Octahedral encode/decode (Cigolle 2014) ─────────────────────────────

float2 OctWrap(float2 v) {
    return (1.0 - abs(v.yx)) * float2(v.x >= 0.0 ? 1.0 : -1.0,
                                      v.y >= 0.0 ? 1.0 : -1.0);
}

float2 OctEncodeDir(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = (n.z >= 0.0) ? n.xy : OctWrap(n.xy);
    return e * 0.5 + 0.5; // map to [0,1]
}

float3 OctDecodeDir(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

// ─── Atlas tile layout helpers ───────────────────────────────────────────

uint3 ProbeIndexToCell(uint linearIdx) {
    uint x = linearIdx % g_probesPerAxis;
    uint y = (linearIdx / g_probesPerAxis) % g_probesPerAxis;
    uint z = linearIdx / (g_probesPerAxis * g_probesPerAxis);
    return uint3(x, y, z);
}

float3 ProbePosition(uint linearIdx) {
    uint3 c = ProbeIndexToCell(linearIdx);
    return float3(gridOriginX, gridOriginY, gridOriginZ)
         + float3(float(c.x), float(c.y), float(c.z)) * probeSpacing;
}

// Atlas tile = (probesPerAxis * probesPerAxis) tiles laid out across X, stacked probesPerAxis
// tiles along Y. Each tile is `(size+2)` to include the 1-texel border. Returns the texel
// coordinate of the probe's first interior texel.
uint2 ProbeAtlasOrigin(uint linearIdx, uint tileSize) {
    uint stride = tileSize + 2u;
    uint tileX  = linearIdx % (g_probesPerAxis * g_probesPerAxis);
    uint tileY  = linearIdx / (g_probesPerAxis * g_probesPerAxis);
    return uint2(tileX * stride + 1u,
                 tileY * stride + 1u);
}

// ─── Sky radiance for a ray that escapes the scene ──────────────────────
//
// The SDF radiance cache samples the sky cubemap in the ray direction and multiplies by the sky
// colour (radiance_cache_trace_cs.hlsl). Same cubemap, same Z-up to Y-up swizzle, same multiply.
float3 SkyRadiance(float3 dir) {
    const float3 tint = float3(skyColorR, skyColorG, skyColorB);
    if (g_skyRadianceSrv == kSentinelSrvIndex) {
        return tint;
    }
    TextureCube<float4> sky = ResourceDescriptorHeap[g_skyRadianceSrv];
    return sky.SampleLevel(linearClamp, float3(dir.x, dir.z, dir.y), 0).rgb * tint;
}

// ─── Known-radiance diagnostic (followup-01) ────────────────────────────
//
// mode 0 = constant L, mode 1 = L * (0.5 + 0.5 dot(dir, d0)) with d0 = normalize(0.3, 0.5, 0.8), a
// smooth non-negative function of direction that crosses every octahedral tile edge and corner.
static const float3 kKnownDir = float3(0.30305, 0.50508, 0.80812);
float3 KnownRadiance(float L, float3 dir, uint mode) {
    // mode 2 (per-probe parity) is applied by the caller through L itself; here it is a constant.
    const float v = (mode == 1u) ? L * (0.5 + 0.5 * dot(dir, kKnownDir)) : L;
    return float3(v, v, v);
}

// ─── Trace one probe ray and shade the hit with the sun ───────────────────
//
// Returns (radiance, hitT): the direct sun bounce `albedo * sunRad / pi` at the hit, or the sky
// radiance with hitT = maxTraceDistance for an escaping ray. `hitInfo` = (lit, albedo luminance,
// front-facing, visibility); `hitP`, `hitN` and `hitAlbedo` let the caller add the indirect term.
float4 TraceProbeRay(float3 origin, float3 dir, out float4 hitInfo,
                     out float3 hitP, out float3 hitN, out float3 hitAlbedo) {
    hitInfo   = float4(0.0, 0.0, 0.0, 0.0);
    hitP      = float3(0.0, 0.0, 0.0);
    hitN      = float3(0.0, 0.0, 0.0);
    hitAlbedo = float3(0.0, 0.0, 0.0);
    if (g_tlasSrvIndex == kSentinelSrvIndex) {
        return float4(SkyRadiance(dir), maxTraceDistance);
    }
    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[g_tlasSrvIndex];

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = kRayTMin;
    ray.TMax      = maxTraceDistance;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();

    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        const float  hitT = q.CommittedRayT();
        hitP = origin + dir * hitT;

        // Surface attributes come from the global distance field, the same source the SDF writer
        // shades with: inline RayQuery reports the hit distance but no attributes. Fall back to
        // `-dir` only where the field has no gradient (outside every cascade).
        float3 N = GdfNormal(hitP);
        if (dot(N, N) < 0.5) N = -dir;
        hitN = N;
        float3 L    = normalize(float3(lightDirX, lightDirY, lightDirZ));
        float  NdL  = max(dot(N, L), 0.0);

        // Shadow ray, as the SDF writer's `TraceGDF(shadowOrigin, L, ...) == -1` test.
        float visibility = 0.0;
        if (NdL > 0.0) {
            RayDesc shadowRay;
            shadowRay.Origin    = hitP + N * kSurfaceBias;
            shadowRay.Direction = L;
            shadowRay.TMin      = kRayTMin;
            shadowRay.TMax      = maxTraceDistance;

            RayQuery<RAY_FLAG_FORCE_OPAQUE
                   | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
            sq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, shadowRay);
            sq.Proceed();
            visibility = (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
        }

        float3 sunRad = float3(lightColorR, lightColorG, lightColorB)
                      * lightIntensity * NdL * visibility;

        // Lambertian BRDF: the radiance leaving a diffuse surface is `albedo/pi * E`. The SDF
        // writer carries the same term: `radiance = albedo * (sunRad / kPI + indirect)`.
        // @ref Majercik 2019 — probe atlases store E/pi (radiance-like).
        // Albedo comes from the field (GBA = diffuse reflectance); only a hit outside every
        // cascade (distance sentinel 1e9) falls back to the average albedo.
        const float4 surface = SampleGdfSurface(hitP);
        const float3 albedo = surface.x < 1e8 ? saturate(surface.gba)
                                              : float3(groundAlbedo, groundAlbedo, groundAlbedo);
        hitAlbedo = albedo;
        // z: 1 when the committed triangle faced the ray (diagnostic, H-63 follow-on).
        hitInfo = float4((NdL * visibility) > 0.0 ? 1.0 : 0.0,
                         dot(albedo, float3(0.2126, 0.7152, 0.0722)),
                         q.CommittedTriangleFrontFace() ? 1.0 : 0.0, visibility);
        return float4(albedo * (sunRad / kPI), hitT);
    }
    return float4(SkyRadiance(dir), maxTraceDistance);
}

#endif // UZ_DDGI_PROBE_COMMON_HLSLI
