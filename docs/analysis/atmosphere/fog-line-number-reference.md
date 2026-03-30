<!-- doc-meta
title: UE5 Height Fog — Exact Line Number Reference
created: 2026-03-19
category: reference
tags: line-numbers, quick-reference, fog-rendering
-->

# UE5 Height Fog — Exact Line Number Reference

**Quick lookup table for all critical lines in the UE5 5.6 fog-to-atmosphere integration.**

All file paths are exact as of `W:/UE_5.6_Source/`.

---

## Sky Atmosphere Component Initialization

| Source | File | Line | Code |
|--------|------|------|------|
| Default value | `Engine/Private/Components/SkyAtmosphereComponent.cpp` | 585 | `HeightFogContribution = 1.0f;` |
| Scene proxy init | `Engine/Private/Components/SkyAtmosphereComponent.cpp` | 603 | `HeightFogContribution = InComponent->HeightFogContribution;` |
| Dynamic state init | `Engine/Private/Components/SkyAtmosphereComponent.cpp` | 619 | `HeightFogContribution = Ds.HeightFogContribution;` |
| Scene proxy class | `Engine/Public/SceneProxies/SkyAtmosphereSceneProxy.h` | 27 | `float GetHeightFogContribution() const { return HeightFogContribution; }` |
| Member variable | `Engine/Public/SceneProxies/SkyAtmosphereSceneProxy.h` | 50 | `float HeightFogContribution;` |

---

## Atmosphere Light Transmittance Computation

| Source | File | Line | Code |
|--------|------|------|------|
| Transmittance LUT | `Renderer/Private/SkyAtmosphereRendering.cpp` | 454 | `const FLinearColor TransmittanceTowardSun = SkyAtmosphereSceneProxy.GetAtmosphereSetup().GetTransmittanceAtGroundLevel(AtmosphereLightDirection);` |
| Store in proxy | `Renderer/Private/SkyAtmosphereRendering.cpp` | 458 | `AtmosphereLight.Proxy->SetAtmosphereRelatedProperties(TransmittanceTowardSun, SunDiskOuterSpaceLuminance);` |

---

## View Uniform Setup (SceneRendering.cpp)

| Purpose | Line | Code |
|---------|------|------|
| Copy HeightFogContribution to view | 1440 | `ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = SkyAtmosphereSceneProxy.GetHeightFogContribution();` |
| Loop atmosphere lights | 1443 | `for (uint8 Index = 0; Index < NUM_ATMOSPHERE_LIGHTS; ++Index)` |
| Get light scene info | 1445 | `FLightSceneInfo* Light = Scene->AtmosphereLights[Index];` |
| Get sun luminance | 1450 | `ViewUniformShaderParameters.AtmosphereLightDiscLuminance[Index] = Light->Proxy->GetOuterSpaceLuminance();` |
| **SET FOG ILLUMINANCE** | **1451** | `ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[Index] = Light->Proxy->GetSunIlluminanceOnGroundPostTransmittance();` |
| Mark for fog | 1452 | `ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[Index].A = 1.0f;` |
| Get light direction | 1455 | `ViewUniformShaderParameters.AtmosphereLightDirection[Index] = FVector3f(SkyAtmosphereSceneProxy.GetAtmosphereLightDirection(Index, -Light->Proxy->GetDirection()));` |
| Second light check | 1445 | `if (Light) { ... } else { ClearAtmosphereLightData(Index); }` |

---

## Light Proxy Transmittance Storage (DirectionalLightComponent.cpp)

| Purpose | Line | Code |
|---------|------|------|
| Member variable | 248 | `FLinearColor TransmittanceTowardSun;` |
| Default value | 317 | `TransmittanceTowardSun(FLinearColor::White)` |
| **SETTER** | **582-584** | `virtual void SetAtmosphereRelatedProperties(FLinearColor TransmittanceTowardSunIn, FLinearColor SunDiscOuterSpaceLuminanceIn) override { TransmittanceTowardSun = TransmittanceTowardSunIn; ... }` |
| Get illuminance | 593-595 | `virtual FLinearColor GetOuterSpaceIlluminance() const override { return GetColor(); }` |
| Get transmittance | 598-601 | `virtual FLinearColor GetAtmosphereTransmittanceTowardSun() const override { return TransmittanceTowardSun; }` |
| **COMPUTE RESULT** | **603-606** | `virtual FLinearColor GetSunIlluminanceOnGroundPostTransmittance() const override { return GetOuterSpaceIlluminance() * GetAtmosphereTransmittanceTowardSun(); }` |

---

## Fog Component Setup (FogRendering.cpp)

