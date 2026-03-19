<!-- doc-meta
title: Frostbite Physically Based Volumetric Rendering Analysis
category: analysis
engine: ue5
theme: rendering
created: 2026-03-15
updated: 2026-03-15
status: active
author: docs-operator
-->

# Frostbite Physically Based Volumetric Rendering

## Overview

This document provides a comprehensive technical analysis of Frostbite's unified volumetric rendering system (SIGGRAPH 2015, Sébastien Hillaire). The system implements physically based single-scattering volumetric effects with participating media, volumetric shadows, and particle interactions, achieving 2.95ms total cost on PS4 900p. This analysis serves as both reference and UzEngine adaptation guide.

## Context

Volumetric rendering transforms flat fog systems into rich, physically meaningful volumetric effects. Frostbite's approach decouples material properties (absorption, scattering, phase function) from lighting, enabling consistent results across different light types and configurations. Understanding this system is essential for implementing production-quality volumetric rendering in UzEngine.

## Core Rendering Equation

The fundamental single-scattering volumetric rendering equation combines transmittance, incoming radiance, and scattered light:

### Transmittance (Beer-Lambert Law)

$$T_r(x, x_s) = e^{-\int_0^s \sigma_t(x) \, dt}$$

Where:
- $T_r$ = transmittance (fraction of light reaching from $x$ to $x_s$)
- $\sigma_t$ = extinction coefficient at position $x$ (absorption + scattering)
- $s$ = distance along the ray

Represents exponential light attenuation through a participating medium.

### Complete Single-Scattering Equation

$$L_i(x, \omega_o) = T_r(x, x_s) \cdot L_s(x_s, \omega_o) + \int_0^s T_r(x, x_t) \cdot \sigma_t(x) \cdot L_{\text{scat}}(x_t, \omega_i) \, dt$$

Where:
- $L_i$ = total incoming radiance at camera
- $L_s(x_s, \omega_o)$ = direct light from surface behind medium
- $L_{\text{scat}}(x_t, \omega_i)$ = scattered light from volumetric sample
- $\sigma_t$ = extinction coefficient
- Integration from camera ($x$) through medium to surface ($x_s$)

### Scattered Light Integration

$$L_{\text{scat}}(x_t, \omega_i) = \rho \cdot \sum_{l=0}^{\text{lights}} f_{v,l} \cdot V_{\text{is}}(x_t, l) \cdot L_i(x_t, l)$$

Where:
- $\rho = \sigma_s / \sigma_t$ = albedo (scattering vs total extinction)
- $f_{v,l}$ = phase function evaluation (directional scattering distribution)
- $V_{\text{is}}(x_t, l)$ = visibility term combining opaque and volumetric shadows
- Sum over all light sources

### Combined Shadow Visibility

$$V_{\text{is}}(x, l) = \text{shadowMap}(x, l) \cdot \text{volumetricShadowMap}(x, l)$$

Product of regular shadow map (opaque shadows) and volumetric shadow map (shadowing through participating media).

## Froxel Grid Architecture

Frostbite uses **frustum-aligned 3D textures** (froxels = frustum voxels) to organize volumetric computations:

### Grid Structure

- **Horizontal resolution**: 8×8 tiles (default), can be reduced to 4×4 or expanded to 16×16
- **Depth slices**: 64 slices (default) distributed logarithmically
- **Total froxels**: 8 × 8 × 64 = 4,096 per camera frustum
- **Alignment**: Froxel boundaries align with tiled-deferred light tiles (16×16 on PS4)
  - This enables reuse of per-tile culled light lists
  - Reduces per-froxel light iteration

### Key Design Decisions

1. **Frustum-aligned, not world-aligned**: Moves with camera, captures depth variation naturally
2. **Smaller than light tiles**: Multiple froxel tiles per light tile allows finer granularity
3. **Logarithmic depth distribution**: More slices near camera where density changes matter
4. **Integer division care**: Resolution must divide evenly or use careful boundary handling

### Coordinate Mapping

