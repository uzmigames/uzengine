// composite.ps.hlsl -- Final post-process composite
//
// Combines: HDR scene * SSAO + SSR + Bloom -> Exposure -> CAS Sharpen ->
//           ACES Tonemap -> Color Grading -> Gamma
//
// References:
//   [Narkowicz 2015] "ACES Filmic Tone Mapping Curve"
//   [TheRealMJP] "BakingLab/ACES.hlsl"
//   [AMD 2019] "FidelityFX Contrast Adaptive Sharpening"
//   [UE5] Engine/Shaders/Private/PostProcessCombineLUTs.usf
//   [Hable 2010] "Filmic Tonemapping Operators"

cbuffer CompositeParams : register(b0) {
    float  exposure;        // EV100 exposure multiplier (default 1.0, or manual override)
    float  bloomStrength;   // bloom mix factor (default 0.04)
    float  gamma;           // gamma value (default 2.2)
    float  sharpenStrength; // CAS sharpening (0 = off, 0.4 = subtle, 0.8 = strong)
    float  colorTemperature;    // white balance temp shift (-1..1, 0=neutral)
    float  colorTint;           // green-magenta tint (-1..1, 0=neutral)
    float  colorSaturation;     // saturation multiplier (0..2, 1=neutral)
    float  colorContrast;       // contrast multiplier (0..2, 1=neutral)
    // Cinematic post (slots 8-11)
    float  chromaticAberration; // radial CA intensity (0=off, 0.005=subtle)
    float  vignetteIntensity;   // vignette darkness (0=off, 0.3=subtle, 1.0=heavy)
    float  vignetteFalloff;     // vignette falloff power (default 2.0)
    float  filmGrainStrength;   // noise overlay intensity (0=off, 0.05=subtle)
    // DoF params (slots 12-15)
    float  dofFocusDistance;    // focus distance in world units (0=disabled)
    float  dofAperture;         // aperture (lower = more blur, default 5.6)
    float  dofFocalLength;      // focal length mm (default 50)
    float  _compositePad;       // reserved
};

Texture2D    hdrTex         : register(t0);   // HDR scene (linear, pre-tonemap)
Texture2D    ssaoTex        : register(t1);   // SSAO (R8, blurred)
Texture2D    bloomTex       : register(t2);   // Final bloom mip (upsampled)
Texture2D    exposureTex    : register(t3);   // Auto exposure (1x1 R16_FLOAT, adapted value)
Texture2D    ssrTex         : register(t4);   // SSR reflections (RGBA16F, RGB=color, A=confidence)
SamplerState linearClamp    : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// White balance temperature/tint approximation (Planckian locus)
float3 WhiteBalance(float3 color, float temp, float tint) {
    // Temperature: shift along blue-yellow axis
    // Tint: shift along green-magenta axis
    float t = temp * 0.1;
    float g = tint * 0.1;
    float3 balance = float3(1.0 + t, 1.0 + g, 1.0 - t);
    return color * balance;
}

