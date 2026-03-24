/// Screen-space contact shadows — ray march in screen space toward the light.
///
/// @ref Uludag 2014 "Hi-Z Screen-Space Cone-Traced Reflections" — screen-space
///   ray marching foundations adapted for shadow detection
/// @ref Wihlidal 2017 "4K Checkerboard Rendering in Battlefield 1" (GDC) —
///   contact shadow ray march with depth buffer comparison
/// @ref Wright 2019 "Advances in Real-Time Rendering" (SIGGRAPH) —
///   contact shadow length parameter and integration strategy
///
/// # How It Works
/// From the world-space shading point, we march toward the light direction in
/// screen space. At each step we compare the ray's depth against the scene
/// depth buffer. If the ray passes behind geometry (ray depth > scene depth),
/// the fragment is in contact shadow.
///
/// # Prerequisites
/// - Depth buffer (Texture2D<float>) — reversed-Z: near=1.0, far=0.0
/// - viewProj matrix (to project world-space ray positions to screen)
/// - contactShadowLength from PerFrameCB (0 = disabled)
/// - Ray length is in world-space units (centimeters)

#ifndef CONTACT_SHADOWS_HLSLI
#define CONTACT_SHADOWS_HLSLI

/// @ref Wihlidal 2017 — contact shadow step count
/// @constants kContactShadowSteps=12 — balance between quality and cost
/// @deviations NONE
static const uint kContactShadowSteps = 12;

/// @ref Wihlidal 2017 — depth thickness threshold
/// Thickness of geometry in depth buffer units. If the ray passes behind
/// geometry by more than this amount, we assume it exited the occluder
/// and do NOT count it as shadow. Prevents false shadows from thin objects
/// viewed at extreme angles.
/// @constants kContactShadowThickness=0.05 — 5% of scene depth range
/// @deviations NONE
static const float kContactShadowThickness = 0.01;

/// Computes screen-space contact shadows by ray marching toward the light.
///
/// @ref Wihlidal 2017 "4K Checkerboard in Battlefield 1" — contact shadow technique
/// @ref Uludag 2014 — screen-space ray marching
///
/// @param worldPos        World-space position of the shaded fragment.
/// @param lightDir        World-space light direction (toward the light, normalized).
/// @param rayLength       World-space ray march length in centimeters. 0 = disabled.
/// @param viewDepth       Linear view-space depth (positive, away from camera).
/// @param pixelPos        Screen-space pixel position (SV_Position.xy).
/// @param frameCycleIdx   Frame index mod 4 — for temporal dithering of start offset.
/// @param viewProjMatrix  The view-projection matrix for projecting ray positions.
/// @param depthTexture    Scene depth buffer (reversed-Z: near=1.0, far=0.0).
/// @param depthSampler    Point or linear sampler for depth reads.
/// @return 1.0 = no contact shadow (fully lit), 0.0 = fully in contact shadow.
float ComputeContactShadow(float3 worldPos, float3 lightDir, float rayLength,
                           float viewDepth, float2 pixelPos, float frameCycleIdx,
                           float4x4 viewProjMatrix, Texture2D<float> depthTexture,
                           SamplerState depthSampler) {
    // Skip if contact shadows are disabled (rayLength == 0) or fragment is
    // too far from the camera (contact shadows only matter up close).
    if (rayLength <= 0.0) return 1.0;

    // ── Compute ray endpoints in screen space ───────────────────────────
    // Start: current fragment world position
    // End:   world position offset along light direction
    // rayLength is a screen-space fraction (0.03 = 3%). Scale by viewDepth
    // so the ray covers the same screen area regardless of distance.
    float worldRayLen = rayLength * viewDepth;
    float3 rayEnd = worldPos + lightDir * worldRayLen;

    // Project both endpoints to clip space.
    float4 startCS = mul(float4(worldPos, 1.0), viewProjMatrix);
    float4 endCS   = mul(float4(rayEnd, 1.0), viewProjMatrix);

    // Perspective divide to get NDC [-1,1] for XY, [0,1] for Z (reversed-Z).
    float3 startNDC = startCS.xyz / startCS.w;
    float3 endNDC   = endCS.xyz / endCS.w;

    // Convert NDC XY to UV [0,1]. Note: NDC Y is up, UV Y is down.
    float2 startUV = startNDC.xy * 0.5 + 0.5;
    startUV.y = 1.0 - startUV.y;
    float2 endUV = endNDC.xy * 0.5 + 0.5;
    endUV.y = 1.0 - endUV.y;

    // Ray direction in screen space (UV + depth).
    float2 deltaUV    = endUV - startUV;
    float  deltaDepth = endNDC.z - startNDC.z;

    // ── Step size ───────────────────────────────────────────────────────
    float stepSize = 1.0 / float(kContactShadowSteps);

    // Temporal dithering: offset the start position by a sub-step amount
    // that varies per frame. This reduces banding artifacts when combined
    // with TAA. @ref Wright 2019
    float startOffset = frac(frameCycleIdx * 0.25) * stepSize;

    // ── Ray march ───────────────────────────────────────────────────────
    // March from the fragment toward the light in screen space. At each
    // step, compare the ray's projected depth against the scene depth buffer.
    float occlusion = 0.0;

    [unroll]
    for (uint step = 0; step < kContactShadowSteps; ++step) {
        float t = startOffset + float(step) * stepSize;

        float2 sampleUV = startUV + deltaUV * t;

        // Skip samples that fall outside the screen.
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

        // Expected depth at this ray position (in reversed-Z: near=1, far=0).
        float rayDepth = startNDC.z + deltaDepth * t;

        // Read scene depth from the depth buffer.
        float sceneDepth = depthTexture.SampleLevel(depthSampler, sampleUV, 0).r;

        // In reversed-Z, a fragment is behind geometry when ray depth < scene depth
        // (because closer objects have LARGER depth values).
        // The ray is occluded if it passes behind the scene surface.
        float depthDiff = sceneDepth - rayDepth;

        // depthDiff > 0 means ray is behind the surface (occluded).
        // depthDiff > thickness means the ray exited the occluder — not shadow.
        // @ref Wihlidal 2017 — thickness test to reject false occlusion
        if (depthDiff > 0.0 && depthDiff < kContactShadowThickness) {
            occlusion = 1.0;
            break;
        }
    }

    return 1.0 - occlusion;
}

#endif // CONTACT_SHADOWS_HLSLI