For a froxel at tile $(tx, ty)$ and depth slice $dz$:
- World position: determined by frustum reconstruction
- Light tile lookup: $(tx / \text{froxelTileRatio}, ty / \text{froxelTileRatio})$ maps to light tile
- Depth bounds: logarithmically distributed from near to far plane

## 3-Step Pipeline

Frostbite's volumetric rendering follows a three-pass architecture:

### Pass 1: Material Properties Voxelization

**Input**: Depth fog, height fog, local fog volumes, density textures
**Operation**: Accumulate participating media properties into 3D V-Buffer texture
**Output**: Two RGBA16F textures per froxel with scattering, extinction, emissive, phase

### Pass 2: Froxel Light Scattering

**Input**: V-Buffer, light data, shadow maps
**Operation**: Per-froxel evaluation of scattered light from all light sources
**Output**: Scattering/Transmittance buffer (accumulated illumination + extinction)

### Pass 3: Final Volume Rendering

**Input**: Froxel scattering buffer, opaque scene depth
**Operation**: Ray-march froxel grid from camera to surface, accumulate with energy conservation
**Output**: Final fog color + transmittance applied to opaque/transparent surfaces

## V-Buffer Format

The V-Buffer stores participating media properties per froxel, using two RGBA16F textures for full material representation:

### Texture Layout

| Texture | R | G | B | A | Format | Purpose |
|---------|---|---|---|---|--------|---------|
| V-Buffer 0 | Scattering R | Scattering G | Scattering B | Extinction (σ_t) | RGBA16F | Light scattering color and total extinction |
| V-Buffer 1 | Emissive R | Emissive G | Emissive B | Phase (g) | RGBA16F | Self-emissive light and phase function parameter |

### Data Semantics

- **Scattering RGB**: Wavelength-dependent scattering coefficient σ_s (m⁻¹)
  - Typically correlated with absorption (warmer fogs = more red/green scattering)
  - Used to weight contribution of each light to final color

- **Extinction (A)**: Total extinction coefficient σ_t = σ_a + σ_s (m⁻¹)
  - Wavelength-independent (single channel) for now
  - Controls how much light is absorbed/scattered per unit distance
  - Used in transmittance calculation: $T_r = e^{-\sigma_t \cdot d}$

- **Emissive RGB**: Self-emissive irradiance per unit volume (W/m³)
  - Volumetric light sources (fire, bioluminescence)
  - Added directly to final result

- **Phase (A)**: Henyey-Greenstein phase function parameter g ∈ [-1, 1]
  - g = 0: isotropic (equal scattering in all directions)
  - g > 0: forward scattering (mist, dust)
  - g < 0: backscattering (atmospheric particles)
  - Single value per froxel (no multi-lobe support yet)

### Voxelization Process

During material voxelization, properties are accumulated from multiple sources:

1. **Clear**: Initialize V-Buffer to zero (or default fog)
2. **Accumulate**: For each fog volume/fog component
   - Sample density texture at froxel center (if applicable)
   - Multiply material σ_s, σ_t by density
   - Add (not replace) to V-Buffer
   - Average phase function (weighted by density)
3. **Finalize**: Clamp values to valid ranges, convert to texture format

## Participating Media Material Model

The foundation of physically based volumetric rendering is a coherent material representation with meaningful parameters:

### Core Coefficients

| Coefficient | Symbol | Units | Range | Meaning |
|-------------|--------|-------|-------|---------|
| Absorption | σ_a | m⁻¹ | [0, ∞) | Light absorbed per unit distance |
| Scattering | σ_s | m⁻¹ | [0, ∞) | Light scattered per unit distance |
| Extinction | σ_t | m⁻¹ | [0, ∞) | Total loss per unit distance; σ_t = σ_a + σ_s |
| Albedo | ρ | dimensionless | [0, 1] | Scattering vs extinction; ρ = σ_s / σ_t |
| Phase | g | dimensionless | [-1, 1] | Henyey-Greenstein parameter |
| Emissive | σ_e | W/m³ | [0, ∞) | Self-emitted light per unit volume |

