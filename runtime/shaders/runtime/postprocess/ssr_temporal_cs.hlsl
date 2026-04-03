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

static const float TEMPORAL_BLEND_ALPHA = 0.05;
static const float VELOCITY_THRESHOLD   = 0.01;

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

    // 3×3 neighbourhood AABB for clamping
    float4 minVal = currentSSR;
    float4 maxVal = currentSSR;

    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            float2 sampleUV = uv + float2(x, y) * texelSize;
            float4 s = currentTex.SampleLevel(pointClamp, sampleUV, 0);
            minVal = min(minVal, s);
            maxVal = max(maxVal, s);
        }
    }

    // Sample history and clamp to neighbourhood AABB
    float4 historySSR = historyTex.SampleLevel(linearClamp, prevUV, 0);
    historySSR = clamp(historySSR, minVal, maxVal);

    // Velocity-based blend: faster motion → more current frame
    float velMag = length(velocity);
    float alpha = lerp(TEMPORAL_BLEND_ALPHA, 1.0, saturate(velMag / VELOCITY_THRESHOLD));

    outputSSR[DTid.xy] = lerp(historySSR, currentSSR, alpha);
}
