<!-- doc-meta
title: Godot 4 Rendering Patterns Analysis
description: Analysis of Godot 4 Forward+ clustering, SDFGI, and post-processing for lean engine architecture
author: Architecture Team
date: 2026-03-22
-->

# Godot 4 Rendering Patterns: Quality-to-Code Ratio Analysis

This document analyzes Godot 4's rendering pipeline, focusing on architectural patterns that achieve high visual quality with minimal complexity. Godot's renderer is designed as a **forward-clustered system** with deferred environment-only lighting via **SDFGI** (Signed Distance Field Global Illumination). Key innovation: Godot separates dynamic lights (forward) from static GI (deferred), avoiding deferred rendering's complexity.

---

## 1. Forward+ Clustering (Godot's Core Pattern)

### 1.1 Cluster Builder Architecture

**Pattern Name**: Cluster Builder Pattern

**Godot Implementation** (`cluster_builder_rd.h`, `cluster_render.glsl`):

Godot uses a **light cluster grid** built per-frame:
- Cluster grid: 32x32x32 voxel cells (from `cluster_size = 32`)
- Cluster divisors: 1, 2, or 4 (configurable for 16/32/64 cell clusters)
- Two passes: **Render** (per-light geometry) + **Store** (compact cluster data)

**Render Pass**:
```cpp
// cluster_builder_rd.h:64-92
struct RenderElementData {
    uint32_t type;           // OMNI_LIGHT, SPOT_LIGHT, DECAL, REFLECTION_PROBE
    uint32_t touches_near;   // Camera near plane intersection
    uint32_t touches_far;    // Camera far plane intersection
    uint32_t original_index; // Original light index
    float transform_inv[12]; // Transposed 3x4 transform
    float scale[3];          // Bounding volume scale
    uint32_t has_wide_spot_angle;
};
```

**Key Insight**: Each light renders its bounding volume (sphere for omni, cone for spot) to an off-screen texture. Cluster indices are accumulated per cell.

**Store Pass**:
```
RenderElement → Cluster Texture (per-cell light list) → Shader SRV
```

The shader uses this to loop only over relevant lights:
```glsl
// Pseudo-code from scene_forward_clustered_inc.glsl
uint cluster_index = cluster_from_screen_pos(screen_uv, depth);
for (uint light_idx = 0; light_idx < light_count[cluster_index]; light_idx++) {
    uint light_id = light_list[cluster_index][light_idx];
    // Shade with light_id
}
```

**Problem Solved**:
- Avoids per-pixel loop over ALL lights (O(n) → O(k) where k = lights in cluster)
- Works with forward rendering (no G-buffer)
- Efficient for moderate light counts (8-256 per cluster typical)

**UzEngine Adaptation**:

UzEngine has a similar system but could adopt Godot's **touch_near/touch_far** optimization:

```cpp
// uz_cluster_builder.h: add to ClusterElementData
struct {
    uint32_t touches_near; // Lights affecting near plane
    uint32_t touches_far;  // Lights affecting far plane
} touch_bits;

// In cluster shader: cull lights touching only one plane
if (depth < z_near && !light.touches_near) continue;
if (depth > z_far && !light.touches_far) continue;
```

**Godot's Leanness Lesson**:
- Cluster grid built with **two compute passes** (render geometry + store indices), not three
- No deferred G-buffers needed (forward rendering only)
- Light culling happens in C++ (cluster_builder_rd.cpp), not shader

---

## 2. Lighting: PBR + GGX BRDF

### 2.1 StandardMaterial3D Properties

**Reference Material Storage** (`material.h:147-168`):

Godot exposes 20 texture parameters covering standard PBR:

```cpp
enum TextureParam {
    TEXTURE_ALBEDO,
    TEXTURE_METALLIC,
    TEXTURE_ROUGHNESS,
    TEXTURE_EMISSION,
    TEXTURE_NORMAL,
    TEXTURE_RIM,
    TEXTURE_CLEARCOAT,
    TEXTURE_FLOWMAP,
    TEXTURE_AMBIENT_OCCLUSION,
    TEXTURE_HEIGHTMAP,
    TEXTURE_SUBSURFACE_SCATTERING,
    TEXTURE_SUBSURFACE_TRANSMITTANCE,
    TEXTURE_BACKLIGHT,
    TEXTURE_REFRACTION,
    TEXTURE_DETAIL_MASK,
    TEXTURE_DETAIL_ALBEDO,
    TEXTURE_DETAIL_NORMAL,
    TEXTURE_ORM,           // Combined metallic/roughness/AO
    TEXTURE_BENT_NORMAL,
    TEXTURE_MAX = 20
};
```

