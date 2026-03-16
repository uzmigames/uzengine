# Lighting & IBL Pipeline

<!-- doc-meta
title: Lighting & IBL Pipeline
category: reference
engine: uzengine
theme: rendering
created: 2026-03-16
updated: 2026-03-16
status: active
author: docs-operator
-->

## Overview

This document provides comprehensive coverage of UzEngine's physically-based lighting system, with emphasis on Image-Based Lighting (IBL) pipeline. It describes the mathematical foundations, GPU implementation, and integration points for ambient and direct lighting.

## Lighting Architecture

UzEngine uses a **split-sum IBL approximation** (Karis 2013) for indirect ambient lighting combined with direct multi-light evaluation. The architecture consists of three main components:

1. **Direct Lighting** — Sun (directional light), point lights, and spot lights
2. **Ambient Lighting** — Spherical harmonics (SH) for diffuse, cubemaps for specular
3. **Sky Environment** — Bruneton atmospheric scattering with dynamic cubemap capture

### Coordinate System & Units

- **Z-up, X-forward, left-handed** — same as UE5
- **Distances**: Centimeters (cm) for mesh coordinates
- **Angles**: Degrees in ECS components, radians in shaders/GPU
- **Color space**: Linear RGB until final tonemap

## Direct Lighting

### Directional Sun Light

The primary light source in outdoor scenes. Casts **cascaded shadow maps (CSM)** with 4 cascades and 2048×2048 resolution.

| Property | Range | Notes |
|----------|-------|-------|
| Color | RGB | Linear sRGB color |
| Intensity | 0.0–16.0 | Typical outdoor: 1.0–2.0 |
| Min Roughness | 0.0–1.0 | Specular cone hardness — 0 = mirror, 1 = soft |
| Cast Shadows | Boolean | Enable CSM rendering |

**Shadow cascade parameters** (see `18-shadow-system.md` for details):
- 4 cascades with automatic frustum splitting
- Texel snapping prevents shadow swimming
- PCF filtering (3×3 soft shadows)
- Cascade blending with transition width

### Point Lights

Omni-directional lights from a world position. Supported up to **64 simultaneously** in the scene, though GPU performance budget may limit actual evaluation.

```cpp
struct PointLight {
    Vec3   position;          // World position (cm)
    float  intensity;         // 0.0–16.0
    Color  color;             // Linear RGB
    float  attenuation_radius; // Light falloff distance (cm)
    float  source_radius;      // For soft shadows (planned Phase 3)
};
```

**Attenuation formula** (inverse-square with smooth falloff):

```hlsl
float distance = length(light_pos - pixel_pos);
float attenuation = saturate(1.0 - (distance / light_radius)^2);
attenuation = attenuation * attenuation;  // Quadratic falloff
```

**Limitations** (Phase 1):
- No per-light shadows (point light shadows planned Phase 3)
- No distance culling (all lights evaluated, capped at 64)
- Forward shading only (no deferred light culling)

### Spot Lights

Cone-shaped lights with angular falloff. Same interface as point lights plus cone parameters.

```cpp
struct SpotLight : PointLight {
    Vec3   direction;           // Light cone direction (Z-up)
    float  inner_cone_angle;    // Degrees (full bright)
    float  outer_cone_angle;    // Degrees (falloff edge)
    float  source_radius;       // Soft shadow radius
};
```

**Cone attenuation**:

```hlsl
float3 toLight = normalize(light_pos - pixel_pos);
float cosTheta = dot(toLight, -light_direction);  // -light_direction because cone points forward

float inner_radians = radians(inner_cone_angle);
float outer_radians = radians(outer_cone_angle);

float cone_atten = smoothstep(cos(outer_radians), cos(inner_radians), cosTheta);
```

## Image-Based Lighting (IBL) Pipeline

The IBL system uses **split-sum approximation** to integrate environment lighting over a hemisphere with respect to roughness, using three precomputed tables:

### 1. BRDF Integration LUT

**File**: `engine/renderer/ibl/brdf_lut_pass.h`

A **512×512 RG16F** texture containing preintegrated BRDF response for all (rough, cosTheta) pairs.

