/// UzEngine GI hit material — the per-mesh material table by TLAS InstanceID and the exact
/// surface attributes a hit reads from it (triangle UV albedo, interpolated vertex normal, the
/// mesh's emission). Split out of `gi_trace_common.hlsli` (Phase D stage 3) so a writer that
/// declares its own b2 trace constants (`ddgi_probe_common.hlsli`) can shade its hits with the
/// same material the reflection pass, the feed trace and the reference path tracer use; the
/// functions are the ones `gi_trace_common.hlsli` has always provided, unchanged.
///
/// # Requirements
/// The including shader must declare, BEFORE this file:
///   - `kGITraceSentinelSrv` (gi_trace_common.hlsli) or an equal 0xFFFFFFFF constant under
///     that name
///   - `SamplerState linearClamp : register(s2);` (the static bilinear-clamp sampler)
///
/// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<> reports no
///      attributes; the raw vertex / index views supply them.

#ifndef UZ_GI_HIT_MATERIAL_HLSLI
#define UZ_GI_HIT_MATERIAL_HLSLI

#ifdef UZ_GI_HIT_UV_WRAP
/// r.GI.HitMaterial.WrapUv (docs/analysis/lumen-radiance-cache-stability/07-firstbounce-diffuse-decomposition): the
/// hit's albedo / emissive texture is sampled with the shared root signature's bilinear WRAP static
/// sampler (s3, dx12_pso_creation.cpp) instead of the bilinear CLAMP one, as the raster path
/// (anisotropic wrap, s0) and the reference path tracer (its `linearWrap`) sample the same textures.
/// A tiled triangle UV outside [0, 1] then reads the tiled content instead of the texture's edge
/// texel (Sponza's floors and walls: edges 0.66-0.78x of the texture mean, which depressed every GI
/// consumer's hit albedo and the gather's E/pi to 0.880 of the path tracer's). Only the
/// `UZ_GI_HIT_UV_WRAP=1` permutations compile this block; the default pipelines keep the committed
/// text. One resource per register: `gdf_surface.hlsli` / `sdf_reflections.hlsli` already bind s3
/// under their own names when they precede this file, so reuse that declaration, else declare it
/// here and publish it for a later include of either.
#if defined(UZ_GDF_WRAP_SAMPLER)
#define UZ_GI_HIT_WRAP_SAMPLER UZ_GDF_WRAP_SAMPLER
#elif defined(UZ_SDF_REFLECTION_WRAP_SAMPLER)
#define UZ_GI_HIT_WRAP_SAMPLER UZ_SDF_REFLECTION_WRAP_SAMPLER
#else
SamplerState giHitWrapSampler : register(s3);   // linear WRAP: the shared static sampler table's s3
#define UZ_GI_HIT_WRAP_SAMPLER giHitWrapSampler
#define UZ_GDF_WRAP_SAMPLER giHitWrapSampler
#define UZ_SDF_REFLECTION_WRAP_SAMPLER giHitWrapSampler
#endif
#endif // UZ_GI_HIT_UV_WRAP

/// Mirrors GIMeshMaterialGPU (path_tracer_pass.h, 64 bytes). Phase D stage 2
/// appended the mesh's emission (`emissiveSrv`, `emissive`) for the surface
/// radiance cache's cell lighting (`GITrace_HitEmissive`); the path tracer's
/// hit shading (`GITrace_HitSurface`, `GITrace_HitAlbedo`) never reads it.
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
    uint   emissiveSrv;         ///< Emissive texture SRV (sentinel = colour only).
    float3 emissive;            ///< Emissive colour x intensity (linear radiance); zero = no emission.
    uint   padding;
};

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
#ifdef UZ_GI_HIT_UV_WRAP
        // r.GI.HitMaterial.WrapUv: the WRAP sampler reads the tiled content at a UV outside [0, 1].
        albedo *= albedoTex.SampleLevel(UZ_GI_HIT_WRAP_SAMPLER, safeUv, 0.0).rgb;
#else
        albedo *= albedoTex.SampleLevel(linearClamp, safeUv, 0.0).rgb;
#endif
    }
    // Firewall: a bad read may darken this hit but must never NaN-poison the path.
    if (!all(isfinite(albedo)))     albedo = max(mat.tint, 0.0);
    if (!all(isfinite(geomNormal))) { geomNormal = 0.0; return false; }
    return true;
}

// ─── Phase D stage 2: the emission leaving a hit (surface radiance cache) ───

/// The interpolated triangle UV of a hit from the raw geometry views (the UV
/// part of GITrace_HitSurface, re-read only for textured emitters).
float2 GITrace_HitUV(GIMeshMaterial mat, uint primIndex, float2 bary) {
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
        uv += weights[corner] * asfloat(vertices.Load2(vertexIndex * mat.vertexStride + mat.uvOffset));
    }
    return uv;
}

/// Radiance the hit emits (the GBuffer's emissive term for the same mesh): the
/// table's emissive colour x intensity, times its emissive texture at the
/// triangle UV when one is bound (the albedo path's sampler and raw UV). Zero
/// without a table, without an entry, or for a non-emissive mesh; a bad read
/// falls back to the colour (never NaN).
float3 GITrace_HitEmissive(uint meshId, uint primIndex, float2 bary, uint materialTableSrv) {
    if (materialTableSrv == kGITraceSentinelSrv) return 0.0;
    StructuredBuffer<GIMeshMaterial> table = ResourceDescriptorHeap[materialTableSrv];
    GIMeshMaterial mat = table[meshId];
    float3 emissive = max(mat.emissive, 0.0);
    if (!any(emissive > 0.0)) return 0.0;
    if (mat.emissiveSrv != kGITraceSentinelSrv && mat.vbSrv != kGITraceSentinelSrv
        && mat.ibSrv != kGITraceSentinelSrv) {
        float2 uv = GITrace_HitUV(mat, primIndex, bary);
        float2 safeUv = (isfinite(uv.x) && isfinite(uv.y)) ? uv : float2(0.0, 0.0);
        Texture2D<float4> emissiveTex = ResourceDescriptorHeap[NonUniformResourceIndex(mat.emissiveSrv)];
#ifdef UZ_GI_HIT_UV_WRAP
        // r.GI.HitMaterial.WrapUv: the albedo path's WRAP sampler at the same raw UV.
        emissive *= emissiveTex.SampleLevel(UZ_GI_HIT_WRAP_SAMPLER, safeUv, 0.0).rgb;
#else
        emissive *= emissiveTex.SampleLevel(linearClamp, safeUv, 0.0).rgb;
#endif
    }
    if (!all(isfinite(emissive))) emissive = max(mat.emissive, 0.0);
    return emissive;
}

#endif // UZ_GI_HIT_MATERIAL_HLSLI
