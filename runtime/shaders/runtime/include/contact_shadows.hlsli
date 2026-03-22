/// Contact shadow ray marching — screen-space shadow for fine detail.
///
/// Marches a ray in clip/NDC space along the light direction from the surface
/// position. At each step, the ray's depth is compared against the scene depth
/// buffer. A hit indicates contact shadow occlusion.
///
/// @ref Screen-space contact shadow ray marching technique
/// @ref Jimenez 2016 "Interleaved Gradient Noise" — temporal dither for ray offset

#ifndef CONTACT_SHADOWS_HLSLI
#define CONTACT_SHADOWS_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Interleaved Gradient Noise
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Jimenez 2016 "Interleaved Gradient Noise"
/// @constants magic = (0.06711056, 0.00583715, 52.9829189) (RandomInterleavedGradientNoise.ush:16)
///            frameJitter = FrameId * float2(47, 17) * 0.695 (RandomInterleavedGradientNoise.ush:14)
/// @deviations NONE
///
/// Temporal dithering noise from Jimenez 2014 "Next Generation Post Processing in
/// Call of Duty: Advanced Warfare". Returns [0,1].
///
/// # Parameters
/// - uv:      pixel position (SV_Position.xy + 0.5).
/// - frameId: frame index modulo 8 for temporal variation.
///
/// # Returns
/// Pseudo-random value in [0, 1].
float InterleavedGradientNoise(float2 uv, float frameId) {
    uv += frameId * (float2(47, 17) * 0.695f);
    const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(uv, magic.xy)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen-Space Contact Shadow Ray Cast
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Screen-space contact shadow ray cast
/// @constants CompareToleranceScale = 2.0 (DeferredLightingCommon.ush:68)
///            NumSteps = 8 (DeferredLightingCommon.ush:197, ScreenSpaceShadows.usf:143)
///            StepOffset = Dither - 0.5 (ScreenSpaceShadowRayCast.ush:26)
///            Step = 1.0 / NumSteps (ScreenSpaceShadowRayCast.ush:27)
/// @deviations UzEngine uses viewProj instead of TranslatedWorldToClip. UzEngine does
///   NDC-to-UV conversion inline (ndc*0.5+0.5, flip Y) instead of ScreenPositionScaleBias.
///   UzEngine samples depth texture directly instead of SceneTexturesStruct.
///
/// Casts a ray from the world-space surface position along the light direction
/// in clip/NDC space. At each step along the ray, the depth is compared against
/// the scene depth buffer. Returns the distance along the ray to the first hit,
/// or a negative value on miss.
///
/// # Parameters
/// - worldPos:   world-space surface position.
/// - lightDir:   world-space light direction (normalized, pointing toward light).
/// - rayLength:  world-space ray length (computed from contactShadowLength).
/// - numSteps:   number of ray march steps (Default: 8).
/// - dither:     per-pixel dither value [0,1] from InterleavedGradientNoise.
/// - vp:         view-projection matrix (combined).
/// - depthTex:   depth buffer texture (R32_FLOAT, device Z).
/// - depthSampler: point/linear sampler for depth lookups.
///
/// # Returns
/// Distance along ray to first hit (positive), or -1.0 on miss.
float CastContactShadowRay(
    float3 worldPos, float3 lightDir, float rayLength, int numSteps,
    float dither, row_major float4x4 vp,
    Texture2D<float> depthTex, SamplerState depthSampler)
{
    /// @ref Screen-space ray cast —14-16 — project ray start and end to clip space
    float4 rayStartClip = mul(float4(worldPos, 1.0f), vp);
    float4 rayDirClip   = mul(float4(lightDir * rayLength, 0.0f), vp);
    float4 rayEndClip   = rayStartClip + rayDirClip;

    /// @ref Screen-space ray cast —18-19 — perspective divide to NDC
    float3 rayStart = rayStartClip.xyz / rayStartClip.w;
    float3 rayEnd   = rayEndClip.xyz / rayEndClip.w;

    float3 rayStep = rayEnd - rayStart;

    /// @ref Screen-space ray cast —23-24 — compute depth tolerance
    /// Project a pure Z offset by rayLength to get the depth range of the ray in NDC.
    float4 rayDepthClip = rayStartClip + mul(float4(0, 0, rayLength, 0), vp);
    float3 rayDepth = rayDepthClip.xyz / rayDepthClip.w;

    /// @ref Screen-space ray cast —26-29 — step offset and tolerance
    const float stepOffset = dither - 0.5f;
    const float step = 1.0f / (float)numSteps;

    /// @ref Contact shadow technique —68 — CompareToleranceScale = 2.0
    /// Larger values reduce false positives but objects appear less grounded.
    const float compareToleranceScale = 2.0f;
    const float compareTolerance = abs(rayDepth.z - rayStart.z) * step * compareToleranceScale;

    float sampleTime = stepOffset * step + step;

    /// @ref Screen-space ray cast —33 — sample start depth to avoid self-intersection
    /// Convert NDC to UV for depth buffer lookup.
    /// UzEngine: NDC.xy [-1,1] -> UV [0,1], with Y flip (D3D convention: UV.y=0 at top).
    float2 startUV = float2(rayStart.x * 0.5f + 0.5f, -rayStart.y * 0.5f + 0.5f);
    float startDepth = depthTex.SampleLevel(depthSampler, startUV, 0).r;

    /// @ref Screen-space ray cast —36-62 — ray march loop
    [unroll]
    for (int i = 0; i < numSteps; i++) {
        float3 samplePos = rayStart + rayStep * sampleTime;

        // Convert NDC sample position to UV for depth lookup
        float2 sampleUV = float2(samplePos.x * 0.5f + 0.5f, -samplePos.y * 0.5f + 0.5f);
        float sampleDepth = depthTex.SampleLevel(depthSampler, sampleUV, 0).r;

        /// @ref Screen-space ray cast —45 — skip self-intersection with start pixel
        if (sampleDepth != startDepth) {
            /// @ref Screen-space ray cast —47-48 — depth comparison with tolerance
            float depthDiff = samplePos.z - sampleDepth;
            bool bHit = abs(depthDiff + compareTolerance) < compareTolerance;

            if (bHit) {
                /// @ref Screen-space ray cast —55 — off-screen masking
                bool bValidPos = all(samplePos.xy > -1.0f) && all(samplePos.xy < 1.0f);
                return bValidPos ? (rayLength * sampleTime) : -1.0f;
            }
        }

        sampleTime += step;
    }

    return -1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Contact Shadow Application
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Contact shadow technique —156-229 — ApplyContactShadowWithShadowTerms
/// @ref Contact shadow technique —168-174 — ContactShadowLength computation
/// @ref Contact shadow technique —193-223 — ray cast and shadow application
/// @constants CastingIntensity = 1.0 (LightRendering.cpp:641)
///            NumSteps = 8 (DeferredLightingCommon.ush:197)
/// @deviations reference has per-material CastContactShadow flag (GBuffer stencil bit) and
///   per-shading-model overrides (Hair=0.2, Eye=0.5). UzEngine applies uniformly to all
///   pixels — no per-pixel CastContactShadow check. reference also has subsurface exponential
///   attenuation (DeferredLightingCommon.ush:211-217) which is omitted until SSS is implemented.
///   UzEngine uses viewDepth (camera distance) to scale screen-space fraction to world-space
///   ray length, which is equivalent to the reference engine's GetScreenRayLengthMultiplierForProjectionType
///   (Common.ush:1506-1509) for perspective projection.
///
/// Computes contact shadow for a single light. Returns a shadow factor [0,1]
/// where 0 = fully shadowed and 1 = fully lit.
///
/// # Parameters
/// - worldPos:            world-space surface position.
/// - lightDir:            world-space light direction (toward light, normalized).
/// - contactShadowLength: screen-space fraction (0.02 = 2% of screen). 0 = off.
/// - viewDepth:           camera-space distance to the surface (for screen-to-world scale).
/// - pixelPos:            SV_Position.xy for dither noise.
/// - frameCount:          frame index for temporal noise variation.
/// - vp:                  view-projection matrix.
/// - depthTex:            scene depth buffer (R32_FLOAT).
/// - depthSampler:        sampler for depth lookups.
///
/// # Returns
/// Contact shadow factor: 1.0 = fully lit, 0.0 = fully shadowed.
float ComputeContactShadow(
    float3 worldPos, float3 lightDir, float contactShadowLength,
    float viewDepth, float2 pixelPos, float frameCount,
    row_major float4x4 vp,
    Texture2D<float> depthTex, SamplerState depthSampler)
{
    // Early out if contact shadows disabled
    if (contactShadowLength <= 0.0f) {
        return 1.0f;
    }

    /// @ref Contact shadow technique —169 — ContactShadowLengthScreenScale
    /// @ref Standard projection —1506-1509 — GetScreenRayLengthMultiplierForProjectionType
    /// For perspective projection, screen-space fraction maps to world-space distance
    /// proportional to viewDepth. reference precomputes this via ScreenRayLengthMultiplier
    /// which for perspective is effectively: SceneDepth * ViewToClip[0][0]^-1 (half FOV tangent).
    /// We use viewDepth directly as the scale factor (the screen fraction already accounts
    /// for the projection geometry — a 2% screen fraction at 1000cm depth = 20cm ray).
    float rayLengthWorld = contactShadowLength * viewDepth;

    /// @ref Screen-space shadow CS —140 — InterleavedGradientNoise with StateFrameIndexMod8
    float dither = InterleavedGradientNoise(pixelPos + 0.5f, fmod(frameCount, 8.0f));

    /// @ref Contact shadow technique —197 — ShadowRayCast with 8 steps
    /// @ref Screen-space shadow CS —143 — CastScreenSpaceShadowRay with 8 steps
    float hitDistance = CastContactShadowRay(
        worldPos, lightDir, rayLengthWorld, 8,
        dither, vp, depthTex, depthSampler);

    /// @ref Contact shadow technique —199-219 — hit result application
    /// ContactShadowCastingIntensity = 1.0 (default)
    /// @deviations reference returns binary hit * ContactShadowCastingIntensity (per-light param).
    ///   UzEngine does not yet have per-light ContactShadowCastingIntensity, so we apply a
    ///   smooth falloff based on hit position along the ray (closer hit = stronger shadow,
    ///   far hit along ray = weaker shadow) and a camera distance fade. This matches the
    ///   visual quality of PoC1 (keijiro-style contact shadows) while maintaining the reference implementation's
    ///   ray cast logic. When per-light intensity is added, this can be replaced with the
    ///   exact reference binary approach modulated by ContactShadowCastingIntensity.
    if (hitDistance > 0.0f) {
        // Smooth falloff: hits earlier along the ray (closer to surface) produce stronger
        // shadows; hits near the end of the ray produce weaker shadows. This prevents the
        // hard binary edges that occur with a flat 0/1 result.
        // t=0 at ray origin (strongest shadow), t=1 at ray end (weakest shadow).
        float t = saturate(hitDistance / rayLengthWorld);
        float contactOcclusion = 1.0f - smoothstep(0.0f, 1.0f, t);

        // Camera distance fade: contact shadows are a close-range detail effect.
        // Fade out over 5000 cm (50 m) to avoid artifacts at distance where screen-space
        // ray marching becomes unreliable due to limited step count.
        // PoC1 used 50.0 (view-space units = cm in UzEngine).
        // equivalent: ContactShadowLength itself scales with depth via
        // GetScreenRayLengthMultiplierForProjectionType, so distant surfaces naturally get
        // longer rays with fewer effective steps. The distance fade compensates for this.
        const float contactShadowFadeDistance = 5000.0f; // 50 m in cm
        float distFade = 1.0f - saturate(viewDepth / contactShadowFadeDistance);
        contactOcclusion *= distFade;

        return 1.0f - contactOcclusion;
    }

    return 1.0f;
}

#endif // CONTACT_SHADOWS_HLSLI
