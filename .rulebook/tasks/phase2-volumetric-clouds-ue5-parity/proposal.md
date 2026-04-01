<!-- doc-meta
title: Volumetric Clouds — UE5 Feature Parity
phase: 2
priority: P1
status: proposed
depends-on: phase0-3.1.0-volumetric-clouds-rewrite
created: 2026-04-01
-->

# Volumetric Clouds — UE5 Feature Parity

## Motivation

UzEngine has a working volumetric cloud system (CloudPass, VolumetricCloudsComponent, ray-march
compute shader) but it only covers ~35% of the features present in UE5's volumetric cloud system.
The goal is to bring UzEngine's clouds to full UE5 feature parity based on analysis of:

- **UE5.6 Source**: `VolumetricCloudComponent.h/.cpp` and `VolumetricCloudRendering.h`
- **UE4.27 Documentation**: Volumetric Clouds reference (Epic Games)
- **Published papers**: Schneider 2015, Hillaire 2020, Wrenninge 2015

## Current State (What We Have)

### Working
- Half-res compute ray-march with 4-octave procedural FBM noise
- Dual-lobe Henyey-Greenstein phase function
- Beer-Lambert extinction + Frostbite analytical integration
- Multi-scatter energy approximation (3-bounce, Schneider 2015)
- Beer-Powder silver lining effect
- Light marching toward sun (6 steps) for self-shadow
- Height gradient for cumulus shape profile
- Aerial perspective sky color fade-out
- Wind animation
- Half-res → full-res composite with geometry masking (depth)
- ECS component with ~25 parameters
- Lua API (42 getter/setter functions)
- Scene file serialization

### Stub / Not Wired
- `cloud_noise_shape_cs.hlsl` — STUB (empty)
- `cloud_noise_detail_cs.hlsl` — STUB (empty)
- `cloud_weather_cs.hlsl` — STUB (empty)
- `cloud_temporal_cs.hlsl` — implemented but DISABLED (temporalBlendFactor=1.0)

## Gap Analysis: UzEngine vs UE5

### Component Properties (UE5 Source: VolumetricCloudComponent.h)

| UE5 Property | UE5 Default | UzEngine | Status |
|---|---|---|---|
| LayerBottomAltitude | 5.0 km | layerBottomAltitudeKm = 7.0 | EXISTS — fix default |
| LayerHeight | 10.0 km | layerHeightKm = 6.0 | EXISTS — fix default |
| TracingStartMaxDistance | 350.0 km | tracingStartMaxDistance = 350.0 | EXISTS |
| TracingStartDistanceFromCamera | 0.0 km | tracingStartDistanceFromCamera = 0.0 | EXISTS |
| TracingMaxDistanceMode | DistanceFromCloudLayerEntryPoint | tracingMaxDistanceMode | EXISTS |
| TracingMaxDistance | 50.0 km | tracingMaxDistanceKm = 50.0 | EXISTS |
| PlanetRadius | 6360.0 km | — | MISSING |
| GroundAlbedo | (170,170,170) ≈ 0.4 linear | — | MISSING |
| Material (volume domain) | default cloud mat | — | N/A (no material graph) |
| bUsePerSampleAtmosphericLightTransmittance | false | — | MISSING |
| SkyLightCloudBottomOcclusion | 0.5 | skyLightCloudBottomOcclusion = 0.2 | EXISTS — fix default |
| ViewSampleCountScale | 1.0 | viewSampleCountScale = 0.5 | EXISTS — fix default |
| ReflectionViewSampleCountScale | 1.0 | reflectionViewSampleCountScale = 0.1 | EXISTS — fix default |
| ShadowViewSampleCountScale | 1.0 | shadowViewSampleCountScale = 0.5 | EXISTS — fix default |
| ShadowReflectionViewSampleCountScale | 1.0 | shadowReflectionViewSampleCountScale = 0.1 | EXISTS — fix default |
| ShadowTracingDistance | 15.0 km | shadowTracingDistance = 10.0 | EXISTS — fix default |
| StopTracingTransmittanceThreshold | 0.005 | stopTracingTransmittanceThreshold = 0.01 | EXISTS — fix default |
| AerialPerspectiveRayleighScatteringStartDistance | 0.0 | 0.0 | EXISTS |
| AerialPerspectiveRayleighScatteringFadeDistance | 0.0 | 10.0 | EXISTS — fix default |
| AerialPerspectiveMieScatteringStartDistance | 0.0 | 0.0 | EXISTS |
| AerialPerspectiveMieScatteringFadeDistance | 0.0 | 10.0 | EXISTS — fix default |
| bHoldout | false | — | MISSING (low priority) |
| bRenderInMainPass | true | — | MISSING (low priority) |
| bVisibleInRealTimeSkyCaptures | true | — | MISSING |