### Artist Authoring Options

Artists author properties in one of two intuitive forms:

**Option A: Physical Coefficients**
- Author: absorption σ_a, scattering σ_s, phase g
- Engine derives: σ_t = σ_a + σ_s, ρ = σ_s / σ_t
- Advantage: Direct physical meaning

**Option B: Intuitive Albedo + Extinction**
- Author: albedo ρ (0=black fog, 1=bright fog), extinction σ_t (higher=denser)
- Engine derives: σ_s = ρ × σ_t, σ_a = (1 - ρ) × σ_t
- Advantage: Easier artist understanding (RGB color + density)

### Typical Values

- **Fog (outdoor)**: σ_t ≈ 0.01-0.1 m⁻¹, ρ ≈ 0.8-1.0 (mostly forward scattering), g ≈ 0.0-0.3
- **Smoke**: σ_t ≈ 0.5-2.0 m⁻¹, ρ ≈ 0.3-0.7 (absorbs light), g ≈ -0.2-0.0
- **Dust/Haze**: σ_t ≈ 0.05-0.2 m⁻¹, ρ ≈ 0.9-1.0 (high scattering), g ≈ 0.3-0.5

## Froxel Light Scattering

Per-froxel integration computes the total scattered light from all light sources:

### Integration Steps (per froxel)

1. **Sample PM properties**: Read V-Buffer at froxel center
   - Extract: σ_s, σ_t, σ_e, g

2. **Sum all lights**:
   - Indirect light: Sample diffuse SH light probe at volume center
     - Evaluate phase function as cosine lobe integrated over probe
   - Sun light: Sample cascaded shadow map
   - Local lights: Use per-tile light culling (from tiled-deferred renderer)
     - For each culled light: evaluate with regular shadow map

3. **Output**: Accumulated scattering buffer

### Sun/Ambient Integration

**Indirect Light (SH)**:
```
indirectLight = SHProbe.EvaluateAndIntegrate(phase_function, g)
```

Uses spherical harmonics diffuse probe at volume center. Phase function is integrated as cosine lobe approximation weighted by g parameter. Single sample per froxel (no per-light variation).

**Sun Light**:
- Sample cascaded shadow map at froxel position
- Multiply sun radiance by shadow term
- Evaluate phase function toward sun direction

### Local Lights Integration

1. **Light culling**: Reuse per-tile light lists from deferred lighting
2. **Skip if no scattering**: If σ_s = 0, skip local light computation
3. **Shadow evaluation**:
   - Regular shadow map: block-level shadow (does volume block light to surface?)
   - Volumetric shadow map: shadow through medium (how much light reaches froxel?)
   - Combined: $V = V_{\text{shadowMap}} \times V_{\text{volumetricShadowMap}}$

4. **Accumulate**: Per light, add $f_{v,l} \cdot V \cdot L_i$ to froxel scattering

## Temporal Integration

A single scattering/extinction sample per frame causes severe aliasing and undersampling at high extinction values. Frostbite applies temporal integration to stabilize the result:

### Jitter Pattern

- **Halton sequence**: Low-discrepancy 2D sequence for camera jitter
- **Same offset per ray**: All froxel samples along view ray use same jitter
- **Sync material + scattering jitter**: Keep density and illumination samples aligned

### Reprojection and Blending

1. **Current frame**: Render froxel scattering at jittered position
2. **Reproject previous**: Use previous froxel values (reprojected via velocity)
3. **Blend**: Current = 95% previous + 5% current (exponential moving average)

```hlsl
float4 current_scattering = ComputeScattering(...);
float4 previous = ReprojectPreviousFrame(uv, depth);
float4 result = lerp(current_scattering, previous, 0.95);
```

**Blend weight rationale**: 5% weight on current frame achieves smooth temporal stability while remaining responsive to light/material changes.

### Remaining Issues

Temporal integration introduces artifacts in certain scenarios:

