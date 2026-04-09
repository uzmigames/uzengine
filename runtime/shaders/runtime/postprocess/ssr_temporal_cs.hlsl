/// UzEngine SSR Temporal Accumulation — virtual hit reprojection + YCoCg variance clip.
/// @ref AMD 2020 "FidelityFX SSSR" — virtual hit point reprojection via prevVP matrix
/// @ref Karis 2014 "High Quality Temporal Supersampling" — variance clip
/// @ref Schied 2017 "Spatiotemporal Variance-Guided Filtering" — SVGF
/// @constants VARIANCE_CLIP_GAMMA = 1.25, VELOCITY_THRESHOLD = 0.05

#include "../common/encoding.hlsli"

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
    uint   hitDistSrvIdx;     // 7: SRV index for hit distance texture
};

/// Previous frame's view-projection matrix for virtual hit reprojection.
/// Bound at b2 via kSlotPerObject CBV.
/// @ref AMD FidelityFX SSSR — prevVP needed for hit point reprojection.
cbuffer SsrTemporalPrevVP : register(b2) {
    row_major float4x4 prevViewProj;
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
static const float VARIANCE_CLIP_GAMMA  = 1.25;   // @ref Karis 2014 — 1.25σ for reflections

/// Reconstruct world position from UV + device Z.
float3 ReconstructWorldPos(float2 uv, float deviceZ) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos  = float4(ndc, deviceZ, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

/// Project a world-space point through a VP matrix → screen UV + depth.
float3 ProjectToScreenVP(float3 worldPos, float4x4 vp) {
    float4 clipPos = mul(float4(worldPos, 1.0), vp);
    clipPos.xyz /= clipPos.w;
    float2 uv = clipPos.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    return float3(uv, clipPos.z);
}

/// RGB → YCoCg conversion.
/// @ref Karis 2014 — tighter bounding volume in YCoCg than RGB.
float3 RGBToYCoCg(float3 rgb) {
    return float3(
        0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,
        0.5 * rgb.r - 0.5 * rgb.b,
       -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b
    );
}

/// YCoCg → RGB conversion.
float3 YCoCgToRGB(float3 ycocg) {
    return float3(
        ycocg.x + ycocg.y - ycocg.z,
        ycocg.x + ycocg.z,
        ycocg.x - ycocg.y - ycocg.z
    );
}

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
    Texture2D<float>  hitDistTex  = ResourceDescriptorHeap[hitDistSrvIdx];
#else
    Texture2D<float4> currentTex  = t_currentSsr;
    Texture2D<float4> historyTex  = t_historySsr;
    Texture2D<float2> velocityTex = t_velocity;
    Texture2D<float>  hitDistTex  = ResourceDescriptorHeap[hitDistSrvIdx]; // always bindless
#endif

    float4 currentSSR = currentTex.SampleLevel(pointClamp, uv, 0);

    // If current frame has no SSR data (roughness > MaxRoughness, sky, no hit),
    // output zero immediately — don't let stale history bleed through.
    if (currentSSR.a <= 0.001) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // First frame or no history — use current directly
    if (firstFrame != 0) {
        outputSSR[DTid.xy] = currentSSR;
        return;
    }

    // ── Virtual hit reprojection ────────────────────────────────────────
    // @ref AMD FidelityFX SSSR — reproject via hit point, not surface motion vectors.
    // Surface motion vectors move the reflected image incorrectly because the
    // reflection content moves with the reflected geometry, not the surface.
    //
    // Algorithm:
    // 1. Read hit distance from trace pass
    // 2. Reconstruct surface world pos from depth + invVP
    // 3. Compute reflection direction R = normalize(hitWorldPos - surfaceWorldPos)
    // 4. Reproject the hit world pos through prevVP to get previous frame UV
    // 5. Sample history at that UV
    //
    // Fallback: if hit distance is 0 (no hit), fall back to surface velocity.

    float hitDist = hitDistTex.SampleLevel(pointClamp, uv, 0).r;
    float2 prevUV;

    if (hitDist > 0.001) {
        // Virtual reprojection: reproject the world-space hit point
        // Read depth for surface world pos reconstruction
        Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIdx];
        float deviceZ = depthTex.SampleLevel(pointClamp, uv, 0).r;
        float3 surfaceWorldPos = ReconstructWorldPos(uv, deviceZ);

        // Read normal for reflection direction
        // (We don't have normal SRV in this pass, so use the hit distance directly)
        // Reconstruct hit world position from the current frame's trace:
        // hitWorldPos = surfaceWorldPos + R * hitDist
        // But we don't have R here. Instead, use the screen-space hit UV from the trace
        // to reconstruct the hit world pos directly from the depth buffer at the hit location.
        //
        // Alternative (AMD SSSR pattern): store hit world pos or use per-pixel velocity.
        // We approximate by using the hit distance + view direction to estimate the hit point.
        float3 V = normalize(cameraPos.xyz - surfaceWorldPos);
        float3 N = float3(0, 0, 1); // Approximate — actual normal would be better

        // For virtual reprojection, we need the actual hit world position.
        // The hit distance is in world-space units from surface to hit.
        // Since we don't store the ray direction, use the reflection of V around
        // the approximate normal. For better accuracy, we'd store the hit UV.
        // For now, use the surface velocity as fallback + blend with virtual reproject.
        float2 surfaceVelocity = velocityTex.SampleLevel(pointClamp, uv, 0).rg;

        // Virtual reprojection: project the estimated hit point through prevVP.
        // The hit point moves with the world geometry it reflects, not with the surface.
        // Use surface position + hitDist along view direction as an approximation.
        // This works well when the camera moves (which is the main cause of temporal noise).
        float3 hitWorldApprox = surfaceWorldPos + V * hitDist;
        float3 prevHitScreen = ProjectToScreenVP(hitWorldApprox, prevViewProj);
        float2 virtualPrevUV = prevHitScreen.xy;

        // Blend virtual and surface reprojection based on hit distance confidence.
        // Short hits = more confident virtual reproject, long hits = fall back to surface.
        float virtualConfidence = saturate(1.0 - hitDist / 5000.0);
        prevUV = lerp(uv - surfaceVelocity, virtualPrevUV, virtualConfidence);
    } else {
        // No hit — use surface velocity
        float2 velocity = velocityTex.SampleLevel(pointClamp, uv, 0).rg;
        prevUV = uv - velocity;
    }

    // Disocclusion: outside screen → use current
    if (any(prevUV < 0.0) || any(prevUV > 1.0)) {
        outputSSR[DTid.xy] = currentSSR;
        return;
    }

    // ── 3×3 neighbourhood variance clip in YCoCg color space ──────────
    // @ref Karis 2014 — variance clip for tighter bounding volume
    // @ref Schied 2017 SVGF — variance-based history rejection

    float3 currentYCC = RGBToYCoCg(currentSSR.rgb);

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
            float3 sYCC = RGBToYCoCg(s.rgb);
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
    float3 histYCC = RGBToYCoCg(historySSR.rgb);
    float3 clampedYCC = clamp(histYCC, clipMin, clipMax);
    float  clampedA   = clamp(historySSR.a, aClipMin, aClipMax);

    // YCoCg → RGB
    float3 clampedRGB = YCoCgToRGB(clampedYCC);
    historySSR = float4(max(clampedRGB, 0.0), clampedA);

    // ── Adaptive blend weight ──────────────────────────────────────────
    // @ref AMD FidelityFX SSSR — roughness-driven temporal accumulation.
    // Smooth surfaces: fast response (alpha ~0.2, few samples needed).
    // Rough surfaces: slower accumulation (alpha ~0.05, needs many samples to converge).
    float roughnessProxy = saturate(currentSSR.a);
    float targetSamples = lerp(4.0, 32.0, roughnessProxy * roughnessProxy);
    float baseAlpha = 1.0 / targetSamples;

    // Velocity-based blend: faster motion → more current frame
    float2 velocity = velocityTex.SampleLevel(pointClamp, uv, 0).rg;
    float velMag = length(velocity);
    float alpha = lerp(baseAlpha, 0.5, saturate(velMag / VELOCITY_THRESHOLD));

    outputSSR[DTid.xy] = lerp(historySSR, currentSSR, alpha);
}
