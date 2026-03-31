/// UzEngine Parallax Occlusion Mapping.
///
/// Steep parallax with linear interpolation and self-shadowing.
/// Supports both bindless (SM6.6 ResourceDescriptorHeap) and traditional
/// texture binding via compile-time UZ_BINDLESS define.

#ifndef UZ_MATERIAL_POM_HLSLI
#define UZ_MATERIAL_POM_HLSLI

#include "../common/constants.hlsli"

// ─── Bindless texture sampling abstraction ───────────────────────────────────

// When UZ_BINDLESS is defined, textures are accessed via ResourceDescriptorHeap
// using a uint index. Otherwise, a Texture2D is passed directly.
// The POM functions below provide overloads for both paths.

#ifdef UZ_BINDLESS
#include "../common/bindless.hlsli"
#endif

// ─── Internal: Sample heightmap ──────────────────────────────────────────────

/// Sample height value from a heightmap texture (bindless path).
/// Returns inverted height (1 - texel.r) because POM traces from the surface
/// downward: depth 0 = surface, depth 1 = deepest point.
#ifdef UZ_BINDLESS
float SampleHeight_Bindless(uint heightmapIndex, SamplerState samp, float2 uv)
{
    return 1.0f - UzSampleTexture2DLevel(heightmapIndex, samp, uv, 0).r;
}
#endif

/// Sample height value from a heightmap texture (traditional path).
float SampleHeight_Tex(Texture2D heightmap, SamplerState samp, float2 uv)
{
    return 1.0f - heightmap.SampleLevel(samp, uv, 0).r;
}

// ─── Parallax Occlusion Mapping ──────────────────────────────────────────────

/// Parallax Occlusion Mapping with steep parallax + linear interpolation.
///
/// Marches rays through the heightfield in tangent space, finding the
/// intersection between the view ray and the displaced surface. Uses
/// linear interpolation between the last two layers for sub-layer accuracy.
///
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping with Approximate
///      Soft Shadows for Detailed Surface Rendering" (SIGGRAPH 2006 Course)
/// @constants maxLoopIterations=64 (hard safety limit to prevent GPU hang)
/// @deviations NONE
///
/// @param uv             Input texture coordinates.
/// @param viewDirTS      View direction in tangent space (must NOT be normalized
///                       to unit length — keep the original TS transform so
///                       viewDirTS.z is the cosine with the surface normal).
/// @param heightScale    Maximum displacement depth in UV-space units.
/// @param minLayers      Minimum number of layers (used at perpendicular view).
/// @param maxLayers      Maximum number of layers (used at grazing view).
/// @param heightmapIndex Bindless descriptor index of the heightmap texture.
/// @param samp           Sampler state for heightmap sampling.
/// @return               Displaced UV coordinates.
#ifdef UZ_BINDLESS
float2 ParallaxOcclusionMapping(
    float2       uv,
    float3       viewDirTS,
    float        heightScale,
    float        minLayers,
    float        maxLayers,
    uint         heightmapIndex,
    SamplerState samp)
{
    // Layer count increases at grazing angles for better accuracy
    float numLayers = lerp(maxLayers, minLayers, abs(viewDirTS.z));
    float layerDepth = 1.0f / numLayers;
    float currentLayerDepth = 0.0f;

    // UV offset per layer step — viewDirTS.xy / viewDirTS.z projects the
    // view ray onto the tangent plane, then we scale by heightScale
    float2 deltaUV = viewDirTS.xy / viewDirTS.z * heightScale / numLayers;

    float2 currentUV = uv;
    float currentDepth = SampleHeight_Bindless(heightmapIndex, samp, currentUV);

    // Steep parallax: march until the ray goes below the heightfield
    [loop]
    for (int i = 0; i < 64 && currentLayerDepth < currentDepth; ++i)
    {
        currentUV -= deltaUV;
        currentDepth = SampleHeight_Bindless(heightmapIndex, samp, currentUV);
        currentLayerDepth += layerDepth;
    }

    // Linear interpolation between the last two layers for sub-texel accuracy
    float2 prevUV = currentUV + deltaUV;
    float afterDepth  = currentDepth - currentLayerDepth;
    float beforeDepth = SampleHeight_Bindless(heightmapIndex, samp, prevUV)
                        - currentLayerDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth);

    return lerp(currentUV, prevUV, weight);
}
#endif // UZ_BINDLESS

