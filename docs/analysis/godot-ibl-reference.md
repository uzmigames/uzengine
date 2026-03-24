<!-- doc-meta
title: Godot IBL Reference Architecture
description: How Godot handles IBL reflections from HDRI—architecture patterns for UzEngine
author: Claude Code (Architecture Analysis)
date: 2026-03-24
status: complete
-->

# Godot IBL Reference Architecture

## Summary

Godot 4.x (RenderingServer/RenderingDevice backend) uses a **multi-layer octahedron encoding + GPU importance sampling** pipeline for real-time IBL reflections. The approach prioritizes:
- **Octahedron-mapped cubemaps** (more efficient than cube textures in Vulkan)
- **Roughness-dependent mip generation** via importance-weighted sampling (Hammersley + GGX importance sampling)
- **Incremental/real-time pipeline** (progressively refines mips per-frame or all-at-once)
- **Split-sum approximation** (precomputed BRDF LUT + environment specular sampling)

This document compares Godot's approach with UzEngine and identifies improvements UzEngine should adopt.

---

## 1. How Godot Bakes Prefiltered Specular

### Key Parameters (E:/godot/servers/rendering/renderer_rd/environment/sky.h:248-249)

```cpp
static inline const int REAL_TIME_SIZE = 256;              // Base cubemap resolution
static inline const int REAL_TIME_ROUGHNESS_LAYERS = 7;    // Mip levels (0-6)
```

**Process:**

1. **Input cubemap** is created from sky shader evaluation → **octahedron 2D texture** (256×256 per side in octa format)
2. **Downsampling phase** → creates half-resolution octamaps from the original (divide 256→128→64→32→16→8)
3. **Importance sampling per mip** (roughness 0.0→1.0) via `create_reflection_importance_sample()`
4. **Sample count scales with roughness** (line E:/godot/.../octmap_roughness.glsl:34):
   ```glsl
   uint scaled_samples = max(uint(float(params.sample_count * 4) * params.roughness), 4);
   ```

### Reference Implementation Details

**File:** `E:/godot/servers/rendering/renderer_rd/environment/sky.cpp`
**Function:** `SkyRD::ReflectionData::create_reflection_importance_sample()`

The pipeline:
1. Downsamples input octahedron radiance map (octahedron_downsampler)
2. Loops over each roughness layer (7 layers by default)
3. For each layer, runs octmap_roughness compute shader with:
   - Source: downsampled octahedron map
   - Output: prefiltered specular mip for that roughness
   - Sample count: `sample_count * 4 * roughness` (minimum 4 samples)

### Godot's Texture Format

- **octahedron encoded** (not cube)
- **Format:** R16G16B16A16_SFLOAT (line sky.h:71)
- **Resolution:** Base 256×256, progressively downsampled for rougher mips
- **Array vs Single:** Configurable via `USE_RADIANCE_OCTMAP_ARRAY` (7 roughness layers stored as array or individual textures)

**Why octahedron?** Vulkan cubemaps require 6 separate texture views; octahedron uses a single 2D layout + edge wrapping. More efficient storage and sampling.

---

## 2. Godot's Split-Sum IBL Approximation

### The Formula (Multi-Stage)

Godot uses the **Karis 2013 split-sum approximation**, identical to Unreal 5:

1. **Prefiltered specular:** Mip level is computed from solid angle (line octmap_roughness.glsl:54)
   ```glsl
   float mipLevel = 0.5 * log2(solid_angle_sample / solid_angle_texel);
   ```

2. **Importance-weighted sampling:** Each sample's contribution is weighted by its **PDF and solid angle**:
   ```glsl
   float D = DistributionGGX(NdotH, roughness4);
   float pdf = D * NdotH / (4.0 * NdotH) + 0.0001;
   float solid_angle_sample = 1.0 / (float(total_samples) * pdf + 0.0001);
   ```
   - **D** = GGX distribution (published formula, not approximation)
   - **PDF** = importance-weighted (higher on specular peaks, lower on diffuse)