### 2.2 Light Compute (GGX + Burley Diffuse)

**Pattern Name**: Standard Microfacet BRDF

**Godot Implementation** (`scene_forward_lights_inc.glsl:16-200`):

GGX Distribution:
```glsl
/// @ref Trowbridge Reitz 1975 "Average Irregularity Representation" + Berry 1923
half D_GGX(half NoH, half roughness, hvec3 n, hvec3 h) {
    half a = NoH * roughness;
    float k = roughness / (1.0 - NoH * NoH + a * a);
    half d = k * k * half(1.0 / M_PI);
    return saturateHalf(d);
}
```

Visibility (Smith):
```glsl
/// @ref Smith 1967 "Geometrical shadowing"
half V_GGX(half NdotL, half NdotV, half alpha) {
    half v = half(0.5) / mix(half(2.0) * NdotL * NdotV, NdotL + NdotV, alpha);
    return saturateHalf(v);
}
```

Fresnel + Metallic:
```glsl
/// @ref Google Filament - albedo-based metallic
hvec3 F0(half metallic, half specular, hvec3 albedo) {
    half dielectric = half(0.16) * specular * specular;  // ~0.04 for dielectrics
    return mix(hvec3(dielectric), albedo, hvec3(metallic));
}
```

**Anisotropic BRDF** (optional):
```glsl
/// Trowbridge-Reitz with anisotropy
half D_GGX_anisotropic(half cos_theta_m, half alpha_x, half alpha_y, ...);
```

**Problem Solved**:
- Physically-based specular = metallic parameter drives F0
- Anisotropic support for brushed metals, cloth
- Specular energy preservation via Smith G term

**UzEngine Adaptation**:

UzEngine already has GGX + Burley. Godot's **anisotropic extension** is valuable:

```hlsl
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
/// Enables directional roughness for anisotropic materials (brushed metal, woven cloth)
float3 DGX_Anisotropic(float3 N, float3 H, float alpha_x, float alpha_y, float3 T, float3 B);
```

---

## 3. Shadows: PCF + Soft Shadows + Atlas

### 3.1 Shadow Atlas Management

**Pattern Name**: Dynamic Shadow Atlas

**Godot Implementation** (`light_storage.h:51-56`):

```cpp
enum ShadowAtlastQuadrant : uint32_t {
    QUADRANT_SHIFT = 27,      // Atlas split: 4 quadrants (2048×2048 → 4×1024×1024)
    OMNI_LIGHT_FLAG = 1 << 26, // Dual-paraboloid (omni) vs orthogonal (spot)
    SHADOW_INDEX_MASK = OMNI_LIGHT_FLAG - 1,
    SHADOW_INVALID = 0xFFFFFFFF
};

// Light data includes soft shadow size (PCSS approximation)
float soft_shadow_size;
float soft_shadow_scale;
float shadow_bias;
float shadow_normal_bias;
```

**Atlas Rect Encoding**:
```cpp
// light_storage.h:152
float atlas_rect[4]; // [x, y, width, height] in normalized space
```

**Omni Lights** → **Dual-Paraboloid Projection**:
- Two 1024×1024 maps per omni light (front + back hemisphere)
- Avoids cube map seams common in hardware cubemap rendering
- ~60% less memory than 6-face cube

**Spot Lights** → **Orthogonal Projection**:
- Single perspective frustum
- More efficient than cube mapping

**Problem Solved**:
- One global atlas (2048×2048) for all dynamic shadows
- Avoids texture-per-light memory overhead
- Quadrant layout allows efficient reuse

**UzEngine Adaptation**:

UzEngine's shadow atlas is similar (see `frame_setup.cpp:2b9ad75`). Godot's **soft_shadow_size + soft_shadow_scale** parameters could extend UzEngine's PCSS:

```cpp
// In ShadowAtlasManager::UpdateLightData()
lightData.soft_shadow_size = light.penumbra_size;  // Distance-based penumbra
lightData.soft_shadow_scale = light.distance_fade_shadow;
```

