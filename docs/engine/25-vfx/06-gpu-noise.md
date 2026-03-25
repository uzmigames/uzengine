---
title: GPU Noise Functions Reference
status: Done (Phase 3.61.0)
last-updated: 2026-03-24
---

# GPU Noise Functions Reference

Complete reference for GPU noise functions used in VFX modules and particle effects.

> **Header**: `runtime/shaders/runtime/include/vfx_common.hlsli`
> **Language**: HLSL/DirectX Shader Language

## Overview

The VFX system includes a suite of coherent noise functions optimized for GPU compute shaders. These functions generate smooth, continuous noise fields suitable for organic particle motion, turbulence, and fluid-like effects.

**Functions**:
- `Gradient3D()` — Selects gradient vectors for noise basis
- `Perlin3D()` — Classic 3D Perlin noise (smooth, directional artifacts)
- `SimplexNoise3D()` — Simplex noise (fewer artifacts, cheaper)
- `FBM3D()` — Fractal Brownian motion (multi-octave noise)
- `CurlNoise3D()` — Divergence-free curl noise (for fluid-like motion)

---

## Gradient3D

Low-level function that selects one of 12 gradient directions.

### Signature

```hlsl
float3 Gradient3D(int3 ip)
```

### Parameters

| Name | Type | Description |
|------|------|-------------|
| `ip` | int3 | Integer lattice coordinates |

### Returns

One of 12 unit-length gradient vectors (cube edge midpoints):

```
(±1, ±1,  0)  —  ±X, ±Y
(±1,  0, ±1)  —  ±X, ±Z
( 0, ±1, ±1)  —  ±Y, ±Z
```

### Implementation Details

- Hashes 3D integer coordinates via PCG hash function
- Selects one of 12 gradients using modulo 12
- Gradient set chosen for isotropy (uniform distribution in 3D)
- Cache-friendly lookup via small LUT (12 entries)

### Usage

Used internally by `Perlin3D()` and `SimplexNoise3D()`. Not typically called directly.

---

## Perlin3D

Classic 3D Perlin noise with improved quintic interpolation.

### Signature

```hlsl
float Perlin3D(float3 pos)
```

### Parameters

| Name | Type | Description |
|------|------|-------------|
| `pos` | float3 | Continuous 3D sampling position |

### Returns

Noise value approximately in range **[-1.0, 1.0]**

### Algorithm

1. **Lattice**: Floor input to find enclosing unit cube
2. **Gradients**: Compute dot-product gradients at all 8 cube corners
3. **Interpolation**: Blend with Perlin's improved quintic fade curve
   - Fade function: `t = t³ * (t * (t*6 - 15) + 10)`
   - Eliminates second-derivative discontinuities (C2 continuous)
4. **Trilinear blend**: Interpolate across x, y, z axes

### Characteristics

| Property | Value |
|----------|-------|
| Frequency response | Good isotropy, some directional artifacts |
| Interpolation | Smooth (C2 continuous) |
| Performance | 8 gradient lookups + 3-level trilinear interpolation |
| Cache behavior | Good locality (only 8 nearby lattice points) |
| Artifacts | Visible grid direction bias (but subtle with multiple octaves) |

### Example Usage

```hlsl
// Perturb velocity with noise
float3 pos = Positions[idx];
float noiseVal = Perlin3D(pos * 0.5f);  // Scale spatial frequency
Velocities[idx] += noiseVal * float3(1, 0, 1);  // X/Z perturbation
```

### Visual Characteristics

- Smooth, continuous gradients (good for force fields)
- Slightly directional (aligned to grid axes)
- Works well for organic shapes and turbulence
- Suited for multiple-octave layering (FBM)

---

## SimplexNoise3D

Simplex noise using tetrahedral decomposition.

### Signature

```hlsl
float3 SimplexNoise3D(float3 v)
```

### Parameters

| Name | Type | Description |
|------|------|-------------|
| `v` | float3 | Continuous 3D sampling position |

### Returns

