/// UzEngine Contact Shadows — Screen-Space Ray March for Fine Shadow Detail
///
/// @ref Wihlidal 2017 "4K Checkerboard in Battlefield 1" — contact shadow technique
/// @ref UE5 ContactShadows.usf — clip-space march, linear-depth thickness test
///
/// Short-range screen-space ray march from each pixel toward the light to add
/// fine-detail shadows that CSM/PCSS cannot resolve (e.g., small protrusions,
/// wrinkles, contact of objects resting on surfaces).
///
/// Block B.4b (2026-10-06): rewritten. The previous march linearised the depth with the
/// standard-Z formula (every depth came out as ~nearZ), assumed the ray approaches the
/// camera at its full world-space length ("toward light = decreasing depth") and stepped
/// 16 ray lengths across the screen; it never found an occluder. Now the ray is
/// interpolated in clip space (perspective-correct in screen space; the clip w of
/// Mat4::Perspective is the view depth in cm) and each sample compares the scene's
/// view depth with the ray point's: a surface in front of the ray point by less than
/// the thickness occludes it.
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
/// Self-occlusion guard, as a fraction of the ray point's view depth: the receiver's own
/// surface, seen at the first samples, is never more than this in front of the ray.
static const float kContactSelfBias = 0.0005;

/// Screen-space contact shadow.
///
/// @param worldPos     World-space position of the receiver pixel.
/// @param lightDir     Normalized direction toward the light source.
/// @param depthTex     Full-res depth buffer (reversed-Z).
/// @param viewProjMat  View-projection matrix for projecting world → clip.
/// @param invViewProjMat Unused (kept for the call sites).
/// @param screenWidth  Full-res width.
/// @param screenHeight Full-res height.
/// @param nearZ        Near plane distance.
/// @param farZ         Far plane distance.
/// @param rayLength    World-space ray length in cm (r.ContactShadow.Length x far plane).
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
    float rayLength)
{
    float4 startCS = mul(float4(worldPos, 1.0), viewProjMat);
    float4 endCS   = mul(float4(worldPos + lightDir * rayLength, 1.0), viewProjMat);
    if (startCS.w <= nearZ) return 1.0;
    // A ray toward a light behind the camera crosses the near plane: cut it there.
    if (endCS.w < nearZ) {
        float a = (startCS.w - nearZ) / max(startCS.w - endCS.w, 1e-6);
        endCS = lerp(startCS, endCS, a);
    }
    float2 startUV = startCS.xy / startCS.w * float2(0.5, -0.5) + 0.5;
    float2 endUV   = endCS.xy / endCS.w * float2(0.5, -0.5) + 0.5;
    float uvLen = length(endUV - startUV);
    if (uvLen < 1e-6) return 1.0;
    // Limit the screen-space extent of the march (long rays near the camera).
    float tMax = min(1.0, kContactMaxDistUV / uvLen);

    for (uint step = 1; step <= kContactMaxSteps; ++step) {
        float t = tMax * float(step) / float(kContactMaxSteps);
        float4 cs = lerp(startCS, endCS, t);        // linear in clip space = perspective-correct on screen
        float2 uv = cs.xy / cs.w * float2(0.5, -0.5) + 0.5;
        if (any(uv < 0.0) || any(uv > 1.0)) break;
        int2 samplePx = int2(uv * float2(screenWidth, screenHeight));
#ifdef UZ_BINDLESS
        float sampleRawDepth = depthTex.Load(int3(samplePx, 0));
#else
        float sampleRawDepth = 0.0;
#endif
        if (IsSkyDepth(sampleRawDepth)) continue;
        float sceneZ = LinearizeDepthReversedZ(sampleRawDepth, nearZ, farZ);
        float rayZ   = cs.w;                        // view depth of the ray point (cm)
        float diff   = rayZ - sceneZ;               // > 0: the scene surface is in front of the ray point
        if (diff > kContactSelfBias * rayZ && diff < kContactThickness * rayZ) {
            return 0.0;
        }
    }
    return 1.0; // No occluder found
}

#endif // UZ_CONTACT_SHADOWS_HLSLI
