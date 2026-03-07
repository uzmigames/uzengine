// bloom_up.ps.hlsl -- Bloom upsample pass
//
// 9-tap tent filter upsample [Jimenez 2014].
// Reads the lower-res bloom mip and blends additively into the current level.

cbuffer BloomUpParams : register(b0) {
    float2 srcTexelSize;   // 1.0 / source (lower-res) mip resolution
    float  bloomIntensity; // blend weight for this mip (default ~0.7)
    float  _upPad;
};

Texture2D    srcTex       : register(t0);   // Lower-res bloom mip
SamplerState linearClamp  : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float2 uv = input.texcoord;
    float2 ts = srcTexelSize;

    // 9-tap tent filter (3x3 bilinear taps)
    float3 color = float3(0, 0, 0);
    color += srcTex.SampleLevel(linearClamp, uv + float2(-1, -1) * ts, 0).rgb * 1.0;
    color += srcTex.SampleLevel(linearClamp, uv + float2( 0, -1) * ts, 0).rgb * 2.0;
    color += srcTex.SampleLevel(linearClamp, uv + float2( 1, -1) * ts, 0).rgb * 1.0;
    color += srcTex.SampleLevel(linearClamp, uv + float2(-1,  0) * ts, 0).rgb * 2.0;
    color += srcTex.SampleLevel(linearClamp, uv,                        0).rgb * 4.0;
    color += srcTex.SampleLevel(linearClamp, uv + float2( 1,  0) * ts, 0).rgb * 2.0;
    color += srcTex.SampleLevel(linearClamp, uv + float2(-1,  1) * ts, 0).rgb * 1.0;
    color += srcTex.SampleLevel(linearClamp, uv + float2( 0,  1) * ts, 0).rgb * 2.0;
    color += srcTex.SampleLevel(linearClamp, uv + float2( 1,  1) * ts, 0).rgb * 1.0;
    color /= 16.0;

    return float4(color * bloomIntensity, 1.0);
}