Noise value approximately in range **[-1.0, 1.0]**

### Algorithm

1. **Skew**: Transform 3D space into rhombohedral simplex lattice
2. **Simplex ID**: Identify which of 6 possible tetrahedra contains the point
3. **Corners**: Evaluate radial kernel at 4 simplex corners
4. **Blend**: Weighted sum of kernel values (w^4 falloff)

### Characteristics

| Property | Value |
|----------|-------|
| Frequency response | More isotropic than Perlin (fewer visible directions) |
| Grid artifacts | Minimal directional bias |
| Performance | 4 gradient lookups (vs 8 for Perlin) |
| Smoothness | C2 continuous (like Perlin) |
| Advantages | Cheaper, fewer directional artifacts |

### Comparison: Perlin vs Simplex

| Aspect | Perlin3D | SimplexNoise3D |
|--------|----------|---|
| GPU cost | ~8 gradient lookups | ~4 gradient lookups |
| Isotropy | Good | Excellent (minimal direction artifacts) |
| Visual quality | Good (subtle grid) | Excellent (more organic) |
| Use case | Force fields, FBM | Organic noise, fluids |
| Recommended | General purpose | When isotropy matters most |

### Example Usage

```hlsl
// Smoke turbulence (isotropic, fewer artifacts)
float3 smokeNoise = SimplexNoise3D(pos * frequency + time);
Velocities[idx] += smokeNoise * strength;
```

---

## FBM3D

Fractal Brownian Motion — multi-octave noise with decreasing amplitude.

### Signature

```hlsl
float3 FBM3D(float3 pos, float time, int octaves)
```

### Parameters

| Name | Type | Description |
|------|------|-------------|
| `pos` | float3 | 3D sampling position |
| `time` | float | Animation time offset (added to pos.x per octave) |
| `octaves` | int | Number of noise layers (clamped to [1, 8]) |

### Returns

3D noise vector, each component in approximately **[-1.0, 1.0]**

### Algorithm

1. **Layer loop**: Accumulate `octaves` layers of Perlin3D
2. **Lacunarity 2.0**: Frequency doubles each octave (finer detail)
3. **Gain 0.5**: Amplitude halves each octave (progressive damping)
4. **Time**: Added to pos.x for temporal animation
5. **Uncorrelated channels**: X/Y/Z sample different offset noise fields
6. **Normalization**: Divide by sum of amplitudes

```hlsl
for (int i = 0; i < octaves; i++) {
    // Each component uses different offset seeds for decorrelation
    result.x += amplitude * Perlin3D(p + kOffsetX);
    result.y += amplitude * Perlin3D(p + kOffsetY);
    result.z += amplitude * Perlin3D(p + kOffsetZ);

    frequency *= 2.0f;     // Lacunarity: frequency doubles
    amplitude *= 0.5f;     // Gain: amplitude halves
}
```

### Characteristics

| Property | Value |
|----------|-------|
| Detail levels | Octave count determines frequency range |
| Temporal animation | Time offset creates flowing motion |
| Isotropy | Good (uses uncorrelated offset channels) |
| Cost | O(octaves) — each octave = 1 Perlin3D call × 3 components |
| Output range | Each component ≈ [-1, 1] after normalization |

### Performance

| Octaves | Approx Cost | Notes |
|---------|------------|-------|
| 1 | Very fast | Just one Perlin3D (low detail) |
| 2 | 2× Perlin3D | Good balance for turbulence |
| 3 | 3× Perlin3D | Default for TurbulenceModule |
| 4 | 4× Perlin3D | More detail, still reasonable |
| 8 | 8× Perlin3D | Maximum (fine detail, expensive) |

**Recommendation**: Use 2-4 octaves for real-time effects. Profiling shows 4 octaves is sustainable at 60 FPS with typical particle counts.

### Example Usage

```hlsl
// Turbulent wind field with animation
float3 time_animated_noise = FBM3D(pos * frequency, cbFrameData.time, 3);
Velocities[idx] += time_animated_noise * turbulence_strength;
```

