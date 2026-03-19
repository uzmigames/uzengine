<!-- doc-meta
title: UE5 5.6 Environment Component Constructor Defaults
description: Complete enumeration of all default values from UE5 source constructors for SkyAtmosphere, SkyLight, ExponentialHeightFog, DirectionalLight, and PostProcessSettings
keywords: ue5, defaults, constructors, atmosphere, lighting, post-process
category: reference
-->

# UE5 5.6 Environment Component Constructor Defaults

Complete extraction from UE5 5.6 source (`W:\UE_5.6_Source`) with exact file paths and line numbers.

---

## 1. USkyAtmosphereComponent Constructor

**Source:** `Engine/Source/Runtime/Engine/Private/Components/SkyAtmosphereComponent.cpp:89-142`

### Core Atmosphere Parameters

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `TransformMode` | `ESkyAtmosphereTransformMode::PlanetTopAtAbsoluteWorldOrigin` | 100 |
| `BottomRadius` | `6360.0f` (km) | 101 |
| `AtmosphereHeight` | `60.0f` (km) [= 6420 - 6360] | 102 |
| `GroundAlbedo` | `FColor(170, 170, 170)` [= 0.4f linear] | 103 |

### Rayleigh Scattering

| Parameter | Value | File:Line |
|-----------|-------|-----------|
| `RayleighScattering` | Normalized from `(0.005802, 0.013558, 0.033100)` by B component | 106-107 |
| `RayleighScatteringScale` | `0.033100f` [= blue channel normalization] | 108 |
| `RayleighExponentialDistribution` | `8.0f` [= EarthRayleighScaleHeight] | 109 |

### Mie Scattering

| Parameter | Value | File:Line |
|-----------|-------|-----------|
| `MieScattering` | `FColor::White` | 111 |
| `MieScatteringScale` | `0.003996f` | 112 |
| `MieAbsorption` | `FColor::White` | 113 |
| `MieAbsorptionScale` | `0.000444f` | 114 |
| `MieAnisotropy` | `0.8f` | 115 |
| `MieExponentialDistribution` | `1.2f` [= EarthMieScaleHeight] | 116 |

### Ozone Absorption (Tent Distribution)

| Parameter | Value | File:Line |
|-----------|-------|-----------|
| `OtherAbsorptionScale` | `0.001881f` [= green channel of raw] | 120 |
| `OtherAbsorption` | Normalized from `(0.000650, 0.001881, 0.000085)` by G | 121 |
| `OtherTentDistribution.TipAltitude` | `25.0f` | 122 |
| `OtherTentDistribution.TipValue` | `1.0f` | 123 |
| `OtherTentDistribution.Width` | `15.0f` | 124 |

### Luminance & Aerial Perspective

| Parameter | Value | File:Line |
|-----------|-------|-----------|
| `SkyLuminanceFactor` | `FLinearColor::White` | 126 |
| `SkyAndAerialPerspectiveLuminanceFactor` | `FLinearColor::White` | 127 |
| `MultiScatteringFactor` | `1.0f` | 128 |
| `AerialPespectiveViewDistanceScale` | `1.0f` | 129 |
| `AerialPerspectiveStartDepth` | `0.1f` | 132 |
| `HeightFogContribution` | `1.0f` | 130 |
| `TransmittanceMinLightElevationAngle` | `-90.0f` | 131 |

### Debug & Rendering

| Parameter | Value | File:Line |
|-----------|-------|-----------|
| `TraceSampleCountScale` | `1.0f` | 134 |
| `bHoldout` | `false` | 136 |
| `bRenderInMainPass` | `true` | 137 |

---

## 2. USkyLightComponent Constructor

