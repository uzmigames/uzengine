<!-- doc-meta
title: UE5 Height Fog Shader Code — Exact Snippets
created: 2026-03-19
category: rendering
tags: height-fog, shader-code, hlsl
-->

# UE5 Height Fog Shader Code — Exact Snippets for Reference

All code extracted from `W:/UE_5.6_Source/Engine/Shaders/Private/HeightFogCommon.ush`.

---

## FogStructData Definition (Lines 64-85)

```hlsl
struct FogStructData
{
    float4 ExponentialFogParameters;
    float4 ExponentialFogParameters2;
    float4 ExponentialFogColorParameter;
    float4 ExponentialFogParameters3;
    float4 SkyAtmosphereAmbientContributionColorScale;  // ← Sky atmosphere scale
    float4 InscatteringLightDirection;
    float4 DirectionalInscatteringColor;
    float2 SinCosInscatteringColorCubemapRotation;
    float3 FogInscatteringTextureParameters;
    float EndDistance;
    float ApplyVolumetricFog;
    float VolumetricFogStartDistance;
    float VolumetricFogNearFadeInDistanceInv;
    float3 VolumetricFogAlbedo;
    float VolumetricFogPhaseG;
};
```

---

## ComputeInscatteringColor Function (Lines 140-183)

This is THE function that adds sky atmosphere color to fog.

```hlsl
float3 ComputeInscatteringColor(float3 CameraToReceiver, float CameraToReceiverLength, FogStructData FogData, uint EyeIndex)
{
    half3 Inscattering = FogData.ExponentialFogColorParameter.xyz;

#if PERMUTATION_SUPPORT_FOG_INSCATTERING_TEXTURE
    BRANCH
    if (FogData.ExponentialFogParameters3.z > 0)
    {
        float FadeAlpha = saturate(CameraToReceiverLength * FogData.FogInscatteringTextureParameters.x + FogData.FogInscatteringTextureParameters.y);
        float3 CubemapLookupVector = CameraToReceiver;
        // Rotate around Z axis
        CubemapLookupVector.xy = float2(
            dot(CubemapLookupVector.xy, float2(FogData.SinCosInscatteringColorCubemapRotation.y, -FogData.SinCosInscatteringColorCubemapRotation.x)),
            dot(CubemapLookupVector.xy, FogData.SinCosInscatteringColorCubemapRotation.xy)
        );
        float3 DirectionalColor;
        float3 NonDirectionalColor;

        // Fetch cubemap at different mips for fade
        DirectionalColor = TextureCubeSampleLevel(FogStruct.FogInscatteringColorCubemap, SharedFogInColorScatteringSampler, CubemapLookupVector, 0).xyz;
        NonDirectionalColor = TextureCubeSampleLevel(FogStruct.FogInscatteringColorCubemap, SharedFogInColorScatteringSampler, CubemapLookupVector, FogData.FogInscatteringTextureParameters.z).xyz;
        Inscattering *= lerp(NonDirectionalColor, DirectionalColor, FadeAlpha);
    }
#endif

    // ===== SKY ATMOSPHERE CONTRIBUTION START =====
#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
    float3 SkyAmbient = FogData.SkyAtmosphereAmbientContributionColorScale.rgb  // Scale from fog component
                      * View.SkyAtmosphereHeightFogContribution.xxx              // Blend factor from sky component
                      * GetViewDistanceSkyLightColor();                          // Sky light color
    #if PROJECT_EXPFOG_MATCHES_VFOG
    SkyAmbient *= FogData.VolumetricFogAlbedo;
    #endif
    Inscattering += SkyAmbient;  // ← Add to inscattering
#endif
    // ===== SKY ATMOSPHERE CONTRIBUTION END =====

    return Inscattering;
}
```

**Key insight**: The fog color is `SkyAtmosphereAmbientContributionColorScale * SkyAtmosphereHeightFogContribution * SkyLightColor`.

---

## Directional Light Inscattering with Sky Atmosphere (Lines 304-345)

This is where the directional light gets the blue color from the atmosphere.

