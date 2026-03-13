/// Editor overlay text pixel shader — samples font atlas R8 for alpha.
///
/// SM6.6 (UZ_BINDLESS): samples font atlas via ResourceDescriptorHeap[fontAtlasIndex]
/// and sampler via SamplerDescriptorHeap[samplerIndex].
///
/// SM5 (!UZ_BINDLESS): samples font atlas via register-bound t0 / s0.
///
/// The font atlas is an R8 texture where the red channel is glyph coverage.
/// Output color uses the vertex color RGB with atlas-sampled alpha.

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

#ifdef UZ_BINDLESS
cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint fontAtlasIndex;
    uint samplerIndex;
    uint reserved;
};

float4 main(PSInput input) : SV_Target {
    Texture2D fontAtlas = ResourceDescriptorHeap[fontAtlasIndex];
    SamplerState samp = SamplerDescriptorHeap[samplerIndex];

    float alpha = fontAtlas.Sample(samp, input.texcoord).r;
    return float4(input.color.rgb, input.color.a * alpha);
}
#else
Texture2D    fontAtlas : register(t0);
SamplerState samp     : register(s0);

float4 main(PSInput input) : SV_Target {
    float alpha = fontAtlas.Sample(samp, input.texcoord).r;
    return float4(input.color.rgb, input.color.a * alpha);
}
#endif
