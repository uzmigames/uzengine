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
// Screen-Space Contact Shadow Ray Cast (Accumulated Occlusion)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref keijiro "ContactShadows" — accumulated occlusion contact shadow technique
/// @ref iq 2013 "Raymarching Primitives" — soft shadow via min/max over full march
/// @ref Screen-space contact shadow ray cast — NDC projection, depth comparison
/// @constants CompareToleranceScale = 2.0 (DeferredLightingCommon.ush:68)
///            RejectionScale = 4.0 (keijiro ContactShadows rejectionDepth ratio)
///            MinThicknessBase = 0.01 (keijiro ContactShadows threshold base)
///            Step = 1.0 / NumSteps
/// @deviations UzEngine uses viewProj instead of TranslatedWorldToClip. UzEngine does
///   NDC-to-UV conversion inline (ndc*0.5+0.5, flip Y) instead of ScreenPositionScaleBias.
///   UzEngine samples depth texture directly instead of SceneTexturesStruct.
///   UzEngine uses accumulated max-occlusion (keijiro/iq style) instead of first-hit binary.
///
/// Casts a ray from the world-space surface position along the light direction
/// in clip/NDC space. Instead of returning on the first hit, this marches the
/// ENTIRE ray and accumulates the maximum occlusion weight across all steps.
/// Each hit's weight is based on:
///   - depth difference (closer behind surface = stronger shadow)
///   - position along ray (earlier hits = stronger shadow, fading toward ray end)
/// The darkest point along the ray determines the final occlusion, matching the
/// keijiro/iq approach of accumulating over the full march for robust contact
/// darkness at object intersections.
///
/// # Parameters
/// - worldPos:     world-space surface position.
/// - lightDir:     world-space light direction (normalized, pointing toward light).
/// - rayLength:    world-space ray length (computed from contactShadowLength).
/// - numSteps:     number of ray march steps (Default: 8).
/// - dither:       per-pixel dither value [0,1] from InterleavedGradientNoise.
/// - vp:           view-projection matrix (combined).
/// - depthTex:     depth buffer texture (R32_FLOAT, device Z).
/// - depthSampler: point/linear sampler for depth lookups.
///
/// # Returns
/// Occlusion value: 0.0 = fully lit (no contact shadow), 1.0 = fully shadowed.
float CastContactShadowRay(
    float3 worldPos, float3 lightDir, float rayLength, int numSteps,
    float dither, row_major float4x4 vp,
    Texture2D<float> depthTex, SamplerState depthSampler)
{
    /// @ref Screen-space ray cast — project ray start and end to clip space
    float4 rayStartClip = mul(float4(worldPos, 1.0f), vp);
    float4 rayDirClip   = mul(float4(lightDir * rayLength, 0.0f), vp);
    float4 rayEndClip   = rayStartClip + rayDirClip;

    /// @ref Screen-space ray cast — perspective divide to NDC
    float3 rayStart = rayStartClip.xyz / rayStartClip.w;
    float3 rayEnd   = rayEndClip.xyz / rayEndClip.w;

    float3 rayStep = rayEnd - rayStart;

    /// @ref Screen-space ray cast — compute depth tolerance from ray's NDC depth range
    /// Project a pure Z offset by rayLength to get the depth range of the ray in NDC.
    float4 rayDepthClip = rayStartClip + mul(float4(0, 0, rayLength, 0), vp);
    float3 rayDepth = rayDepthClip.xyz / rayDepthClip.w;

    /// @ref Screen-space ray cast — step offset and tolerance
    const float stepOffset = dither - 0.5f;
    const float step = 1.0f / (float)numSteps;

    /// @ref Contact shadow technique — CompareToleranceScale = 2.0
    const float compareToleranceScale = 2.0f;
    const float compareTolerance = abs(rayDepth.z - rayStart.z) * step * compareToleranceScale;

    /// @ref keijiro ContactShadows — rejection depth is a multiple of the compare tolerance.
    /// Samples deeper than this are behind thick geometry (walls, floors) and should not
    /// contribute to contact shadows. The factor of 4 gives enough range to catch contact
    /// regions without bleeding through walls.
    const float rejectionDepth = compareTolerance * 4.0f;

    float sampleTime = stepOffset * step + step;

    /// @ref Screen-space ray cast — sample start depth to avoid self-intersection
    /// Convert NDC to UV for depth buffer lookup.
    /// UzEngine: NDC.xy [-1,1] -> UV [0,1], with Y flip (D3D convention: UV.y=0 at top).
    float2 startUV = float2(rayStart.x * 0.5f + 0.5f, -rayStart.y * 0.5f + 0.5f);
    float startDepth = depthTex.SampleLevel(depthSampler, startUV, 0).r;

    /// @ref keijiro ContactShadows — accumulated occlusion over full ray march
    /// @ref iq 2013 — darkest point along ray wins (max occlusion, not first hit)
    float occlusion = 0.0f;

    [loop]
    for (int i = 0; i < numSteps; i++) {
        float3 samplePos = rayStart + rayStep * sampleTime;

        // Convert NDC sample position to UV for depth lookup
        float2 sampleUV = float2(samplePos.x * 0.5f + 0.5f, -samplePos.y * 0.5f + 0.5f);

        // Break early if ray leaves screen bounds (no valid depth data beyond edges)
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f)) break;

        float sampleDepth = depthTex.SampleLevel(depthSampler, sampleUV, 0).r;

        /// @ref Screen-space ray cast — skip self-intersection with start pixel
        if (sampleDepth != startDepth) {
            /// @ref keijiro ContactShadows — depth difference and occlusion weighting
            /// Reversed-Z: higher z = closer to camera. When ray passes behind a surface,
            /// sampleDepth > samplePos.z, so we compute (sampleDepth - samplePos.z) to get
            /// a positive value for occlusion (matching PoC1 view-space convention).
            float depthDiff = sampleDepth - samplePos.z;

            // Position along ray normalized to [0,1]: 0 = ray start, 1 = ray end
            float offs = (float)i / (float)numSteps;

            /// @ref keijiro ContactShadows — dynamic minimum thickness threshold
            /// Near the ray start (offs~0), require a larger depth difference to avoid
            /// self-shadowing noise. Near the ray end (offs~1), accept thinner features.
            float minThickness = 0.01f * (1.0f - offs);

            if (depthDiff > minThickness && depthDiff < rejectionDepth) {
                /// @ref keijiro ContactShadows — weight = proximity to surface * closeness to origin
                /// smoothstep ramps from 1.0 at the surface to 0.0 at rejection depth,
                /// then modulated by (1-offs) so early-ray hits dominate.
                float weight = smoothstep(0.0f, rejectionDepth, depthDiff);
                weight = (1.0f - weight) * (1.0f - offs);
                occlusion = max(occlusion, weight);
            }
        }

        sampleTime += step;
    }

    /// @ref iq 2013 "Raymarching Primitives" — final smoothstep on accumulated occlusion
    /// Applies a soft curve to the max occlusion: res*res*(3-2*res). This smooths the
    /// transition between lit and shadowed regions, preventing hard shadow edges from
    /// individual sample weights.
    occlusion = occlusion * occlusion * (3.0f - 2.0f * occlusion);

    return occlusion;
}

