// clouds_temporal.ps.hlsl — Temporal accumulation for volumetric clouds
//
// Reprojects previous frame's cloud result using motion vectors,
// blends with current frame to amortize raymarching cost and reduce noise.
//
// References:
//   [UE5] TemporalAA.usf — neighborhood clamping
//   [Karis 2014] "High Quality Temporal Supersampling"
//
// Root signature:
//   b0: 4 root constants (temporalBlendFactor, historyValid, _pad, _pad)
//   t0: current frame cloud result (RGBA16F)
//   t1: previous frame cloud history (RGBA16F)
//   t2: velocity buffer (RG16F motion vectors)

cbuffer TemporalParams : register(b0) {
    float temporalBlendFactor;  // 0.95 = 95% history reuse
    float historyValid;         // 0.0 = invalid (camera cut), 1.0 = valid
    float _tpad0;
    float _tpad1;
    float _tpad2;
    float _tpad3;
    float _tpad4;
    float _tpad5;
    float _tpad6;
    float _tpad7;
    float _tpad8;
    float _tpad9;
    float _tpad10;
    float _tpad11;
    float _tpad12;
    float _tpad13;
};

Texture2D    currentTex   : register(t0);
Texture2D    historyTex   : register(t1);
Texture2D    velocityTex  : register(t2);
SamplerState linearClamp  : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float4 current = currentTex.Load(int3(input.position.xy, 0));

    // If history is invalid (camera cut, first frame), return current directly
    if (historyValid < 0.5) {
        return current;
    }

    // Read motion vector to find where this pixel was last frame
    float2 velocity = velocityTex.Load(int3(input.position.xy, 0)).rg;
    float2 historyUV = input.uv - velocity;

    // Out-of-bounds rejection — if reprojected UV is outside [0,1], use current
    if (historyUV.x < 0.0 || historyUV.x > 1.0 ||
        historyUV.y < 0.0 || historyUV.y > 1.0) {
        return current;
    }

    // Sample history with bilinear filtering
    float4 history = historyTex.SampleLevel(linearClamp, historyUV, 0);

    // -----------------------------------------------------------------------
    // Neighborhood clamping (3x3 min/max of current frame)
    // Prevents ghosting from stale history by constraining to plausible range
    // -----------------------------------------------------------------------
    float4 neighborMin = current;
    float4 neighborMax = current;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x) {
        if (x == 0 && y == 0) continue;
        float4 n = currentTex.Load(int3(input.position.xy + int2(x, y), 0));
        neighborMin = min(neighborMin, n);
        neighborMax = max(neighborMax, n);
    }

    // Clamp history to current neighborhood range
    float4 clampedHistory = clamp(history, neighborMin, neighborMax);

    // Adaptive blend factor: reduce history weight when clamping was aggressive
    float clampDist = length(history.rgb - clampedHistory.rgb);
    float adaptiveBlend = lerp(temporalBlendFactor, 0.5, saturate(clampDist * 5.0));

    // Blend: high history reuse for stable clouds, lower when fast motion
    float4 result = lerp(current, clampedHistory, adaptiveBlend);

    return result;
}