**Shader-side PCSS** (contact-hardened shadows):
```hlsl
/// @ref Salvi 2008 "Fast Soft Shadow Filtering" — blocker search + penumbra estimation
float2 GetPenumbraSize(float2 shadowUV, float lightDepth, Texture2D shadowMap) {
    float avgBlockerDepth = 0.0f;
    uint blockerCount = 0;

    for (uint i = 0; i < BLOCKER_SEARCH_SAMPLES; ++i) {
        float sampledDepth = shadowMap.Sample(samplerShadow, shadowUV + diskSamples[i]).r;
        if (sampledDepth < lightDepth) {
            avgBlockerDepth += sampledDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0) return float2(0, 0);  // Fully lit

    avgBlockerDepth /= blockerCount;
    float penumbraWidth = (lightDepth - avgBlockerDepth) / avgBlockerDepth;
    return penumbraWidth * float2(soft_shadow_size, soft_shadow_scale);
}
```

---

## 4. Reflections & Probes

### 4.1 Reflection Probes + Cubemap Filtering

**Pattern Name**: Prefiltered Cubemap Array

**Godot Implementation** (sky.h + renderer_gi.h):

```cpp
// sky.h:71-80: Sky texture set variants
enum SkyTextureSetVersion {
    SKY_TEXTURE_SET_BACKGROUND,          // Full resolution (2048×2048)
    SKY_TEXTURE_SET_HALF_RES,            // 1024×1024
    SKY_TEXTURE_SET_QUARTER_RES,         // 512×512
    SKY_TEXTURE_SET_OCTMAP,              // Octahedron encoding (2D)
    SKY_TEXTURE_SET_OCTMAP_HALF_RES,
    SKY_TEXTURE_SET_OCTMAP_QUARTER_RES,
    SKY_TEXTURE_SET_MAX
};
```

**Key Insight**: Godot generates **octahedron-encoded** sky environment maps instead of cube faces:
- Octahedron mapping: 8 vertices of octahedron → 2D square projection
- Avoids cube map seams
- Better UV distribution than simple cubemap

```glsl
// sdfgi_integrate.glsl:108-116
vec3 octahedron_encode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += n.x >= 0 ? -t : t;
    n.y += n.y >= 0 ? -t : t;
    return normalize(n);
}
```

**Reflection Probes** = Cubemap arrays sampled by world position.

**Problem Solved**:
- Octahedron avoids seams and pole distortion
- Multiple LOD variants for fast fallback
- Lower bandwidth than 6-face cubemaps

**UzEngine Adaptation**:

UzEngine's IBL uses standard cubemaps. Switching to octahedron encoding could improve visual quality:

```hlsl
/// @ref Cigolle et al. 2014 "Survey of Efficient Representations for Independent Unit Vectors"
float3 OctahedronDecode(float2 uv) {
    float3 n = float3(uv * 2.0f - 1.0f, 1.0f - abs(uv.x - 0.5f) - abs(uv.y - 0.5f));
    if (n.z < 0.0f) {
        float3 temp = n;
        n.xy = (1.0f - abs(temp.yx)) * (temp.xy >= 0.0f ? 1.0f : -1.0f);
    }
    return normalize(n);
}
```

---

## 5. Post-Processing

### 5.1 Bloom (Glow)

**Pattern Name**: Threshold-based HDR Bloom

**Godot Implementation** (effects shader suite):

1. **Extract bright pixels** (threshold pass)
   - Input: HDR color
   - Output: pixels > threshold, downsampled to 1/2 resolution

2. **Gaussian blur** (iterative, 4× downsamples)
   - 1/2 → 1/4 → 1/8 → 1/16 resolution
   - 5-tap Gaussian each level

3. **Blend back** (upsample + composite)
   - Upscale + blend at each mip level
   - Final composite: output + bloom * bloom_intensity

**Problem Solved**:
- Efficient pyramid approach avoids full-screen large blur
- HDR aware (samples > 1.0 brightness)
- Configurable threshold + intensity

### 5.2 Tone Mapping (ACES + Color Grading)

**Godot Tone Curve**: ACES-like mapping (not exact ACES, but similar philosophy)

