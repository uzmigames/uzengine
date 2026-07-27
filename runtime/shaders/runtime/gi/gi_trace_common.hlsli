/// UzEngine GI Trace Common — shared b2 constants + world-hit helpers for
/// every shader that traces the scene for GI (DDGI atlas writer, reference
/// path tracer; the SDF radiance-cache tracer shares the same b2 layout but
/// keeps its own declarations for its sphere-trace specifics).
///
/// The cbuffer mirrors `RadianceCacheTraceConstants` (radiance_cache.h) —
/// any layout change must update BOTH files and the static_assert in
/// test_gi_gpu.cpp.
///
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
///                     Irradiance Probes"
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>

#ifndef GI_TRACE_COMMON_HLSLI
#define GI_TRACE_COMMON_HLSLI

static const uint kGITraceSentinelSrv = 0xFFFFFFFFu;

// ─── Trace constants at b2 (mirrors RadianceCacheTraceConstants) ───────────

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
    uint  shBufferUav;          // Only used by the SH probe writer.
    uint  _padTC;

    // NOTE: these MUST be float4/uint4 — scalar arrays (`float x[4]`) get one
    // 16-byte register PER ELEMENT in HLSL cbuffers, which silently shifts
    // every field below (the garbage-sun/sky bug of 2026-07-27).
    float4 gdfCenterExtent0;    // xyz = clipmap center (world cm), w = half-extent.
    float4 gdfCenterExtent1;
    float4 gdfCenterExtent2;
    float4 gdfCenterExtent3;
    uint4  gdfSrv;              // x..w = clipmap SRV indices 0..3.

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
    float groundAlbedo;          // Legacy scalar fallback (kept for layout).

    float sceneAlbedoR;          // Scene-average albedo fallback (linear).
    float sceneAlbedoG;
    float sceneAlbedoB;
    uint  skyCubemapSrv;         // Atmosphere sky cubemap SRV (sentinel = flat sky).

    uint  materialTableSrv;      // StructuredBuffer<GIMeshMaterial>, indexed by InstanceID (= meshId).
    uint  rcProbeShSrv;          // Radiance-cache SH buffer SRV — multi-bounce feedback.
    uint  _padHit0;
    uint  _padHit1;
};

/// Mirrors GIMeshMaterialGPU (radiance_cache.h, 16 bytes).
struct GIMeshMaterial {
    float3 tint;                 // BaseColor tint factor (linear).
    uint   albedoSrv;            // Albedo texture SRV (sentinel = tint only).
};

// ─── GDF clipmap sampling (cascade walk) ───────────────────────────────────

/// GDF clipmap distance sample. Returns a large value when the clipmap does
/// not cover the point.
float GITrace_SampleGdfClip(uint srvIdx, float3 p, float4 centerExtent) {
    if (srvIdx == kGITraceSentinelSrv || centerExtent.w <= 0.0) return 1e9;
    float3 local = (p - centerExtent.xyz) / centerExtent.w;
    if (any(abs(local) > 1.0)) return 1e9;
    Texture3D<float> gdfTex = ResourceDescriptorHeap[srvIdx];
    SamplerState samp = SamplerDescriptorHeap[1];
    return gdfTex.SampleLevel(samp, local * 0.5 + 0.5, 0).r;
}

/// Cascade walk: finest clipmap covering `p` wins.
float GITrace_SampleGdf(float3 p) {
    float d = GITrace_SampleGdfClip(gdfSrv.x, p, gdfCenterExtent0);
    if (d < 1e8) return d;
    d = GITrace_SampleGdfClip(gdfSrv.y, p, gdfCenterExtent1);
    if (d < 1e8) return d;
    d = GITrace_SampleGdfClip(gdfSrv.z, p, gdfCenterExtent2);
    if (d < 1e8) return d;
    return GITrace_SampleGdfClip(gdfSrv.w, p, gdfCenterExtent3);
}

/// Surface normal at a world-space hit via the GDF central-difference
/// gradient. Falls back to the negated incident direction when the GDF has
/// no data (gradient degenerate). Always faces against the incident ray.
float3 GITrace_GdfNormal(float3 hitPos, float3 incidentDir) {
    const float e = 5.0; // cm — matches the SDF tracer's epsilon
    float3 g = float3(
        GITrace_SampleGdf(hitPos + float3(e, 0, 0)) - GITrace_SampleGdf(hitPos - float3(e, 0, 0)),
        GITrace_SampleGdf(hitPos + float3(0, e, 0)) - GITrace_SampleGdf(hitPos - float3(0, e, 0)),
        GITrace_SampleGdf(hitPos + float3(0, 0, e)) - GITrace_SampleGdf(hitPos - float3(0, 0, e)));
    float len = length(g);
    if (len <= 1e-5) return -incidentDir;
    float3 n = g / len;
    return (dot(n, incidentDir) > 0.0) ? -n : n;
}

// ─── Miss + hit shading inputs ─────────────────────────────────────────────

/// Sky radiance for a miss ray — atmosphere cubemap when available, flat
/// skylight color otherwise. Z-up world → Y-up cubemap swizzle.
float3 GITrace_SkyRadiance(float3 dir) {
    if (skyCubemapSrv != kGITraceSentinelSrv) {
        TextureCube<float4> sky = ResourceDescriptorHeap[skyCubemapSrv];
        SamplerState samp = SamplerDescriptorHeap[1];
        return sky.SampleLevel(samp, float3(dir.x, dir.z, dir.y), 0).rgb;
    }
    return float3(skyColorR, skyColorG, skyColorB);
}

/// Hit-surface albedo: per-mesh material table entry (tint × albedo-texture
/// average). The texture average comes free from the sampler — the LAST mip
/// of a mip-mapped texture is its box-filtered average, so SampleLevel at a
/// huge LOD clamps to it. Falls back to the scene-average constant.
float3 GITrace_HitAlbedo(uint meshId) {
    if (materialTableSrv == kGITraceSentinelSrv) {
        return float3(sceneAlbedoR, sceneAlbedoG, sceneAlbedoB);
    }
    StructuredBuffer<GIMeshMaterial> table = ResourceDescriptorHeap[materialTableSrv];
    GIMeshMaterial mat = table[meshId];
    float3 albedo = mat.tint;
    if (mat.albedoSrv != kGITraceSentinelSrv) {
        Texture2D<float4> albedoTex = ResourceDescriptorHeap[mat.albedoSrv];
        SamplerState samp = SamplerDescriptorHeap[1];
        albedo *= albedoTex.SampleLevel(samp, float2(0.5, 0.5), 16.0).rgb;
    }
    return albedo;
}

#endif // GI_TRACE_COMMON_HLSLI
