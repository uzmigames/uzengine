/// UzEngine equirectangular HDR panorama to cubemap conversion.
/// @ref Standard equirectangular projection (spherical coordinates → UV)
///
/// Root constants at b0: { cubemapSize, equirectSrvIndex, 0, 0 }
/// Output: RWTexture2DArray<float4> cubemap at u0

#include "../common/constants.hlsli"

cbuffer RootConstants : register(b0) {
    uint cubemapSize;
    uint equirectSrvIndex;
    uint _pad0;
    uint _pad1;
};

#ifdef UZ_BINDLESS
#else
Texture2D<float4> equirectMap : register(t0);
#endif

RWTexture2DArray<float4> outputCubemap : register(u0);
SamplerState linearSampler : register(s0);

float3 CubeFaceDirection(uint face, float2 uv) {
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;
    switch (face) {
        case 0: return normalize(float3( 1, -v, -u));
        case 1: return normalize(float3(-1, -v,  u));
        case 2: return normalize(float3( u,  1,  v));
        case 3: return normalize(float3( u, -1, -v));
        case 4: return normalize(float3( u, -v,  1));
        case 5: return normalize(float3(-u, -v, -1));
        default: return float3(0, 0, 1);
    }
}

/// Convert direction to equirectangular UV
float2 DirectionToEquirect(float3 dir) {
    float phi = atan2(dir.z, dir.x);        // [-PI, PI]
    float theta = asin(clamp(dir.y, -1, 1)); // [-PI/2, PI/2]
    float u = phi * M_1_PI * 0.5 + 0.5;     // [0, 1]
    float v = theta * M_1_PI + 0.5;          // [0, 1]
    return float2(u, 1.0 - v); // flip V for top-down
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= cubemapSize || DTid.y >= cubemapSize) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float(cubemapSize);
    float3 dir = CubeFaceDirection(DTid.z, uv);
    float2 equirectUV = DirectionToEquirect(dir);

#ifdef UZ_BINDLESS
    Texture2D<float4> hdrTex = ResourceDescriptorHeap[equirectSrvIndex];
    float3 color = hdrTex.SampleLevel(linearSampler, equirectUV, 0).rgb;
#else
    float3 color = equirectMap.SampleLevel(linearSampler, equirectUV, 0).rgb;
#endif

    outputCubemap[DTid] = float4(color, 1.0);
}