```glsl
/// @ref Academy of Motion Picture Arts and Sciences - ACES tone mapping
/// Compressed to fit [0,1] output range while preserving color balance
vec3 tone_map(vec3 linear_color) {
    // Shadow, midtone, highlight preservation
    // Godot uses a simplified version for performance
}
```

### 5.3 Depth of Field (DOF)

**Pattern**: Circle-of-confusion blur

- **Near focus** + **far focus** zones
- Parameterized by focal distance + aperture size
- Implemented as separable blur (horizontal → vertical)

### 5.4 Screen-Space Ambient Occlusion (SSAO)

**Pattern**: Horizon-based approximation

- Sample depth buffer in hemisphere around pixel
- Estimate occlusion from depth discontinuities
- Fast, low memory bandwidth

### 5.5 Screen-Space Indirect Illumination (SSIL)

**Pattern**: Ray-marched GI proxy

- Cast rays from screen-space positions
- Sample depth + normal to approximate indirect light
- Blurred + temporally reprojected

**Problem Solved**:
- No separate GI computation (deferred env lights)
- All post-process effects stacked efficiently
- Configurable quality/performance tradeoff

**UzEngine Adaptation**:

UzEngine's post-process system is similar but could benefit from:

1. **ACES tone mapping** instead of current LUT
2. **Proper bloom pyramid** (currently single pass?)
3. **DOF parameterization** per-material (focus distance per object)

---

## 6. Material System: StandardMaterial3D

### 6.1 Complete Parameter Set

**Godot's StandardMaterial3D** exposes ~50+ properties:

**Core PBR**:
- `albedo_color` + `albedo_texture` (SRGB)
- `metallic` + `metallic_texture` (linear, [0,1])
- `roughness` + `roughness_texture` (linear, [0,1])
- `specular` (controls F0 for dielectrics, 0.5 default)

**Normal Mapping**:
- `normal_scale` (strength)
- `normal_texture` (tangent-space or object-space)

**Emission**:
- `emission` + `emission_texture`
- `emission_energy_multiplier` (HDR brightness control)

**Advanced Shading**:
- `anisotropy` + `anisotropy_flowmap` (brushed direction)
- `clearcoat` + `clearcoat_roughness` (car paint, varnish)
- `rim` + `rim_tint` (fabric rim lighting)
- `subsurface_scattering` + `transmittance` (skin, wax)
- `backlight` (translucent materials)

**Height-Based Details**:
- `heightmap_scale` (parallax strength)
- `detail_mask` + `detail_albedo` + `detail_normal` (micro surface)

**Environment**:
- `ambient_light_source` (self-shadowing toggle)
- `cull_mode` (back/front/disabled)
- `alpha_scissor_threshold` (alpha test cutoff)

**Problem Solved**:
- Complete reference for material richness
- Per-material override for every standard technique
- No simplifications or missing parameters

**UzEngine Adaptation**:

UzEngine material system is simpler. Adding **detail textures** + **anisotropy** would improve visual richness:

```cpp
// material_component.h: add
struct DetailLayerData {
    RID detail_albedo;
    RID detail_normal;
    RID detail_mask;
    float detail_scale = 4.0f;  // UV scale multiplier
    float detail_strength = 0.5f; // Blend with base
};
```

---

## 7. Atmosphere & Fog

### 7.1 Volumetric Fog (Froxel Grid)

**Pattern Name**: Froxel Fog Grid + Temporal Reprojection

**Godot Implementation** (`volumetric_fog.glsl`):

```glsl
// Local compute group: 4×4×4 = 64 threads per dispatch
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#define DENSITY_SCALE 1024.0  // Fixed-point density storage

struct VolumeUBO {
    vec2 fog_frustum_size_begin;   // Near plane frustum size
    vec2 fog_frustum_size_end;     // Far plane frustum size
    float fog_frustum_end;         // Far distance
    float z_near;
    float z_far;
    float time;

    ivec3 fog_volume_size;         // Typical: 64×32×32 cells
    uint directional_light_count;

    uint use_temporal_reprojection;
    uint temporal_frame;
    float detail_spread;           // Power for logarithmic depth distribution
    float temporal_blend;          // [0,1] blend with history

    mat4 to_prev_view;            // Previous frame's view transform
    mat4 transform;               // Current view to world
};
```

