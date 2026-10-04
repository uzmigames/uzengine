/// UzEngine — Global Distance Field surface sampling (shared).
///
/// The GDF clipmaps store the signed distance to the nearest surface in R
/// (centimetres) and that surface's diffuse reflectance in GBA. Both probe
/// writers need those attributes:
///
///   * `radiance_cache_trace_cs.hlsl` sphere-traces the field itself, so it
///     gets the hit position from the trace.
///   * `ddgi_probe_update_cs.hlsl` traces the TLAS with an inline `RayQuery`,
///     which reports a hit distance but no hit attributes — an SBT/hit-group
///     pipeline with a bindless geometry table would be needed for those.
///     Sampling the GDF at the hit point gives it the same normal and albedo
///     the SDF writer uses, so the two probe writers agree on surface shading
///     and only differ in how visibility is resolved.
///
/// # Requirements
/// The including shader must, BEFORE the include, declare:
///   - `SamplerState linearClamp : register(s2);` (static sampler, always bound)
///   - `kSentinelSrvIndex` (from `../common/constants.hlsli`)
///   - `kSurfaceEpsilon` — central-difference offset for the normal, in cm
///   - the shared `RadianceCacheTraceConstants` cbuffer, whose clipmap fields
///     are named `gdfClipmapSrv0..3`, `gdfCenter{0..3}{X,Y,Z}`, `gdfExtent0..3`
///
/// @ref Hart 1996 "Sphere Tracing: A Geometric Method for the Antialiased Ray
///      Tracing of Implicit Surfaces" — central-difference gradient normal

#ifndef UZ_GDF_SURFACE_HLSLI
#define UZ_GDF_SURFACE_HLSLI

/// Samples one clipmap. Returns `(distance, reflectance.rgb)`; a point outside
/// the clipmap returns a large positive distance so the caller falls through to
/// the next, coarser cascade.
float4 SampleGdfClipmap(uint srvIdx, float3 worldPos, float3 center, float extent) {
    if (srvIdx == kSentinelSrvIndex || extent <= 0.0) {
        return float4(1e9, 0.0, 0.0, 0.0);
    }
    float3 local = (worldPos - center) / extent;       // [-1,1] inside the clipmap
    if (any(abs(local) > 1.0)) {
        return float4(1e9, 0.0, 0.0, 0.0);
    }
    float3 uvw = local * 0.5 + 0.5;                    // [0,1]
    Texture3D<float4> gdfTex = ResourceDescriptorHeap[srvIdx];
    return gdfTex.SampleLevel(linearClamp, uvw, 0);
}

float4 SampleGdfSurface(float3 worldPos) {
    // Try cascades from finest to coarsest. Each cascade is 4x larger so the
    // first one that contains the point gives the most accurate distance.
    float4 d = SampleGdfClipmap(gdfClipmapSrv0,
                               worldPos,
                               float3(gdfCenter0X, gdfCenter0Y, gdfCenter0Z),
                               gdfExtent0);
    if (d.x < 1e8) return d;
    d = SampleGdfClipmap(gdfClipmapSrv1,
                         worldPos,
                         float3(gdfCenter1X, gdfCenter1Y, gdfCenter1Z),
                         gdfExtent1);
    if (d.x < 1e8) return d;
    d = SampleGdfClipmap(gdfClipmapSrv2,
                         worldPos,
                         float3(gdfCenter2X, gdfCenter2Y, gdfCenter2Z),
                         gdfExtent2);
    if (d.x < 1e8) return d;
    return SampleGdfClipmap(gdfClipmapSrv3,
                            worldPos,
                            float3(gdfCenter3X, gdfCenter3Y, gdfCenter3Z),
                            gdfExtent3);
}

float SampleGdfDistance(float3 worldPos) {
    return SampleGdfSurface(worldPos).x;
}

/// @constants Central differences use offsets +/- one hit tolerance.
/// Returns a zero vector where the field has no gradient (outside every cascade).
float3 GdfNormal(float3 position) {
    float3 dx = float3(kSurfaceEpsilon, 0.0, 0.0);
    float3 dy = float3(0.0, kSurfaceEpsilon, 0.0);
    float3 dz = float3(0.0, 0.0, kSurfaceEpsilon);
    float3 gradient = float3(
        SampleGdfDistance(position + dx) - SampleGdfDistance(position - dx),
        SampleGdfDistance(position + dy) - SampleGdfDistance(position - dy),
        SampleGdfDistance(position + dz) - SampleGdfDistance(position - dz));
    float magnitude = length(gradient);
    return magnitude > 0.0 ? gradient / magnitude : float3(0.0, 0.0, 0.0);
}

#endif // UZ_GDF_SURFACE_HLSLI