1. **Material animation trails**: Moving fog volumes leave ghosting
   - Potential solution: Velocity-based reprojection for moving volumes
   - Current workaround: Higher jitter variance breaks trails

2. **Intersecting volumes**: Multiple overlapping fog volumes cause color separation
   - No simple solution yet
   - Requires better volume blending

3. **Animated volumes**: Fluid simulation (smoke, fire) causes trails
   - Velocity field would help but adds complexity
   - Trade-off: Accept some ghosting or increase computational cost

4. **Moving light trails**: Lights moving through scene cause persistent shadows
   - Potential: Neighbor clamping (clamp reprojected value to neighborhood)
   - Current: Accept artifacts or increase blend weight

## Energy-Conservative Integration

**This is the critical difference between correct and incorrect volumetric rendering.**

### The Naive Approach (WRONG)

```hlsl
float4 accumScatteringTransmittance = float4(0.0, 0.0, 0.0, 1.0);
for (uint textureDepth = 0; textureDepth < volumeDepth; ++textureDepth) {
    uint4 coord = uint4(DispatchThreadId.xy, textureDepth, 0);
    float4 scatteringExtinction = g_ScatteringExtinctionVolume.Load(coord);
    const float transmittance = exp(-scatteringExtinction.a * stepLen);

    // WRONG: adds current scattering * accumulated transmittance
    accumScatteringTransmittance.rgb += scatteringExtinction.rgb * accumScatteringTransmittance.a;
    accumScatteringTransmittance.a *= transmittance;
    g_FinalScatteringTransmittanceVolumeOut[coord.xyz] = accumScatteringTransmittance;
}
```

**Problem**: This treats scattering as a discrete sample and ignores integration of transmittance over the froxel depth. At high extinction (σ_t > 1000), the naive approach severely underestimates light contribution and creates energy loss.

### The Correct Approach (ENERGY-CONSERVATIVE)

The fundamental issue: We have a **single scattering sample S at froxel center** but need to integrate transmittance **over entire froxel depth D**.

Correct integration over depth:

$$\int_0^D e^{-\sigma_t \cdot x} \cdot S \, dx = S \cdot \frac{1 - e^{-\sigma_t \cdot D}}{\sigma_t}$$

This represents: "How much of the scattering sample at the froxel center actually reaches the camera after integrating transmittance effects through the froxel volume?"

**Correct HLSL code**:

```hlsl
float4 accumScatteringTransmittance = float4(0.0, 0.0, 0.0, 1.0);
for (uint textureDepth = 0; textureDepth < volumeDepth; ++textureDepth) {
    uint4 coord = uint4(DispatchThreadId.xy, textureDepth, 0);
    float4 scatteringExtinction = g_ScatteringExtinctionVolume.Load(coord);

    float extinction = scatteringExtinction.a;
    float stepLen = // froxel depth in world units

    // CORRECT: integrate transmittance over step length
    float transmittance = exp(-extinction * stepLen);

    // Integrate: ∫ e^(-σt·x) * S dx = S * (1 - e^(-σt·D)) / σt
    // Avoid division by zero
    float3 scattering_contribution;
    if (extinction > 1e-5) {
        scattering_contribution = scatteringExtinction.rgb * (1.0 - transmittance) / extinction;
    } else {
        // Linear approximation when extinction is near zero
        scattering_contribution = scatteringExtinction.rgb * stepLen;
    }

    // Accumulate with current transmittance
    accumScatteringTransmittance.rgb += scattering_contribution * accumScatteringTransmittance.a;
    accumScatteringTransmittance.a *= transmittance;

    g_FinalScatteringTransmittanceVolumeOut[coord.xyz] = accumScatteringTransmittance;
}
```

### Why This Matters

1. **Energy conservation**: Light is neither created nor destroyed
2. **High extinction fix**: At σ_t = 5000, naive approach loses 95%+ energy; correct approach preserves it
3. **Volumetric shadow correctness**: Without correct integration, volumetric shadows leak light
4. **Convergence to ground truth**: At infinite extinction, result correctly becomes fully opaque

