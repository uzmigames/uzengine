// hiz_generate.ps.hlsl -- Hierarchical-Z min reduction for SSR
//
// Generates a mip chain where each texel stores the MINIMUM depth of the
// 2x2 parent texels. Used for Hi-Z tracing acceleration in SSR.
//
// References:
//   [McGuire 2014] "Efficient GPU Screen-Space Ray Tracing"
//   UE5 — HZB generation pass

cbuffer HiZParams : register(b0) {
    float2 srcTexelSize;  // 1/srcWidth, 1/srcHeight
    float2 _pad;
};

Texture2D<float> srcDepth : register(t0);
SamplerState     pointClamp : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float PSMain(PSInput input) : SV_TARGET {
    // Gather 2x2 texels from source mip and take min (closest depth)
    float2 uv = input.texcoord;

    float d00 = srcDepth.SampleLevel(pointClamp, uv + float2(-0.25, -0.25) * srcTexelSize, 0).r;
    float d10 = srcDepth.SampleLevel(pointClamp, uv + float2( 0.25, -0.25) * srcTexelSize, 0).r;
    float d01 = srcDepth.SampleLevel(pointClamp, uv + float2(-0.25,  0.25) * srcTexelSize, 0).r;
    float d11 = srcDepth.SampleLevel(pointClamp, uv + float2( 0.25,  0.25) * srcTexelSize, 0).r;

    return min(min(d00, d10), min(d01, d11));
}
