# Atmosphere System

<!-- doc-meta
title: Atmosphere System
category: spec
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: docs-operator
-->

## Overview

The atmosphere system provides a physically-based sky with a transmittance LUT,
height-based exponential fog, and aerial perspective scattering. All atmosphere
state is driven by ECS components (`SkyAtmosphereComponent`, `HeightFogComponent`).
The `AtmosphereRenderer` and `FogRenderer` extract component data per frame and
register render graph passes without accessing the scene renderer internals directly.

## Architecture

### Render Graph Pass Order

```
[Transmittance LUT compute]  (if dirty — on sky parameter change)
         |
         v
[Sky Cubemap Capture]        (if skyIBLDirty — sky changed since last IBL bake)
         |
         v
[Sky Atmosphere fullscreen]  (draws procedural sky into HDR scene color)
         |
         v
[Height Fog]                 (blends height-based exponential fog over scene)
         |
         v
[Deferred Lighting]          (reads transmittance LUT SRV for aerial perspective)
```

The IBL bake (`PostFrameAtmosphereBake`) runs after render graph execution to ensure
the sky cubemap contains valid data before `BakeEnvironment()` is dispatched.

### SkyAtmosphereComponent

ECS component read via `World::View<SkyAtmosphereComponent>()`. First component found
wins (singleton semantics). Carries Rayleigh scattering coefficients, Mie scattering
coefficients, absorption, planet radius, atmosphere thickness, sun disk parameters, and
sky luminance factor. All values match UE5 `SkyAtmosphereComponent` defaults.

### Transmittance LUT

- **Shader**: `transmittance_lut_cs.hlsl`
- **Purpose**: Precomputes sun transmittance through the atmosphere for each
  (altitude, view zenith angle) pair
- **Triggered**: Once at startup and whenever sky parameters change
- **Output**: Bound as SRV to the deferred lighting pass for aerial perspective

### Sky Atmosphere Pass

- **Shader**: `sky_atmosphere_ps.hlsl`
- **Type**: Fullscreen triangle raster pass
- **Reads**: Transmittance LUT SRV, `PerFrameCB` (sun direction, sun color)
- **Writes**: HDR scene color (blended over pixels at max depth)
- **Includes**: `aerial_perspective.hlsli` for atmospheric inscattering math

### Aerial Perspective

`aerial_perspective.hlsli` implements atmospheric inscattering applied in the deferred
lighting pass. For each lit pixel, it computes the amount of sky light scattered into
the view ray over the ray's travel distance through the atmosphere. This produces the
characteristic haze effect where distant surfaces appear desaturated and sky-tinted.