### Visual Characteristics

- Creates organic, flowing patterns
- Adds detail at multiple scales (self-similar)
- Continuous temporal animation when time varies
- Uncorrelated X/Y/Z components prevent grid-aligned motion
- Ideal for fire, smoke, and fluid-like effects

---

## CurlNoise3D

Divergence-free curl noise for fluid-like particle motion.

### Signature

```hlsl
float3 CurlNoise3D(float3 pos, float time)
```

### Parameters

| Name | Type | Description |
|------|------|-------------|
| `pos` | float3 | 3D sampling position |
| `time` | float | Animation time offset |

### Returns

Divergence-free 3D velocity vector (curl of FBM3D)

### Algorithm

1. **Basis**: Sample FBM3D at current position and three perturbed neighbors
2. **Finite differences**: Compute approximate partial derivatives (∂F/∂x, ∂F/∂y, ∂F/∂z)
3. **Cross product**: Compute curl = ∇ × F
   ```
   curl(F) = (dFz/dy - dFy/dz, dFx/dz - dFz/dx, dFy/dx - dFx/dy)
   ```
4. **Result**: Inherently divergence-free (∇·curl ≡ 0)

### Mathematical Property

```
∇ · (∇ × F) = 0  (always, mathematically)
```

This means particles moved by curl noise velocity field never compress or expand — ideal for incompressible fluids.

### Characteristics

| Property | Value |
|----------|-------|
| Divergence | Zero (∇·v = 0, incompressible) |
| GPU cost | ~7-8 FBM3D evaluations (expensive) |
| Use case | Fluid-like effects (smoke, fire, water) |
| Temporal | Animated via time offset (smooth flow) |
| Visual quality | Organic, non-compressive turbulence |

### Performance Trade-off

CurlNoise3D is more expensive than FBM3D (7-8× evaluations vs 3× for FBM3D with 4 octaves). Use when:
- ✅ Visual quality requires fluid-like incompressibility
- ✅ Smoke/fire effects where compression artifacts are visible
- ✅ Large-scale particle counts can absorb the cost

Skip CurlNoise3D when:
- ❌ Lower-end targets (mobile, VR) where cost is critical
- ❌ Fine-grained turbulence (FBM3D achieves similar look with less cost)
- ❌ Real-time constraints limit compute shader time

### Example Usage

```hlsl
// Smoke turbulence with incompressible flow
float3 divergence_free = CurlNoise3D(pos * 0.5f, cbFrameData.time);
Velocities[idx] += divergence_free * smoke_strength;
```

### Visual Characteristics

- Particles flow like incompressible fluid (no clustering or voids)
- Smooth, swirling patterns
- Excellent for organic fire/smoke effects
- More expensive but visually superior to FBM3D for fluid sims

---

## Integration with VFX Modules

### TurbulenceModule

Uses `FBM3D()` for velocity perturbation.

```hlsl
// Inside TurbulenceModule shader code
float3 turbulence = FBM3D(Positions[idx] * cbModule.frequency,
                           cbFrameData.time,
                           cbModule.octaves);
Velocities[idx] += turbulence * cbModule.strength;
```

**Parameters**:
- `strength`: Scales the turbulence vector
- `frequency`: Controls spatial scale (smaller = larger noise patterns)
- `octaves`: Number of detail layers (1-8, clamped)

### Custom Module Example

```hlsl
// Custom fluid-like turbulence module
float3 FluidTurbulence(float3 pos, float time, float strength) {
    // Incompressible curl noise for fluid effects
    float3 curl = CurlNoise3D(pos * 0.25f, time);
    return curl * strength;
}

// In compute shader:
if (idx < cbDispatchData.numParticles && Ages[idx] >= 0.0) {
    Velocities[idx] += FluidTurbulence(Positions[idx],
                                        cbFrameData.time,
                                        cbModule.strength);
}
```

---

## Performance Profiling

### Typical Costs (per particle, on modern GPU)

