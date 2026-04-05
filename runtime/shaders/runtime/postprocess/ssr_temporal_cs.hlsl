/// UzEngine SSR Temporal Accumulation — 3×3 neighborhood clamping + velocity reprojection.
/// @ref Karis 2014 "High Quality Temporal Supersampling"
/// @ref Schied 2017 "Spatiotemporal Variance-Guided Filtering"
/// @constants TEMPORAL_BLEND_ALPHA = 0.05, VELOCITY_THRESHOLD = 0.01

cbuffer SsrTemporalRootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   currentSsrSrvIdx;  // 0
    uint   historySsrSrvIdx;  // 1
    uint   velocitySrvIdx;    // 2
    uint   depthSrvIdx;       // 3
#else
    uint   _srvPad0;          // 0
    uint   _srvPad1;          // 1
    uint   _srvPad2;          // 2
    uint   _srvPad3;          // 3
#endif
    uint   outputWidth;       // 4
    uint   outputHeight;      // 5
    uint   firstFrame;        // 6: 1 = no valid history
    uint   reserved;          // 7
};

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float  nearPlane, farPlane;
};

RWTexture2D<float4> outputSSR : register(u0);
#ifndef UZ_BINDLESS
Texture2D<float4> t_currentSsr : register(t0);
Texture2D<float4> t_historySsr : register(t1);
Texture2D<float2> t_velocity   : register(t2);
#endif
SamplerState linearClamp : register(s0);
SamplerState pointClamp  : register(s1);

static const float VELOCITY_THRESHOLD   = 0.05;   // UV space — 5% of screen ≈ 54px at 1080p
static const float VARIANCE_CLIP_GAMMA  = 1.25;   // @ref UE5 — 1.25σ for reflections

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

#ifdef UZ_BINDLESS
    Texture2D<float4> currentTex  = ResourceDescriptorHeap[currentSsrSrvIdx];
    Texture2D<float4> historyTex  = ResourceDescriptorHeap[historySsrSrvIdx];
    Texture2D<float2> velocityTex = ResourceDescriptorHeap[velocitySrvIdx];
#else
    Texture2D<float4> currentTex  = t_currentSsr;
    Texture2D<float4> historyTex  = t_historySsr;
    Texture2D<float2> velocityTex = t_velocity;
#endif

    float4 currentSSR = currentTex.SampleLevel(pointClamp, uv, 0);

    // If current frame has no SSR data (roughness > MaxRoughness, sky, no hit),
    // output zero immediately — don't let stale history bleed through.
    // This prevents temporal ghosting on surfaces that shouldn't have SSR.
    if (currentSSR.a <= 0.001) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // First frame or no history — use current directly
    if (firstFrame != 0) {
        outputSSR[DTid.xy] = currentSSR;
        return;
    }

    // Reprojection via velocity buffer
    float2 velocity = velocityTex.SampleLevel(pointClamp, uv, 0).rg;
    float2 prevUV   = uv - velocity;

    // Disocclusion: outside screen → use current
    if (any(prevUV < 0.0) || any(prevUV > 1.0)) {
        outputSSR[DTid.xy] = currentSSR;
        return;
    }

    // 3×3 neighbourhood variance clip in YCoCg color space.
    // @ref Karis 2014 "High Quality Temporal Supersampling" — variance clip
    // @ref Schied 2017 "SVGF" — variance-based history rejection
    // YCoCg prevents luma changes from over-clamping chrominance.

    // RGB → YCoCg conversion
    // @ref Karis 2014 — YCoCg for tighter bounding volume
    float3 currentYCC = float3(
        0.25 * currentSSR.r + 0.5 * currentSSR.g + 0.25 * currentSSR.b,
        0.5 * currentSSR.r - 0.5 * currentSSR.b,
       -0.25 * currentSSR.r + 0.5 * currentSSR.g - 0.25 * currentSSR.b
    );

    float3 moment1 = currentYCC;
    float3 moment2 = currentYCC * currentYCC;
    float  alphaM1 = currentSSR.a;
    float  alphaM2 = currentSSR.a * currentSSR.a;

    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            float2 sampleUV = uv + float2(x, y) * texelSize;
            float4 s = currentTex.SampleLevel(pointClamp, sampleUV, 0);
            float3 sYCC = float3(
                0.25 * s.r + 0.5 * s.g + 0.25 * s.b,
                0.5 * s.r - 0.5 * s.b,
               -0.25 * s.r + 0.5 * s.g - 0.25 * s.b
            );
            moment1 += sYCC;
            moment2 += sYCC * sYCC;
            alphaM1 += s.a;
            alphaM2 += s.a * s.a;
        }
    }

    float3 mean   = moment1 / 9.0;
    float3 stddev = sqrt(max(moment2 / 9.0 - mean * mean, 0.0));
    float  aMean  = alphaM1 / 9.0;
    float  aStd   = sqrt(max(alphaM2 / 9.0 - aMean * aMean, 0.0));

    float3 clipMin = mean - VARIANCE_CLIP_GAMMA * stddev;
    float3 clipMax = mean + VARIANCE_CLIP_GAMMA * stddev;
    float  aClipMin = aMean - VARIANCE_CLIP_GAMMA * aStd;
    float  aClipMax = aMean + VARIANCE_CLIP_GAMMA * aStd;

    // Sample history and clamp in YCoCg space
    float4 historySSR = historyTex.SampleLevel(linearClamp, prevUV, 0);
    float3 histYCC = float3(
        0.25 * historySSR.r + 0.5 * historySSR.g + 0.25 * historySSR.b,
        0.5 * historySSR.r - 0.5 * historySSR.b,
       -0.25 * historySSR.r + 0.5 * historySSR.g - 0.25 * historySSR.b
    );
    float3 clampedYCC = clamp(histYCC, clipMin, clipMax);
    float  clampedA   = clamp(historySSR.a, aClipMin, aClipMax);

    // YCoCg → RGB conversion
    float3 clampedRGB = float3(
        clampedYCC.x + clampedYCC.y - clampedYCC.z,
        clampedYCC.x + clampedYCC.z,
        clampedYCC.x - clampedYCC.y - clampedYCC.z
    );
    historySSR = float4(max(clampedRGB, 0.0), clampedA);

    // Adaptive blend: roughness-driven target sample count.
    // @ref UE5 SSDTemporalAccumulation.usf — blend weight = 1/(accumulatedSamples + 1)
    // Mirror surfaces (roughness ~0): alpha ~1.0 (instant response)
    // Rough surfaces (roughness ~0.5): alpha ~1/16 (accumulate ~16 samples)
    // We approximate with roughness-based target since we don't track accum count.
    float targetSamples = lerp(1.0, 32.0, saturate(currentSSR.a) * saturate(currentSSR.a));
    float baseAlpha = 1.0 / targetSamples;

    // Velocity-based blend: faster motion → more current frame
    float velMag = length(velocity);
    float alpha = lerp(baseAlpha, 0.5, saturate(velMag / VELOCITY_THRESHOLD));

    outputSSR[DTid.xy] = lerp(historySSR, currentSSR, alpha);
}