### Mathematical Proof

For constant extinction σ_t over distance D:

$$T_r(D) = e^{-\sigma_t \cdot D}$$

Scattering contribution integrated over depth:

$$\int_0^D e^{-\sigma_t \cdot x} \cdot S(x) \, dx \approx S \cdot \frac{1 - e^{-\sigma_t \cdot D}}{\sigma_t}$$

At D→0: Result → S·D (linearly scales with small depth)
At D→∞: Result → S/σ_t (constant for infinite thickness)

## Volumetric Shadow Maps

Volumetric shadow maps extend the regular shadow system to account for shadows **through** participating media:

### Architecture

- **3-level clip-map**: Three cascades oriented on camera frustum
- **32×32×32 resolution**: Sufficient for perceptual shadow quality
- **Computed per light**: Every point/spot light with volumetric shadows enabled

### Storage Format

Store transmittance (not extinction) directly in 3D texture:

```
transmittance_3d[x, y, z] = e^(-∫ σ_t dt)
```

Directly represents: "How much light reaches this point from the light source through participating media?"

### Generation Per Light Type

**Point Lights**:
- **Projection**: Ortho projection along each face of 32³ cube
- **Two passes**:
  1. Ray-march in +Z direction to compute transmittance
  2. Composite 6 faces into atlas
- **Cost**: 0.14 ms per point light (PS4)

**Spot Lights**:
- **Projection**: Perspective from light position toward center
- **Single pass**: Ray-march in light direction
- **Cost**: 0.04 ms per spot light (PS4)

### Integration with Opaque Shadow System

The unified visibility term combines both shadow types:

```hlsl
// In froxel light scattering pass
float opaque_shadow = sampleShadowMap(position, light);
float volumetric_shadow = sampleVolumetricShadowMap(position, light);
float final_visibility = opaque_shadow * volumetric_shadow;

float3 scattered_light = phase_function * final_visibility * light_radiance;
```

### Performance (PS4 900p)

| Operation | Time |
|-----------|------|
| Ray-march 32³ spot light | 0.04 ms |
| Ray-march 32³ point light | 0.14 ms |
| 1k particles (default voxel) | 0.03 ms |
| 1k particles (high quality) | 0.25 ms |

## Particle Interaction

Particles contribute to volumetric rendering through voxelization into the extinction volume:

### Particle Voxelization Pipeline

**Pass 1: Clear**
```hlsl
g_VoxelExtinctionVolume.Clear();
```

**Pass 2: Voxelize Particles**
```hlsl
// For each particle (thousands per emitter)
uint3 voxel_coord = WorldToVoxelCoord(particle.position);
float extinction_contribution = particle.extinction * particle.scale_factor;

// Use InterlockedAdd with uint to avoid atomicity issues
uint extinction_as_uint = FloatToUint_Extinc(extinction_contribution);
InterlockedAdd(g_VoxelExtinctionUint[voxel_coord], extinction_as_uint);
```

Uses intermediate uint cascaded extinction volume (no floats in atomics on older HW).

**Pass 3: Convert and Add**
```hlsl
// Convert uint → float16, add to final extinction volume
for each voxel:
    uint extinc_uint = g_VoxelExtinctionUint[voxel];
    float extinc_float = UintToFloat_Extinc(extinc_uint);
    g_VoxelExtinctionFinal[voxel] += extinc_float;
```

### Voxelization Modes

**Default Mode** (0.03 ms):
- Per-particle voxel: 2×2×2 cube
- Shape: Conservative cube
- Suitable for background particles

**High Quality Mode** (0.25 ms):
- Per-particle voxel: Trilinear interpolation across 8 neighboring voxels
- Shape: Sphere-like (smooth contribution)
- Suitable for hero particles, close-up effects

### Extinction Normalization

**Critical issue**: Without normalization, particles over-shadow the scene.

