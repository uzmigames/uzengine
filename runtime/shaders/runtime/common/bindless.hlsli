/// UzEngine bindless resource access utilities.
///
/// Provides helper macros and functions for accessing textures through the
/// SM6.6 ResourceDescriptorHeap, with sentinel-guarded sampling that returns
/// white (1,1,1,1) when no texture is bound (kSentinelSrvIndex).
///
/// When UZ_BINDLESS is defined (SM6.6+ path), all texture access goes
/// through ResourceDescriptorHeap[index]. When undefined (SM5 fallback),
/// shaders must declare explicit t-register bindings and cannot use these
/// helpers — the macros expand to comments as reminders.

#ifndef UZ_COMMON_BINDLESS_HLSLI
#define UZ_COMMON_BINDLESS_HLSLI

#include "constants.hlsli"

// ─── Bindless resource access macros ──────────────────────────────────────

#ifdef UZ_BINDLESS

// SM6.6+ bindless via ResourceDescriptorHeap
#define UZ_TEXTURE2D(type, idx)       ResourceDescriptorHeap[idx]
#define UZ_TEXTURECUBE(type, idx)     ResourceDescriptorHeap[idx]
#define UZ_TEXTURE3D(type, idx)       ResourceDescriptorHeap[idx]
#define UZ_TEXTURE2DARRAY(type, idx)  ResourceDescriptorHeap[idx]
#define UZ_RWTEXTURE2D(type, idx)     ResourceDescriptorHeap[idx]
#define UZ_RWTEXTURE3D(type, idx)     ResourceDescriptorHeap[idx]
#define UZ_BUFFER(type, idx)          ResourceDescriptorHeap[idx]
#define UZ_RWBUFFER(type, idx)        ResourceDescriptorHeap[idx]

// ─── Sentinel-guarded texture sampling functions ──────────────────────────
//
// All functions return float4(1,1,1,1) when srvIndex == kSentinelSrvIndex,
// which means "no texture bound". This matches the engine convention where
// fallback textures are white (so material parameters control the value
// directly) except for normal maps (flat blue) and displacement (black).

/// Sample a Texture2D with automatic LOD selection.
/// Returns white if srvIndex is the sentinel (no texture bound).
float4 UzSampleTexture2D(uint srvIndex, SamplerState samp, float2 uv)
{
    if (srvIndex == kSentinelSrvIndex)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    Texture2D<float4> tex = ResourceDescriptorHeap[srvIndex];
    return tex.Sample(samp, uv);
}

/// Sample a Texture2D at an explicit mip level.
/// Returns white if srvIndex is the sentinel.
float4 UzSampleTexture2DLevel(uint srvIndex, SamplerState samp, float2 uv, float mipLevel)
{
    if (srvIndex == kSentinelSrvIndex)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    Texture2D<float4> tex = ResourceDescriptorHeap[srvIndex];
    return tex.SampleLevel(samp, uv, mipLevel);
}

/// Sample a TextureCube with automatic LOD selection.
/// Returns white if srvIndex is the sentinel.
float4 UzSampleTextureCube(uint srvIndex, SamplerState samp, float3 dir)
{
    if (srvIndex == kSentinelSrvIndex)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    TextureCube<float4> tex = ResourceDescriptorHeap[srvIndex];
    return tex.Sample(samp, dir);
}

/// Sample a TextureCube at an explicit mip level.
/// Returns white if srvIndex is the sentinel.
float4 UzSampleTextureCubeLevel(uint srvIndex, SamplerState samp, float3 dir, float mipLevel)
{
    if (srvIndex == kSentinelSrvIndex)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    TextureCube<float4> tex = ResourceDescriptorHeap[srvIndex];
    return tex.SampleLevel(samp, dir, mipLevel);
}

/// Load a single texel from a Texture2D (no filtering, integer coordinates).
/// Returns white if srvIndex is the sentinel.
float4 UzLoadTexture2D(uint srvIndex, int2 coord)
{
    if (srvIndex == kSentinelSrvIndex)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    Texture2D<float4> tex = ResourceDescriptorHeap[srvIndex];
    return tex.Load(int3(coord, 0));
}

/// Sample a Texture3D at an explicit mip level.
/// Returns white if srvIndex is the sentinel.
float4 UzSampleTexture3DLevel(uint srvIndex, SamplerState samp, float3 uvw, float mipLevel)
{
    if (srvIndex == kSentinelSrvIndex)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    Texture3D<float4> tex = ResourceDescriptorHeap[srvIndex];
    return tex.SampleLevel(samp, uvw, mipLevel);
}

/// Sample a Texture2DArray at an explicit mip level.
/// Returns white if srvIndex is the sentinel.
float4 UzSampleTexture2DArrayLevel(uint srvIndex, SamplerState samp, float3 uvSlice, float mipLevel)
{
    if (srvIndex == kSentinelSrvIndex)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    Texture2DArray<float4> tex = ResourceDescriptorHeap[srvIndex];
    return tex.SampleLevel(samp, uvSlice, mipLevel);
}

#else // !UZ_BINDLESS

// ─── SM5 fallback ─────────────────────────────────────────────────────────
//
// Without SM6.6 bindless, shaders must declare explicit texture bindings
// (e.g., Texture2D myTex : register(t0)). The macros below expand to
// comments as reminders; the sampling helper functions are NOT available
// in this path — shaders must sample their explicitly-bound textures
// directly.
//
// Example SM5 usage:
//   Texture2D albedoTex : register(t0);
//   SamplerState linearSampler : register(s0);
//   float4 color = albedoTex.Sample(linearSampler, uv);

#define UZ_TEXTURE2D(type, idx)       /* SM5: use explicit t-register binding */
#define UZ_TEXTURECUBE(type, idx)     /* SM5: use explicit t-register binding */
#define UZ_TEXTURE3D(type, idx)       /* SM5: use explicit t-register binding */
#define UZ_TEXTURE2DARRAY(type, idx)  /* SM5: use explicit t-register binding */
#define UZ_RWTEXTURE2D(type, idx)     /* SM5: use explicit u-register binding */
#define UZ_RWTEXTURE3D(type, idx)     /* SM5: use explicit u-register binding */
#define UZ_BUFFER(type, idx)          /* SM5: use explicit t-register binding */
#define UZ_RWBUFFER(type, idx)        /* SM5: use explicit u-register binding */

#endif // UZ_BINDLESS

#endif // UZ_COMMON_BINDLESS_HLSLI
