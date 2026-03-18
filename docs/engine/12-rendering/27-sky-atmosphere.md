# Sky Atmosphere System

<!-- doc-meta
title: Sky Atmosphere System
category: spec
engine: uzengine
theme: rendering
created: 2026-03-16
updated: 2026-03-16
status: active
author: docs-operator
-->

## Overview

UzEngine provides a physically-based sky atmosphere renderer implementing the **Hillaire 2020 single-scatter model** with **Bruneton 2017 transmittance parameterization**. The system renders a dynamic, realistic sky with wavelength-dependent Rayleigh scattering (blue color), Mie scattering (haze), and sun disk contributions. Atmosphere parameters follow **UE5 conventions** for seamless portability of sky configurations.

The implementation uses a **precomputed transmittance LUT** to eliminate expensive per-pixel secondary ray marching, delivering interactive frame rates while maintaining visual fidelity. All distances are in **kilometers** (not meters) to match planetary-scale conventions and avoid floating-point precision issues.

## Architecture

The system consists of three main components:

### 1. Transmittance LUT Pass (Compute)
- **Input**: Atmosphere parameters (Rayleigh/Mie scattering, scale heights, planet radius)
- **Output**: 256×64 RGBA16F texture encoding optical transmittance from any (height, zenith_angle) to the top of the atmosphere
- **Frequency**: Cached and recomputed only when atmosphere parameters change (dirty flag invalidation)
- **Integration**: 40-step numerical integration along ray toward top of atmosphere (< 0.5% error vs reference)
- **Parameterization**: Bruneton 2017 non-linear UV mapping concentrating precision near the horizon where transmittance gradients are steepest
- **Pipeline**: Compute shader dispatch [32, 8, 1] thread groups of [8, 8, 1] threads (256×64 total)

### 2. Sky Atmosphere Pass (Pixel Shader)
- **Input**: Depth G-Buffer (for sky/geometry masking), precomputed transmittance LUT, atmosphere parameters, camera transform, sun direction/color
- **Output**: Rendered sky into backbuffer pixels where depth == 1.0 (far plane only)
- **Frequency**: Once per frame
- **Integration**: Frostbite analytical per-step integration (same as UE5) — avoids underflow from accumulated exp(-tau)
  ```
  Sint = (scatter - scatter * exp(-ext * dt)) / max(ext, eps)
  L   += Throughput * Sint
  T   *= exp(-ext * dt)
  ```
- **View Integration**: 16 steps from camera toward atmosphere top
- **Ordering**: Renders AFTER geometry + lighting (preserves lit scene) and BEFORE fog

### 3. Sky Cubemap Capture Pass (Compute)
- **Input**: Same atmosphere parameters (no sun disk for pure IBL)
- **Output**: 64×64×6 RGBA16F cubemap for IBL irradiance/prefiltered specular computation
- **Frequency**: Cached when atmosphere changes
- **Purpose**: IBL input (sky-only environment lighting, no sun), prevents environment-map seams

## Physical Model

### Rayleigh Scattering

Wavelength-dependent molecular scattering from atmospheric gases (N₂, O₂). Causes the **blue color of the sky**.

- **Coefficients** (Earth default): β_R = (5.802, 13.558, 33.1) × 10⁻³ km⁻¹
  - Red: 5.802 × 10⁻³
  - Green: 13.558 × 10⁻³
  - Blue: 33.1 × 10⁻³ (blue has ~6× higher scattering)
- **Density**: Exponential, halves every **8 km** (scale height H_R = 8 km)
- **Phase function**: Isotropic (uniform in all directions)
- **Formula**: β_R(h) = β_R(sea) × exp(-h / H_R)

### Mie Scattering

Wavelength-independent scattering from aerosols and suspended particles (dust, pollution, humidity). Creates **haze and sun glow**.

- **Coefficient** (Earth default): β_M = 3.996 × 10⁻³ km⁻¹
- **Absorption**: Separate absorption coefficient 1.11× β_M (tunable via mieAbsorption)
- **Anisotropy**: Henyey-Greenstein phase function with **g = 0.8** (strong forward scattering toward sun)
- **Density**: Exponential, halves every **1.2 km** (scale height H_M = 1.2 km, much lower than Rayleigh)
- **Formula**: β_M(h) = β_M(sea) × exp(-h / H_M)

### Extinction

Total light absorption along a ray path:

```
τ(t) = ∫[0,t] (β_R(h) + β_M(h) + β_abs(h)) ds
```