| Axis | Resolution | Semantics | Format |
|------|-----------|-----------|--------|
| X | 512 | Roughness (0.0–1.0) | Linear |
| Y | 512 | cosTheta = dot(N, V) (0.0–1.0) | Linear |
| R | 16F | Fresnel scale (Schlick response) | R16Float |
| G | 16F | Specular magnitude (NDF×G) | R16Float |

**Integration parameters**:
- **Samples**: 1024 per texel (importance sampling)
- **Distribution**: GGX importance sampling (Hammersley sequence)
- **Fresnel**: Schlick approximation with f0=1.0
- **Geometry**: Smith G1 correlated (both view + light directions)
- **Baking**: Once at initialization, never updated
- **Dirty flag**: None (static resource)

**CPU cost**: ~5–10ms on first frame (compute shader, async)
**GPU lifetime**: Entire application

**Usage in shader**:

```hlsl
// After fetching roughness/metallic from material
float4 brdf_lookup = tex2D(brdfLut, float2(roughness, cosTheta));
float fresnel_scale = brdf_lookup.r;
float spec_integral = brdf_lookup.g;

vec3 specular = fresnel_scale * f0 + (1.0 - fresnel_scale) * spec_color;
```

### 2. Irradiance Cubemap

**File**: `engine/renderer/ibl/irradiance_baker.h`

A **32×32 per-face RGBA16F** cubemap (192×192 flattened) containing cosine-weighted radiance integrated from the environment.

| Property | Value | Notes |
|----------|-------|-------|
| Resolution | 32×32 per face | 6 faces = 192 KB total |
| Format | RGBA16F | 16-bit float per channel |
| Samples | 64 per texel | Cosine-weighted hemisphere sampling |
| Update | Per-frame | Recalculated when sky changes |
| Dirty flag | SkyAtmosphere.dirtyIrradiance | Set on component param change |

**Integration process** (compute shader, `irradiance_baker_cs.hlsl`):

```hlsl
float3 irradiance = 0;
for (int i = 0; i < sample_count; ++i) {
    float3 dir = importance_sample_cosine(roughness=0, i, sample_count);
    float3 contrib = env_cubemap.SampleLevel(dir, 0).rgb;  // LOD 0
    float weight = 1.0 / sample_count;  // Cosine weight implicit in sample distribution
    irradiance += contrib * weight;
}
```

**Key properties**:
- Roughness is irrelevant for diffuse irradiance (always uses LOD 0)
- Cosine weighting is implicit in the hemisphere sampling
- Blurs high-frequency details, retaining color/brightness
- Essential for Lambertian diffuse response across all materials

### 3. Prefiltered Specular Cubemap

**File**: `engine/renderer/ibl/specular_baker.h`

A **128×128 base, 5 mip levels RGBA16F** cubemap with GGX-prefiltered radiance at different roughness levels.

| Property | Value | Notes |
|----------|-------|-------|
| Base Resolution | 128×128 per face | 6 faces × 5 mips = 1.1 MB |
| Format | RGBA16F | Per-channel accuracy |
| Mip Levels | 5 | LOD 0–4 map to roughness 0.0–1.0 |
| Samples | 256 per texel | GGX importance sampling |
| Update | Per-frame | Feeds from sky cubemap |

**LOD-to-roughness mapping**:

```
LOD 0 → roughness = 0.0  (mirror-like)
LOD 1 → roughness = 0.25 (shiny)
LOD 2 → roughness = 0.50 (balanced)
LOD 3 → roughness = 0.75 (rough)
LOD 4 → roughness = 1.0  (diffuse)
```

**Integration process** (GGX importance sampling):

```hlsl
float3 specular = 0;
float weight = 0;
for (int i = 0; i < sample_count; ++i) {
    // GGX importance sample
    float3 h = importance_sample_ggx(roughness, i, sample_count);
    float3 v = normalize(viewDir);
    float3 l = normalize(2.0 * dot(v, h) * h - v);

    float ndotl = saturate(dot(n, l));
    if (ndotl > 0.0) {
        float3 contrib = env_cubemap.SampleLevel(l, lod).rgb * ndotl;
        specular += contrib;
        weight += ndotl;
    }
}
specular /= weight;
```

### IBLBaker Orchestration

**File**: `engine/renderer/ibl/ibl_baker.h`

Central coordinator that manages BRDF LUT, irradiance, and prefiltered cubemap baking.

