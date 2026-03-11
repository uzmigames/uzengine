/// Textured quad pixel shader — SM6.6 bindless texture sampling.
///
/// Samples a texture via ResourceDescriptorHeap[textureIndex] using
/// SamplerDescriptorHeap[samplerIndex], then multiplies by vertex color.

struct PSInput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

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