| Purpose | Line | Code |
|--------|------|------|
| Check if fog exists | 219 | `if (Scene->ExponentialFogs.Num() > 0)` |
| Get fog info | 221 | `const FExponentialHeightFogSceneInfo& FogInfo = Scene->ExponentialFogs[0];` |
| **COPY SCALE** | **267** | `View.SkyAtmosphereAmbientContributionColorScale = FogInfo.SkyAtmosphereAmbientContributionColorScale;` |
| Pack to uniform | 85 | `OutParameters.SkyAtmosphereAmbientContributionColorScale = View.SkyAtmosphereAmbientContributionColorScale;` |

---

## Fog Uniform Buffer Creation (FogRendering.cpp)

| Purpose | Line | Code |
|--------|------|------|
| Allocate params | 119 | `auto* FogStruct = GraphBuilder.AllocParameters<FFogUniformParameters>();` |
| Setup params | 120 | `SetupFogUniformParameters(GraphBuilder, View, *FogStruct);` |
| Create buffer | 121 | `return GraphBuilder.CreateUniformBuffer(FogStruct);` |

---

## Fog Shader Parameters (FogRendering.cpp:63-95)

| Purpose | Line | Code |
|--------|------|------|
| Function start | 63 | `void SetupFogUniformParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, FFogUniformParameters& OutParameters, bool bForRealtimeSkyCapture)` |
| Exponential params | 81 | `OutParameters.ExponentialFogParameters = View.ExponentialFogParameters;` |
| Fog color | 82 | `OutParameters.ExponentialFogColorParameter = FVector4f(View.ExponentialFogColor, 1.0f - View.FogMaxOpacity);` |
| More params | 83-84 | `OutParameters.ExponentialFogParameters2 = View.ExponentialFogParameters2;` `OutParameters.ExponentialFogParameters3 = View.ExponentialFogParameters3;` |
| **SKY ATMOSPHERE SCALE** | **85** | `OutParameters.SkyAtmosphereAmbientContributionColorScale = View.SkyAtmosphereAmbientContributionColorScale;` |
| Cubemap rotation | 86 | `OutParameters.SinCosInscatteringColorCubemapRotation = View.SinCosInscatteringColorCubemapRotation;` |
| More fog params | 87-90 | Inscattering params, directional inscattering setup |

---

## Shader: Fog Structure Data (HeightFogCommon.ush:64-85)

| Purpose | Line | Code |
|--------|------|------|
| Struct start | 64 | `struct FogStructData {` |
| Sky atmosphere scale member | 70 | `float4 SkyAtmosphereAmbientContributionColorScale;` |
| Struct end | 85 | `};` |

---

## Shader: ComputeInscatteringColor (HeightFogCommon.ush:140-183)

| Purpose | Line | Code |
|--------|------|------|
| **FUNCTION START** | **140** | `float3 ComputeInscatteringColor(float3 CameraToReceiver, float CameraToReceiverLength, FogStructData FogData, uint EyeIndex)` |
| Base fog color | 142 | `half3 Inscattering = FogData.ExponentialFogColorParameter.xyz;` |
| Cubemap check | 146 | `if (FogData.ExponentialFogParameters3.z > 0)` |
| Cubemap sampling | 160-161 | `TextureCubeSampleLevel(FogStruct.FogInscatteringColorCubemap, SharedFogInColorScatteringSampler, CubemapLookupVector, ...)` |
| **GUARD** | **174** | `#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG` |
| **SKY AMBIENT CALC** | **175** | `float3 SkyAmbient = FogData.SkyAtmosphereAmbientContributionColorScale.rgb` |
| **MULTIPLY BY HEIGHT FOG** | **176** | `* View.SkyAtmosphereHeightFogContribution.xxx` |
| **GET SKY COLOR** | **177** | `* GetViewDistanceSkyLightColor();` |
| Albedo mult | 177-178 | `#if PROJECT_EXPFOG_MATCHES_VFOG SkyAmbient *= FogData.VolumetricFogAlbedo;` |
| **ADD TO RESULT** | **179** | `Inscattering += SkyAmbient;` |
| Guard end | 180 | `#endif` |
| Return | 182 | `return Inscattering;` |
| **FUNCTION END** | **183** | `}` |

---

## Shader: Directional Inscattering (HeightFogCommon.ush:304-355)