| Function | Evaluations | Approx Cost | Notes |
|----------|-------------|------------|-------|
| Perlin3D | 1 call | ~4 μs | 8 gradient lookups |
| SimplexNoise3D | 1 call | ~2.5 μs | 4 gradient lookups (cheaper) |
| FBM3D (3 octaves) | 3 Perlin3D | ~12 μs | 3 channels × 3 octaves |
| FBM3D (4 octaves) | 4 Perlin3D | ~16 μs | 3 channels × 4 octaves |
| CurlNoise3D | ~8 FBM3D | ~120 μs | 8 samples + finite differences |

**GPU Compute**: On a typical 256-thread dispatch with modern GPU:
- FBM3D: Negligible overhead
- CurlNoise3D: Noticeable but acceptable if not applied every frame

### Optimization Tips

**Do's**:
- ✅ Use SimplexNoise3D instead of Perlin3D for better isotropy at same cost
- ✅ Cache FBM3D results if applying same noise to multiple particles
- ✅ Profile on target hardware (performance varies widely by GPU)
- ✅ Use lower octave counts (2-3) if cost is critical
- ✅ Apply CurlNoise3D only to particles that need it (not all)

**Don'ts**:
- ❌ Avoid CurlNoise3D per-frame on every particle (too expensive)
- ❌ Don't use more octaves than needed for visual quality
- ❌ Avoid calling Perlin3D directly in tight loops (use FBM3D or SimplexNoise3D)

---

## Common Patterns

### Pattern 1: Organic Turbulence

```hlsl
float3 turbulence = FBM3D(Positions[idx] * 0.5f, cbFrameData.time, 3);
Velocities[idx] += turbulence * 2.0f;
```

Good for fire, smoke, explosive effects.

### Pattern 2: Directional Wind Field

```hlsl
float wind_magnitude = FBM3D(Positions[idx] * 0.1f, cbFrameData.time, 2).x;
float3 wind_direction = float3(1, 0, 0);  // East wind
Velocities[idx] += wind_direction * wind_magnitude * 5.0f;
```

Creates consistent directional wind with turbulent variation.

### Pattern 3: Swirling Motion (Incompressible)

```hlsl
float3 swirl = CurlNoise3D(Positions[idx] * 0.25f, cbFrameData.time);
Velocities[idx] += swirl * 3.0f;
```

Ideal for vortex, dust devil, tornado effects.

### Pattern 4: Noise-Based Size Variation

```hlsl
float noise = Perlin3D(Positions[idx] * 2.0f);  // Range ≈ [-1, 1]
float size_scale = 0.5f + 0.5f * noise;  // Range [0, 1]
Sizes[idx] *= size_scale;
```

Particles vary in size based on spatial position.

---

## Debugging Tips

### Visualizing Noise

To debug noise patterns, output as colors in a visualizer:

```hlsl
// Map noise [-1, 1] to color [0, 1]
float3 noise = FBM3D(Positions[idx] * 0.5f, 0.0f, 3);  // time = 0 for static
float3 color = 0.5f + 0.5f * noise;  // Remap to [0, 1]
Colors[idx] = float4(color, 1.0f);
```

Result: Particles colored by their noise value, revealing noise patterns.

### Noise Frequency Tuning

```hlsl
// Too high frequency (noisy, jittery):
float3 noisy = FBM3D(Positions[idx] * 5.0f, time, 4);

// Too low frequency (smooth, large-scale):
float3 smooth = FBM3D(Positions[idx] * 0.1f, time, 4);

// Just right (balanced detail and scale):
float3 balanced = FBM3D(Positions[idx] * 0.5f, time, 3);
```

Start with `frequency = 0.5f` and adjust ±50% based on visual results.

---

## See Also

- [VFX Module API](04-module-api.md) — TurbulenceModule usage
- [Particle System](02-particle-system.md) — Compute shader dispatch
- `runtime/shaders/runtime/include/vfx_common.hlsli` — Full HLSL implementation
