// sss_blur.ps.hlsl -- Separable Subsurface Scattering blur
//
// Screen-space diffusion blur for SSS materials.
// Based on [Jimenez 2015] "Separable Subsurface Scattering"
//
// Reads the diffuse-only lighting buffer and applies a Gaussian-like kernel
// weighted by depth similarity (bilateral) to approximate light diffusion
// through skin/wax/marble.

cbuffer SSSParams : register(b0) {
    float2 texelSize;      // 1/width, 1/height
    float2 blurDirection;  // (1,0) for horizontal, (0,1) for vertical
    float3 scatterColor;   // subsurface scatter color (skin: 0.8, 0.3, 0.15)
    float  scatterRadius;  // world-space scatter radius
    float  sssStrength;    // overall SSS strength multiplier
    float3 _sssPad;
};

Texture2D    colorTex   : register(t0);  // Diffuse lighting (or previous SSS pass)
Texture2D    depthTex   : register(t1);  // Linear depth
SamplerState linearClamp : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// Burley normalized profile kernel (5 samples per side)
static const float offsets[5] = { 0.0, 1.0, 2.0, 3.0, 4.0 };
static const float weights[5] = { 0.382, 0.242, 0.061, 0.006, 0.0002 };

float4 PSMain(PSInput input) : SV_TARGET {
    float3 centerColor = colorTex.Sample(linearClamp, input.texcoord).rgb;
    float  centerDepth = depthTex.Load(int3(input.position.xy, 0)).r;

    if (sssStrength < 0.001 || scatterRadius < 0.001) {
        return float4(centerColor, 1.0);
    }

    // Scale blur radius by depth (closer objects get more blur)
    float blurScale = scatterRadius / max(centerDepth, 0.1);
    float2 step = blurDirection * texelSize * blurScale * sssStrength;

    float3 result = centerColor * weights[0];
    float totalWeight = weights[0];

    [unroll]
    for (int i = 1; i < 5; ++i) {
        float2 offset = step * offsets[i];

        // Positive direction
        float2 uvP = input.texcoord + offset;
        float3 colP = colorTex.Sample(linearClamp, uvP).rgb;
        float  depP = depthTex.Sample(linearClamp, uvP).r;
        float  wP = weights[i] * saturate(1.0 - abs(depP - centerDepth) * 100.0);
        result += colP * scatterColor * wP;
        totalWeight += wP;

        // Negative direction
        float2 uvN = input.texcoord - offset;
        float3 colN = colorTex.Sample(linearClamp, uvN).rgb;
        float  depN = depthTex.Sample(linearClamp, uvN).r;
        float  wN = weights[i] * saturate(1.0 - abs(depN - centerDepth) * 100.0);
        result += colN * scatterColor * wN;
        totalWeight += wN;
    }

    return float4(result / max(totalWeight, 0.001), 1.0);
}