The implementation references UE5's Bruneton model. Full froxel-based volumetric aerial
perspective (matching UE5's `FComputeSkyAtmosphereCommon`) is a Phase 3 planned feature.

### Height Fog

`FogRenderer` (`fog_renderer.h/cpp`) reads `HeightFogComponent` from ECS (first found)
and registers the height fog pass if the component exists.

Height fog density distribution:

```
Density(z) = fogDensity * exp(-fogHeightFalloff * (z - fogBaseHeight))
```

Transmission per ray:

```
T = exp(-density * rayLength)
```

CPU precomputation — `RayOriginTerms` avoids per-pixel `exp()`:

```cpp
RayOriginTerms.x = fogDensity * exp(-fogHeightFalloff * (cameraZ - fogBaseHeight));
RayOriginTerms.y = fogHeightFalloff;
```

- **Shader**: `height_fog_ps.hlsl`
- **Type**: Fullscreen triangle raster pass over the HDR buffer
- **Reads**: Depth buffer SRV, `PerFrameCB`, `HeightFogCB`
- **Supports**: Two-layer fog, directional inscattering (sun halo, Mie-style)

`HeightFogComponent` parameters (Phase 2.22.0):

**Core exponential fog** (Phase 1): `fogColor`, `fogDensity`, `fogBaseHeight`,
`fogHeightFalloff`, `fogCutoffDistance`, `fogInscatteringColor`, `dirInscatteringExponent`,
`dirInscatteringStartDistance`, and a second layer with independent density and height.

**Volumetric fog** (Phase 2.22.0, UE5 parity): `bEnableVolumetricFog`, `VolumetricFogScatteringDistribution`,
`VolumetricFogAlbedo`, `VolumetricFogEmissive`, `VolumetricFogExtinctionScale`,
`VolumetricFogDistance`, `VolumetricFogStartDistance`, `VolumetricFogNearFadeInDistance`.
Enables full volumetric fog rendering in the froxel grid, with phase function control,
emissive contribution, and distance-based fade. Single-layer volumetric fog (Phase 3 planned:
multi-layer volumetric support via dedicated VolumetricFogComponent).

**Inscattering texture** (Phase 2.22.0, UE5 parity): `InscatteringColorCubemap`,
`InscatteringColorCubemapAngle`, `InscatteringTextureTint`, `FullyDirectionalInscatteringColorDistance`,
`NonDirectionalInscatteringColorDistance`. Allows directional inscattering to be driven by
an external cubemap (e.g., captured sky) with rotation and tinting. Complements sky
atmosphere contribution with directional lighting information.

### Sky Cubemap Capture

Two shader variants capture the procedural sky into a 6-face RGBA16F cubemap for IBL:

- `sky_cubemap_cs.hlsl` — compute shader dispatch, one thread group per face
- `sky_cubemap_ps.hlsl` — raster variant rendering 6 fullscreen quads

The captured cubemap is passed to `IBLBaker::BakeEnvironment()` after graph execution.

### Volumetric Clouds (M0-M5)

`VolumetricCloudsComponent` supplies the first cloud entity's settings to
`CloudPass`. All cloud producers and consumers are tracked render-graph passes
using the current frame's camera; there is no deferred previous-frame dispatch.
The default path registers `Cloud_Raymarch` after lighting, reads scene depth
and disk-loaded shape/detail/STBN resources, writes half-resolution RGBA16F
(premultiplied radiance, transmittance), then `Cloud_Composite` blends it into
HDR scene color. Depth clips cloud rays against opaque geometry.

The march intersects spherical cloud shells in kilometers, clamps distance by
`tracingMaxDistanceKm`, and uses a counted **256-iteration** upper bound.
Perspective-scaled steps respect component min/max step sizes, grow faster in
empty space, and use footprint-derived mip levels. A 128x128x64 R8 offline
spatiotemporal blue-noise volume jitters the initial sample by pixel and frame.
Noise is never baked on the GPU. `r.Cloud.ProceduralNoise=true` preserves the
current broken-cloud FBM default; false samples the loaded 128-cubed
Perlin-Worley shape volume. Both use the 32-cubed detail volume.

Lighting integrates Beer-Lambert extinction analytically per segment.
Self-shadow light marches have at most eight samples. Up to eight Oz/Kulla
scattering orders attenuate scattering/extinction/eccentricity by 0.5, with
height-dependent sky illumination and optional powder (default scale zero).
Dual-lobe HG is the default phase; HG/Draine is a compile-time option.
Cloud aerial perspective remains exponential sky-color haze, not the planned
camera-to-cloud atmosphere LUT integration. Silver-lining acceptance remains
open; brighter multi-scatter interiors are shipped.

`r.Clouds.TemporalUpscale=false` keeps the default half-resolution path.
Enabling it selects the quarter-resolution raymarch and a rotating 4x4 Bayer
sample, writes cloud-weighted distance, and registers `Cloud_Temporal`
to produce full-resolution ping-pong history before compositing. The resolve
uses cloud-point reprojection for sky, closest reversed-Z depth and velocity
for geometry, 3x3 YCoCg/transmittance variance clipping, and invalid-history
rejection. It never reads uninitialized history.

An optional `layers` array contains one to four shells with altitude, height,
density scale, shape/detail amount, weather RGBA channel/exponent, bias and
`exp * exp(exp_exponent * h) + linear * h + constant` profiles.
Empty weather paths mean uniform coverage; authored images load as linear
RGBA8 through VFS. Overlapping layers add density, gaps are skipped, and all
layers share one 256-iteration budget. Omitted layers and a neutral single
layer retain the original path. Global `cloudType=0.5` retains its original
height gradient; other types interpolate flatter stratus and taller cumulus.
CloudCB remains **1024 bytes**, with layer arrays at offsets 368/432/496.

`r.Clouds.GroundShadows=false` is opt-in. When enabled with an eligible sun,
`Cloud_Shadow_Cascade0..2` and `Cloud_Shadow_Resolve0..2` run before
deferred lighting, generating and temporally resolving one to three Beer
shadow cascades. Defaults are two 512-square maps over 50 km. An alternate
deferred-lighting pipeline attenuates direct sunlight, and alternate cloud
raymarch pipelines sample BSM optical depth instead of local light marches.
These passes declare noise/weather/history dependencies and use initialized
history gating. Readable cloud-shaped ground silhouettes remain opt-in visual
polish, not a completed default feature.

Remaining artistic literals are controlled by `r.Cloud.*` CVars. Their shipped
defaults exactly match the former shader constants:

| CVar suffix | Default |
|---|---|
| `SkyBottomVisibility` | 0.8 (also legacy sky normalization divisor) |
| `WindZScale`, `DetailWindZScale` | 0.05, 0.1 |
| `DetailFrequencyScale`, `DetailUVScale`, `DetailMipScale` | 2, 0.25, 0.125 |
| `WeatherUVScale` | 0.002 |
| `HeightBottomStart`, `HeightBottomEnd` | 0, 0.07 |
| `HeightTopEnd`, `HeightTopStart` | 1, 0.3 |
| `HeightStratusEnd`, `HeightStratusStart`, `HeightCumulusStart` | 0.35, 0.1, 0.8 |
| `LightMarchHeightScale` | 2 (maximum self-shadow distance in layer heights) |
| `AerialPerspectiveDensity` | 0.04 |

Keep height-ramp endpoints distinct when authoring overrides. Noise/weather
retain the existing s3 wrap sampler; shadow sampling retains s2, never s0.

## Key Files

| File | Purpose |
|---|---|
| `runtime/engine/renderer/atmosphere/atmosphere_renderer.h/cpp` | Render graph pass registration + IBL bake orchestration |
| `runtime/engine/renderer/fog/fog_renderer.h/cpp` | Height fog ECS extraction + pass registration |
| `runtime/shaders/runtime/atmosphere/sky_atmosphere_ps.hlsl` | Procedural sky fullscreen pass |
| `runtime/shaders/runtime/atmosphere/transmittance_lut_cs.hlsl` | Transmittance LUT compute shader |
| `runtime/shaders/runtime/atmosphere/height_fog_ps.hlsl` | Height fog fullscreen pass |
| `runtime/shaders/runtime/atmosphere/sky_cubemap_cs.hlsl` | Sky cubemap capture (compute variant) |
| `runtime/shaders/runtime/atmosphere/sky_cubemap_ps.hlsl` | Sky cubemap capture (raster variant) |
| `runtime/shaders/runtime/include/aerial_perspective.hlsli` | Atmospheric inscattering math |

## UE5 Reference

| UzEngine System | UE5 Equivalent |
|---|---|
| `SkyAtmosphereComponent` | `USkyAtmosphereComponent` |
| `HeightFogComponent` | `UExponentialHeightFogComponent` |
| Transmittance LUT | `RenderSkyAtmosphereLutTransmittance`, `SkyAtmosphereCommon.usf` |
| Aerial perspective | `FComputeSkyAtmosphereCommon`, `SkyAtmosphereCommon.usf` |
| Height fog density | `FExponentialHeightFogSceneInfo::ComputeInscatteringColor`, `HeightFogCommon.ush` |
| Sky pass | `FSkyPassMeshProcessor`, `SkyPassRendering.cpp` |
| Bruneton model | `SkyAtmosphere.usf` (Sebastien Hillaire's implementation in UE5) |

## Status

| Feature | Status |
|---|---|
| Transmittance LUT compute | Done |
| Procedural sky fullscreen pass | Done |
| Sky cubemap capture (CS + PS) | Done |
| Height fog (single layer) | Done |
| Height fog (two layers) | Done |
| Height fog directional inscattering | Done |
| Volumetric fog parameters (Phase 2.22.0) | Done |
| Inscattering texture cubemap parameters (Phase 2.22.0) | Done |
| Aerial perspective (`aerial_perspective.hlsli`) | Done |
| Aerial perspective in deferred lighting | Done |
| Full Bruneton froxel volumetric aerial perspective | Planned (Phase 3) |
| SkyAtmosphereComponent trace quality parameters (Phase 2.22.0) | Done |
| SkyAtmosphereComponent full parameter set | Done |
| HeightFogComponent ECS integration | Done |
| HeightFogComponent full UE5 property parity (Phase 2.22.0) | Done |