Where:
- **Rayleigh extinction** = Rayleigh scattering (no absorption)
- **Mie extinction** = Mie scattering + Mie absorption (≈ 1.11× scattering)
- **Transmittance** = exp(-τ)

### In-Scattering

Light scattered into the view direction at each point along the ray:

```
L_in = β_R(h) × P_R(theta) × L_sun × T(h → top)
     + β_M(h) × P_M(theta) × L_sun × T(h → top)
```

Where:
- **P_R(theta)** = Rayleigh phase (isotropic)
- **P_M(theta)** = Henyey-Greenstein phase (peaks forward at angle θ = 0)
- **T(h → top)** = Transmittance from altitude h to top of atmosphere (precomputed in LUT)

## Transmittance LUT

### Parameterization

The LUT encodes **sun transmittance** — optical transmittance along a ray from (height, zenith_angle) toward the sun. Uses Bruneton 2017 non-linear UV mapping:

| Axis | Range | Physical Meaning |
|------|-------|------------------|
| **U** | [0, 1] | Non-linear distance from horizon to atmosphere top |
| **V** | [0, 1] | Non-linear altitude from sea level to top of atmosphere |

The non-linear mapping concentrates precision where transmittance changes most rapidly:
- **V = 0**: Sea level (high extinction, transmittance ≈ varies)
- **V = 1**: Top of atmosphere (T ≈ 1.0 everywhere)
- **U = 0**: Horizon (steepest transmittance gradient)
- **U = 1**: Zenith (T ≈ constant, less precision needed)

### Validation

Expected values for Earth-like atmosphere:

| Sample Point | Expected Transmittance | Notes |
|---|---|---|
| Sea level, zenith | T ≈ (0.97, 0.98, 0.99) | Most transparent (least air mass) |
| Sea level, horizon | T ≈ (0.7, 0.8, 0.9) | Least transparent (maximum air mass) |
| Top of atmosphere (any angle) | T ≈ (1.0, 1.0, 1.0) | No extinction above atmosphere |

### Format

- **Resolution**: 256×64 pixels
- **Format**: RGBA16F (16-bit float per channel)
- **Channels**: R=Red transmittance, G=Green, B=Blue, A=Unused
- **Caching**: Recomputed only when atmosphere parameters change (dirty flag)

## SkyAtmosphereComponent

The ECS component controlling all atmosphere parameters. Follows UE5 SkyAtmosphereComponent conventions.

### Key Parameters

```cpp
// Planet geometry (kilometers)
float bottomRadiusKm = 6360.0f;      // Planet radius to sea level
float atmosphereHeightKm = 60.0f;    // Thickness above sea level

// Rayleigh scattering (km⁻¹)
Vec3  rayleighScattering = {5.802e-3, 13.558e-3, 33.1e-3};
float rayleighScatteringScale = 1.0f;  // Artistic multiplier
float rayleighScaleHeightKm = 8.0f;    // Density scale height

// Mie scattering (km⁻¹)
float mieScattering = 3.996e-3f;
float mieScatteringScale = 1.0f;       // Artistic multiplier
float mieAbsorption = 4.4e-3f;         // Separate absorption
float mieAbsorptionScale = 1.0f;
float mieAnisotropy = 0.8f;            // Forward scattering (Henyey-Greenstein g)
float mieScaleHeightKm = 1.2f;

// Ground reflection
Vec3 groundAlbedo = {0.3f, 0.3f, 0.3f};

// Multi-scattering approximation
float multiScatteringFactor = 1.0f;    // 0 = single, 1 = full

// Artistic control
Vec3 skyLuminanceFactor = {1.0f, 1.0f, 1.0f};  // Per-channel brightness
float heightFogContribution = 1.0f;

// Transform mode
enum TransformMode {
    PlanetTopAtComponentTransform = 0,   // World Z=0 is sea level
    PlanetCenterAtComponentTransform = 1 // World Z=component position
};
```

### Sun Parameters

Sun direction, color, and intensity are driven by a `DirectionalLightComponent` with `atmosphereSunLight=true`. The fallback fields below are used only when no linked light exists:

```cpp
Vec3  sunIlluminance = {20.f, 20.f, 20.f};    // Fallback illuminance
float sunAngularRadius = 0.00872665f;         // ~0.5° (Earth's sun)
float sunDiskIntensity = 1.0f;                // Fallback sun disk brightness
```

### IBL Integration

Sky ambient lighting is controlled by `SkyLightComponent`, not this component. The deprecated `skyLightIntensity` and `skyLightColor` fields are ignored when a SkyLightComponent exists.

## SkyAtmosphereCB Layout