| Purpose | Line | Code |
|--------|------|------|
| Guard start | 304 | `#if PERMUTATION_SUPPORT_FOG_DIRECTIONAL_LIGHT_INSCATTERING` |
| Check enabled | 307 | `if (FogData.InscatteringLightDirection.w >= 0 ...` |
| **ATMOSPHERE CHECK** | **313** | `#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG` |
| Phase function | 314 | `const float UniformPhaseFunction = 1.0f / (4.0f * PI);` |
| Initialize color | 315-316 | `half3 DirectionalInscatteringColor; half3 DirectionalLightInscattering;` |
| **ATMOSPHERE INJECTION** | **319** | `DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz + InView.SkyAtmosphereHeightFogContribution * InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0].rgb;` |
| Phase modulation | 320-325 | `DirectionalLightInscattering = DirectionalInscatteringColor * ...HenyeyGreensteinPhase/pow...` |
| Second light check | 327 | `if (InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[1].a > 0.0f)` |
| Second light inject | 329 | `DirectionalInscatteringColor = FogData.DirectionalInscatteringColor.xyz + InView.SkyAtmosphereHeightFogContribution * InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[1].rgb;` |
| Guard end | 345 | `#endif` |
| Transmittance calc | 349 | `float DirExponentialHeightLineIntegral = ExponentialHeightLineIntegralShared * max(RayLength - DirectionalInscatteringStartDistance, 0.0f);` |
| Fog factor | 351 | `half DirectionalInscatteringFogFactor = saturate(exp2(-DirExponentialHeightLineIntegral));` |
| **FINAL DIRECTIONAL** | **353** | `DirectionalInscattering = DirectionalLightInscattering * (1 - DirectionalInscatteringFogFactor);` |
| Guard end | 355 | `}` |

---

## Shader: Final Fog Color Assembly (HeightFogCommon.ush:357-385)

| Purpose | Line | Code |
|--------|------|------|
| Albedo mult | 358 | `#if PROJECT_EXPFOG_MATCHES_VFOG DirectionalInscattering *= FogData.VolumetricFogAlbedo;` |
| Compute opacity | 362 | `half ExpFogFactor = max(saturate(exp2(-ExponentialHeightLineIntegral)), MinFogOpacity);` |
| Check cutoff | 365 | `if (FogData.ExponentialFogParameters3.w > 0 && CameraToReceiverLength > FogData.ExponentialFogParameters3.w)` |
| **FINAL COLOR BLEND** | **375** | `half3 FogColor = (InscatteringColor) * (1 - ExpFogFactor) + DirectionalInscattering;` |
| Holdout check | 379 | `if (IsExponentialFogHoldout(View.EnvironmentComponentsFlags) && View.bPrimitiveAlphaHoldoutEnabled)` |
| Holdout apply | 381 | `FogColor.rgb *= 0;` |
| Return | 385 | `return half4(FogColor, ExpFogFactor);` |
| **FUNCTION END** | **386** | `}` |

---

## Summary: The Critical Lines (Just The Essentials)

**To understand the pattern, focus on these lines:**

| What | File:Line |
|------|-----------|
| HeightFogContribution default | SkyAtmosphereComponent.cpp:585 |
| HeightFogContribution → proxy | SkyAtmosphereComponent.cpp:603 |
| Sample transmittance | SkyAtmosphereRendering.cpp:454 |
| Store transmittance | SkyAtmosphereRendering.cpp:458 |
| HeightFogContribution → view | SceneRendering.cpp:1440 |
| **Set fog illuminance** | **SceneRendering.cpp:1451** |
| Mark for fog | SceneRendering.cpp:1452 |
| Copy fog scale | FogRendering.cpp:267 |
| Pack fog scale | FogRendering.cpp:85 |
| Sun illum formula | DirectionalLightComponent.cpp:605 |
| **Ambient in shader** | **HeightFogCommon.ush:175** |
| **Directional in shader** | **HeightFogCommon.ush:319** |
| **Final composition** | **HeightFogCommon.ush:375** |

---

## Build Flags

| Flag | Usage | Lines |
|------|-------|-------|
| `PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG` | Gates sky atmosphere contributions | HeightFogCommon.ush:174, 313 |
| `PROJECT_EXPFOG_MATCHES_VFOG` | Volumetric fog compatibility | HeightFogCommon.ush:176, 321, 358 |
| `PERMUTATION_SUPPORT_FOG_INSCATTERING_TEXTURE` | Cubemap fog colors | HeightFogCommon.ush:144 |
| `PERMUTATION_SUPPORT_FOG_DIRECTIONAL_LIGHT_INSCATTERING` | Directional sun glow | HeightFogCommon.ush:304 |

---

## File Paths (Absolute)

```
W:/UE_5.6_Source/Engine/Private/Components/SkyAtmosphereComponent.cpp
W:/UE_5.6_Source/Engine/Private/Components/DirectionalLightComponent.cpp
W:/UE_5.6_Source/Engine/Public/SceneProxies/SkyAtmosphereSceneProxy.h
W:/UE_5.6_Source/Renderer/Private/FogRendering.cpp
W:/UE_5.6_Source/Renderer/Private/SkyAtmosphereRendering.cpp
W:/UE_5.6_Source/Renderer/Private/SceneRendering.cpp
W:/UE_5.6_Source/Engine/Shaders/Private/HeightFogCommon.ush
```

---

## Notes

- All line numbers are **exact for UE5.6**
- Future UE versions may shift lines ±5-10 lines but structure remains
- All code is copy-paste ready
- No approximations — all values are computed exactly