float4 PSMain(PSInput input) : SV_TARGET {
    int2 pc = int2(input.position.xy);

    float3 hdr  = hdrTex.Load(int3(pc, 0)).rgb;
    float  ao   = ssaoTex.Sample(linearClamp, input.texcoord).r;
    float3 bloom = bloomTex.Sample(linearClamp, input.texcoord).rgb;

    // Read auto exposure value from 1x1 texture (adapted luminance -> exposure)
    float autoExp = exposureTex.Load(int3(0, 0, 0)).r;
    // If auto exposure is disabled, autoExp will be 1.0 (white fallback texture).
    // The `exposure` constant acts as a multiplier for manual control.
    float finalExposure = exposure * max(autoExp, 0.001);

    // Apply SSAO + bloom + exposure
    float3 color = hdr * ao;
    color = lerp(color, color + bloom, bloomStrength);

    // Blend SSR reflections (additive, weighted by confidence alpha)
    float4 ssr = ssrTex.Sample(linearClamp, input.texcoord);
    color += ssr.rgb * ssr.a;

    color *= finalExposure;

    // -----------------------------------------------------------------------
    // Contrast Adaptive Sharpening (simplified AMD FidelityFX CAS)
    // Applied in post-exposure HDR space before tonemap.
    // Counteracts TAA softening without ringing on high-contrast edges.
    // -----------------------------------------------------------------------
    if (sharpenStrength > 0.001) {
        // 5-tap cross neighbors (post-exposure HDR)
        float3 cN = hdrTex.Load(int3(pc + int2( 0, -1), 0)).rgb * ao * finalExposure;
        float3 cS = hdrTex.Load(int3(pc + int2( 0,  1), 0)).rgb * ao * finalExposure;
        float3 cW = hdrTex.Load(int3(pc + int2(-1,  0), 0)).rgb * ao * finalExposure;
        float3 cE = hdrTex.Load(int3(pc + int2( 1,  0), 0)).rgb * ao * finalExposure;

        // Per-channel min/max of the 5-tap cross
        float3 mnRGB = min(color, min(min(cN, cS), min(cW, cE)));
        float3 mxRGB = max(color, max(max(cN, cS), max(cW, cE)));

        // Adaptive weight: sqrt(min/max) -- flat areas get more sharpening,
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

    // -----------------------------------------------------------------------
    // Color grading (post-tonemap, in [0,1] range)
    // Applied after ACES so grading operates on display-referred values.
    // -----------------------------------------------------------------------

    // White balance (temperature + tint)
    if (abs(colorTemperature) > 0.001 || abs(colorTint) > 0.001) {
        color = WhiteBalance(color, colorTemperature, colorTint);
        color = saturate(color);
    }

    // Contrast (pivot at mid-gray 0.18 in linear, ~0.5 after tonemap)
    if (abs(colorContrast - 1.0) > 0.001) {
        color = pow(max(color, 0.0), colorContrast);
    }

    // Saturation (luminance-preserving)
    if (abs(colorSaturation - 1.0) > 0.001) {
        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
        color = max(lerp(float3(luma, luma, luma), color, colorSaturation), 0.0);
    }

    // Gamma correction (linear -> sRGB)
    color = pow(max(color, 0.0), 1.0 / gamma);

    // -----------------------------------------------------------------------
    // Chromatic Aberration — per-channel radial UV offset (post-gamma for visible result)
    // -----------------------------------------------------------------------
    if (chromaticAberration > 0.0001) {
        float2 center = float2(0.5, 0.5);
        float2 dir = input.texcoord - center;
        float dist2 = dot(dir, dir);
        float caOffset = chromaticAberration * dist2;
        float r = hdrTex.Sample(linearClamp, input.texcoord + dir * caOffset).r;
        float b = hdrTex.Sample(linearClamp, input.texcoord - dir * caOffset).b;
        // Apply same post-process chain approximation for CA channels
        color.r = lerp(color.r, pow(saturate(r * finalExposure * 0.3), 1.0/gamma), 0.5);
        color.b = lerp(color.b, pow(saturate(b * finalExposure * 0.3), 1.0/gamma), 0.5);
    }

    // -----------------------------------------------------------------------
    // Vignette — smooth radial darkening
    // -----------------------------------------------------------------------
    if (vignetteIntensity > 0.001) {
        float2 center = float2(0.5, 0.5);
        float dist = length(input.texcoord - center) * 1.414; // normalize to [0,1] at corners
        float vignette = 1.0 - saturate(pow(dist, vignetteFalloff) * vignetteIntensity);
        color *= vignette;
    }

    // -----------------------------------------------------------------------
    // Film Grain — pseudo-random noise overlay
    // -----------------------------------------------------------------------
    if (filmGrainStrength > 0.001) {
        float2 seed = input.texcoord * 1000.0 + float2(exposure, bloomStrength) * 100.0;
        float noise = frac(sin(dot(seed, float2(12.9898, 78.233))) * 43758.5453);
        noise = (noise - 0.5) * filmGrainStrength;
        color += noise;
        color = saturate(color);
    }

    return float4(color, 1.0);
}