Per-frame constant buffer (256 bytes) bound at register `b2` in the sky atmosphere pass:

| Offset | Size | Field | HLSL Type | Purpose |
|--------|------|-------|-----------|---------|
| 0 | 16 | rayleighScattering | float4 | xyz = RGB coefficients (km⁻¹), w = scale height (km) |
| 16 | 16 | mieScattering | float4 | x = coeff, y = anisotropy g, z = scale height, w = unused |
| 32 | 16 | groundAlbedo | float4 | xyz = reflectance, w = unused |
| 48 | 16 | sunIlluminance | float4 | xyz = sun color × illuminance, w = angular radius (rad) |
| 64 | 4 | bottomRadiusKm | float | Planet radius |
| 68 | 4 | atmosphereHeightKm | float | Atmosphere thickness |
| 72 | 4 | sunDiskIntensity | float | Sun disk brightness |
| 76 | 4 | multiScatteringFactor | float | Multi-scatter strength [0,1] |
| 80 | 4 | rayleighScatteringScale | float | Artistic Rayleigh scale |
| 84 | 4 | mieScatteringScale | float | Artistic Mie scale |
| 88 | 4 | mieAbsorptionScale | float | Artistic Mie absorption scale |
| 92 | 4 | mieAbsorption | float | Mie absorption (km⁻¹) |
| 96 | 16 | skyLuminanceFactor | float4 | Per-channel brightness tint |
| 112 | 144 | _skyPad | float[36] | Padding to 256 bytes |

## Artistic Controls

The system separates **physical extinction** (used for transmittance LUT) from **artistic in-scattering**. This allows realistic transmittance while permitting color/intensity adjustments for creative control.

| Parameter | Effect | Range | Notes |
|-----------|--------|-------|-------|
| **rayleighScatteringScale** | Intensity of blue scattering | 0–4 | 1.0 = physical; >1 = vivid; <1 = washed |
| **mieScatteringScale** | Intensity of haze/glow | 0–4 | 1.0 = physical; >1 = more haze |
| **mieAbsorptionScale** | Strength of light absorption | 0–2 | Higher = darker sky during absorption |
| **skyLuminanceFactor** | Per-channel brightness | (0–4)³ | Allows color grading (e.g., warm sunset) |
| **multiScatteringFactor** | Multiple-scattering contribution | 0–1 | 0 = single only; 1 = full multi-scatter |

These multipliers affect **only in-scattering** — the transmittance LUT uses physical values, ensuring view-dependent absorption remains realistic.

## Render Graph Integration

### Pass Ordering

```
Frame:
  1. Transmittance LUT Pass (compute)  ← if atmosphere dirty
  2. Sky Cubemap Capture (compute)     ← if atmosphere dirty
  3. Geometry rendering (G-Buffer)
  4. Lighting pass
  5. Sky Atmosphere Pass (pixel)       ← renders only sky pixels
  6. Fog pass
  7. Post-processing (tonemap, bloom)
```

### Barriers

Barriers are automatically managed by the render graph:

| Pass | Resource | State | Reason |
|------|----------|-------|--------|
| Transmittance LUT | lutTarget (u0) | UnorderedAccess | Compute write |
| Sky Atmosphere | transmittanceLutRes (t1) | ShaderResource | Pixel shader read |
| Sky Atmosphere | depthGBuffer (t0) | ShaderResource | Depth read |
| Sky Atmosphere | backbuffer | RenderTarget | Color output |

The graph automatically inserts a **UAV→SRV barrier** between Transmittance LUT (write) and Sky Atmosphere (read).

### Dirty Flag Protocol

The atmosphere is recomputed when:
1. Component parameters change (Rayleigh, Mie, radius, etc.)
2. SkyAtmosphereComponent is first added to a scene
3. A linked DirectionalLight's `atmosphereSunLight` flag changes

The renderer maintains a version counter per component. If the counter hasn't changed since last frame, both compute passes are skipped.

## Coordinate System & Units

All conventions follow **UE5 for seamless compatibility**:

| Quantity | Unit | Notes |
|----------|------|-------|
| **Distances (all)** | **Kilometers** | bottomRadiusKm, atmosphereHeightKm, scale heights |
| **Scattering coefficients** | **km⁻¹** | Not m⁻¹; use 10⁻³ km⁻¹ for typical values |
| **World position** | Centimeters | 1 world unit = 1 cm (UE5 parity) |
| **Conversion** | WORLD_TO_KM = 0.00001 | Multiply cm position by this to get km |
| **Planet center** | Sky-space (0, 0, 0) | Camera positioned relative to sea level via WORLD_TO_KM |
| **Z-axis** | Up direction | Z-up world (same as UzEngine) |
| **Angles** | Radians | Zenith angle, angular radius, etc. |