### Rendering Features (UE5 Source: VolumetricCloudRendering.h)

| Feature | UE5 | UzEngine | Status |
|---|---|---|---|
| Cloud Shadow Maps (2 lights) | Full: orthographic CSM, temporal AA, cascade system | None | MISSING — Critical |
| Cloud Sky AO (ambient occlusion from clouds) | Full: sky AO map, configurable strength/depth/samples | None | MISSING |
| Dual atmospheric light support (sun + moon) | Full: 2-light arrays for all shadow/scatter params | Single sun only | MISSING |
| Per-sample atmospheric light transmittance | Optional flag, per-sample LUT lookup | None | MISSING |
| Cloud shadow on ground (ground darkening) | Via CloudShadowMap sampled in deferred lighting | None | MISSING — Critical |
| God rays / light shafts through clouds | Via cloud shadow + volumetric fog interaction | None | MISSING |
| Beer Shadow Maps (BSM) | Alternative to ray-marched shadows | None | MISSING |
| Conservative Density (two-step material eval) | Material node: skip expensive eval if density=0 | None | MISSING |
| Ground Contribution (bottom lit from ground albedo) | GroundAlbedo property + shader integration | None | MISSING |
| Cloud Emissive (self-illumination) | Material output node | None | MISSING |
| Multi-scatter octaves (configurable count) | Via Volumetric Advanced Material Output | Fixed 3-bounce approximation | PARTIAL |
| Blue noise temporal dithering | Full 3D blue noise with Bayer pattern | None | MISSING |
| Quarter-res + temporal reconstruction (16 frames) | Full: reconstruct over 16 frames | Half-res, no temporal | MISSING — Critical |
| Min/Max depth optimization | Depth hierarchy for early ray termination | None | MISSING |
| Cloud rendering in reflections | Separate sample count scale for reflections | Not rendered in reflections | MISSING |
| Cloud rendering in sky captures | bVisibleInRealTimeSkyCaptures flag | Not captured | MISSING |
| Holdout / alpha composite mode | bHoldout property | None | MISSING (low priority) |
| Volumetric fog interaction during reconstruction | ShouldVolumetricCloudsApplyFogDuringReconstruction() | None | MISSING |
| Separated Mie/Rayleigh aerial perspective | VolumetricCloudWantsSeparatedAtmosphereMieRayLeigh() | Combined only | MISSING |
| Local light sampling on clouds | VolumetricCloudWantsToSampleLocalLights() | None | MISSING (low priority) |
| Debug view modes | Conservative density viz, debug view mode | None | MISSING |
| Weather map (2D coverage/type texture) | Material-driven, RGBA channels per layer | Procedural only | MISSING |
| Shape noise (precomputed 3D) | 3D noise textures (Perlin-Worley, Worley) | Procedural FBM fallback | PARTIAL |
| Detail noise (precomputed 3D) | High-frequency 3D noise | None | MISSING |
| Async compute overlap | Cloud as async compute pass | Sequential only | MISSING |

### Material System (UE5 Volumetric Advanced Material Output)

| Feature | Description | UzEngine | Status |
|---|---|---|---|
| Volume domain material | Cloud shape/density defined in material graph | Hardcoded in compute shader | MISSING (different architecture) |
| Volumetric Advanced Output node | 11 outputs: Albedo, Extinction, Emissive, AmbientOcclusion, PhaseG, PhaseG2, MultiScatteringContribution/Occlusion/Eccentricity, ConservativeDensity, GroundContribution | N/A | N/A — UzEngine uses component params instead of material graph |
| Per-pixel cloud type | Material can vary cloudType across the volume | Single global cloudType | PARTIAL |

## Approach

UzEngine does NOT have a material graph system, so we implement equivalent features as:
1. **Component parameters** — exposed to ECS and Lua
2. **Texture inputs** — weather map, noise textures loaded from disk
3. **Shader compile-time defines** — feature toggles (e.g., `#define CLOUD_GROUND_CONTRIBUTION 1`)

## References

- UE5.6 Source: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Classes/Components/VolumetricCloudComponent.h`
- UE5.6 Source: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/VolumetricCloudRendering.h`
- @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
- @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
- @ref Wrenninge 2015 "Art-Directable Multiple Volumetric Scattering"
- @ref Oz & Kulla 2017 "Approximate Multiple Scattering Octaves"
- @ref Karis 2014 "High Quality Temporal Supersampling"
- [UE4.27 Volumetric Clouds Reference](https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/LightingAndShadows/VolumetricClouds/VolumetricCloudsReference)
- [UE5.7 Volumetric Cloud Component](https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-in-unreal-engine)