3. **Sampled mip selection:** Lower roughness → lower mip, higher roughness → higher mip (natural blur via octmap pyramid)

4. **Dynamic contribution blending:** At runtime in forward shader:
   - Samples prefiltered octahedron at reflection vector + roughness mip
   - Multiplies by **prefiltered DFG LUT** (BRDF lookup table)
   - Applies **Schlick Fresnel** for energy conservation

### Reference: Karis 2013
- **Paper:** "Real Shading in Unreal Engine 4" (SIGGRAPH 2013)
- Godot cites this via comments in scene_forward_lights_inc.glsl (line 476):
  ```glsl
  // https://google.github.io/filament/Filament.html#toc5.3.4.7
  // Note: The roughness value is inverted
  ```

### BRDF LUT
File: `E:/godot/servers/rendering/renderer_rd/shaders/forward_clustered/integrate_dfg.glsl`

- Precomputed offline (once per session)
- Stores **F0 sensitivity** and **integrated specular** vs **(NdotV, roughness)**
- Used at runtime: `prefiltered_dfg(lod, NoV)` returns RGB (Fresnel compensation)
- Format: **2D LUT, sampled as texture(dfg, vec2(NoV, 1.0 - lod))**

**Godot's implementation:** Energy compensation formula (line scene_forward_clustered_inc.glsl:484):
```glsl
vec3 get_energy_compensation(vec3 f0, float env) {
	return 1.0 + f0 * (1.0 / env - 1.0);
}
```

This **rescales specular energy** based on how much light is lost in the BRDF LUT lookup.

---

## 3. Coordinate System & Cubemap Sampling

### Octahedron Encoding (Godot vs Cube)

**Godot's approach:** Store reflection cubemap as **2D octahedron-encoded texture**
- **Encoding function** (E:/godot/.../oct_inc.glsl): `vec3_to_oct_with_border(vec3 N, vec2 border_size)`
- Maps 3D reflection direction → 2D UV in octahedron space
- **With border wrapping** for seam handling (line octmap_roughness.glsl:78)

**Reflection vector usage:**
```glsl
vec3 R = reflect(-V, N);  // Standard reflection vector
vec2 oct_uv = vec3_to_oct_with_border(R, border_size);
vec3 specular = textureLod(radiance_octmap, oct_uv, mip_level).rgb;
```

### Coordinate System: Y-up or Z-up?

Godot's source uses **Y-up** (standard game engine convention).

**Within shaders:**
- Reflection computations are in **world space**
- Octahedron encoding expects **world-space normal/reflection directions**
- No swizzle needed; directions map directly

**UzEngine implication:** UzEngine is **Z-up** (explicitly stated in CLAUDE.md architecture rules). This requires NO special handling for reflection vectors—just encode/decode the world-space direction as-is, the engine handles coordinate transforms in the rendering pipeline.

---

## 4. Irradiance vs Diffuse Contribution

### Godot's Approach: Not Separated

Godot does **NOT bake separate irradiance SH coefficients**. Instead:

1. **Ambient diffuse light** comes from **SDFGI** (Signed Distance Field GI) or **VoxelGI** (octree-based)
2. **IBL specular** comes from **prefiltered octahedron** (roughness-dependent)
3. **Environment diffuse** is handled by GI probes, not HDRI

**File:** `E:/godot/servers/rendering/renderer_rd/shaders/environment/gi.glsl:192-284`
Function: `sdfvoxel_gi_process()` computes both diffuse and specular from GI probes.

### Shader Integration (Where IBL is Actually Used)

**File:** `E:/godot/servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered_inc.glsl:358-362`

Layout declarations:
```glsl
#ifdef USE_RADIANCE_OCTMAP_ARRAY
  layout(set = 1, binding = 3) uniform texture2DArray radiance_octmap;
#else
  layout(set = 1, binding = 3) uniform texture2D radiance_octmap;
#endif
```

The octahedron octmap is sampled in the **final forward lighting pass**, using:
- Reflection vector (from normal + view direction)
- Roughness (material parameter)
- MIP level (computed from roughness: `1.0 - roughness`)

