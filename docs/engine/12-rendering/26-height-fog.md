# Height Fog System

<!-- doc-meta
title: Height Fog System
category: reference
engine: uzengine
theme: rendering
created: 2026-03-16
updated: 2026-03-16
status: active
author: docs-operator
-->

## Overview

Height fog renders exponential height-based atmospheric fog over the scene, creating depth perception and distance atmosphere. The system uses a fullscreen compute pass that blends fog color over the HDR scene based on pixel depth and camera height.

## Architecture

```
Input: HDR scene color + depth
         ↓
Height Fog Computation
  • Reconstruct world position from depth
  • Evaluate fog density vs. height
  • Compute inscattering (sun-facing halo)
  • Blend fog over scene
         ↓
Output: Fogged HDR scene color
```

The height fog pass executes **after lighting** and **before tonemap**, operating in HDR space.

## Mathematical Model

UzEngine height fog is based on **UE5's `CalculateLineIntegralShared` implementation** (Bruneton 2017).

### Density Distribution

Fog density decreases exponentially with height:

```
Density(z) = fogDensity * exp(-fogHeightFalloff * (z - fogBaseHeight))
```

Where:
- `z` = world height (cm)
- `fogDensity` = maximum density at `fogBaseHeight`
- `fogHeightFalloff` = height falloff rate (1/cm)
- `fogBaseHeight` = height where density = fogDensity

**Example**: With `fogBaseHeight = 0` and `fogHeightFalloff = 0.01`:
- At z=0: density = 100% of fogDensity
- At z=100: density = 37% (exp(-1))
- At z=500: density = 0.67% (exp(-5))

### Transmission (Visibility)

Visibility along a ray segment decreases exponentially:

```
Transmission(ray_length, avg_density) = exp(-avg_density * ray_length)
```

The pixel is blended as:

```
FinalColor = SceneColor * Transmission + FogColor * (1 - Transmission)
```

### Two-Layer Fog

UzEngine supports two independent fog layers (primary + secondary):

```cpp
struct HeightFogComponent {
    // Primary layer
    Color  fogColor;              // Primary fog color
    float  fogDensity;            // Primary maximum density
    float  fogHeightFalloff;      // Height falloff rate
    float  fogBaseHeight;         // Height of maximum density

    // Secondary layer (optional)
    Color  fogColorSecondary;
    float  fogDensitySecondary;
    float  fogHeightFalloffSecondary;
    float  fogBaseHeightSecondary;

    // Common parameters
    float  fogMaxOpacity;          // 0.0–1.0 (clamps fog alpha at infinity)
    float  fogStartDistance;       // Camera distance to start rendering fog
    float  fogCutoffDistance;      // Distance where fog reaches max opacity
};
```

**Why two layers?**
- Primary for ground-level fog (mist, ground-hugging gas)
- Secondary for higher-altitude fog (clouds, dust layers)
- Enables complex layered atmosphere effects

### Directional Inscattering

Sun-facing halo effect (Mie scattering approximation):

```hlsl
float3 inscattering = inscatteringColor * pow(saturate(sunDot), inscatteringExponent);
inscattering *= max(0.0, 1.0 - depth / inscatteringStartDistance);
fogColor += inscattering;
```

Where:
- `sunDot` = dot(ray_direction, sun_direction)
- `inscatteringColor` = Color of sun light scattered into eye
- `inscatteringExponent` = 4.0–32.0 (sharper = smaller sun disk)
- `inscatteringStartDistance` = Distance to start inscattering

**Effect**: Creates visible "light shafts" when looking toward the sun through fog.

## CPU Precomputation

**Key optimization**: Avoid expensive `pow()` per-pixel by precomputing:

```
RayOriginTerms = fogDensity * exp(-fogHeightFalloff * (cameraZ - fogBaseHeight))
```

Computed once per frame at CPU, uploaded to GPU.

```cpp
float ComputeRayOriginTerms(
    float cameraZ,
    float fogDensity,
    float fogHeightFalloff,
    float fogBaseHeight) {
    return fogDensity * exp(-fogHeightFalloff * (cameraZ - fogBaseHeight));
}
```

**Benefit**: Shader only computes `exp(-rayOriginTerms * ray_length)` per-pixel (one fast intrinsic).

## HeightFogComponent

ECS component providing artist controls:

```cpp
/// Exponential height-based fog for atmospheric depth perception.
struct HeightFogComponent {
    // Primary layer
    Color  fogColor = {0.8, 0.8, 0.8};     // White/gray fog
    float  fogDensity = 0.001;              // Max density at base height
    float  fogHeightFalloff = 0.002;        // Height falloff rate
    float  fogBaseHeight = 0.0;             // Height of maximum density (cm)

    // Secondary layer (optional, use for layers)
    Color  fogColorSecondary = {0.8, 0.8, 0.8};
    float  fogDensitySecondary = 0.0;       // 0 = disabled
    float  fogHeightFalloffSecondary = 0.002;
    float  fogBaseHeightSecondary = 5000.0; // Higher altitude layer

    // Directional inscattering (sun-facing halo)
    Color  inscatteringColor = {1.0, 0.8, 0.5};  // Orange/yellow
    float  inscatteringExponent = 8.0;      // Sharpness of sun disk
    float  inscatteringStartDistance = 10000.0;  // Distance to fade out

    // Global parameters
    float  fogMaxOpacity = 0.8;              // Clamp alpha at infinity
    float  fogStartDistance = 0.0;           // Min distance to start fog
    float  fogCutoffDistance = 50000.0;      // Max distance for full opacity

    bool   enabled = true;                   // Enable/disable fog
};
```

| Parameter | Default | Range | Purpose |
|-----------|---------|-------|---------|
| `fogColor` | White | RGB | Primary fog color |
| `fogDensity` | 0.001 | 0.0–1.0 | Maximum fog thickness |
| `fogHeightFalloff` | 0.002 | 0.0–0.1 | How fast fog decreases with height |
| `fogBaseHeight` | 0 | -∞ to +∞ | Height where fog is thickest |
| `fogMaxOpacity` | 0.8 | 0.0–1.0 | Fog alpha clamp at distance |
| `fogStartDistance` | 0 | 0–∞ | Min distance before fog appears |
| `fogCutoffDistance` | 50km | 0–∞ | Distance for full opacity |

## GPU Implementation

**File**: `engine/renderer/passes/atmosphere/height_fog_pass.h`

Fullscreen compute pass executed after lighting, before tonemap.

### Constant Buffer

**HeightFogCB** (512 bytes, root parameter b2):

```cpp
struct HeightFogCB {
    // Camera
    Vec4 cameraWorldPos;      // World position of camera eye (32 bytes)

    // Primary fog layer
    Vec4 fogColor;            // RGB color + opacity
    float fogRayOriginTerms;  // Precomputed density at camera height
    float fogHeightFalloff;   // exp decay with height
    float fogBaseHeight;      // Height of maximum density
    float _pad1;              // Alignment

    // Secondary fog layer
    Vec4 fogColorSecondary;
    float fogRayOriginTerms2;
    float fogHeightFalloff2;
    float fogBaseHeight2;
    float _pad2;

    // Directional inscattering
    Vec4 inscatteringColor;
    float inscatteringExponent;
    float inscatteringStartDistance;
    float _pad3, _pad4;

    // Global parameters
    float fogMaxOpacity;
    float fogStartDistance;
    float fogCutoffDistance;
    float _pad5;

    // Derived values (precomputed at CPU)
    float invFogCutoffDistance;  // 1.0 / fogCutoffDistance
    float _pad6, _pad7, _pad8;
};
```

### Shader Implementation

```hlsl
// height_fog_cs.hlsl
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID) {
    uint2 pixelPos = dispatchId.xy;
    float2 uv = pixelPos / float2(renderTargetSize);

    // 1. Fetch scene color (HDR)
    float3 sceneColor = sceneColorSRV.Load(pixelPos);

    // 2. Fetch and decode depth
    float depth = depthSRV.Load(pixelPos).r;  // Linear depth in cm
    if (depth <= 0.0 || depth >= 1e8) {
        fogColorUAV[pixelPos] = float4(sceneColor, 1.0);
        return;  // Skip fogging sky/far plane
    }

    // 3. Apply fog start distance
    if (depth < fogStartDistance) {
        fogColorUAV[pixelPos] = float4(sceneColor, 1.0);
        return;  // Fog hasn't started yet
    }

    // 4. Reconstruct world position
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 worldPos4 = mul(clipPos, invViewProj);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    // 5. Ray segment from camera to pixel
    float3 rayDir = normalize(worldPos - cameraWorldPos);
    float rayLength = length(worldPos - cameraWorldPos);

    // 6. Compute fog transmission for primary layer
    float cameraHeight = cameraWorldPos.z;
    float pixelHeight = worldPos.z;

    // Average height along ray (approximate)
    float avgHeight = (cameraHeight + pixelHeight) * 0.5;
    float density = fogDensity * exp(-fogHeightFalloff * (avgHeight - fogBaseHeight));
    float transmission = exp(-density * rayLength);

    // 7. Apply secondary layer
    if (fogDensitySecondary > 0.0) {
        float density2 = fogDensitySecondary * exp(-fogHeightFalloff2 * (avgHeight - fogBaseHeight2));
        float transmission2 = exp(-density2 * rayLength);
        transmission *= transmission2;  // Compound
    }

    // 8. Clamp transmission (fog max opacity)
    transmission = max(transmission, 1.0 - fogMaxOpacity);

    // 9. Directional inscattering (sun-facing halo)
    float3 sunDir = normalize(sunLightDirection);
    float sunDot = saturate(dot(rayDir, sunDir));
    float3 inscatter = inscatteringColor * pow(sunDot, inscatteringExponent);
    float inscatterFade = max(0.0, 1.0 - depth * invFogCutoffDistance);
    inscatter *= inscatterFade * inscatteringStartDistance;

    // 10. Composite fog
    float3 fogColorFinal = fogColor.rgb + inscatter;
    float3 resultColor = sceneColor * transmission + fogColorFinal * (1.0 - transmission);

    // 11. Write result (UAV, no alpha channel used)
    fogColorUAV[pixelPos] = float4(resultColor, 1.0);
}
```

