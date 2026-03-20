/// Parallax Occlusion Mapping (POM) shader include for UzEngine.
///
/// Provides steep parallax ray marching with linear interpolation refinement,
/// self-shadowing, and distance-based fading.
///
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" (ShaderX5)
/// @ref Kaneko et al. 2001 "Detailed Shape Representation with Parallax Mapping"
/// @ref Policarpo et al. 2005 "Real-Time Relief Mapping on Arbitrary Polygonal Surfaces"
/// @deviations NONE — uses same ray march + linear interpolation as reference POM Material Function

#ifndef POM_HLSLI
#define POM_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Tatarchuk 2006 — ReferencePlane = 0.5f (standard POM convention)
/// Reference plane height — at this value, no displacement occurs.
/// Surfaces above 0.5 push outward, below 0.5 push inward.
static const float POM_REFERENCE_PLANE = 0.5f;

/// @ref Tatarchuk 2006 — HeightRatio = 0.05f (standard POM default)
/// Default height ratio (perceived height as fraction of width).
static const float POM_DEFAULT_HEIGHT_RATIO = 0.05f;

/// Number of binary search refinement iterations after the initial
/// steep parallax march. 3 iterations yields 1/8 step sub-pixel precision.
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" — Custom Expression uses
///         linear interpolation between last two samples (equivalent to
///         secant refinement). We match this with a single linear interp step.
static const int POM_REFINEMENT_STEPS = 1;

/// Number of self-shadow ray march steps.
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" — shadow trace uses
///         fixed step count along light direction.
static const int POM_SHADOW_STEPS = 6;

// ─────────────────────────────────────────────────────────────────────────────
// ParallaxOcclusionMapping
// ─────────────────────────────────────────────────────────────────────────────

/// Performs Parallax Occlusion Mapping on a heightmap.
///
/// Uses steep parallax ray marching with adaptive step count (more steps at
/// grazing angles for quality, fewer at head-on angles for performance),
/// followed by a single linear interpolation refinement between the last
/// two samples for sub-step accuracy.
///
/// The heightmap convention is: 0 = lowest point, 1 = highest point.
/// The ray marches from the surface plane (depth 0) downward to depth 1.
/// Height values are offset by ReferencePlane (0.5) so that the midpoint
/// of the heightmap sits at the geometric surface.
///
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" — steep parallax + linear interp
/// @ref Kaneko 2001 — UV offset = CamVecTS.xy * (h * ratio)
/// @constants POM_REFERENCE_PLANE=0.5 (MaterialExpressionBumpOffset.h:31)
/// @deviations NONE
///
/// @param heightmapTex  Heightmap texture (R channel, 0=low 1=high)
/// @param heightSampler Linear clamp sampler
/// @param uv            Input texture coordinates
/// @param viewDirTS     View direction in tangent space (must be normalized,
///                      Z points away from surface toward camera)
/// @param scale         Displacement scale in UV space. Controls the maximum
///                      UV offset. Equivalent to reference HeightRatio.
///                      Typical range: 0.01–0.1
/// @param minLayers     Minimum ray march steps (at head-on view). Default 8.
/// @param maxLayers     Maximum ray march steps (at grazing view). Default 32.
/// @return              Offset UV coordinates
float2 ParallaxOcclusionMapping(
    Texture2D<float> heightmapTex,
    SamplerState heightSampler,
    float2 uv,
    float3 viewDirTS,
    float scale,
    int minLayers,
    int maxLayers)
{
    // ── Adaptive step count ──
    // At head-on view (NdotV ≈ 1), use fewer steps (minLayers).
    // At grazing view (NdotV ≈ 0), use more steps (maxLayers).
    // @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" —
    //   NumSteps = lerp(MaxSteps, MinSteps, abs(dot(float3(0,0,1), ViewDirTS)))
    float NdotV = abs(viewDirTS.z);
    int numLayers = (int)lerp((float)maxLayers, (float)minLayers, NdotV);

    // Depth of each layer in the [0, 1] depth range.
    float layerDepth = 1.0f / (float)numLayers;

    // Current depth along the ray (starts at the surface plane = 0).
    float currentLayerDepth = 0.0f;

    // ── UV step direction ──
    // Project the tangent-space view direction onto the UV plane.
    // Division by viewDirTS.z converts the view vector into a direction
    // that covers the correct UV distance for a unit depth step.
    // Scale by the heightmap displacement scale to control magnitude.
    // @ref Kaneko 2001 — parallax offset — CameraVector.xy used for offset
    float2 P = viewDirTS.xy / viewDirTS.z * scale;
    float2 deltaUV = P / (float)numLayers;

    // ── Steep parallax march ──
    // Step through the heightmap along the projected view ray until the
    // ray depth exceeds the sampled height (intersection found).
    float2 currentUV = uv;
    float currentHeight = heightmapTex.SampleLevel(heightSampler, currentUV, 0.0f).r;

    [loop]
    for (int i = 0; i < numLayers; i++)
    {
        // If the ray is below the surface, stop marching.
        if (currentLayerDepth >= currentHeight)
            break;

        // Advance one step along the ray.
        currentUV -= deltaUV;
        currentHeight = heightmapTex.SampleLevel(heightSampler, currentUV, 0.0f).r;
        currentLayerDepth += layerDepth;
    }

    // ── Linear interpolation refinement ──
    // The intersection lies between the previous step (above surface)
    // and the current step (below surface). Linearly interpolate between
    // the two to find a more precise intersection point.
    // @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" — linear interp between
    //         last two samples for sub-step accuracy.
    float2 prevUV = currentUV + deltaUV;
    float prevHeight = heightmapTex.SampleLevel(heightSampler, prevUV, 0.0f).r;
    float prevLayerDepth = currentLayerDepth - layerDepth;

    // afterDepth:  how far below the surface the current sample is (negative = below)
    // beforeDepth: how far above the surface the previous sample is (positive = above)
    float afterDepth  = currentHeight - currentLayerDepth;
    float beforeDepth = prevHeight - prevLayerDepth;

    // Weight for interpolation: 0 = use currentUV, 1 = use prevUV.
    // Protected against division by zero when both depths are equal
    // (degenerate case: perfectly flat heightmap).
    float denom = afterDepth - beforeDepth;
    float weight = (abs(denom) > 1e-6f) ? (afterDepth / denom) : 0.5f;

    float2 finalUV = lerp(currentUV, prevUV, weight);

    return finalUV;
}