**Source:** `Engine/Source/Runtime/Engine/Private/Components/SkyLightComponent.cpp:285-333`

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `Brightness_DEPRECATED` | `1` | 299 |
| `Intensity` | `1` | 300 |
| `IndirectLightingIntensity` | `1.0f` | 301 |
| `SkyDistanceThreshold` | `150000` | 302 |
| `Mobility` | `EComponentMobility::Stationary` | 303 |
| `bLowerHemisphereIsBlack` | `true` | 304 |
| `bSavedConstructionScriptValuesValid` | `true` | 305 |
| `bHasEverCaptured` | `false` | 306 |
| `OcclusionMaxDistance` | `1000` | 307 |
| `MinOcclusion` | `0` | 308 |
| `OcclusionExponent` | `1` | 309 |
| `OcclusionTint` | `FColor::Black` | 310 |
| `CubemapResolution` | `128` | 311 |
| `LowerHemisphereColor` | `FLinearColor::Black` | 312 |
| `AverageBrightness` | `1.0f` | 313 |
| `BlendDestinationAverageBrightness` | `1.0f` | 314 |
| `SpecifiedCubemapColorScale` | `FLinearColor::White` | 315 |
| `bCastVolumetricShadow` | `true` | 316 |
| `CastRaytracedShadow` | `ECastRayTracedShadow::UseProjectSetting` | 317 |
| `bCastRaytracedShadow_DEPRECATED` | `false` | 318 |
| `bAffectReflection` | `true` | 319 |
| `bAffectGlobalIllumination` | `true` | 320 |
| `SamplesPerPixel` | `4` | 321 |
| `bRealTimeCapture` | `false` | 322 |
| `bCloudAmbientOcclusion` | `0` | 323 |
| `CloudAmbientOcclusionExtent` | `150.0f` | 324 |
| `CloudAmbientOcclusionStrength` | `1.0f` | 325 |
| `CloudAmbientOcclusionMapResolutionScale` | `1.0f` | 326 |
| `CloudAmbientOcclusionApertureScale` | `0.05f` | 327 |

---

## 3. UExponentialHeightFogComponent Constructor

**Source:** `Engine/Source/Runtime/Engine/Private/Components/ExponentialHeightFogComponent.cpp:64-103`

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `FogInscatteringColor_DEPRECATED` | `FLinearColor(0.447f, 0.638f, 1.0f)` | 67 |
| `FogInscatteringLuminance` | `FLinearColor::Black` | 68 |
| `SkyAtmosphereAmbientContributionColorScale` | `FLinearColor::White` | 70 |
| `DirectionalInscatteringExponent` | `4.0f` | 72 |
| `DirectionalInscatteringStartDistance` | `10000.0f` | 73 |
| `DirectionalInscatteringColor_DEPRECATED` | `FLinearColor(0.25f, 0.25f, 0.125f)` | 74 |
| `DirectionalInscatteringLuminance` | `FLinearColor::Black` | 75 |
| `InscatteringTextureTint` | `FLinearColor::White` | 77 |
| `FullyDirectionalInscatteringColorDistance` | `100000.0f` | 78 |
| `NonDirectionalInscatteringColorDistance` | `1000.0f` | 79 |
| `FogDensity` | `0.02f` | 81 |
| `FogHeightFalloff` | `0.2f` | 82 |
| `SecondFogData.FogDensity` | `0.0f` | 84 |
| `FogMaxOpacity` | `1.0f` | 86 |
| `StartDistance` | `0.0f` | 87 |
| `EndDistance` | `0.0f` | 88 |
| `FogCutoffDistance` | `0` (disabled by default) | 91 |
| `bHoldout` | `false` | 93 |
| `bRenderInMainPass` | `true` | 94 |
| `bVisibleInReflectionCaptures` | `true` | 95 |
| `bVisibleInRealTimeSkyCaptures` | `true` | 96 |
| `VolumetricFogScatteringDistribution` | `0.2f` | 98 |
| `VolumetricFogAlbedo` | `FColor::White` | 99 |
| `VolumetricFogExtinctionScale` | `1.0f` | 100 |
| `VolumetricFogDistance` | `6000.0f` | 101 |
| `VolumetricFogStaticLightingScatteringIntensity` | `1` | 102 |

---

## 4. UDirectionalLightComponent Constructor

