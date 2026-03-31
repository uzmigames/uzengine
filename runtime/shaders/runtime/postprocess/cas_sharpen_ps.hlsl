/// UzEngine CAS (Contrast Adaptive Sharpening) — passthrough for now.
/// @ref AMD FidelityFX CAS — ffx_cas.h
/// Root constants b0: { ldrSrvIndex, 0, 0, 0 }

#include "../common/constants.hlsli"

#ifdef UZ_BINDLESS
cbuffer RootConstants : register(b0) { uint ldrSrvIndex; uint _rc1, _rc2, _rc3; };
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