/// Parallax Occlusion Mapping (traditional Texture2D path).
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping"
/// @constants maxLoopIterations=64
/// @deviations NONE
float2 ParallaxOcclusionMapping(
    float2       uv,
    float3       viewDirTS,
    float        heightScale,
    float        minLayers,
    float        maxLayers,
    Texture2D    heightmap,
    SamplerState samp)
{
    float numLayers = lerp(maxLayers, minLayers, abs(viewDirTS.z));
    float layerDepth = 1.0f / numLayers;
    float currentLayerDepth = 0.0f;

    float2 deltaUV = viewDirTS.xy / viewDirTS.z * heightScale / numLayers;

    float2 currentUV = uv;
    float currentDepth = SampleHeight_Tex(heightmap, samp, currentUV);

    [loop]
    for (int i = 0; i < 64 && currentLayerDepth < currentDepth; ++i)
    {
        currentUV -= deltaUV;
        currentDepth = SampleHeight_Tex(heightmap, samp, currentUV);
        currentLayerDepth += layerDepth;
    }

    float2 prevUV = currentUV + deltaUV;
    float afterDepth  = currentDepth - currentLayerDepth;
    float beforeDepth = SampleHeight_Tex(heightmap, samp, prevUV)
                        - currentLayerDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth);

    return lerp(currentUV, prevUV, weight);
}

// ─── POM Self-Shadowing ──────────────────────────────────────────────────────

/// Trace from the displaced surface point toward the light to determine
/// self-shadowing. Returns a shadow factor in [0, 1] where 0 = fully
/// shadowed and 1 = fully lit.
///
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping with Approximate
///      Soft Shadows for Detailed Surface Rendering"
/// @constants None
/// @deviations NONE
///
/// @param uv             Displaced UV (output of ParallaxOcclusionMapping).
/// @param lightDirTS     Light direction in tangent space.
/// @param heightScale    Maximum displacement depth (same as POM).
/// @param heightmapIndex Bindless descriptor index of the heightmap texture.
/// @param samp           Sampler state.
/// @param numSamples     Number of trace steps (higher = softer shadows).
/// @return               Shadow factor in [0, 1].
#ifdef UZ_BINDLESS
float PomSelfShadow(
    float2       uv,
    float3       lightDirTS,
    float        heightScale,
    uint         heightmapIndex,
    SamplerState samp,
    int          numSamples)
{
    // Current depth at the displaced point
    float initialDepth = SampleHeight_Bindless(heightmapIndex, samp, uv);

    // Step size and UV delta toward the light
    float layerDepth = initialDepth / max(float(numSamples), 1.0f);
    float2 deltaUV = lightDirTS.xy / lightDirTS.z * heightScale / max(float(numSamples), 1.0f);

    float2 currentUV = uv;
    float currentLayerDepth = initialDepth;
    float shadow = 1.0f;

    [loop]
    for (int i = 0; i < numSamples; ++i)
    {
        currentUV += deltaUV;
        currentLayerDepth -= layerDepth;

        float sampledDepth = SampleHeight_Bindless(heightmapIndex, samp, currentUV);

        // If the heightfield is above the current trace point, we're in shadow
        if (sampledDepth > currentLayerDepth)
        {
            // Soft shadow: larger occlusion at deeper intersections
            float occluder = (sampledDepth - currentLayerDepth);
            shadow = min(shadow, 1.0f - occluder);
        }
    }

    return max(shadow, 0.0f);
}
#endif // UZ_BINDLESS

/// POM self-shadowing (traditional Texture2D path).
/// @ref Tatarchuk 2006 "Practical Parallax Occlusion Mapping"
/// @constants None
/// @deviations NONE
float PomSelfShadow(
    float2       uv,
    float3       lightDirTS,
    float        heightScale,
    Texture2D    heightmap,
    SamplerState samp,
    int          numSamples)
{
    float initialDepth = SampleHeight_Tex(heightmap, samp, uv);

    float layerDepth = initialDepth / max(float(numSamples), 1.0f);
    float2 deltaUV = lightDirTS.xy / lightDirTS.z * heightScale / max(float(numSamples), 1.0f);

    float2 currentUV = uv;
    float currentLayerDepth = initialDepth;
    float shadow = 1.0f;

    [loop]
    for (int i = 0; i < numSamples; ++i)
    {
        currentUV += deltaUV;
        currentLayerDepth -= layerDepth;

        float sampledDepth = SampleHeight_Tex(heightmap, samp, currentUV);

        if (sampledDepth > currentLayerDepth)
        {
            float occluder = (sampledDepth - currentLayerDepth);
            shadow = min(shadow, 1.0f - occluder);
        }
    }

    return max(shadow, 0.0f);
}

#endif // UZ_MATERIAL_POM_HLSLI