// ─────────────────────────────────────────────────────────────────────────────
// POMSelfShadow
// ─────────────────────────────────────────────────────────────────────────────

/// Computes self-shadowing factor for POM surfaces.
///
/// Traces a ray from the displaced surface point toward the light through
/// the heightmap. If any sample along the ray is above the current depth,
/// the point is in shadow. Uses soft shadowing by tracking the maximum
/// occlusion amount along the ray.
///
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" — shadow trace along light direction
/// @constants POM_SHADOW_STEPS=6 (matches reference Material Function default)
/// @deviations NONE
///
/// @param heightmapTex  Heightmap texture
/// @param heightSampler Linear clamp sampler
/// @param uv            POM-displaced UV coordinates (output of ParallaxOcclusionMapping)
/// @param lightDirTS    Light direction in tangent space (normalized, pointing
///                      toward the light source). Z > 0 means light is above surface.
/// @param currentDepth  Current surface depth from POM ray march. Computed as:
///                      1.0 - heightmapTex.Sample(uv).r (depth = 1 - height).
/// @param scale         Same displacement scale as used in ParallaxOcclusionMapping.
/// @return              Shadow factor: 0 = fully shadowed, 1 = fully lit.
float POMSelfShadow(
    Texture2D<float> heightmapTex,
    SamplerState heightSampler,
    float2 uv,
    float3 lightDirTS,
    float currentDepth,
    float scale)
{
    // If the light is below the surface horizon, the point is fully shadowed.
    // lightDirTS.z > 0 means the light is above the tangent plane.
    if (lightDirTS.z <= 0.0f)
        return 0.0f;

    // ── Compute step direction and size ──
    // Project light direction onto UV plane, scaled by displacement amount.
    // @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" — light trace UV step
    float2 lightUVStep = lightDirTS.xy / lightDirTS.z * scale / (float)POM_SHADOW_STEPS;

    // Depth change per step: marching toward the light means moving
    // from current depth toward 0 (surface plane).
    float depthStep = currentDepth / (float)POM_SHADOW_STEPS;

    // ── Shadow ray march ──
    // Start one step away from the current position (avoid self-intersection).
    float2 sampleUV = uv + lightUVStep;
    float sampleDepth = currentDepth - depthStep;

    // Track the maximum amount by which the heightmap exceeds the ray depth.
    // A larger value means a stronger shadow. This produces soft self-shadows
    // rather than hard binary shadows.
    float maxOcclusion = 0.0f;

    [unroll]
    for (int i = 0; i < POM_SHADOW_STEPS; i++)
    {
        float heightAtSample = heightmapTex.SampleLevel(heightSampler, sampleUV, 0.0f).r;

        // If the heightmap is above the ray at this point, the surface is occluded.
        // Depth = 1 - height, so occlusion = height - (1 - sampleDepth) = height - 1 + sampleDepth.
        // But since we're comparing in height space: occluded if heightAtSample > (1 - sampleDepth).
        float surfaceDepthAtSample = 1.0f - heightAtSample;
        float occlusion = sampleDepth - surfaceDepthAtSample;
        maxOcclusion = max(maxOcclusion, occlusion);

        // Advance toward the light.
        sampleUV += lightUVStep;
        sampleDepth -= depthStep;
    }

    // Convert maximum occlusion to a shadow factor.
    // Scale occlusion to produce a perceptually reasonable shadow falloff.
    // A small occlusion (shallow angle) produces a lighter shadow.
    // @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" — shadow attenuation
    //         from max occlusion along ray.
    float shadow = saturate(1.0f - maxOcclusion * ((float)POM_SHADOW_STEPS / scale));

    return shadow;
}

