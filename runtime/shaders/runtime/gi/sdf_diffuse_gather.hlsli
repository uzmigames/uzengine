/// @ref Wright et al. 2022 "Lumen" — surface final gather backed by a world radiance cache.
/// @ref Pharr, Jakob and Humphreys 2023 "Physically Based Rendering", cosine-weighted hemisphere sampling.
#ifndef UZ_SDF_DIFFUSE_GATHER_HLSLI
#define UZ_SDF_DIFFUSE_GATHER_HLSLI

#include "sdf_reflections.hlsli"

/// @constants Cosine PDF=cos(theta)/pi cancels E/pi's cosine and pi, giving mean radiance.
/// Samples are bounded to [1,32]. The 0.999 pole threshold keeps the tangent basis nondegenerate.
/// The shared origin function preserves the raster surface; dynamic expansion handles tracing.
/// Hash multipliers 1664525,1013904223,747796405 and xor 0x9e3779b9 decorrelate pixel/frame samples.
float3 GatherSdfDiffuse(SdfReflectionScene scene, float3 position, float3 normal, float3 geometricNormal,
                        uint2 pixel, uint frame, uint samples) {
    float3 origin;
    if (!ResolveSdfRayOrigin(scene, position, geometricNormal, origin)) return 0.0;
    float3 up = abs(normal.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    uint count = clamp(samples, 1u, 32u);
    uint seed = pixel.x * 1664525u + pixel.y * 1013904223u + frame * 747796405u;
    float2 rotation = float2(RadicalInverseVdC(seed), RadicalInverseVdC(seed ^ 0x9e3779b9u));
    float3 irradianceOverPi = 0.0;
    [loop] for (uint i = 0; i < count; ++i) {
        float pdf;
        float3 sampleDirection = SampleCosineHemisphere(frac(Hammersley(i, count) + rotation), pdf);
        float3 direction = tangent * sampleDirection.x + bitangent * sampleDirection.y + normal * sampleDirection.z;
        irradianceOverPi += TraceSdfSurfaceRadiance(scene, origin, direction, 0u, geometricNormal);
    }
    return irradianceOverPi / float(count);
}

#endif
