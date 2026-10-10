/// Editor overlay sprite pixel shader — samples the RGBA8 UI icon atlas.
///
/// Draws the sprite batch of TextRenderer (AddSprite): each quad maps one
/// cell of the procedural icon atlas (VfxGenerateIconAtlas, 256x256 RGBA8)
/// and multiplies it by the vertex tint, so one white glyph serves every
/// accent colour. Straight (non-premultiplied) alpha — the pipeline blends
/// SrcAlpha / InvSrcAlpha like the rect and text batches.
///
/// SM6.6 (UZ_BINDLESS): atlas via ResourceDescriptorHeap[atlasIndex], sampler
/// via SamplerDescriptorHeap[samplerIndex] (root constants shared with
/// overlay_vs.hlsl: vertex buffer, atlas, sampler, vertex offset).
///
/// SM5 (!UZ_BINDLESS): atlas at t0 (TextRenderer binds it with
/// SetFragmentTexture(0, ...)), sampler at s0.

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

#ifdef UZ_BINDLESS
cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint atlasIndex;      ///< Bindless SRV of the RGBA8 icon atlas.
    uint samplerIndex;
    uint vertexOffset;    ///< Matches overlay_vs.hlsl (unused in PS).
};

float4 main(PSInput input) : SV_Target {
    Texture2D<float4> atlas = ResourceDescriptorHeap[atlasIndex];
    SamplerState samp = SamplerDescriptorHeap[samplerIndex];
    return atlas.Sample(samp, input.texcoord) * input.color;
}
#else
Texture2D<float4> atlas : register(t0);
SamplerState      samp  : register(s0);

float4 main(PSInput input) : SV_Target {
    return atlas.Sample(samp, input.texcoord) * input.color;
}
#endif
