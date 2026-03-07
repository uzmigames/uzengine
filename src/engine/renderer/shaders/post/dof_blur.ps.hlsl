// dof_blur.ps.hlsl -- Depth of Field separable Gaussian blur
//
// Blurs the scene color weighted by CoC. Two-pass separable (H then V).
// Reads CoC from a separate texture to weight blur radius per pixel.

cbuffer DofBlurParams : register(b0) {
    float2 texelSize;     // 1/width, 1/height
    float2 blurDirection; // (1,0) for horizontal, (0,1) for vertical
    float  maxBlurRadius; // maximum blur in texels (default 8)
    float3 _blurPad;
};

Texture2D    colorTex   : register(t0);  // HDR scene (or previous blur pass)
Texture2D    cocTex     : register(t1);  // CoC texture (R16_FLOAT, signed pixels)
SamplerState linearClamp : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float coc = cocTex.Sample(linearClamp, input.texcoord).r;
    float absCoc = min(abs(coc), maxBlurRadius);

    if (absCoc < 0.5) {
        return colorTex.Sample(linearClamp, input.texcoord);
    }

    // Variable-width Gaussian blur (7 samples each direction)
    float3 result = float3(0, 0, 0);
    float totalWeight = 0.0;

    int sampleCount = clamp(int(absCoc), 1, 7);
    float stepSize = absCoc / float(sampleCount);

    for (int i = -sampleCount; i <= sampleCount; ++i) {
        float offset = float(i) * stepSize;
        float2 sampleUV = input.texcoord + blurDirection * texelSize * offset;

        float sampleCoc = cocTex.Sample(linearClamp, sampleUV).r;
        float sampleAbsCoc = min(abs(sampleCoc), maxBlurRadius);

        // Weight: Gaussian * CoC contribution (prevent sharp pixels from bleeding)
        float sigma = max(absCoc * 0.5, 1.0);
        float weight = exp(-0.5 * (offset * offset) / (sigma * sigma));
        // Only blur pixels that also have blur (prevent sharp bleeding into blurred)
        weight *= saturate(sampleAbsCoc / max(absCoc, 0.1));

        result += colorTex.Sample(linearClamp, sampleUV).rgb * weight;
        totalWeight += weight;
    }

    return float4(result / max(totalWeight, 0.001), 1.0);
}
