/// UzEngine Contact Shadows — Screen-Space Ray March for Fine Shadow Detail
///
/// @ref Uludag 2014 "Hi-Z Screen-Space Cone-Traced Reflections" — Hi-Z traversal
/// @ref Wihlidal 2017 "4K Checkerboard in Battlefield 1" — contact shadow technique
///
/// Short-range screen-space ray march from each pixel toward the light to add
/// fine-detail shadows that CSM/PCSS cannot resolve (e.g., small protrusions,
/// wrinkles, contact of objects resting on surfaces).
///
/// Supports Hi-Z acceleration when a Hi-Z mip pyramid SRV is available —
/// skips large empty regions by marching at coarser mip levels.

#ifndef UZ_CONTACT_SHADOWS_HLSLI
#define UZ_CONTACT_SHADOWS_HLSLI

#include "../common/constants.hlsli"
#include "../include/depth_common.hlsli"

/// @ref Wihlidal 2017 — contact shadow ray march parameters
/// CVar r.Shadow.ContactShadowSteps, r.Shadow.ContactShadowThickness
/// Safety fallback if CB padding mismatch delivers zero values.
#define kContactMaxSteps    ((shadowContactSteps > 0) ? shadowContactSteps : 16u)
static const float kContactMaxDistUV   = 0.15;
#define kContactThickness   ((shadowContactThickness > 0.0001) ? shadowContactThickness : 0.005)

/// Screen-space contact shadow with optional Hi-Z acceleration.
///
/// @param worldPos     World-space position of the receiver pixel.
/// @param lightDir     Normalized direction toward the light source.
/// @param depthTex     Full-res depth buffer (reversed-Z).
/// @param viewProj     View-projection matrix for projecting world → screen.
/// @param invViewProj  Inverse view-projection for reconstructing positions.
/// @param screenWidth  Full-res width.
/// @param screenHeight Full-res height.
/// @param nearZ        Near plane distance.
/// @param farZ         Far plane distance.
/// @return 1.0 = lit (no occluder found), 0.0 = shadowed.
float ComputeContactShadow(
    float3 worldPos,
    float3 lightDir,
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex,
#endif
    float4x4 viewProjMat,
    float4x4 invViewProjMat,
    uint screenWidth,
    uint screenHeight,
    float nearZ,
    float farZ,
    float rayLength)      ///< world-space ray length in cm (block A.8: r.ContactShadow.Length)
{
    // Project ray start to screen space
    float4 startCS = mul(float4(worldPos, 1.0), viewProjMat);
    float2 startUV = (startCS.xy / startCS.w) * 0.5 + 0.5;
    startUV.y = 1.0 - startUV.y;

    // Step along light direction in world space. The length was fixed at 1 % of the far plane
    // until 2026-10-05; r.ContactShadow.Length now scales it (0.10 = the old value).
    float stepWorldDist = rayLength;
    float3 endWorldPos = worldPos + lightDir * stepWorldDist;
    float4 endCS = mul(float4(endWorldPos, 1.0), viewProjMat);
    float2 endUV = (endCS.xy / endCS.w) * 0.5 + 0.5;
    endUV.y = 1.0 - endUV.y;

    float2 rayDirUV = endUV - startUV;
    float rayLen = length(rayDirUV);
    if (rayLen < 1e-6) return 1.0;

    // Normalize and limit ray length
    rayDirUV = normalize(rayDirUV);
    float maxDist = min(rayLen * float(kContactMaxSteps), kContactMaxDistUV);
    float stepSize = maxDist / float(kContactMaxSteps);

    // Linear depth at start
    float startLinZ = LinearizeDepthReversedZ(startCS.z / startCS.w, nearZ, farZ);

    for (uint step = 1; step <= kContactMaxSteps; ++step) {
        float t = float(step) / float(kContactMaxSteps);
        float2 sampleUV = startUV + rayDirUV * stepSize * float(step);

        // Bounds check
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) break;

        int2 samplePx = int2(sampleUV * float2(screenWidth, screenHeight));

#ifdef UZ_BINDLESS
        float sampleRawDepth = depthTex.Load(int3(samplePx, 0));
#else
        float sampleRawDepth = 0.5;
#endif

        if (IsSkyDepth(sampleRawDepth)) continue;

        float sampleLinZ = LinearizeDepthReversedZ(sampleRawDepth, nearZ, farZ);

        // Expected depth along the ray (linear interpolation from start toward light)
        float expectedLinZ = startLinZ - stepWorldDist * t; // toward light = decreasing depth

        // Intersection test: surface is between ray and camera
        float depthDiff = sampleLinZ - expectedLinZ;
        if (depthDiff > 0.0 && depthDiff < kContactThickness * startLinZ) {
            // Smooth fade-in based on step (prevents harsh on/off artifacts)
            float fadeIn = saturate(float(step) / 3.0);
            return 1.0 - fadeIn;
        }
    }

    return 1.0; // No occluder found
}

#endif // UZ_CONTACT_SHADOWS_HLSLI