```hlsl
#if PERMUTATION_SUPPORT_FOG_DIRECTIONAL_LIGHT_INSCATTERING
    // if InscatteringLightDirection.w is negative then it's disabled,
    // otherwise it holds directional inscattering start distance
    BRANCH
    if (FogData.InscatteringLightDirection.w >= 0
    #if PERMUTATION_SUPPORT_FOG_INSCATTERING_TEXTURE
        && FogData.ExponentialFogParameters3.z == 0
    #endif
    )
    {
#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG
        const float UniformPhaseFunction = 1.0f / (4.0f * PI);
        half3 DirectionalInscatteringColor;
        half3 DirectionalLightInscattering;

        // ===== ATMOSPHERE LIGHT COLOR INJECTION START =====
        // This is where the blue/orange sun glow comes from!
        DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz
                                     + InView.SkyAtmosphereHeightFogContribution
                                     * InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb;

        DirectionalLightInscattering = DirectionalInscatteringColor
        #if PROJECT_EXPFOG_MATCHES_VFOG
                * HenyeyGreensteinPhase(FogData.VolumetricFogPhaseG, dot(-CameraToReceiverNormalized, InView.AtmosphereLightDirection[0].xyz));
        #else
                * pow(saturate(dot(CameraToReceiverNormalized, InView.AtmosphereLightDirection[0].xyz)), FogData.DirectionalInscatteringColor.w) * UniformPhaseFunction;
        #endif
        // ===== ATMOSPHERE LIGHT COLOR INJECTION END =====

        // Handle second atmosphere light if present
        if (InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[1].a > 0.0f)
        {
            DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz
                                         + InView.SkyAtmosphereHeightFogContribution
                                         * InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[1].rgb;
            DirectionalLightInscattering += DirectionalInscatteringColor
            #if PROJECT_EXPFOG_MATCHES_VFOG
                    * HenyeyGreensteinPhase(FogData.VolumetricFogPhaseG, dot(-CameraToReceiverNormalized, InView.AtmosphereLightDirection[1].xyz));
            #else
                    * pow(saturate(dot(CameraToReceiverNormalized, InView.AtmosphereLightDirection[1].xyz)), FogData.DirectionalInscatteringColor.w) * UniformPhaseFunction;
            #endif
            }
#else
        // Setup a cosine lobe around the light direction to approximate inscattering
        half3 DirectionalLightInscattering = FogData.DirectionalInscatteringColor.xyz
        #if PROJECT_EXPFOG_MATCHES_VFOG
                * HenyeyGreensteinPhase(FogData.VolumetricFogPhaseG, dot(-CameraToReceiverNormalized, InView.AtmosphereLightDirection[0].xyz));
        #else
                * pow(saturate(dot(CameraToReceiverNormalized, FogData.InscatteringLightDirection.xyz)), FogData.DirectionalInscatteringColor.w);
        #endif
#endif

        // Calculate the line integral of the eye ray through the haze
        float DirectionalInscatteringStartDistance = PROJECT_EXPFOG_MATCHES_VFOG ? 0.0 : FogData.InscatteringLightDirection.w;
        float DirExponentialHeightLineIntegral = ExponentialHeightLineIntegralShared * max(RayLength - DirectionalInscatteringStartDistance, 0.0f);

        // Calculate transmittance (how much light makes it through fog to camera)
        half DirectionalInscatteringFogFactor = saturate(exp2(-DirExponentialHeightLineIntegral));

        // Final inscattering from the light
        DirectionalInscattering = DirectionalLightInscattering * (1 - DirectionalInscatteringFogFactor);
    }
#endif
```

**Critical line 319**:
```hlsl
DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz
                             + InView.SkyAtmosphereHeightFogContribution
                             * InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb;
```

This **adds** the atmosphere-filtered light color to the directional inscattering. This is what makes the fog blue toward the sun.

---

## Final Fog Color Assembly (Lines 357-385)