**Halton Sequence** for temporal dither:
```glsl
const vec3 halton_map[TEMPORAL_FRAMES] = vec3[](
    vec3(0.5, 0.33333333, 0.2),
    vec3(0.25, 0.66666667, 0.4),
    // ... 16 frames total
);
// Sample: offset = halton_map[temporal_frame]
```

**Key Optimizations**:
1. **Logarithmic depth distribution** (`detail_spread` power): More samples near camera
2. **Temporal reprojection**: Reuse previous frame's fog data, blend with current
3. **Halton dithering**: Deterministic temporal noise for smooth accumulation
4. **Compute dispatch with corner offset**: Allows dynamic region updates

**Problem Solved**:
- Real-time volumetric fog at playable FPS
- Temporal stability (no flickering)
- Dynamic light interaction (directional + omni + spot)

**UzEngine Adaptation**:

UzEngine could adopt Godot's **temporal reprojection** to improve volumetric fog stability:

```cpp
// In FogRenderer::UpdateFogVolume()
struct FogFrameData {
    uint32_t temporal_frame = (frame_index % 16);
    vec3 temporal_dither = halton_sequences[temporal_frame];
    float temporal_blend = 0.8f;  // Blend 80% prev + 20% current
};
```

---

## 8. SDFGI: Godot's Unique GI Approach

### 8.1 Signed Distance Field Global Illumination

**Pattern Name**: Multi-Cascade SDF-based Probe Grid

**Godot Implementation** (`sdfgi_*.glsl` suite):

SDFGI is Godot's alternative to deferred GI. Instead of baking static lightmaps or using real-time voxel GI, SDFGI uses **precomputed signed distance fields** to approximate geometry, then bounces light through them.

**Architecture**:

1. **SDF Baking** (per-scene, offline):
   - Geometry → 3D SDF texture (8 cascades, multi-resolution)
   - Each cascade: 4× larger bounding box
   - Cascade 0: fine detail (0-8m), Cascade 7: coarse detail (0-1km)

2. **Probe Grid** (per cascade):
   - Regular grid of light probes (e.g., 64×64×8 grid per cascade)
   - Each probe: Spherical harmonics (SH) coefficients for irradiance

3. **Direct Light Injection** (`sdfgi_direct_light.glsl`):
   - Directional light → traces through SDF cascade
   - Injects direct light into probe grid

4. **Indirect Integration** (`sdfgi_integrate.glsl`):
   - For each probe: sample neighboring probes' SH
   - Estimate bounce light from SDF geometry
   - Encode result as SH coefficients

5. **Runtime Probing** (in final shader):
   ```glsl
   vec3 irradiance = SampleSDFGIProbe(world_position);  // Interpolate SH grid
   diffuse_light += irradiance;
   ```

**SH Encoding** (reduces memory):
```glsl
// SDFGI stores ~4 SH coefficients per probe (L1 spherical harmonics)
// Allows efficient interpolation across probe grid
struct SHCoefficients {
    float c[4];  // DC + 3 linear terms
};
```

**RGBE Encoding** (light color storage):
```glsl
// sdfgi_integrate.glsl:118-143
uint rgbe_encode(vec3 color) {
    const float pow2to9 = 512.0f;
    const float B = 15.0f;
    const float N = 9.0f;
    const float LN2 = 0.6931471805599453094172321215;

    float cMax = max(color.r, max(color.g, color.b));
    float expp = max(-B - 1.0f, floor(log(cMax) / LN2)) + 1.0f + B;
    float sMax = floor((cMax / pow(2.0f, expp - B - N)) + 0.5f);

    // ... encode RGB as 9-bit mantissa + 5-bit exponent
}
```

**Temporal Updates**:
```cpp
// sdfgi_integrate.glsl:52-65
#define HISTORY_BITS 10  // Exponential moving average over 1024 frames

// Per probe: blend new sample with history (slow temporal update)
new_sh = mix(old_sh, sampled_sh, 1.0 / (1.0 + history_weight));
```

**Problem Solved**:
- **Precomputed** but not static (temporal updates per frame)
- **Dynamic**: Handles moving objects (probes interpolate)
- **Efficient**: No per-pixel raymarching (only probe-to-probe)
- **Artistic control**: SDF baking can include interior/exterior modes

**Why It's Unique**:
- Not voxel-based (no "flooded" voxel memory)
- Not screen-space (no dependency on camera view)
- Not lightmap-based (no seaming)
- Fallback to sky irradiance if SDF unavailable