**Unnormalized (WRONG)**:
```hlsl
// Each particle's extinction added directly
g_VoxelExtinction[voxel] += particle.extinction;
// Result: too opaque
```

**Normalized (CORRECT)**:
```hlsl
// Distribute particle extinction across its voxel volume
float voxel_volume = voxel_cell_size_cubed; // ~1m³
float particle_volume = 1.0; // Unit sphere at radius 1
float voxels_per_particle = voxel_volume / particle_volume;

float normalized_extinction = particle.extinction / voxels_per_particle;
g_VoxelExtinction[voxel] += normalized_extinction;
```

Ensures particle extinction scales with voxel occupancy, not absolute count.

## Performance Data

### Full Pipeline (PS4 900p)

Complete benchmark of volumetric rendering on PlayStation 4 at 900p resolution.

**Test Scene**:
- 1 sun light + cascaded shadow
- 14 point lights (2 with volumetric shadows)
- 6 local fog volumes (all with density textures)
- ~8×8 froxel tiles, 64 depth slices

**Component Breakdown** (8×8 tiles):

| Component | Time | Notes |
|-----------|------|-------|
| Material voxelization | 0.45 ms | Fog + height fog + local volumes |
| Froxel light scattering | 2.00 ms | Integration of all lights |
| Final accumulation | 0.40 ms | Ray-march froxel grid |
| Application (fog pass) | 0.10 ms | Apply to opaque surfaces |
| **Total** | **2.95 ms** | ~3% of 33.3ms frame |

**Component Breakdown** (16×16 tiles):

| Component | Time | Notes |
|-----------|------|-------|
| Material voxelization | 0.15 ms | 3× speedup |
| Froxel light scattering | 0.50 ms | 4× speedup |
| Final accumulation | 0.08 ms | 5× speedup |
| Application (fog pass) | 0.10 ms | Constant |
| **Total** | **0.83 ms** | ~2.5% of 33.3ms frame |

### Light Scattering Breakdown (8×8 tiles)

Detailed cost of different light types:

| Component | Time |
|-----------|------|
| Local lights (14 point lights) | 1.10 ms |
| Sun scattering | 0.50 ms |
| Temporal integration | 0.40 ms |
| **Total** | **2.00 ms** |

### Scalability

- **Tile size trade-off**: 8×8 tiles 3.5× slower than 16×16, but 3.5× higher quality
  - Default: 8×8 for good quality
  - Performance mode: 16×16 or 4×4 tiles

- **Depth slices**: 64 slices is production sweet spot
  - 32 slices: 30% faster but visible banding near camera
  - 128 slices: imperceptible improvement, rarely justified

- **Temporal integration**: +0.4 ms but enables 50% lower spatial resolution without artifacting

## Validation

Frostbite validated the volumetric rendering pipeline against physically-based path tracing reference (Mitsuba renderer):

### Methodology

1. **Identical scene setup**: Exact camera, lights, geometry in both Frostbite and Mitsuba
2. **Single scattering only**: Both renderers configured for single-scattering-only to match
3. **Phase function**: Test with g=0 (isotropic) and g=0.9 (forward scattering)
4. **Metrics**: Compare luminance gradients and color accuracy across scene depth

### Results

**Scene 1 (Local fog volume)**:
- Frostbite vs Mitsuba luminance curves match within 2-3% error
- Color hue preserved across depth
- Forward scattering (g=0.9) produces expected light shaft

**Scene 2 (Multiple volumes with shadows)**:
- Volumetric shadow application verified correct
- Froxel integration accuracy confirmed
- Energy conservation validated at high extinction values

### Key Validation Takeaway

The mathematical framework (transmittance integral + energy conservation) when correctly implemented produces results indistinguishable from ground-truth physically-based rendering.

## UzEngine Adaptation Strategy

### What To Adopt

1. **Froxel architecture**: Frustum-aligned 3D textures aligned with tiled-deferred light tiles
   - Default: 8×8 tiles, 64 depth slices
   - Cost: ~3 ms on modern GPUs at 1080p

