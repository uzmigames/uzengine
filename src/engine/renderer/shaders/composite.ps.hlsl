// composite.ps.hlsl -- Final post-process composite
//
// Combines: HDR scene * SSAO + Bloom -> Auto Exposure -> ACES Tonemap -> Gamma
//
// References:
//   [Narkowicz 2015] "ACES Filmic Tone Mapping Curve"
//   [TheRealMJP] "BakingLab/ACES.hlsl"
//   UE5 — Engine/Shaders/Private/PostProcessCombineLUTs.usf

cbuffer CompositeParams : register(b0) {
    float  exposure;        // EV100 exposure multiplier (default 1.0)
    float  bloomStrength;   // bloom mix factor (default 0.04)
    float  gamma;           // gamma value (default 2.2)
    float  _compPad;
};

Texture2D    hdrTex       : register(t0);   // HDR scene (linear, pre-tonemap)
Texture2D    ssaoTex      : register(t1);   // SSAO (R8, blurred)
Texture2D    bloomTex     : register(t2);   // Final bloom mip (upsampled)
SamplerState linearClamp  : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float3 hdr  = hdrTex.Sample(linearClamp, input.texcoord).rgb;
    float  ao   = ssaoTex.Sample(linearClamp, input.texcoord).r;
    float3 bloom = bloomTex.Sample(linearClamp, input.texcoord).rgb;

    // Apply SSAO to scene
    float3 color = hdr * ao;

    // Add bloom
    color = lerp(color, color + bloom, bloomStrength);

    // Apply exposure
    color *= exposure;

    // ACES Tonemapper (BakingLab reference -- TheRealMJP, MIT License)
    {
        static const float3x3 ACESInputMat = {
            {0.59719, 0.35458, 0.04823},
            {0.07600, 0.90834, 0.01566},
            {0.02840, 0.13383, 0.83777}
        };
        static const float3x3 ACESOutputMat = {
            { 1.60475, -0.53108, -0.07367},
            {-0.10208,  1.10813, -0.00605},
            {-0.00327, -0.07276,  1.07602}
        };

        color = mul(ACESInputMat, color);
        color = (color * (color + 0.0245786) - 0.000090537)
              / (color * (0.983729 * color + 0.432951) + 0.238081);
        color = mul(ACESOutputMat, color);
        color = saturate(color);
    }

    // Gamma correction (linear -> sRGB)
    color = pow(max(color, 0.0), 1.0 / gamma);

    return float4(color, 1.0);
}
