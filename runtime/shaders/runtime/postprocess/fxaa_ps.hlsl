/// UzEngine FXAA — passthrough for now.
/// @ref Lottes 2009 "FXAA 3.11"

#include "../common/constants.hlsli"

#ifdef UZ_BINDLESS
cbuffer RootConstants : register(b0) { uint ldrSrvIndex; };
#endif

SamplerState samClamp : register(s2);

#ifndef UZ_BINDLESS
Texture2D t_ldr : register(t0);
#endif

struct PSIn { float4 position : SV_Position; float2 texCoord : TEXCOORD0; };

float4 main(PSIn i) : SV_Target
{
#ifdef UZ_BINDLESS
    Texture2D<float4> ldr = ResourceDescriptorHeap[ldrSrvIndex];
    return ldr.SampleLevel(samClamp, i.texCoord, 0);
#else
    return t_ldr.SampleLevel(samClamp, i.texCoord, 0);
#endif
}