**UzEngine Adaptation**:

UzEngine uses **IBL only** (environment cubemap). SDFGI would be valuable for:

1. **Dynamic area lighting** (interior spaces with sky outside)
2. **Night scenes** (ambient occlusion becomes critical)
3. **Bounce light** from colored walls

However, SDFGI requires significant architecture:
- Offline SDF generation tool (Godot uses Embree for BVH)
- Probe grid management in C++
- Multi-cascade support
- Temporal history buffers

**Simplified UzEngine Alternative**:
Instead of full SDFGI, add **ambient occlusion from screen-space normals**:
```hlsl
/// @ref Kautz et al. 2006 "Hardware Accelerated Global Illumination"
float3 ambient_occlusion = SampleAmbientOcclusion(normal);
diffuse_light *= (1.0f - ambient_occlusion * ao_strength);
```

---

## 9. Summary: Godot's Leanness Principles

### What Godot Does Well

| Feature | Godot's Approach | Why Lean |
|---------|------------------|---------|
| **Lighting** | Forward + Clustering | No G-buffer memory, dynamic light count independent of resolution |
| **GI** | SDFGI probes | Precomputed geometry, no per-pixel raymarching |
| **Shadows** | Dual-paraboloid omni | Avoids cube map seams, ~33% less memory |
| **Reflections** | Octahedron-encoded cubemaps | Better UV distribution, fewer seams |
| **Fog** | Froxel grid + temporal reprojection | Stable, no frame-to-frame jitter |
| **Materials** | StandardMaterial3D with 20 textures | Covers 95% of use cases |
| **Post-process** | Bloom pyramid + ACES | Stacked efficiently, no separate passes |

### Key Architectural Patterns

1. **Separation of Concerns**:
   - Dynamic lights (forward) ≠ static GI (deferred)
   - Avoids complexity of full deferred rendering

2. **Temporal Smoothing**:
   - SDFGI probes: exponential moving average
   - Volumetric fog: temporal dither + reprojection
   - Reduces aliasing, increases perceived quality

3. **Octahedron Encoding**:
   - Replaces cube maps (seams) with 2D octahedron projection
   - Same quality, better UV distribution

4. **Multi-Cascade Approach**:
   - Sky cascades (background/half/quarter res)
   - SDF cascades (8 scales, coarse-to-fine)
   - Allows rapid fallback without popping

5. **Compute-First**:
   - Clustering built via compute (not vertex culling)
   - Fog updated via compute (not vertex processing)
   - Volumetric shadows via compute (not ray tracing)

---

## 10. References

**Published Papers**:
- @ref Trowbridge Reitz 1975 "Average Irregularity Representation"
- @ref Smith 1967 "Geometrical Shadowing of Random-Rough Surfaces"
- @ref Kautz et al. 2006 "Hardware Accelerated Global Illumination by Image Space Photon Mapping"
- @ref Salvi 2008 "Fast Soft Shadow Filtering"
- @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
- @ref Cigolle et al. 2014 "Survey of Efficient Representations for Independent Unit Vectors"

**Godot Engine Source**:
- `servers/rendering/renderer_rd/cluster_builder_rd.h` — Forward+ clustering
- `servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl` — Light compute (GGX)
- `servers/rendering/renderer_rd/shaders/environment/sdfgi_*.glsl` — SDFGI pipeline
- `servers/rendering/renderer_rd/shaders/environment/volumetric_fog.glsl` — Froxel fog
- `servers/rendering/renderer_rd/environment/sky.h` — Sky & reflection probes
- `scene/resources/material.h` — StandardMaterial3D parameter reference

**Godot Documentation**:
- https://docs.godotengine.org/en/stable/tutorials/3d/using_3d_characters/using_3d_characters.html (material system overview)
- https://github.com/godotengine/godot/tree/master/servers/rendering — Renderer source directory

---

## Next Steps for UzEngine

1. **Adopt octahedron encoding** for sky cubemaps (visual quality + memory savings)
2. **Implement temporal SDFGI** for dynamic areas (if GI module expands)
3. **Extend cluster builder** with touch_near/touch_far to cull lights more aggressively
4. **Add anisotropic material support** to StandardMaterial-equivalent
5. **Temporal bloom stabilization** (Godot uses Halton dithering, could reduce flicker)