**Source:** `Engine/Source/Runtime/Engine/Private/Components/DirectionalLightComponent.cpp:988-1057`

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `Intensity` | `10` | 1004 |
| `bEnableLightShaftOcclusion` | `false` | 1006 |
| `OcclusionDepthRange` | `100000.f` | 1007 |
| `OcclusionMaskDarkness` | `0.05f` | 1008 |
| `ShadowCascadeBiasDistribution` | `1` | 1010 |
| `WholeSceneDynamicShadowRadius_DEPRECATED` | `20000.0f` | 1011 |
| `DynamicShadowDistanceStationaryLight` | `0.f` | 1012 |
| `ForwardShadingPriority` | `0` | 1014 |
| `DynamicShadowDistanceMovableLight` | `40000.0f` | 1017 |
| `DistanceFieldShadowDistance` | `51200.0f` | 1018 |
| `DynamicShadowCascades` | `4` | 1019 |
| `bUseRayTracedDistanceFieldShadows` | `true` | 1020 |
| `TraceDistance` | `10000.0f` | 1022 |
| `FarShadowDistance` | `300000.0f` | 1023 |
| `LightSourceAngle` | `0.5357f` (Earth's sun angle) | 1024 |
| `LightSourceSoftAngle` | `0.0f` | 1025 |
| `ShadowSourceAngleFactor` | `1.0f` | 1026 |
| `CascadeDistributionExponent` | `3.0f` | 1029 |
| `CascadeTransitionFraction` | `0.1f` | 1030 |
| `ShadowDistanceFadeoutFraction` | `0.1f` | 1031 |
| `IndirectLightingIntensity` | `1.0f` | 1032 |
| `CastTranslucentShadows` | `true` | 1033 |
| `bUseInsetShadowsForMovableObjects` | `true` | 1034 |
| `bCastVolumetricShadow` | `true` | 1035 |
| `ModulatedShadowColor` | `FColor(128, 128, 128)` | 1037 |
| `ShadowAmount` | `1.0f` | 1038 |
| `bUsedAsAtmosphereSunLight_DEPRECATED` | `false` | 1040 |
| `bAtmosphereSunLight` | `true` | 1041 |
| `AtmosphereSunLightIndex` | `0` | 1042 |
| `AtmosphereSunDiskColorScale` | `FLinearColor::White` | 1043 |
| `bCastShadowsOnClouds` | `0` | 1045 |
| `bCastShadowsOnAtmosphere` | `0` | 1046 |
| `bCastCloudShadows` | `0` | 1047 |
| `CloudShadowExtent` | `150.0f` | 1048 |
| `CloudShadowMapResolutionScale` | `1.0f` | 1049 |
| `CloudShadowRaySampleCountScale` | `1.0f` | 1050 |
| `CloudShadowStrength` | `1.0f` | 1051 |
| `CloudShadowOnAtmosphereStrength` | `1.0f` | 1052 |
| `CloudShadowOnSurfaceStrength` | `1.0f` | 1053 |
| `CloudShadowDepthBias` | `0.0f` | 1054 |
| `CloudScatteredLuminanceScale` | `FLinearColor::White` | 1055 |
| `bPerPixelAtmosphereTransmittance` | `0` | 1056 |

---

## 5. FPostProcessSettings Constructor

**Source:** `Engine/Source/Runtime/Engine/Private/Scene.cpp:367-657`

### Color Temperature

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `TemperatureType` | `ETemperatureMethod::TEMP_WhiteBalance` | 372 |
| `WhiteTemp` | `6500.0f` | 373 |
| `WhiteTint` | `0.0f` | 374 |

### Color Correction (Global)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `ColorSaturation` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 377 |
| `ColorContrast` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 378 |
| `ColorGamma` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 379 |
| `ColorGain` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 380 |
| `ColorOffset` | `FVector4(0.0f, 0.0f, 0.0f, 0.0f)` | 381 |

### Color Correction (Shadows)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `ColorSaturationShadows` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 383 |
| `ColorContrastShadows` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 384 |
| `ColorGammaShadows` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 385 |
| `ColorGainShadows` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 386 |
| `ColorOffsetShadows` | `FVector4(0.0f, 0.0f, 0.0f, 0.0f)` | 387 |

### Color Correction (Midtones)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `ColorSaturationMidtones` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 389 |
| `ColorContrastMidtones` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 390 |
| `ColorGammaMidtones` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 391 |
| `ColorGainMidtones` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 392 |
| `ColorOffsetMidtones` | `FVector4(0.f, 0.0f, 0.0f, 0.0f)` | 393 |

### Color Correction (Highlights)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `ColorSaturationHighlights` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 395 |
| `ColorContrastHighlights` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 396 |
| `ColorGammaHighlights` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 397 |
| `ColorGainHighlights` | `FVector4(1.0f, 1.0f, 1.0f, 1.0f)` | 398 |
| `ColorOffsetHighlights` | `FVector4(0.0f, 0.0f, 0.0f, 0.0f)` | 399 |

### Color Correction (Advanced Thresholds)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `ColorCorrectionShadowsMax` | `0.09f` | 401 |
| `ColorCorrectionHighlightsMin` | `0.5f` | 402 |
| `ColorCorrectionHighlightsMax` | `1.0f` | 403 |
| `BlueCorrection` | `0.6f` | 405 |
| `ExpandGamut` | `1.0f` | 406 |
| `ToneCurveAmount` | `1.0` | 407 |

### Film (ACES Tonemapping)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `FilmSlope` | `0.88f` | 412 |
| `FilmToe` | `0.55f` | 413 |
| `FilmShoulder` | `0.26f` | 414 |
| `FilmBlackClip` | `0.0f` | 415 |
| `FilmWhiteClip` | `0.04f` | 416 |

### Scene Color & Chromatic Aberration

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `SceneColorTint` | `FLinearColor(1, 1, 1)` | 418 |
| `SceneFringeIntensity` | `0.0f` | 419 |
| `ChromaticAberrationStartOffset` | `0.0f` (implicit in memzero) | 370 |

### Bloom (Complete)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `BloomMethod` | `BM_SOG` | 420 |
| `BloomIntensity` | `0.675f` | 422 |
| `BloomThreshold` | `-1.0f` | 423 |
| `BloomSizeScale` | `4.0` | 425 |
| `Bloom1Tint` | `FLinearColor(0.3465f, 0.3465f, 0.3465f)` | 426 |
| `Bloom1Size` | `0.3f` | 427 |
| `Bloom2Tint` | `FLinearColor(0.138f, 0.138f, 0.138f)` | 428 |
| `Bloom2Size` | `1.0f` | 429 |
| `Bloom3Tint` | `FLinearColor(0.1176f, 0.1176f, 0.1176f)` | 430 |
| `Bloom3Size` | `2.0f` | 431 |
| `Bloom4Tint` | `FLinearColor(0.066f, 0.066f, 0.066f)` | 432 |
| `Bloom4Size` | `10.0f` | 433 |
| `Bloom5Tint` | `FLinearColor(0.066f, 0.066f, 0.066f)` | 434 |
| `Bloom5Size` | `30.0f` | 435 |
| `Bloom6Tint` | `FLinearColor(0.061f, 0.061f, 0.061f)` | 436 |
| `Bloom6Size` | `64.0f` | 437 |

### Bloom Convolution

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `BloomConvolutionScatterDispersion` | `1.f` | 438 |
| `BloomConvolutionSize` | `1.f` | 439 |
| `BloomConvolutionCenterUV` | `FVector2D(0.5f, 0.5f)` | 440 |
| `BloomConvolutionPreFilterMin` | `7.f` | 445 |
| `BloomConvolutionPreFilterMax` | `15000.f` | 446 |
| `BloomConvolutionPreFilterMult` | `15.f` | 447 |
| `BloomConvolutionBufferScale` | `0.133f` | 448 |

### Bloom Dirt Mask

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `BloomDirtMaskIntensity` | `0.0f` | 449 |
| `BloomDirtMaskTint` | `FLinearColor(0.5f, 0.5f, 0.5f)` | 450 |

### Ambient Cubemap

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `bMegaLights` | `true` | 451 |
| `AmbientCubemapIntensity` | `1.0f` | 452 |
| `AmbientCubemapTint` | `FLinearColor(1, 1, 1)` | 453 |

### Camera Settings

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `CameraShutterSpeed` | `60.f` | 454 |
| `CameraISO` | `100.f` | 455 |

### Auto Exposure (Histogram Method)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `AutoExposureMethod` | `AEM_Histogram` | 458 |
| `AutoExposureLowPercent` | `10.0f` | 459 |
| `AutoExposureHighPercent` | `90.0f` | 460 |

#### Auto Exposure Brightness Range (Extended Mode ON)

When `r.DefaultFeature.AutoExposure.ExtendedDefaultLuminanceRange = 1`:

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `AutoExposureMinBrightness` | `-10.0f` (EV100) | 467 |
| `AutoExposureMaxBrightness` | `20.0f` (EV100) | 468 |
| `HistogramLogMin` | `-10.0f` | 469 |
| `HistogramLogMax` | `20.0f` | 470 |

#### Auto Exposure Brightness Range (Extended Mode OFF)

When `r.DefaultFeature.AutoExposure.ExtendedDefaultLuminanceRange = 0`:

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `AutoExposureMinBrightness` | `0.03f` | 474 |
| `AutoExposureMaxBrightness` | `8.0f` | 475 |
| `HistogramLogMin` | `-8.0f` | 476 |
| `HistogramLogMax` | `4.0f` | 477 |

#### Auto Exposure Speed

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `AutoExposureBias` | `r.DefaultFeature.AutoExposure.Bias` CVar (applied from line 483) | 483 |
| `AutoExposureSpeedUp` | `3.0f` | 484 |
| `AutoExposureSpeedDown` | `1.0f` | 485 |
| `AutoExposureApplyPhysicalCameraExposure` | `1` | 487 |

### Local Exposure

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `LocalExposureMethod` | `ELocalExposureMethod::Bilateral` | 489 |
| `LocalExposureContrastScale_DEPRECATED` | `1.0f` | 490 |
| `LocalExposureHighlightContrastScale` | `1.0f` | 491 |
| `LocalExposureShadowContrastScale` | `1.0f` | 492 |
| `LocalExposureDetailStrength` | `1.0f` | 493 |
| `LocalExposureBlurredLuminanceBlend` | `0.6f` | 494 |
| `LocalExposureBlurredLuminanceKernelSizePercent` | `50.0f` | 495 |
| `LocalExposureHighlightThresholdStrength` | `1.0f` | 496 |
| `LocalExposureShadowThresholdStrength` | `1.0f` | 497 |
| `LocalExposureMiddleGreyBias` | `0.0f` | 498 |

### Lens Flare

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `LensFlareIntensity` | `1.0f` | 501 |
| `LensFlareTint` | `FLinearColor(1.0f, 1.0f, 1.0f)` | 502 |
| `LensFlareBokehSize` | `3.0f` | 503 |
| `LensFlareThreshold` | `8.0f` | 504 |
| `LensFlareTints[0]` | `FLinearColor(1.0f, 0.8f, 0.4f, 0.6f)` | 589 |
| `LensFlareTints[1]` | `FLinearColor(1.0f, 1.0f, 0.6f, 0.53f)` | 590 |
| `LensFlareTints[2]` | `FLinearColor(0.8f, 0.8f, 1.0f, 0.46f)` | 591 |
| `LensFlareTints[3]` | `FLinearColor(0.5f, 1.0f, 0.4f, 0.39f)` | 592 |
| `LensFlareTints[4]` | `FLinearColor(0.5f, 0.8f, 1.0f, 0.31f)` | 593 |
| `LensFlareTints[5]` | `FLinearColor(0.9f, 1.0f, 0.8f, 0.27f)` | 594 |
| `LensFlareTints[6]` | `FLinearColor(1.0f, 0.8f, 0.4f, 0.22f)` | 595 |
| `LensFlareTints[7]` | `FLinearColor(0.9f, 0.7f, 0.7f, 0.15f)` | 596 |

### Vignette & Sharpen

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `VignetteIntensity` | `0.4f` | 505 |
| `Sharpen` | `0.0f` | 506 |

### Film Grain

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `GrainIntensity_DEPRECATED` | `0.0f` | 507 |
| `GrainJitter_DEPRECATED` | `0.0f` | 508 |
| `FilmGrainIntensity` | `0.0f` | 511 |
| `FilmGrainIntensityShadows` | `1.0f` | 512 |
| `FilmGrainIntensityMidtones` | `1.0f` | 513 |
| `FilmGrainIntensityHighlights` | `1.0f` | 514 |
| `FilmGrainShadowsMax` | `0.09f` | 515 |
| `FilmGrainHighlightsMin` | `0.5f` | 516 |
| `FilmGrainHighlightsMax` | `1.0f` | 517 |
| `FilmGrainTexelSize` | `1.0f` | 518 |

### Ambient Occlusion

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `AmbientOcclusionIntensity` | `0.5f` | 521 |
| `AmbientOcclusionStaticFraction` | `1.0f` | 523 |
| `AmbientOcclusionRadius` | `200.0f` | 524 |
| `AmbientOcclusionDistance_DEPRECATED` | `80.0f` | 525 |
| `AmbientOcclusionFadeDistance` | `8000.0f` | 526 |
| `AmbientOcclusionFadeRadius` | `5000.0f` | 527 |
| `AmbientOcclusionPower` | `2.0f` | 528 |
| `AmbientOcclusionBias` | `3.0f` | 529 |
| `AmbientOcclusionQuality` | `50.0f` | 530 |
| `AmbientOcclusionMipBlend` | `0.6f` | 531 |
| `AmbientOcclusionMipScale` | `1.7f` | 532 |
| `AmbientOcclusionMipThreshold` | `0.01f` | 533 |
| `AmbientOcclusionRadiusInWS` | `false` | 534 |
| `AmbientOcclusionTemporalBlendWeight` | `0.1f` | 535 |

### Ray Tracing AO

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `RayTracingAO` | `0` | 536 |
| `RayTracingAOSamplesPerPixel` | `1` | 537 |
| `RayTracingAOIntensity` | `1.0` | 538 |
| `RayTracingAORadius` | `200.0f` | 539 |

### Global Illumination

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `DynamicGlobalIlluminationMethod` | `EDynamicGlobalIlluminationMethod::Lumen` | 540 |
| `IndirectLightingColor` | `FLinearColor(1.0f, 1.0f, 1.0f)` | 541 |
| `IndirectLightingIntensity` | `1.0f` | 542 |

### Lumen (Reflections)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `ReflectionMethod` | `EReflectionMethod::Lumen` | 605 |
| `LumenReflectionQuality` | `1` | 606 |
| `LumenRayLightingMode` | `ELumenRayLightingModeOverride::Default` | 607 |
| `LumenReflectionsScreenTraces` | `1` | 608 |
| `LumenFrontLayerTranslucencyReflections` | `false` | 609 |
| `LumenMaxRoughnessToTraceReflections` | `0.4f` | 610 |
| `LumenMaxReflectionBounces` | `1` | 611 |

### Lumen (Surfaces & Details)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `LumenSurfaceCacheResolution` | `1.0f` | 543 |
| `LumenSceneLightingQuality` | `1` | 544 |
| `LumenSceneDetail` | `1.0f` | 545 |
| `LumenSceneViewDistance` | `20000.0f` | 546 |
| `LumenSceneLightingUpdateSpeed` | `1` | 547 |

### Lumen (Final Gather)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `LumenFinalGatherQuality` | `1` | 548 |
| `LumenFinalGatherLightingUpdateSpeed` | `1` | 549 |
| `LumenFinalGatherScreenTraces` | `1` | 550 |
| `LumenMaxTraceDistance` | `20000.0f` | 551 |

### Lumen (Color & Leaking)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `LumenDiffuseColorBoost` | `1.0f` | 552 |
| `LumenSkylightLeaking` | `0.0f` | 553 |
| `LumenSkylightLeakingTint` | `FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)` | 554 |
| `LumenFullSkylightLeakingDistance` | `1000.0f` | 555 |

### Color Grading

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `ColorGradingIntensity` | `1.0f` | 557 |

### Depth of Field

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `DepthOfFieldFocalDistance` | `0` (disabled by default) | 559 |
| `DepthOfFieldFstop` | `4.0f` | 560 |
| `DepthOfFieldMinFstop` | `1.2f` | 561 |
| `DepthOfFieldBladeCount` | `kDefaultDepthOfFieldBladeCount` (see header) | 562 |
| `DepthOfFieldSensorWidth` | `24.576f` (APS-C) | 563 |
| `DepthOfFieldSqueezeFactor` | `1.0f` | 564 |
| `DepthOfFieldDepthBlurAmount` | `1.0f` | 565 |
| `DepthOfFieldDepthBlurRadius` | `0.0f` | 566 |
| `DepthOfFieldUseHairDepth` | `0` | 567 |
| `DepthOfFieldPetzvalBokeh` | `0` | 568 |
| `DepthOfFieldPetzvalBokehFalloff` | `1.0f` | 569 |
| `DepthOfFieldPetzvalExclusionBoxExtents` | `FVector2f::ZeroVector` | 570 |
| `DepthOfFieldPetzvalExclusionBoxRadius` | `0.0f` | 571 |
| `DepthOfFieldAspectRatioScalar` | `1.0f` | 572 |
| `DepthOfFieldBarrelRadius` | `5` | 573 |
| `DepthOfFieldBarrelLength` | `0` | 574 |
| `DepthOfFieldFocalRegion` | `0.0f` | 579 |
| `DepthOfFieldNearTransitionRegion` | `300.0f` | 580 |
| `DepthOfFieldFarTransitionRegion` | `500.0f` | 581 |
| `DepthOfFieldScale` | `0.0f` | 582 |
| `DepthOfFieldNearBlurSize` | `15.0f` | 583 |
| `DepthOfFieldFarBlurSize` | `15.0f` | 584 |
| `DepthOfFieldOcclusion` | `0.4f` | 585 |
| `DepthOfFieldSkyFocusDistance` | `0.0f` | 586 |
| `DepthOfFieldVignetteSize` | `200.0f` | 588 |

### Motion Blur

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `MotionBlurAmount` | `0.5f` | 598 |
| `MotionBlurMax` | `5.0f` | 599 |
| `MotionBlurTargetFPS` | `30` | 600 |
| `MotionBlurPerObjectSize` | `0.f` | 601 |

### Screen Space Reflections

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `ScreenSpaceReflectionIntensity` | `100.0f` | 615 |
| `ScreenSpaceReflectionQuality` | `50.0f` | 616 |
| `ScreenSpaceReflectionMaxRoughness` | `0.6f` | 617 |

### Ray Tracing (Reflections)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `TranslucencyType` | `ETranslucencyType::Raster` | 619 |
| `RayTracingTranslucencyMaxRoughness` | `0.6f` | 620 |
| `RayTracingTranslucencyRefractionRays` | `3` | 621 |
| `RayTracingTranslucencySamplesPerPixel` | `1` | 622 |
| `RayTracingTranslucencyShadows` | `EReflectedAndRefractedRayTracedShadows::Hard_shadows` | 623 |
| `RayTracingTranslucencyRefraction` | `1` | 624 |
| `RayTracingTranslucencyMaxPrimaryHitEvents` | `4` | 625 |
| `RayTracingTranslucencyMaxSecondaryHitEvents` | `2` | 626 |
| `RayTracingTranslucencyUseRayTracedRefraction` | `0` | 627 |

### Ray Tracing (Global Illumination)

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `LumenMaxRefractionBounces` | `0` | 613 |
| `RayTracingGIMaxBounces` | (see schema) | 1341 |
| `RayTracingGISamplesPerPixel` | (see schema) | 1344 |

### Path Tracing

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `PathTracingMaxBounces` | `32` | 629 |
| `PathTracingSamplesPerPixel` | `2048` | 630 |
| `PathTracingMaxPathIntensity` | `24.0f` | 631 |
| `PathTracingEnableEmissiveMaterials` | `1` | 632 |
| `PathTracingEnableReferenceDOF` | `0` | 633 |
| `PathTracingEnableReferenceAtmosphere` | `0` | 634 |
| `PathTracingEnableDenoiser` | `1` | 635 |
| `PathTracingIncludeEmissive` | `1` | 637 |
| `PathTracingIncludeDiffuse` | `1` | 638 |
| `PathTracingIncludeIndirectDiffuse` | `1` | 639 |
| `PathTracingIncludeSpecular` | `1` | 640 |
| `PathTracingIncludeIndirectSpecular` | `1` | 641 |
| `PathTracingIncludeVolume` | `1` | 642 |
| `PathTracingIncludeIndirectVolume` | `1` | 643 |

### Mobile & Other

| Parameter | Default Value | File:Line |
|-----------|---------------|-----------|
| `bMobileHQGaussian` | `false` | 645 |
| `UserFlags` | `0` | 647 |

---

## Reference Notes

1. **All bOverride_* flags** are initialized to `false` via `FMemory::Memzero(this, sizeof(FPostProcessSettings))` at line 370
2. **All FColor values** use UE5 convention: R=170 = 0.4 in linear space
3. **Extended Luminance Range** (EV100 mode) depends on CVar `r.DefaultFeature.AutoExposure.ExtendedDefaultLuminanceRange`
4. **Auto Exposure Bias** base value comes from CVar `r.DefaultFeature.AutoExposure.Bias`
5. **Bloom Intensity** may be overwritten by CVar `r.DefaultFeature.Bloom` at runtime (see line 421 comment)
6. **DepthOfFieldBladeCount** references a constant `kDefaultDepthOfFieldBladeCount` defined in the header file
7. **All memzero'd members** (those not explicitly assigned) default to zero/false
