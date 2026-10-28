/// UzEngine GI Trace Common — b2 trace constants + world-hit helpers for the
/// reference path tracer's bounce hits.
///
/// The cbuffer declares rows 1-11 of `RadianceCacheTraceConstants`
/// (radiance_cache.h), scalars only, exactly as `ddgi_probe_common.hlsli`
/// does: every shader that binds that CB must declare a prefix of it, or HLSL
/// register packing shifts every field past the mismatch. Surface attributes
/// come from the global distance field through `gdf_surface.hlsli` (the GDF
/// stores the signed distance in R and the nearest surface's diffuse
/// reflectance in GBA), so the path tracer shades the same surfaces the DDGI
/// and SDF probe writers do; a per-mesh material table, when bound, refines
/// the albedo by TLAS InstanceID.
///
/// # Requirements
/// The including shader must include `../common/constants.hlsli`
/// (`kSentinelSrvIndex`) before this file.
///
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
///                     Irradiance Probes"
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>
/// @ref Microsoft HLSL "Packing Rules for Constant Variables"

#ifndef GI_TRACE_COMMON_HLSLI
#define GI_TRACE_COMMON_HLSLI

static const uint kGITraceSentinelSrv = 0xFFFFFFFFu;

// ─── Trace constants at b2 (rows 1-11 of RadianceCacheTraceConstants) ──────

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
    uint  _shBufferUav;         ///< Radiance cache only; layout mirror.
    uint  _firstWrite;          ///< Radiance cache only; layout mirror.

    // GDF clipmap parameters. They MUST stay scalar to mirror
    // RadianceCacheTraceConstants: HLSL gives every array element its own
    // 16-byte register, so `float x[4]` would occupy 64 bytes instead of 16.
    float gdfCenter0X, gdfCenter0Y, gdfCenter0Z, gdfExtent0;
    float gdfCenter1X, gdfCenter1Y, gdfCenter1Z, gdfExtent1;
    float gdfCenter2X, gdfCenter2Y, gdfCenter2Z, gdfExtent2;
    float gdfCenter3X, gdfCenter3Y, gdfCenter3Z, gdfExtent3;
    uint  gdfClipmapSrv0;
    uint  gdfClipmapSrv1;
    uint  gdfClipmapSrv2;
    uint  gdfClipmapSrv3;

    uint  numGdfClipmaps;
    float skyColorR;            ///< Skylight colour × intensity.
    float skyColorG;
    float skyColorB;

    float lightDirX;
    float lightDirY;
    float lightDirZ;
    float lightIntensity;

    float lightColorR;
    float lightColorG;
    float lightColorB;
    float groundAlbedo;         ///< Albedo for a hit outside every GDF cascade.
};

// ─── GDF surface sampling ──────────────────────────────────────────────────

/// Static bilinear-clamp sampler of the shared root signature (s2); also the
/// sampler `gdf_surface.hlsli` expects under this name.
SamplerState linearClamp : register(s2);
/// Material sampling uses the raster path's anisotropic-wrap static sampler.

/// The per-mesh material table (`GIMeshMaterial`, mirrors GIMeshMaterialGPU) and the exact
/// hit attributes read from it (`GITrace_HitSurface`, `GITrace_HitUV`, `GITrace_HitEmissive`):
/// split into their own header (Phase D stage 3) so the DDGI writer, which declares its own
/// b2 block, shades its hits with the same material. The functions are unchanged.
#include "gi_hit_material.hlsli"

/// Central-difference offset of the GDF normal, in cm — the hit tolerance
/// the DDGI writer uses (ddgi_probe_common.hlsli).
static const float kSurfaceEpsilon = 5.0;

#include "gdf_surface.hlsli"

/// Surface normal at a world-space hit from the GDF gradient, oriented
/// against the incident ray. Falls back to the negated incident direction
/// where the field has no gradient (outside every cascade).
float3 GITrace_GdfNormal(float3 hitPos, float3 incidentDir) {
    float3 n = GdfNormal(hitPos);
    if (dot(n, n) < 0.5) return -incidentDir;
    return (dot(n, incidentDir) > 0.0) ? -n : n;
}

// ─── Miss + hit shading inputs ─────────────────────────────────────────────

/// Skylight colour × intensity (r.SkyLight.* and the SkyLight component), the
/// factor every sky read is scaled by — the raster split-sum
/// (`skyTintSpecular`) and the probe writers' `SkyRadiance` alike.
float3 GITrace_SkyTint() {
    return float3(skyColorR, skyColorG, skyColorB);
}

/// Sky radiance for a miss ray — the atmosphere cubemap scaled by the skylight
/// colour × intensity when bound (the convention of the DDGI and radiance-cache
/// writers: the capture holds the unit-intensity sky, the SkyLight component
/// carries the scene's intensity), the flat skylight colour otherwise.
/// Z-up world → Y-up cubemap swizzle.
float3 GITrace_SkyRadiance(float3 dir, uint skyCubemapSrv) {
    if (skyCubemapSrv != kGITraceSentinelSrv) {
        TextureCube<float4> sky = ResourceDescriptorHeap[skyCubemapSrv];
        return sky.SampleLevel(linearClamp, float3(dir.x, dir.z, dir.y), 0).rgb * GITrace_SkyTint();
    }
    return GITrace_SkyTint();
}

/// Hit-surface albedo. With a material table: the mesh entry (tint × albedo
/// texture average — the LAST mip of a mip-mapped texture is its box-filtered
/// average, so SampleLevel at a large LOD clamps to it). Without one: the GDF
/// surface reflectance at the hit, then `groundAlbedo` outside every cascade.
float3 GITrace_HitAlbedo(uint meshId, float3 hitPos, uint materialTableSrv) {
    if (materialTableSrv != kGITraceSentinelSrv) {
        StructuredBuffer<GIMeshMaterial> table = ResourceDescriptorHeap[materialTableSrv];
        GIMeshMaterial mat = table[meshId];
        float3 albedo = mat.tint;
        if (mat.albedoSrv != kGITraceSentinelSrv) {
            Texture2D<float4> albedoTex = ResourceDescriptorHeap[NonUniformResourceIndex(mat.albedoSrv)];
            albedo *= albedoTex.SampleLevel(linearClamp, float2(0.5, 0.5), 16.0).rgb;
        }
        return albedo;
    }
    const float4 surface = SampleGdfSurface(hitPos);
    return surface.x < 1e8 ? saturate(surface.gba)
                           : float3(groundAlbedo, groundAlbedo, groundAlbedo);
}

#endif // GI_TRACE_COMMON_HLSLI
