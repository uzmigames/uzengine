// composite.ps.hlsl -- Final post-process composite
//
// Combines: HDR scene * SSAO + Bloom -> Exposure -> CAS Sharpen ->
//           ACES Tonemap -> Gamma
//
// References:
//   [Narkowicz 2015] "ACES Filmic Tone Mapping Curve"
//   [TheRealMJP] "BakingLab/ACES.hlsl"
//   [AMD 2019] "FidelityFX Contrast Adaptive Sharpening"
//   UE5 — Engine/Shaders/Private/PostProcessCombineLUTs.usf

cbuffer CompositeParams : register(b0) {
    float  exposure;        // EV100 exposure multiplier (default 1.0)
    float  bloomStrength;   // bloom mix factor (default 0.04)
    float  gamma;           // gamma value (default 2.2)
    float  sharpenStrength; // CAS sharpening (0 = off, 0.4 = subtle, 0.8 = strong)
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
    int2 pc = int2(input.position.xy);

    float3 hdr  = hdrTex.Load(int3(pc, 0)).rgb;
    float  ao   = ssaoTex.Sample(linearClamp, input.texcoord).r;
    float3 bloom = bloomTex.Sample(linearClamp, input.texcoord).rgb;

    // Apply SSAO + bloom + exposure
    float3 color = hdr * ao;
    color = lerp(color, color + bloom, bloomStrength);
    color *= exposure;

    // -----------------------------------------------------------------------
    // Contrast Adaptive Sharpening (simplified AMD FidelityFX CAS)
    // Applied in post-exposure HDR space before tonemap.
    // Counteracts TAA softening without ringing on high-contrast edges.
    // -----------------------------------------------------------------------
    if (sharpenStrength > 0.001) {
        // 5-tap cross neighbors (post-exposure HDR)
        float3 cN = hdrTex.Load(int3(pc + int2( 0, -1), 0)).rgb * ao * exposure;
        float3 cS = hdrTex.Load(int3(pc + int2( 0,  1), 0)).rgb * ao * exposure;
        float3 cW = hdrTex.Load(int3(pc + int2(-1,  0), 0)).rgb * ao * exposure;
        float3 cE = hdrTex.Load(int3(pc + int2( 1,  0), 0)).rgb * ao * exposure;

        // Per-channel min/max of the 5-tap cross
        float3 mnRGB = min(color, min(min(cN, cS), min(cW, cE)));
        float3 mxRGB = max(color, max(max(cN, cS), max(cW, cE)));

        // Adaptive weight: sqrt(min/max) — flat areas get more sharpening,
        // high-contrast edges get less (prevents ringing)
        float3 ampRGB = saturate(sqrt(mnRGB * rcp(mxRGB + 1e-5)));

        // Peak sharpening weight: -1/lerp(8,5,s) from AMD CAS
        float3 peak = -rcp(lerp(8.0, 5.0, saturate(sharpenStrength)));
        float3 wN = ampRGB * peak;
        float3 wD = 1.0 + 4.0 * wN;

        color = max((color + (cN + cS + cW + cE) * wN) / wD, 0.0);
    }

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
