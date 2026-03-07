// bloom_down.ps.hlsl -- Bloom downsample pass
//
// 13-tap downsample filter [Jimenez 2014, "Next Generation Post Processing in CoD:AW"]
// First mip uses Karis average to prevent fireflies from bright specular highlights.
//
// Each pass reads the previous mip and writes to the next (half resolution).

cbuffer BloomDownParams : register(b0) {
    float2 srcTexelSize;   // 1.0 / source resolution
    float  threshold;      // brightness threshold for bloom (default 1.0)
    float  isFirstMip;     // 1.0 for first downsample (applies threshold + Karis), 0.0 otherwise
};

Texture2D    srcTex       : register(t0);
SamplerState linearClamp  : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float Luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Karis average: weight by 1/(1+luma) to prevent fireflies
float3 KarisAverage(float3 a, float3 b, float3 c, float3 d) {
    float wa = 1.0 / (1.0 + Luminance(a));
    float wb = 1.0 / (1.0 + Luminance(b));
    float wc = 1.0 / (1.0 + Luminance(c));
    float wd = 1.0 / (1.0 + Luminance(d));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

float4 PSMain(PSInput input) : SV_TARGET {
    float2 uv = input.texcoord;
    float2 ts = srcTexelSize;

    // 13-tap pattern: 5 bilinear taps in a cross + corners pattern
    // Weights: center=0.125, cross=0.125*2, corners=0.03125*4
    float3 a = srcTex.SampleLevel(linearClamp, uv + float2(-2, -2) * ts, 0).rgb;
    float3 b = srcTex.SampleLevel(linearClamp, uv + float2( 0, -2) * ts, 0).rgb;
    float3 c = srcTex.SampleLevel(linearClamp, uv + float2( 2, -2) * ts, 0).rgb;

    float3 d = srcTex.SampleLevel(linearClamp, uv + float2(-2,  0) * ts, 0).rgb;
    float3 e = srcTex.SampleLevel(linearClamp, uv,                        0).rgb;
    float3 f = srcTex.SampleLevel(linearClamp, uv + float2( 2,  0) * ts, 0).rgb;

    float3 g = srcTex.SampleLevel(linearClamp, uv + float2(-2,  2) * ts, 0).rgb;
    float3 h = srcTex.SampleLevel(linearClamp, uv + float2( 0,  2) * ts, 0).rgb;
    float3 i = srcTex.SampleLevel(linearClamp, uv + float2( 2,  2) * ts, 0).rgb;

    float3 j = srcTex.SampleLevel(linearClamp, uv + float2(-1, -1) * ts, 0).rgb;
    float3 k = srcTex.SampleLevel(linearClamp, uv + float2( 1, -1) * ts, 0).rgb;
    float3 l = srcTex.SampleLevel(linearClamp, uv + float2(-1,  1) * ts, 0).rgb;
    float3 m = srcTex.SampleLevel(linearClamp, uv + float2( 1,  1) * ts, 0).rgb;

    float3 color;
    if (isFirstMip > 0.5) {
        // First mip: Karis average to avoid fireflies
        float3 g0 = KarisAverage(a, b, d, e);
        float3 g1 = KarisAverage(b, c, e, f);
        float3 g2 = KarisAverage(d, e, g, h);
        float3 g3 = KarisAverage(e, f, h, i);
        float3 g4 = KarisAverage(j, k, l, m);
        color = g4 * 0.5 + (g0 + g1 + g2 + g3) * 0.125;

        // Soft threshold: only bloom above threshold, with smooth knee
        float luma = Luminance(color);
        float soft = max(0.0, luma - threshold + 0.5);
        soft = clamp(soft * soft / (2.0 * 0.5 + 0.001), 0.0, 1.0);
        float contribution = max(soft, luma - threshold) / max(luma, 0.001);
        color *= max(contribution, 0.0);
    } else {
        // Subsequent mips: standard 13-tap weighted average
        color  = e * 0.125;
        color += (b + d + f + h) * 0.0625;
        color += (a + c + g + i) * 0.03125;
        color += (j + k + l + m) * 0.125;
    }

    return float4(max(color, 0.0), 1.0);
}