2. **Energy-conservative integration**: CRITICAL correctness fix
   - Use `(1 - e^(-σ_t * d)) / σ_t` formula in accumulation pass
   - Prevents energy loss at high extinction

3. **Participating media material model**: Three properties
   - Extinction σ_t (density)
   - Albedo ρ (color/scattering vs absorption ratio)
   - Phase g (Henyey-Greenstein scattering direction)

4. **Temporal integration**: 5% blend with jittered history
   - Essential for stability at sparse froxel resolution
   - Reduces aliasing under camera motion

5. **Unified shadow system**: Single visibility combining opaque + volumetric shadows
   - Enables coherent lighting across fog and surfaces
   - Critical for visual consistency

### What To Simplify (Phase 1)

1. **Phase function**: Single g value per froxel (no multi-lobe)
   - Sufficient for most art-driven effects
   - Future: support multi-lobe via separate texture channels

2. **Particle voxelization**: Default mode only (2×2×2 cubes)
   - High-quality sphere mode (trilinear) can be added later
   - Particles are secondary to fog system initially

3. **Volumetric shadow maps**: Only for sun (no local light volumetric shadows Phase 1)
   - Sun shadows are perceptually most important
   - Local light shadows can use blobby shadows initially

4. **Indirect light**: Fixed SH probe at volume center
   - No per-froxel probe interpolation
   - Probe updates 1× per frame or per-trigger

### Implementation Phases

**Phase 3.5a (Weeks 1-2): Froxel Architecture**
- [ ] Create FroxelGrid resource class (8×8×64 frustum-aligned texture)
- [ ] Implement frustum-to-froxel coordinate transform
- [ ] Integrate with existing tiled-deferred light culling
- [ ] V-Buffer allocation (RGBA16F×2 per froxel grid)
- [ ] Render graph passes for voxelization → scattering → final

**Phase 3.5b (Weeks 3-4): Material Voxelization**
- [ ] Depth fog contribution (constant extinction + albedo)
- [ ] Height fog contribution (extinction scales with height)
- [ ] Local fog volumes (box, sphere, arbitrary mesh volumes)
- [ ] Density texture sampling in voxelization

**Phase 3.5c (Weeks 5-6): Light Scattering**
- [ ] Sun light integration (cascaded shadows + phase)
- [ ] Indirect light (SH probe) integration
- [ ] Local light integration (reuse light culling)
- [ ] Regular + volumetric shadow combination

**Phase 3.5d (Weeks 7-8): Temporal Integration & Final**
- [ ] Temporal jitter (Halton sequence)
- [ ] History reprojection
- [ ] Energy-conservative final accumulation
- [ ] Application to opaque/transparent surfaces
- [ ] Performance tuning & scalability tiers

**Phase 3.5e (Weeks 9-10): Shadows & Particles**
- [ ] Volumetric shadow map generation (sun only)
- [ ] Particle voxelization (default mode)
- [ ] Particle-fog interaction
- [ ] Documentation + demo scene

**Phase 3.5f: Polish (Weeks 11+)**
- [ ] Multi-lobe phase functions
- [ ] Local light volumetric shadows
- [ ] High-quality particle voxelization
- [ ] Console variables for scalability
- [ ] Profile and optimize hot paths

### File Structure

```
runtime/engine/renderer/
├── volumetric/
│   ├── froxel_grid.h/cpp          # Grid allocation, frustum transform
│   ├── volumetric_material.h       # σt, ρ, g, σe material definition
│   ├── volumetric_voxelizer.h/cpp  # Material voxelization pass
│   ├── volumetric_scattering.h/cpp # Light integration pass
│   ├── volumetric_accumulator.h/cpp# Final integration pass
│   └── volumetric_shadows.h/cpp    # Volumetric shadow maps
│
└── shaders/
    └── volumetric/
        ├── froxel_voxelize.hlsl    # Material → V-Buffer
        ├── froxel_scattering.hlsl  # Light scattering evaluation
        ├── froxel_accumulate.hlsl  # Final energy-conservative integration
        ├── shadow_voxelize.hlsl    # Shadow map generation
        └── apply_volumetric_fog.hlsl # Apply to opaque/transparent
```