```cpp
class IBLBaker {
public:
    void Initialize();  // Create all textures, compile shaders
    void BakeBRDFLut(); // Compute once at init

    void BakeEnvironment(const IBLEnvironment& env) {
        // 1. Validate input cubemap (not null, valid dimensions)
        // 2. Dispatch irradiance compute pass
        // 3. Dispatch specular mip-pyramid compute passes (5 dispatches)
        // 4. Copy results to permanent GPU resources
    }

    RHITextureHandle GetBRDFLut() const;
    RHITextureHandle GetIrradianceMap() const;
    RHITextureHandle GetPrefilteredMap() const;
};
```

**Execution timeline**:
- **Initialization**: BRDF LUT baked once (5–10ms)
- **Per-frame**: Environment maps baked only if sky atmosphere dirty (10–20ms)
- **Caching**: Results reused across frames until environment changes

## Spherical Harmonics (SH) for Diffuse Ambient

**File**: `engine/renderer/lighting/spherical_harmonics.h`

The irradiance cubemap is projected to **L2 SH coefficients** for efficient GPU evaluation.

### SH9 Projection

**L2 spherical harmonics** use 9 float4 coefficients (112 bytes total) to represent a function over the sphere.

```cpp
struct SkyIrradianceSH {
    Vec4 coeff[9];  // SH0–SH8 packed as Vec4
};

// Compute at CPU (from cubemap readback):
void ProjectCubemapToSH(const Cubemap& cubemap, SkyIrradianceSH& out_sh);

// Evaluate at GPU (in pixel shader):
float3 EvaluateSH(const SkyIrradianceSH& sh, const Vec3& normal) {
    float3 result = 0;
    // L0 (constant term)
    result += sh.coeff[0].rgb * 0.282095f;

    // L1 (linear terms, 3 coeffs)
    result += sh.coeff[1].rgb * (2.0f/3.0f) * normal.x;  // Y1,-1 = Y_component
    result += sh.coeff[2].rgb * (2.0f/3.0f) * normal.z;  // Y1,0 = Z_component
    result += sh.coeff[3].rgb * (2.0f/3.0f) * normal.y;  // Y1,+1 = X_component

    // L2 (quadratic terms, 5 coeffs) — omitted for brevity
    // ...

    return max(result, 0.0f);  // SH can be negative; clamp to 0
}
```

### CPU Readback & GPU Upload

**Phase 1 implementation**:

1. **GPU readback** (after sky cubemap capture):
   - Copy cubemap to staging buffer
   - Map staging buffer to CPU memory
   - Read 6 faces × (32×32) pixels = 24,576 float3 samples

2. **SH projection** (CPU):
   ```cpp
   void ProjectCubemapToSH(const std::vector<Vec3>& samples, SkyIrradianceSH& sh) {
       // Iterate over 9 SH coefficients
       // For each coeff, integrate product of cubemap and SH basis over sphere
       // Result: sh.coeff[0..8]
   }
   ```

3. **Upload to GPU** (PerFrameCB):
   - Pack 9 float4 into root CB
   - Bind at root parameter (b2 atmospheric)
   - Shader evaluates via `EvaluateSH(sh, normal)`

### Fallback: Analytic SH

If GPU readback fails (unsupported device), fallback to **analytic Rayleigh SH**:

```cpp
void ComputeRayleighSH(const SkyAtmosphereParams& params, SkyIrradianceSH& sh) {
    // Rayleigh scattering coefficients
    const float3 rayleigh_coeff = {5.802e-3f, 13.558e-3f, 33.1e-3f};

    // Compute SH from scattering phase function
    // Approximates zenith + horizon colors
    // Result: blue at zenith, orange at horizon
}
```

## SkyLight Component

**File**: `engine/lighting/skylighting_component.h`

ECS component providing UI parameters for environment lighting.

```cpp
/// Image-Based Lighting from sky environment.
struct SkyLight {
    /// Intensity multiplier on captured irradiance (0.0–4.0, default 1.0)
    float     intensity;

    /// Color tint applied to irradiance (default white)
    Color     color;

    /// Minimum ambient level (prevents fully dark corners, 0.0–0.5, default 0.04)
    float     min_ambient;

    /// Distance Field Ambient Occlusion (DFAO) multiplier (0.0–1.0, default 0.5)
    /// Controls how much pre-baked AO darkens ambient lighting
    float     dfao_strength;

    /// Per-pixel normal-based occlusion for self-shadowing (0.0–1.0)
    float     per_pixel_ao_strength;

    /// Enable/disable ambient contribution entirely
    bool      enabled;
};
```