### Camera Snapping

To avoid surface-penetration artifacts:
- Camera is snapped to minimum **5 m above planet surface** in sky-space (0.005 km)
- Safe for all viewing angles and altitudes
- Matches UE5's `PlanetRadiusOffset`

## Earth Atmosphere Reference

Default parameters matching Earth's real atmosphere:

| Property | Value | Notes |
|----------|-------|-------|
| **Planet radius** | 6360 km | Sea level to center |
| **Atmosphere height** | 60 km | To edge of exosphere |
| **Rayleigh β (RGB)** | (5.802, 13.558, 33.1) × 10⁻³ km⁻¹ | Wavelength-dependent |
| **Rayleigh scale height** | 8 km | ~78% density below 8 km |
| **Mie β** | 3.996 × 10⁻³ km⁻¹ | Aerosols, particles |
| **Mie absorption** | 4.4 × 10⁻³ km⁻¹ | 1.11× scattering |
| **Mie scale height** | 1.2 km | Aerosols concentrated near surface |
| **Mie anisotropy (g)** | 0.8 | Forward scattering toward sun |
| **Ground albedo** | (0.3, 0.3, 0.3) | SRGB ~0.3 in linear |
| **Sun illuminance (fallback)** | (20, 20, 20) | Directional light overrides |
| **Sun angular radius** | 0.00872665 rad | ~0.5° (Earth's actual sun) |

## Performance Characteristics

| Operation | Time | Frequency | Notes |
|-----------|------|-----------|-------|
| **Transmittance LUT compute** | ~0.3 ms | On atmosphere change | 40-step integration, 256×64 |
| **Sky cubemap capture** | ~0.2 ms | On atmosphere change | 64×64×6, same model |
| **Sky atmosphere pass (pixel)** | ~0.5 ms | Every frame | 16-step integration, fullscreen |
| **Total (first frame)** | ~1.0 ms | Once | Both computes + pixel pass |
| **Total (cached)** | ~0.5 ms | Per frame | Pixel pass only, LUT cached |

Frame time at 1440p (2560×1440):
- 240 FPS when cached (0.5 ms in 4.17 ms frame)
- 200 FPS on atmosphere change (1.0 ms for both computes)

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| **Black sky** | Transmittance LUT not loaded | Check transmittanceLutSrvIndex in SkyAtmospherePassDesc |
| **Wrong sun position** | Light direction not toward sun | Ensure `lightDir` is normalized and points from camera toward sun |
| **Too much haze** | mieScatteringScale too high | Reduce to 0.5–1.5 range |
| **Washed-out colors** | skyLuminanceFactor > 1 or HDR tone mapping | Reduce skyLuminanceFactor or check exposure |
| **Horizon not blue** | Rayleigh scale height too high | Reduce rayleighScaleHeightKm to 6–8 km |
| **Atmosphere changes per frame** | Dirty flag not cleared | Check that component version is incremented only on actual changes |
| **Sky shows geometry seams** | Depth comparison issue | Verify depth == 1.0 test is correct (far plane) |

## References

- [SkyAtmosphereComponent Header](../../../runtime/engine/ecs/environment/sky_atmosphere_component.h)
- [Constant Buffers](../../../runtime/engine/renderer/core/params/constant_buffers.h)
- [Sky Atmosphere Pixel Shader](../../../runtime/shaders/runtime/atmosphere/sky_atmosphere_ps.hlsl)
- [Transmittance LUT Compute](../../../runtime/shaders/runtime/atmosphere/transmittance_lut_cs.hlsl)
- [Sky Atmosphere Pass](../../../runtime/engine/renderer/passes/atmosphere/sky_atmosphere_pass.h)
- [Transmittance LUT Pass](../../../runtime/engine/renderer/passes/atmosphere/transmittance_lut_pass.h)
- Hillaire, Sébastien. "A scalable and production ready sky and atmosphere rendering technique" *SIGGRAPH*, 2020.
- Bruneton, Eric et al. "A Practical Analytic Single Scattering Model for Real-Time Rendering" *EGSR*, 2017.
- [UE5 SkyAtmosphere Documentation](https://docs.unrealengine.com/en-US/rendering-and-graphics/sky-atmosphere-in-unreal-engine/)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-16 | Initial documentation — complete architecture, physics model, CB layout, artistic controls |