### RHI Requirements

- **3D texture support**: RhiTexture with 3D dimension (already supported)
- **Compute shaders**: Voxelization + scattering (already supported)
- **Atomic operations**: InterlockedAdd for particle voxelization (already supported)
- **Texture array support**: Multiple light shadow maps (already supported)

### Lua Scripting API (Phase 4)

```lua
-- Create local fog volume
local volume = uz.scene:create_fog_volume(entity, {
    shape = "box",  -- box, sphere, mesh
    size = {10, 10, 10},

    extinction = 0.5,  -- σt in m⁻¹
    albedo = 0.8,      -- ρ (0=black, 1=white fog)
    phase = 0.3,       -- g (0=isotropic, >0=forward)

    density_texture = "textures/fog_noise.bc7",
})

-- Update global fog parameters
uz.scene:set_fog_extinction(0.1)
uz.scene:set_fog_albedo(0.9)

-- Control volumetric shadows
uz.scene:enable_volumetric_shadows(true)
```

## Key Equations Summary

### Transmittance (Beer-Lambert)
$$T_r(D) = e^{-\sigma_t \cdot D}$$

### Single-Scattering Equation
$$L_i = T_r(D) \cdot L_s + \int_0^D T_r(d) \cdot \sigma_t \cdot L_{\text{scat}} \, dd$$

### Scattered Light
$$L_{\text{scat}} = \rho \cdot f_v(g) \cdot V_{\text{shadow}} \cdot L_{\text{in}}$$

### Energy-Conservative Integration (CRITICAL)
$$\int_0^D e^{-\sigma_t \cdot x} \cdot S \, dx = S \cdot \frac{1 - e^{-\sigma_t \cdot D}}{\sigma_t}$$

### Phase Function (Henyey-Greenstein)
$$f_v(g, \cos\theta) = \frac{1 - g^2}{4\pi(1 + g^2 - 2g\cos\theta)^{3/2}}$$

## References

**Frostbite SIGGRAPH 2015**
- Hillaire, S. "Physically Based and Unified Volumetric Rendering in Frostbite." SIGGRAPH 2015 Course.

**Foundation Papers (cited in presentation)**
- Wenzel et al. [Wenzel07] "Real-Time Volumetric Shadows"
- Engelhardt & Dachsbacher [Engelhardt10] "Epipolar Sampling for Shadows and Crepuscular Rays in Participating Media"
- Mitchell et al. [Mitchell07] "Light Shaft Rendering"
- Wronski et al. [Wronski14] "The State of Real-Time Volumetric Rendering"
- Karis, B. [Karis14] "High Quality Temporal Supersampling"
- Andersson, J. [Andersson11] "Cascaded Translucent Shadows"
- Jansen, J. [Jansen10] "Fourier Opacity Maps" (for particle self-shadowing)
- Elek, O. [Elek09] "Interactive Volumetric Lighting Simulating Scattering and Shadowing"
- Bruneton, E. [Bruneton08] "Precomputed Atmospheric Scattering"

**Physics References**
- PBR Book: "Physically Based Rendering: From Theory to Implementation"
- Lagarde, S. & de Rousiers, C. [Lagarde14] "Moving Frostbite to PBR"

## Related UzEngine Documentation

- [Render Graph Architecture](../../engine/12-rendering/README.md)
- [RHI Abstraction](../../engine/03-rhi/02-dx12-backend.md)
- [Deferred Lighting System](../../engine/12-rendering/deferred-lighting.md)
- [Material System](../../engine/05-shader-pipeline/material-system.md)
- [Sky Atmosphere](ue5-sky-atmosphere-system.md)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-15 | Initial comprehensive analysis with full equations, code examples, performance data, and UzEngine adaptation strategy |