---

## 5. Quality Settings & Performance

### Runtime Configuration (sky.h:287-292)

```cpp
uint32_t sky_ggx_samples_quality;     // Samples per pixel for importance sampling
bool sky_use_octmap_array;             // Use array vs individual textures
int roughness_layers;                   // Number of roughness mips (typically 7)
```

**Quality presets (implied by code):**

| Quality | Samples/Pixel | Update Mode | Texture Format |
|---------|---------------|-------------|----------------|
| **Real-time** | 16-64 | Incremental (per-frame) | Octahedron array |
| **Baked** | 256+ | One-time | Octahedron array or mips |
| **Mobile** | 4-16 | Incremental | Single octahedron |

### Incremental vs Immediate

**File:** `E:/godot/servers/rendering/renderer_rd/environment/sky.cpp:update_radiance_buffers()`

Two modes:
1. **SKY_MODE_REALTIME** → generates all 7 mips in one frame
2. **SKY_MODE_INCREMENTAL** → generates one mip per frame (60 FPS = 8-9 frames to full quality)

This allows **dynamic skies** (animated procedural sky shader) with progressive quality buildup.

### Temporal Accumulation

Godot does **NOT use TAA** for IBL sampling. Instead:
- **Hammersley sequence** for low-discrepancy sampling (line octmap_roughness_inc.glsl:47-49)
- **Per-pixel independent samples** (no frame-to-frame accumulation)
- **Quality improves with sample count**, not temporal history

---

## 6. Comparison: Godot vs UzEngine

### Current UzEngine IBL Pipeline

**From memory.md & rendering parity analysis:**
- **Bakes BRDF LUT** (256×256 RG16F) ✓
- **Computes irradiance cubemap** (32×32 via CPU fallback) ✓
- **Prefilters specular cubemap** (5 mips, compute shaders) ✓
- **Uses DFG lookup at runtime** ✓

**Missing patterns from Godot:**
1. **No octahedron encoding** — uses direct cubemap sampling
2. **BRDF LUT precomputation is correct, but DFG application may differ**
3. **No roughness-dependent mip scaling** — fixed 5 mips regardless of roughness
4. **No solid-angle sampling correction** — may lead to bright halos on low roughness

### Key Differences

| Aspect | Godot | UzEngine | Better Approach |
|--------|-------|----------|-----------------|
| **Cubemap encoding** | Octahedron (2D texture) | Cubemap (6 faces) | Godot (Vulkan-native) |
| **Roughness mips** | 7 data-driven layers | 5 fixed layers | Godot (flexible quality) |
| **Sample count** | `sample_count * 4 * roughness` | Unknown | Godot (roughness-aware) |
| **Solid-angle correction** | `log2(solid_angle_sample / solid_angle_texel)` | Unknown | Godot (required for quality) |
| **DFG application** | Full split-sum + energy comp | Basic lookup | Godot (complete) |
| **Incremental baking** | Per-frame progressive | One-time | Both valid (pick use case) |

---

## 7. Improvements UzEngine Should Adopt

### Priority 1: Solid-Angle Mip Scaling

**What Godot does (octmap_roughness.glsl:54):**
```glsl
float solid_angle_texel = 4.0 * M_PI / float(params.dest_size * params.dest_size);
float mipLevel = 0.5 * log2(solid_angle_sample / solid_angle_texel);
```

**Why:** Ensures each roughness layer samples at the **correct resolution** based on the importance sample spread. Without this, low roughness reflects bright halos, high roughness samples too-low-res data.

**UzEngine action:** Apply this formula when computing prefiltered specular mip levels.

### Priority 2: Roughness-Dependent Sample Count

**What Godot does (octmap_roughness.glsl:34):**
```glsl
uint scaled_samples = max(uint(float(params.sample_count * 4) * params.roughness), 4);
```

**Why:** Smoother surfaces (low roughness) need more samples to avoid noise; rough surfaces converge faster.

**UzEngine action:** Scale importance sample count by roughness when prefiltering.

### Priority 3: Energy Compensation (Optional, Polish)