```hlsl
#if PROJECT_EXPFOG_MATCHES_VFOG
    DirectionalInscattering *= FogData.VolumetricFogAlbedo;
#endif

    // Calculate the amount of light that made it through the fog using the transmission equation
    half ExpFogFactor = max(saturate(exp2(-ExponentialHeightLineIntegral)), MinFogOpacity);

    FLATTEN
    if (FogData.ExponentialFogParameters3.w > 0 && CameraToReceiverLength > FogData.ExponentialFogParameters3.w)
    {
        ExpFogFactor = 1;
        DirectionalInscattering = 0;
    }

    // Fog color is unused when additive / modulate blend modes are active.
    #if (MATERIALBLENDING_ADDITIVE || MATERIALBLENDING_MODULATE) && !FOG_MATERIALBLENDING_OVERRIDE
        half3 FogColor = 0.0;
    #else
        // ===== FINAL FOG COLOR COMPOSITION =====
        half3 FogColor = (InscatteringColor) * (1 - ExpFogFactor) + DirectionalInscattering;
        // ===== Components of FogColor =====
        // InscatteringColor = Base fog color (from fog component)
        //                   + Sky atmosphere contribution (line 179)
        //                   + Cubemap contribution (if used)
        //
        // DirectionalInscattering = Directional light color modulated by phase function
        //                          + Atmosphere light color (line 319)
        //                          weighted by fog transmittance
    #endif

#if SUPPORT_PRIMITIVE_ALPHA_HOLDOUT
    if (IsExponentialFogHoldout(View.EnvironmentComponentsFlags) && View.bPrimitiveAlphaHoldoutEnabled)
    {
        FogColor.rgb *= 0;
    }
#endif

    return half4(FogColor, ExpFogFactor);
}
```

---

## GetExponentialHeightFog Signature (Lines 213-214)

```hlsl
// @param WorldPositionRelativeToCamera = WorldPosition - InCameraPosition.
// It is the vector from the camera to the world position to shade (independent from TranslatedPosition).
half4 GetExponentialHeightFog(float3 WorldPositionRelativeToCamera, float ExcludeDistance, uint EyeIndex, ViewState InView, bool bOverrideOrigin = false, float3 OverrideRayStartRelativeToCamera=0.0f.xxx, float3 OverrideRayDir = 0.0f.xxx, float OverrideRayLength = 0.0f)
{
    // ... implementation calls ComputeInscatteringColor at line 301 ...
}
```

---

## Key Preprocessor Dependencies

**`PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG`**:
- Enables ambient sky contribution in `ComputeInscatteringColor()` (lines 174-180)
- Enables directional atmosphere light contribution (lines 313-325)
- Both must be enabled to get the full blue sky effect

**`PROJECT_EXPFOG_MATCHES_VFOG`**:
- Uses Henyey-Greenstein phase function instead of power-based phase
- Uses volumetric fog albedo in final calculations
- Controls whether volumetric and exponential fog are tightly coupled

---

## Parameters Referenced from ViewState (InView)

**Required view parameters for sky atmosphere integration**:
- `InView.SkyAtmosphereHeightFogContribution` — Blend factor (0-1), from SkyAtmosphere component
- `InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0]` — Light color after atmospheric transmittance
- `InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[1]` — Second light (if enabled)
- `InView.AtmosphereLightDirection[0]` — Sun direction
- `InView.AtmosphereLightDirection[1]` — Second sun direction (if enabled)

These are all set by `SceneRendering.cpp:1440-1456` during view uniform setup.

---

## Utility Functions Called

**`GetViewDistanceSkyLightColor()`** (line 175):
- Returns the ambient sky light color
- Defined in SkyAtmosphereCommon.ush (part of the atmosphere shader library)
- Typically samples the sky light component or sky atmosphere irradiance

**`HenyeyGreensteinPhase(g, cosTheta)`**:
- Henyey-Greenstein scattering phase function
- Controls how much light scatters in the view direction
- `g` parameter controls anisotropy (sun glow sharpness)

**`TextureCubeSampleLevel()`**:
- Samples cubemap inscattering texture at specific MIP level
- Used for distance-based color transitions

---

## Summary for Implementation

### Two Paths to Blue Fog

1. **Ambient path** (lines 174-180):
   ```hlsl
   Inscattering += SkyAtmosphereAmbientContributionColorScale.rgb
                 * SkyAtmosphereHeightFogContribution
                 * GetViewDistanceSkyLightColor();
   ```
   - Uses the sky light's ambient color
   - Modulated by two scale factors

2. **Directional path** (lines 313-325):
   ```hlsl
   DirectionalInscatteringColor += SkyAtmosphereHeightFogContribution
                                 * AtmosphereLightIlluminanceOnGroundPostTransmittance;
   ```
   - Uses the atmosphere light's filtered color
   - Also modulated by height fog contribution factor

Both paths multiply by `SkyAtmosphereHeightFogContribution`, making it the master control.