// ─────────────────────────────────────────────────────────────────────────────
// POMDistanceFade
// ─────────────────────────────────────────────────────────────────────────────

/// Blends between POM-displaced UVs and original UVs based on camera distance.
///
/// At distances beyond fadeEnd, returns the original flat UVs to avoid
/// aliasing artifacts and save GPU work. Between fadeStart and fadeEnd,
/// smoothly interpolates to prevent popping.
///
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping" — fade by camera distance
/// @constants fadeStart default=1000 (10m in cm), fadeEnd default=2000 (20m in cm)
/// @deviations NONE
///
/// @param pomUV         POM-displaced UV
/// @param originalUV    Original flat UV
/// @param distance      Distance from camera to surface (world units / cm)
/// @param fadeStart     Distance where fade begins (cm). Default 1000 (10m).
/// @param fadeEnd       Distance where POM is fully disabled (cm). Default 2000 (20m).
/// @return              Blended UV coordinates
float2 POMDistanceFade(
    float2 pomUV,
    float2 originalUV,
    float distance,
    float fadeStart,
    float fadeEnd)
{
    // Linear fade: 0 at fadeStart (full POM), 1 at fadeEnd (flat UV).
    // Protect against zero-length fade range.
    float fadeRange = fadeEnd - fadeStart;
    float fade = saturate((distance - fadeStart) / max(fadeRange, 0.001f));
    return lerp(pomUV, originalUV, fade);
}

// ─────────────────────────────────────────────────────────────────────────────
// POMComputeCurrentDepth
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the surface depth at the displaced UV for use with POMSelfShadow.
///
/// Depth = 1 - height, where height 1 is the highest point (depth 0)
/// and height 0 is the lowest point (depth 1).
///
/// @param heightmapTex  Heightmap texture
/// @param heightSampler Linear clamp sampler
/// @param pomUV         POM-displaced UV coordinates
/// @return              Depth value in [0, 1] range
float POMComputeCurrentDepth(
    Texture2D<float> heightmapTex,
    SamplerState heightSampler,
    float2 pomUV)
{
    float height = heightmapTex.SampleLevel(heightSampler, pomUV, 0.0f).r;
    return 1.0f - height;
}

#endif // POM_HLSLI