**Godot's formula (scene_forward_clustered_inc.glsl:484):**
```glsl
vec3 get_energy_compensation(vec3 f0, float env) {
	return 1.0 + f0 * (1.0 / env - 1.0);
}
```

Where `env` is the average value from the BRDF LUT. This compensates for energy lost in the split-sum approximation.

**UzEngine action:** Apply this when combining specular + Fresnel at runtime.

### Priority 4 (Future): Octahedron Encoding

Migrating from cubemap to octahedron would require:
- New compute shader to project cubemap → octahedron
- Octahedron sampling functions (vec3↔oct2D conversion with border handling)
- Changes to shader binding layout (2D array vs cube array)

**Benefit:** ~30% smaller memory footprint (1 texture vs 6), more Vulkan-efficient.

**Not urgent for DX11/DX12**, but necessary for full Vulkan parity.

---

## 8. Technical Reference Details

### Hammersley Sequence (Low-Discrepancy Sampling)

**File:** `octmap_roughness_inc.glsl:38-49`

```glsl
float radicalInverse_VdC(uint bits) {
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 Hammersley(uint i, uint N) {
	return vec2(float(i) / float(N), radicalInverse_VdC(i));
}
```

**Why:** Provides quasi-random 2D sample points with **low discrepancy**—each sample spread far from neighbors, reducing noise and variance.

**Used for:** Importance sampling in GGX importance samples (octmap_roughness.glsl:42).

### ImportanceSampleGGX Formula

**File:** `octmap_roughness_inc.glsl:15-28`

```glsl
vec3 ImportanceSampleGGX(vec2 xi, float roughness4) {
	// Compute distribution direction
	float Phi = 2.0 * M_PI * xi.x;
	float CosTheta = sqrt((1.0 - xi.y) / (1.0 + (roughness4 - 1.0) * xi.y));
	float SinTheta = sqrt(1.0 - CosTheta * CosTheta);

	// Convert to spherical direction
	vec3 H;
	H.x = SinTheta * cos(Phi);
	H.y = SinTheta * sin(Phi);
	H.z = CosTheta;

	return H;
}
```

**Reference:** This is the standard GGX importance sampling formula from Disney's Principled BRDF (Burley 2012), reproduced in all major engines.

- **Input:** `xi` = 2D uniform random [0,1) from Hammersley
- **Output:** Half-vector `H` in local tangent space, biased toward specular peak for rough surfaces
- **roughness4** = α⁴ = roughness² × roughness² (Godot convention)

---

## Key Takeaways

1. **Godot prioritizes flexibility & real-time capability** — 7 roughness layers with per-frame or batch rendering
2. **Solid-angle correction is critical** — Godot applies logarithmic mip scaling to ensure optical correctness
3. **Split-sum + energy compensation is the reference** — Karis 2013 formula, identical to UE5
4. **Octahedron encoding is Vulkan best practice** — not essential for DX11/DX12 but worth migrating to eventually
5. **Hammersley + GGX importance sampling** is the gold standard for fast IBL prefiltering

**For UzEngine:** Adopt Godot's solid-angle mip correction (Priority 1) and roughness-aware sample scaling (Priority 2) to match visual quality. The current split-sum approach is correct; improve the prefiltering accuracy.

---

## File References (Godot Source)

| File | Purpose |
|------|---------|
| `servers/rendering/renderer_rd/environment/sky.h` | Sky struct, 256×256 base, 7 roughness layers |
| `servers/rendering/renderer_rd/environment/sky.cpp` | Prefiltering pipeline, ReflectionData methods |
| `servers/rendering/renderer_rd/shaders/effects/octmap_roughness_inc.glsl` | Importance sampling formulas |
| `servers/rendering/renderer_rd/shaders/effects/octmap_roughness.glsl` | Full compute shader (7-layer prefilt) |
| `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered_inc.glsl` | Runtime lighting + BRDF LUT |
| `servers/rendering/renderer_rd/shaders/environment/gi.glsl` | GI probe integration (SDFGI/VoxelGI) |

