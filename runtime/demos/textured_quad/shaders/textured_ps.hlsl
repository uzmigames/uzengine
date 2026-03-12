/// Textured quad pixel shader.
///
/// SM6.6 (UZ_BINDLESS): samples texture via ResourceDescriptorHeap[textureIndex]
/// and sampler via SamplerDescriptorHeap[samplerIndex].
///
/// SM5 (!UZ_BINDLESS): samples texture via register-bound t0 / s0.
///
/// Multiplies sampled color by vertex color.

struct PSInput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

#ifdef UZ_BINDLESS
cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint textureIndex;
    uint samplerIndex;
};

float4 main(PSInput input) : SV_Target {
    Texture2D    tex = ResourceDescriptorHeap[textureIndex];
    SamplerState sam = SamplerDescriptorHeap[samplerIndex];

    float4 texColor = tex.Sample(sam, input.texcoord);
    return texColor * input.color;
}
#else
Texture2D    tex : register(t0);
SamplerState sam : register(s0);

float4 main(PSInput input) : SV_Target {
    float4 texColor = tex.Sample(sam, input.texcoord);
    return texColor * input.color;
}
#endif