### Render Graph Integration

**HeightFogPassDesc**:

```cpp
struct HeightFogPassDesc {
    rg::RgResourceId inputSceneColor;  // Read: HDR scene from previous pass
    rg::RgResourceId inputDepth;       // Read: linear depth buffer
    rg::RgResourceId outputFog;        // Write: fogged HDR scene
    CBAllocation     heightFogCB;      // Constant buffer (precomputed)
    RHIPipelineHandle pipeline;         // Compute PSO
    uint32_t         sceneColorSrvIdx;  // SM6.6 bindless
    uint32_t         depthSrvIdx;       // SM6.6 bindless
    uint32_t         fogColorUavIdx;    // SM6.6 bindless UAV
    RHITextureHandle sceneColorTex;     // SM5 fallback
    RHITextureHandle depthTex;          // SM5 fallback
    RHITextureHandle fogColorTex;       // SM5 fallback UAV
};
```

**Pass declares**:
- **Input scene color**: Read (ShaderResource state)
- **Input depth**: Read (ShaderResource state)
- **Output fog**: Write (UnorderedAccess state)

Barriers scheduled automatically by render graph.

## Integration into Frame Pipeline

**Position in render graph** (after deferred or forward lighting):

```
┌──────────────────────────────┐
│ Geometry & Lighting Passes   │ (PBR or Deferred)
└──────────────┬───────────────┘
               │
┌──────────────▼───────────────┐
│ Sky Atmosphere Pass          │
└──────────────┬───────────────┘
               │
┌──────────────▼───────────────────┐
│ Height Fog Pass (fullscreen)     │
│ Blends fog over lit scene        │
└──────────────┬───────────────────┘
               │
┌──────────────▼───────────────┐
│ Tonemap Pass (HDR→LDR)       │
└──────────────┬───────────────┘
               │
        Final backbuffer
```

## Usage Example

```cpp
// Scene setup
auto heightFog = entity.Add<HeightFogComponent>();
heightFog->fogColor = {0.8, 0.9, 1.0};     // Blueish fog
heightFog->fogDensity = 0.001;              // Light fog
heightFog->fogHeightFalloff = 0.002;        // Decreases quickly with height
heightFog->fogBaseHeight = 0.0;             // Sea level

// Per-frame update (in renderer)
float rayOriginTerms = ComputeRayOriginTerms(
    camera.position.z,
    heightFog->fogDensity,
    heightFog->fogHeightFalloff,
    heightFog->fogBaseHeight);

heightFogCB->fogRayOriginTerms = rayOriginTerms;
heightFogCB->cameraWorldPos = camera.position;

// Pass added to render graph (automatic)
auto heightFogPass = builder.AddPass<HeightFogPass>(heightFogCB);
```

## Parameter Tuning Guide

### Density

| Value | Effect | Use Case |
|-------|--------|----------|
| 0.0001 | Very light haze | Subtle atmosphere |
| **0.001** | **Light fog (default)** | **Typical outdoor** |
| 0.01 | Thick fog | Heavy mist/clouds |
| 0.1+ | Impenetrable | Toxic gas, underwater |

**Rule**: Start with 0.001, adjust based on visibility distance desired.

### Height Falloff