### Integration into Lighting

In the lighting pass (`lighting_ps.hlsl`), after evaluating direct lighting:

```hlsl
float3 ambient = EvaluateSH(skylight.sh, normal);
ambient *= skylight.intensity * skylight.color;
ambient = max(ambient, skylight.min_ambient);

// Apply DFAO occlusion
ambient *= 1.0 - (ao_from_texture * skylight.dfao_strength);

float3 final_color = direct_light + ambient;
```

## Constant Buffer Layout

**PerFrameCB** (uploaded once per frame, root parameter 1):

| Offset | Size | Field | Purpose |
|--------|------|-------|---------|
| 0 | 16 | viewPos | Camera world position |
| 16 | 16 | viewProj | View-projection matrix |
| 32 | 16 | invViewProj | Inverse for position reconstruction |
| 48 | 16 | sunDir | Sun direction (normalized) |
| 64 | 4 | sunColor | Sun color (RGB) |
| 68 | 4 | sunIntensity | Sun intensity multiplier |
| 72 | 16 | skyIrradianceSH[0] | L0 SH coefficient |
| 88 | 16 | skyIrradianceSH[1] | L1 SH coeff 0 |
| ... | ... | ... | ... |
| 144 | 4 | iblIntensity | IBL multiplier |
| 148 | 4 | time | Total elapsed time (seconds) |
| 152 | 4 | deltaTime | Frame delta time (seconds) |

**Root Constant 0** (SM6.6 bindless only):
- Descriptor table indices for BRDF LUT, irradiance, prefiltered cubemaps

## Fallback Behavior

If SkyLight component is absent or disabled:

```cpp
if (!has_skylight || !skylight.enabled) {
    // Fallback: constant ambient (mid-gray)
    ambient = float3(0.03, 0.03, 0.03);
}
```

This prevents scenes from being pitch-black and allows gameplay without IBL setup.

## Phase Roadmap

| Phase | Feature | Status |
|-------|---------|--------|
| 1.2 | Forward PBR + sun shadow maps | ✅ Done |
| 1.3 | Deferred + IBL (BRDF LUT, irradiance, prefiltered cubemaps) | ✅ Done |
| 2.0 | Point light shadows (dual-paraboloid) | Planned |
| 3.0 | Spot light shadows | Planned |
| 3.5 | Global illumination (Lumen-style, radiance caching) | Planned |
| 4.0 | Ray-traced indirect lighting | Planned |

## Key Takeaways

- **Split-sum IBL** separates diffuse (SH9) from specular (prefiltered cubemap) for efficiency
- **BRDF LUT** encodes material response (roughness-dependent); baked once at init
- **Irradiance cubemap** (32×32) stores diffuse environment; low-res but sufficient
- **Prefiltered specular** (128×128, 5 mips) encodes specular reflection detail per roughness
- **SH9 projection** converts irradiance to 9 float4 for O(1) GPU evaluation per pixel
- **Fallback paths** ensure graceful degradation (analytic SH, constant ambient)
- **SkyLight component** provides artist-facing controls (intensity, color tint, DFAO)

## References

- [10-cook-torrance-brdf.md](10-cook-torrance-brdf.md) — BRDF mathematics
- [17-lighting.md](17-lighting.md) — Multi-light system and CPU culling
- [21-renderer-passes.md](21-renderer-passes.md) — Sky Atmosphere and Height Fog passes
- [Karis 2013](https://cdn2.unrealengine.com/resources/files/2014-3/Real%20Shading%20in%20Unreal%20Engine%204%20-%20Siggraph%202013%20Course%20Notes.pdf) — Real shading in Unreal Engine (split-sum approximation)
- [Bruneton 2017](https://ebruneton.github.io/precomputed_atmospheric_scattering/) — Precomputed atmospheric scattering

## Changelog

| Date | Change |
|------|--------|
| 2026-03-16 | Initial creation — comprehensive IBL pipeline reference |
