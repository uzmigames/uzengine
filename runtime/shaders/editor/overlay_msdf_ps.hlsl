/// Editor overlay distance-field text pixel shader (MsdfFont / AddMsdfText).
///
/// The atlas is a single-channel R8 signed distance field baked at runtime by
/// stb_truetype's `stbtt_GetGlyphSDF` (msdf_font.cpp): the glyph outline sits
/// at `onedge_value` 128 and values increase toward the inside. The coverage
/// is reconstructed with a screen-space anti-aliasing band whose width is the
/// distance gradient per pixel, so the edge stays one pixel soft at every
/// render size between the 10 px captions and the 48 px headers.
///
/// A true 3-channel MSDF atlas would replace the single sample below with
/// the median of the three channels; callers and bindings stay the same.
///
/// @ref Green 2007 "Improved Alpha-Tested Magnification for Vector Textures
///      and Special Effects" — distance-field glyphs, smoothstep reconstruction
/// @ref Chlumsky 2015 "Shape Decomposition for Multi-channel Distance Fields"
///      — the 3-channel median upgrade path
/// @constants kSdfEdge = 128/255: `kOnEdgeValue` of msdf_font.cpp normalised
///            by the R8 UNORM read.
///
/// SM6.6 (UZ_BINDLESS): atlas via ResourceDescriptorHeap[fontAtlasIndex],
/// sampler via SamplerDescriptorHeap[samplerIndex].
/// SM5 (!UZ_BINDLESS): atlas at t0, sampler at s0.

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

static const float kSdfEdge = 128.0 / 255.0;

/// Coverage of a distance sample `d` (UNORM, edge at kSdfEdge, inside above).
float SdfCoverage(float d) {
    const float w = max(fwidth(d), 1e-5);
    return smoothstep(kSdfEdge - w, kSdfEdge + w, d);
}

#ifdef UZ_BINDLESS
cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint fontAtlasIndex;  ///< Bindless SRV of the R8 SDF atlas.
    uint samplerIndex;
    uint vertexOffset;    ///< Matches overlay_vs.hlsl (unused in PS).
};

float4 main(PSInput input) : SV_Target {
    Texture2D<float> atlas = ResourceDescriptorHeap[fontAtlasIndex];
    SamplerState samp = SamplerDescriptorHeap[samplerIndex];
    const float coverage = SdfCoverage(atlas.Sample(samp, input.texcoord));
    return float4(input.color.rgb, input.color.a * coverage);
}
#else
Texture2D<float> atlas : register(t0);
SamplerState     samp  : register(s0);

float4 main(PSInput input) : SV_Target {
    const float coverage = SdfCoverage(atlas.Sample(samp, input.texcoord));
    return float4(input.color.rgb, input.color.a * coverage);
}
#endif
