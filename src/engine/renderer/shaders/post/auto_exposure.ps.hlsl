// auto_exposure.ps.hlsl -- Average scene luminance for auto exposure
//
// Fullscreen pass that renders to a 1x1 R16_FLOAT target.
// Samples the HDR scene at a sparse 8x8 grid (64 taps) and outputs
// the weighted average log2 luminance.
//
// A second invocation (adaptation pass) lerps between previous and new
// luminance values for smooth temporal adaptation.
//
// References:
//   [Karis 2013] "Real Shading in Unreal Engine 4" — auto exposure
//   UE5 — Engine/Shaders/Private/PostProcessEyeAdaptation.usf

cbuffer ExposureParams : register(b0) {
    float  minLogLum;          // floor for log2 luminance (default -8.0)
    float  maxLogLum;          // ceiling for log2 luminance (default 4.0)
    float  adaptSpeedUp;       // adaptation speed bright→dark (default 3.0)
    float  adaptSpeedDown;     // adaptation speed dark→bright (default 1.0)
    float  deltaTime;          // frame delta time for smooth adaptation
    float  targetExposure;     // manual override (0 = auto mode)
    float  lowPercentile;      // ignore bottom N% of histogram (default 0.1)
    float  highPercentile;     // ignore top N% of histogram (default 0.98)
};

Texture2D    hdrScene     : register(t0);   // HDR scene (full-res or TAA-resolved)
Texture2D    prevExposure : register(t1);   // previous frame's adapted luminance (1x1 R16_FLOAT)
SamplerState linearClamp  : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float PSMain(PSInput input) : SV_TARGET {
    // Sparse 8x8 grid sampling of HDR scene — 64 taps is enough for average
    float totalLogLum = 0.0;
    float totalWeight = 0.0;

    [unroll]
    for (int y = 0; y < 8; ++y) {
        [unroll]
        for (int x = 0; x < 8; ++x) {
            // Sample at grid centers, leaving small margin to avoid edge artifacts
            float2 uv = float2((x + 0.5) / 8.0, (y + 0.5) / 8.0);
            // Remap to [0.05, 0.95] to avoid screen edges
            uv = uv * 0.9 + 0.05;

            float3 color = hdrScene.SampleLevel(linearClamp, uv, 0).rgb;
            float lum = Luminance(color);

            // Weight: center-weighted (more importance to screen center)
            float2 centered = uv - 0.5;
            float weight = 1.0 - saturate(length(centered) * 1.5);
            weight = max(weight, 0.1);  // minimum weight for edges

            float logLum = clamp(log2(max(lum, 0.00001)), minLogLum, maxLogLum);
            totalLogLum += logLum * weight;
            totalWeight += weight;
        }
    }

    float avgLogLum = totalLogLum / max(totalWeight, 0.001);

    // Convert log-average luminance to EV100 exposure
    // EV100 = log2(averageLuminance * 100 / 12.5) — ISO 100
    // exposure = 1 / (2^EV100 * 1.2) — standard exposure formula
    // Simplified: exposure = 0.1 / max(avgLum, 0.001)
    float avgLum = exp2(avgLogLum);
    float newExposure = 0.18 / max(avgLum, 0.001);  // key value / average luminance
    newExposure = clamp(newExposure, exp2(minLogLum), exp2(maxLogLum));

    // Manual override
    if (targetExposure > 0.0)
        return targetExposure;

    // Temporal adaptation: smooth transition using previous frame's exposure
    float prevExp = prevExposure.SampleLevel(linearClamp, float2(0.5, 0.5), 0).r;
    if (prevExp <= 0.001) prevExp = newExposure;  // first frame

    // Adapt faster when brightening (eyes adapt faster to bright than dark)
    float speed = (newExposure > prevExp) ? adaptSpeedDown : adaptSpeedUp;
    float adapted = prevExp + (newExposure - prevExp) * saturate(1.0 - exp(-deltaTime * speed));

    return adapted;
}
