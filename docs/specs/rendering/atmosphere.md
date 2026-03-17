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

`HeightFogComponent` parameters: `fogColor`, `fogDensity`, `fogBaseHeight`,
`fogHeightFalloff`, `fogCutoffDistance`, `fogInscatteringColor`, `dirInscatteringExponent`,
`dirInscatteringStartDistance`, and a second layer with independent density and height.

### Sky Cubemap Capture

Two shader variants capture the procedural sky into a 6-face RGBA16F cubemap for IBL:

- `sky_cubemap_cs.hlsl` — compute shader dispatch, one thread group per face
- `sky_cubemap_ps.hlsl` — raster variant rendering 6 fullscreen quads

The captured cubemap is passed to `IBLBaker::BakeEnvironment()` after graph execution.

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
| Aerial perspective (`aerial_perspective.hlsli`) | Done |
| Aerial perspective in deferred lighting | Done |
| Full Bruneton froxel volumetric aerial perspective | Planned (Phase 3) |
| SkyAtmosphereComponent full parameter set | Done |
| HeightFogComponent ECS integration | Done |
