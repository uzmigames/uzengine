/// Editor overlay text pixel shader — samples font atlas R8 for alpha.
///
/// The font atlas is an R8 texture where the red channel is glyph coverage.
/// Output color uses the vertex color RGB with atlas-sampled alpha.

cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint fontAtlasIndex;
    uint samplerIndex;
    uint reserved;
};

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

float4 main(PSInput input) : SV_Target {
    Texture2D fontAtlas = ResourceDescriptorHeap[fontAtlasIndex];
    SamplerState samp = SamplerDescriptorHeap[samplerIndex];

    float alpha = fontAtlas.Sample(samp, input.texcoord).r;
    return float4(input.color.rgb, input.color.a * alpha);
}
