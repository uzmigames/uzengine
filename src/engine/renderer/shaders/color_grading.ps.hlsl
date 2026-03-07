// color_grading.ps.hlsl -- Color grading via 3D LUT lookup
//
// Reads the tonemapped scene color (after ACES, before gamma) and applies
// a 32x32x32 3D LUT for cinematic color grading. Also supports simple
// color temperature and tint adjustments.
//
// References:
//   [Hable 2010] "Film/VFX Color Pipeline"
//   UE5 — PostProcessCombineLUTs.usf

cbuffer ColorGradeParams : register(b0) {
    float  lutStrength;       // 0.0 = identity, 1.0 = full LUT
    float  temperature;       // -1.0 (cool) to +1.0 (warm), 0 = neutral
    float  tint;              // -1.0 (green) to +1.0 (magenta), 0 = neutral
    float  saturation;        // 0.0 = mono, 1.0 = default, 2.0 = oversaturated
    float  contrast;          // 0.5 = low, 1.0 = default, 2.0 = high
    float  brightness;        // additive brightness offset (default 0.0)
    float  gamma;             // gamma correction (default 2.2)
    float  _pad;
};

Texture2D    sceneTex   : register(t0);   // Tonemapped scene (linear, post-ACES)
Texture3D    lutTex     : register(t1);   // 32x32x32 3D LUT (or identity)
SamplerState linearClamp : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float3 ApplyTemperature(float3 color, float temp, float tintVal) {
    // Simple white balance approximation (Planckian locus shift)
    float3 warmShift = float3(1.0 + temp * 0.1, 1.0, 1.0 - temp * 0.1);
    float3 tintShift = float3(1.0, 1.0 - tintVal * 0.05, 1.0 + tintVal * 0.05);
    return color * warmShift * tintShift;
}

float4 PSMain(PSInput input) : SV_TARGET {
    int2 pc = int2(input.position.xy);
    float3 color = sceneTex.Load(int3(pc, 0)).rgb;

    // Temperature + tint adjustment (in linear space, before LUT)
    color = ApplyTemperature(color, temperature, tint);

    // Contrast (pivot at midgrey 0.18)
    color = max(0.0, 0.18 + (color - 0.18) * contrast);

    // Brightness
    color = max(0.0, color + brightness);

    // Saturation
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = max(0.0, lum + saturation * (color - lum));

    // 3D LUT lookup (color must be in [0,1] range for proper lookup)
    float3 clamped = saturate(color);
    // Scale and offset to sample LUT texel centers (avoiding edge bleeding)
    float lutSize = 32.0;
    float3 lutUV = (clamped * (lutSize - 1.0) + 0.5) / lutSize;
    float3 lutColor = lutTex.SampleLevel(linearClamp, lutUV, 0).rgb;
    color = lerp(color, lutColor, lutStrength);

    // Gamma correction (linear → sRGB)
    color = pow(max(color, 0.0), 1.0 / gamma);

    return float4(color, 1.0);
}