// ─────────────────────────────────────────────────────────────────────────────
// Contact Shadow Application
// ─────────────────────────────────────────────────────────────────────────────

/// @ref keijiro "ContactShadows" — accumulated occlusion application
/// @ref iq 2013 "Raymarching Primitives" — soft shadow composition
/// @ref Contact shadow technique — ContactShadowLength computation, distance fade
/// @constants CastingIntensity = 1.0 (ContactShadowCastingIntensity default)
///            NumSteps = 8 (standard contact shadow step count)
///            FadeDistance = 5000.0 cm (50 m, screen-space reliability limit)
/// @deviations reference has per-material CastContactShadow flag (GBuffer stencil bit) and
///   per-shading-model overrides (Hair=0.2, Eye=0.5). UzEngine applies uniformly to all
///   pixels — no per-pixel CastContactShadow check. reference also has subsurface exponential
///   attenuation which is omitted until SSS is implemented.
///   UzEngine uses accumulated max-occlusion (keijiro/iq style) instead of binary hit.
///
/// Computes contact shadow for a single light. Returns a shadow factor [0,1]
/// where 0 = fully shadowed and 1 = fully lit.
///
/// The ray cast now returns accumulated occlusion (0=lit, 1=shadowed) computed
/// over the full march, weighted by depth proximity and ray position. This is
/// applied directly with a camera distance fade — no additional smoothstep
/// falloff is needed because the accumulation already produces smooth gradients.
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

    /// @ref Contact shadow technique — ContactShadowLengthScreenScale
    /// @ref Standard projection — GetScreenRayLengthMultiplierForProjectionType
    /// For perspective projection, screen-space fraction maps to world-space distance
    /// proportional to viewDepth. A 2% screen fraction at 1000cm depth = 20cm ray.
    float rayLengthWorld = contactShadowLength * viewDepth;

    /// @ref Jimenez 2016 — InterleavedGradientNoise with StateFrameIndexMod8
    float dither = InterleavedGradientNoise(pixelPos + 0.5f, fmod(frameCount, 8.0f));

    /// @ref keijiro ContactShadows — ray cast with accumulated occlusion, 12 steps
    /// CastContactShadowRay now returns occlusion (0=lit, 1=shadowed) instead of hit distance.
    /// 12 steps (up from 8) gives finer resolution at contact points where thin
    /// occlusion geometry requires tighter depth sampling.
    float occlusion = CastContactShadowRay(
        worldPos, lightDir, rayLengthWorld, 12,
        dither, vp, depthTex, depthSampler);

    /// @ref Contact shadow technique — camera distance fade
    /// Contact shadows are a close-range detail effect. Fade out over 5000 cm (50 m) to
    /// avoid artifacts at distance where screen-space ray marching becomes unreliable due
    /// to limited step count. ContactShadowLength itself scales with depth via
    /// GetScreenRayLengthMultiplierForProjectionType, so distant surfaces naturally get
    /// longer rays with fewer effective steps. The distance fade compensates for this.
    const float contactShadowFadeDistance = 5000.0f; // 50 m in cm
    float distFade = 1.0f - saturate(viewDepth / contactShadowFadeDistance);

    // Apply occlusion with distance fade. The accumulated occlusion from the ray cast
    // already has smooth gradients from the keijiro weighting + iq smoothstep, so no
    // additional falloff conversion is needed.
    return 1.0f - occlusion * distFade;
}

#endif // CONTACT_SHADOWS_HLSLI