| Value | Effect | Height Impact |
|-------|--------|----------------|
| 0.0001 | Constant density (no height variation) | Flat layer |
| **0.002** | **Moderate falloff (default)** | **Decreases to 37% at 500m** |
| 0.01 | Fast falloff | Quickly clears with altitude |
| 0.05 | Very fast falloff | Ground-hugging fog |

**Rule**: Use 0.002 for natural atmosphere. Increase to 0.01+ for ground-clinging ground fog (swamps, caves).

### Base Height

| Value | Effect | Use Case |
|-------|--------|----------|
| -10000 | Underground tunnels | Caves, mines |
| 0 | Sea/ground level (default) | Normal landscape |
| 5000 | Mountain peaks | High-altitude fog |
| 10000+ | Clouds/stratosphere | Unrealistic but stylized |

**Rule**: Align with your world's ground level. Use negative for underground levels.

### Max Opacity

| Value | Effect |
|-------|--------|
| 0.0 | No fog (alpha = 1.0) |
| 0.5 | Fog fades to 50% alpha at distance |
| **0.8** | **Fog becomes very opaque (default)** |
| 1.0 | Fog completely opaque at distance |

**Rule**: 0.8 prevents sky from being totally obscured. Use 1.0 only if you want impenetrable fog.

### Inscattering (Sun Halo)

| inscatteringExponent | Effect |
|----------------------|--------|
| 2.0 | Very wide sun glow (moon-like) |
| **8.0** | **Medium (default)** |
| 32.0 | Sharp sun disk (tiny halo) |
| 128.0+ | Pinpoint sun (no god rays effect) |

**Rule**: 8–16 looks natural; 4–8 for artistic glows.

## Common Issues

### Fog Too Transparent

**Symptom**: Fog barely visible; scene looks the same with/without fog.

**Cause**: `fogDensity` too low.

**Solution**:
```cpp
heightFog->fogDensity = 0.005;  // 5× stronger
```

### Fog Completely Opaque (White/Solid)

**Symptom**: Beyond a certain distance, everything is fog color; can't see through.

**Cause**: `fogDensity` too high, or `fogMaxOpacity` = 1.0.

**Solution**:
```cpp
heightFog->fogDensity = 0.0005;       // Reduce
heightFog->fogMaxOpacity = 0.6;       // Allow 40% transparency at distance
```

### Unnatural Fog Layers

**Symptom**: Fog doesn't follow terrain/gravity; looks flat.

**Cause**: `fogBaseHeight` doesn't match world ground level.

**Solution**:
```cpp
// For a ground at Z = 0 cm:
heightFog->fogBaseHeight = 0.0;

// For a ground at Z = 500 cm (elevated):
heightFog->fogBaseHeight = 500.0;
```

### Sun Halo Too Bright

**Symptom**: Inscattering washes out scene when looking toward sun.

**Cause**: `inscatteringColor` too bright, or `inscatteringExponent` too low.

**Solution**:
```cpp
heightFog->inscatteringColor = {0.5, 0.4, 0.2};  // Darker orange
heightFog->inscatteringExponent = 16.0;          // Sharper disk
```

## Performance

| Component | GPU Time |
|-----------|----------|
| Height fog compute (fullscreen) | 1–2ms |
| Memory bandwidth | Full scene read/write |

**Optimization**: Use simple depth approximation (don't reconstruct world pos if not needed).

## Phase Roadmap

| Phase | Feature | Status |
|-------|---------|--------|
| 1.3 | Basic height fog (exponential, single layer) | ✅ Done |
| 2.0 | Two independent fog layers | Planned |
| 2.5 | Volumetric fog (froxel grid) | Planned |
| 3.0 | Distance fog blending with height fog | Planned |
| 3.5 | Fog shadows (light shafts) | Planned |

## Key Takeaways

- **Height fog** uses exponential density distribution: `density = base * exp(-falloff * (z - baseHeight))`
- **Transmission** through fog: `transmission = exp(-density * rayLength)`
- **CPU precompute**: `rayOriginTerms = density * exp(-falloff * (cameraZ - baseHeight))`
- **Two layers**: Primary (ground) + secondary (clouds/dust)
- **Inscattering**: Sun-facing halo adds directional light scattering
- **Parameters**: Start with density=0.001, heightFalloff=0.002, maxOpacity=0.8

## References

- [21-renderer-passes.md](21-renderer-passes.md) — Height Fog Pass definition
- [Bruneton 2017](https://ebruneton.github.io/precomputed_atmospheric_scattering/) — Atmospheric scattering model
- UE5 `CalculateLineIntegralShared` — Reference implementation

## Changelog

| Date | Change |
|------|--------|
| 2026-03-16 | Initial creation — comprehensive height fog reference and tuning guide |
