/// Equirectangular-to-Cubemap Conversion Compute Shader.
///
/// Converts an HDR equirectangular panorama (2:1 aspect ratio) into a
/// cubemap stored as an RWTexture2DArray with 6 slices. Each thread
/// processes one texel of one cubemap face, mapping the face direction
/// to spherical coordinates and sampling the equirect texture.
///
/// # Resources
/// - b0: Root constants (4 DWORDs) -- cubemapSize, equirectSrvIndex, pad, pad
/// - u0: RWTexture2DArray<float4> output cubemap (RGBA16F, NxNx6)
/// - s0: Bilinear wrap sampler (static sampler)
///
/// # Dispatch
/// [cubemapSize/8, cubemapSize/8, 6] thread groups of [8, 8, 1]
///
/// # Coordinate Convention
/// CubeFaceDirection() returns Y-up directions (DX cubemap convention).
/// The equirect UV mapping uses the standard spherical-to-UV transform:
///   u = atan2(dir.z, dir.x) / (2*PI) + 0.5
///   v = asin(dir.y) / PI + 0.5

#include "../include/ibl.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 (4 DWORDs, 16 bytes).
cbuffer RootConstants : register(b0) {
    uint cubemapSize;       // Output cubemap face resolution in texels
    uint equirectSrvIndex;  // SRV descriptor heap index for the HDR equirect texture
    uint2 _pad;             // Padding to 16-byte alignment
};

SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
Texture2D<float4> g_EquirectTex : register(t0);
#endif

RWTexture2DArray<float4> outputCubemap : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Equirectangular UV Mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a normalised 3D direction to equirectangular UV coordinates.
///
/// Uses the standard spherical mapping where:
///   longitude (phi)   = atan2(dir.z, dir.x)  -> u in [0, 1]
///   latitude  (theta) = asin(dir.y)          -> v in [0, 1]
///
/// The direction must be normalised. The Y component is clamped to [-1, 1]
/// to guard against floating-point drift producing NaN from asin.
///
/// # Parameters
/// - dir: Normalised world-space direction (Y-up, DX cubemap convention).
///
/// # Returns
/// UV coordinates in [0, 1]^2 for sampling the equirectangular texture.
float2 DirectionToEquirectUV(float3 dir) {
    // Longitude: atan2(z, x) gives angle in [-PI, PI], remap to [0, 1]
    float u = atan2(dir.z, dir.x) / (2.0f * PI) + 0.5f;

    // Latitude: asin(y) gives angle in [-PI/2, PI/2], remap to [0, 1]
    // Clamp to prevent NaN from floating-point overshoot beyond [-1, 1]
    float v = asin(clamp(dir.y, -1.0f, 1.0f)) / PI + 0.5f;

    return float2(u, v);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint faceIndex = dtid.z;

    // Out-of-bounds guard for non-power-of-two sizes or partial thread groups
    if (dtid.x >= cubemapSize || dtid.y >= cubemapSize || faceIndex >= 6u) return;

    // Compute normalised UV for this texel (centre of texel)
    float2 uv = (float2(dtid.xy) + 0.5f) / float(cubemapSize);

    // Get the world-space direction for this cubemap face texel
    float3 dir = normalize(CubeFaceDirection(faceIndex, uv));

    // Convert direction to equirectangular UV
    float2 equirectUV = DirectionToEquirectUV(dir);

    // Sample the HDR equirectangular panorama via bindless descriptor heap
#ifdef UZ_BINDLESS
    Texture2D<float4> equirectTex = ResourceDescriptorHeap[equirectSrvIndex];
#else
    Texture2D<float4> equirectTex = g_EquirectTex;
#endif
    float4 color = equirectTex.SampleLevel(linearSampler, equirectUV, 0.0f);

    // Write to the output cubemap face slice
    outputCubemap[uint3(dtid.xy, faceIndex)] = color;
}
