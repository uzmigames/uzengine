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

/// Mirrors GIMeshMaterialGPU (path_tracer_pass.h, 48 bytes).
struct GIMeshMaterial {
    float3 tint;                ///< BaseColor tint factor (linear).
    uint   albedoSrv;           ///< Albedo texture SRV (sentinel = tint only).
    uint   vbSrv;               ///< Raw vertex buffer SRV.
    uint   ibSrv;               ///< Raw index buffer SRV.
    uint   vertexStride;        ///< Byte distance between vertices.
    uint   uvOffset;            ///< Float2 UV byte offset.
    uint   normalOffset;        ///< Normal byte offset.
    uint   indexFormat;         ///< Index element size in bytes (2 or 4).
    uint   normalFormat;        ///< 0 = float3, 1 = signed packed int8 xyz.
    uint   padding;
};

// ─── GDF surface sampling ──────────────────────────────────────────────────

/// Static bilinear-clamp sampler of the shared root signature (s2); also the
/// sampler `gdf_surface.hlsli` expects under this name.
SamplerState linearClamp : register(s2);
/// Material sampling uses the raster path's anisotropic-wrap static sampler.

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

/// Recovers triangle UVs and an object-space interpolated vertex normal.
/// The caller applies the committed instance's inverse-transpose transform.
/// Missing geometry returns false so legacy meshes retain their GDF fallback.
bool GITrace_HitSurface(uint meshId, uint primIndex, float2 bary,
                       uint materialTableSrv, out float3 albedo, out float3 geomNormal) {
    albedo = 0.0;
    geomNormal = 0.0;
    if (materialTableSrv == kGITraceSentinelSrv) return false;
    StructuredBuffer<GIMeshMaterial> table = ResourceDescriptorHeap[materialTableSrv];
    GIMeshMaterial mat = table[meshId];
    if (mat.vbSrv == kGITraceSentinelSrv || mat.ibSrv == kGITraceSentinelSrv)
        return false;

    ByteAddressBuffer vertices = ResourceDescriptorHeap[NonUniformResourceIndex(mat.vbSrv)];
    ByteAddressBuffer indices = ResourceDescriptorHeap[NonUniformResourceIndex(mat.ibSrv)];
    float3 weights = float3(1.0 - bary.x - bary.y, bary.x, bary.y);
    float2 uv = 0.0;
    [unroll]
    for (uint corner = 0; corner < 3; ++corner) {
        uint indexAddress = (primIndex * 3u + corner) * mat.indexFormat;
        uint vertexIndex = indices.Load(indexAddress & ~3u);
        if (mat.indexFormat == 2u)
            vertexIndex = (vertexIndex >> ((indexAddress & 2u) * 8u)) & 0xFFFFu;
        uint vertexAddress = vertexIndex * mat.vertexStride;
        uv += weights[corner] * asfloat(vertices.Load2(vertexAddress + mat.uvOffset));
        float3 normal;
        if (mat.normalFormat == 1u) {
            uint packed = vertices.Load(vertexAddress + mat.normalOffset);
            normal = float3(asint(packed << 24u) >> 24,
                            asint(packed << 16u) >> 24,
                            asint(packed << 8u) >> 24) / 127.0;
        } else {
            normal = asfloat(vertices.Load3(vertexAddress + mat.normalOffset));
        }
        geomNormal += weights[corner] * normal;
    }

    albedo = mat.tint;
    if (mat.albedoSrv != kGITraceSentinelSrv) {
        // Guard the UV: an unexpected vertex layout can make the raw-buffer read
        // produce a non-finite UV. Sampling at NaN returns NaN, and with the
        // firefly clamp off the concave helmet (whose bounce rays strike itself
        // every frame) would accumulate that NaN into a permanently black pixel.
        float2 safeUv = (isfinite(uv.x) && isfinite(uv.y)) ? uv : float2(0.0, 0.0);
        Texture2D<float4> albedoTex = ResourceDescriptorHeap[NonUniformResourceIndex(mat.albedoSrv)];
        albedo *= albedoTex.SampleLevel(linearClamp, safeUv, 0.0).rgb;
    }
    // Firewall: a bad read may darken this hit but must never NaN-poison the path.
    if (!all(isfinite(albedo)))     albedo = max(mat.tint, 0.0);
    if (!all(isfinite(geomNormal))) { geomNormal = 0.0; return false; }
    return true;
}

#endif // GI_TRACE_COMMON_HLSLI
