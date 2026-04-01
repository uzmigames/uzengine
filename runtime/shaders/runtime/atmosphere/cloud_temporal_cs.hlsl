/// UzEngine Cloud Temporal Reprojection — variance-clipped temporal accumulation.
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" — variance clipping in YCoCg
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling" — neighborhood clamping
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
///
/// Takes the current half-res cloud output and blends with reprojected history
/// using variance clipping to reject ghosted samples. Output: temporally stable
/// RGBA16F cloud texture (same format as input).
///
/// Root constants (b0): { currentSrvIndex, historySrvIndex, outputUavIndex, halfWidth }
/// Additional root constants packed: { halfHeight, frameIndex, 0, 0 } at b0 offset 16
/// Constant buffer (b2): prevViewProj + current invViewProj for reprojection.
///
/// NOTE: This shader is not yet wired into the pipeline. CloudPass currently
/// outputs directly without temporal filtering. When wired, the C++ side needs
/// to create a ping-pong history buffer and dispatch this shader after the
/// ray march and before the composite.

#include "../common/constants.hlsli"
#include "../common/bindless.hlsli"

cbuffer RootConstants : register(b0) {
    uint currentSrvIndex;   // current frame cloud output (half-res)
    uint historySrvIndex;   // previous frame temporal result (half-res)
    uint outputUavIndex;    // output temporal result (half-res)
    uint halfWidth;
};

cbuffer TemporalCB : register(b2) {
    float4x4 prevViewProj;  // previous frame unjittered view-projection
    float4x4 invViewProj;   // current frame unjittered inverse view-projection
    uint halfHeight;
    float temporalBlendFactor; // 0.0 = all history, 1.0 = all current (bypass)
    float _pad[2];
};

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Color space conversion for variance clipping
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 — YCoCg color space for better variance clipping behavior
float3 RGBtoYCoCg(float3 rgb) {
    return float3(
         0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,   // Y
         0.5  * rgb.r                - 0.5  * rgb.b,    // Co
        -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b     // Cg
    );
}

float3 YCoCgToRGB(float3 ycocg) {
    float y  = ycocg.x;
    float co = ycocg.y;
    float cg = ycocg.z;
    return float3(
        y + co - cg,
        y      + cg,
        y - co - cg
    );
}

/// Clip a color to the AABB defined by [boxMin, boxMax] along the ray from
/// the AABB center to the color. Returns the clipped color.
/// @ref Salvi 2016 — AABB clipping for temporal anti-aliasing
float3 ClipToAABB(float3 color, float3 boxMin, float3 boxMax) {
    float3 center = 0.5 * (boxMin + boxMax);
    float3 extents = 0.5 * (boxMax - boxMin);

    float3 offset = color - center;
    float3 ts = abs(extents) / max(abs(offset), float3(1e-6, 1e-6, 1e-6));
    float t = saturate(min(ts.x, min(ts.y, ts.z)));
    return center + offset * t;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= halfWidth || DTid.y >= halfHeight) return;

    RWTexture2D<float4> outputTex = ResourceDescriptorHeap[outputUavIndex];

    // Sample current frame cloud output
    float2 uv = (float2(DTid.xy) + 0.5) / float2(halfWidth, halfHeight);
    float4 current = UzSampleTexture2DLevel(currentSrvIndex, linearSampler, uv, 0.0);

    // Bypass temporal if blend factor is 1.0 (debug / first frame)
    if (temporalBlendFactor >= 0.99) {
        outputTex[DTid.xy] = current;
        return;
    }

    // ── Reprojection ─────────────────────────────────────────────────────
    // Reconstruct world position from UV, then project to previous frame
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Use a fixed depth in the cloud layer (approximate — clouds are distant)
    float4 worldPos = mul(float4(ndc, 0.5, 1.0), invViewProj);
    worldPos.xyz /= worldPos.w;

    float4 prevClip = mul(float4(worldPos.xyz, 1.0), prevViewProj);
    float2 prevUV = prevClip.xy / prevClip.w;
    prevUV = prevUV * 0.5 + 0.5;
    prevUV.y = 1.0 - prevUV.y;

    // Check if reprojected UV is within bounds
    bool validHistory = all(prevUV >= 0.0) && all(prevUV <= 1.0);

    if (!validHistory) {
        outputTex[DTid.xy] = current;
        return;
    }

    // Sample history at reprojected location
    float4 history = UzSampleTexture2DLevel(historySrvIndex, linearSampler, prevUV, 0.0);

    // ── Variance clipping in YCoCg ───────────────────────────────────────
    /// @ref Karis 2014 "High Quality Temporal Supersampling"
    /// 3x3 neighborhood statistics for variance clipping.
    /// gamma = 1.25 gives good ghosting rejection without flicker.
    /// @constants gamma = 1.25 (Karis 2014 recommended range: 1.0-1.5)
    float3 m1 = float3(0, 0, 0);   // mean
    float3 m2 = float3(0, 0, 0);   // mean of squares
    float  a1 = 0.0;               // alpha mean
    float  a2 = 0.0;               // alpha mean of squares

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int2 coord = int2(DTid.xy) + int2(dx, dy);
            coord = clamp(coord, int2(0, 0), int2(halfWidth - 1, halfHeight - 1));
            float2 sampleUV = (float2(coord) + 0.5) / float2(halfWidth, halfHeight);
            float4 s = UzSampleTexture2DLevel(currentSrvIndex, linearSampler, sampleUV, 0.0);
            float3 ycocg = RGBtoYCoCg(s.rgb);
            m1 += ycocg;
            m2 += ycocg * ycocg;
            a1 += s.a;
            a2 += s.a * s.a;
        }
    }

    m1 /= 9.0;
    m2 /= 9.0;
    a1 /= 9.0;
    a2 /= 9.0;

    float3 stddev = sqrt(max(m2 - m1 * m1, float3(0, 0, 0)));
    float  aStddev = sqrt(max(a2 - a1 * a1, 0.0));

    static const float kGamma = 1.25;  // Karis 2014
    float3 boxMin = m1 - kGamma * stddev;
    float3 boxMax = m1 + kGamma * stddev;

    // Clip history color to the variance AABB
    float3 historyYCoCg = RGBtoYCoCg(history.rgb);
    float3 clippedYCoCg = ClipToAABB(historyYCoCg, boxMin, boxMax);
    float3 clippedRGB = YCoCgToRGB(clippedYCoCg);

    // Clip history alpha similarly
    float clippedAlpha = clamp(history.a, a1 - kGamma * aStddev, a1 + kGamma * aStddev);

    // ── Temporal blend ───────────────────────────────────────────────────
    // Default blend: 5% current, 95% history (converges in ~20 frames)
    float blendWeight = max(temporalBlendFactor, 0.05);
    float3 finalRGB = lerp(clippedRGB, current.rgb, blendWeight);
    float  finalA   = lerp(clippedAlpha, current.a, blendWeight);

    outputTex[DTid.xy] = float4(max(finalRGB, float3(0, 0, 0)), saturate(finalA));
}
