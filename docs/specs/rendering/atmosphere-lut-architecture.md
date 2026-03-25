<!-- doc-meta
type: spec
category: rendering
status: active
updated: 2026-03-25
version: 1.0
author: docs-operator
-->

# Atmosphere LUT Precomputation Architecture

Reference implementation of Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering" with Hillaire 2020 multi-scatter optimizations. Full precomputation pipeline producing lookup tables for sky rendering and aerial perspective with ~16-30x faster runtime than per-pixel ray marching.

**Reference**: `github.com/ebruneton/precomputed_atmospheric_scattering` (BSD-3 License)

---

## LUT Precomputation Pipeline

```
INITIALIZATION:
├─ TransmittanceLUT (256x64 RGBA16F 2D)
│  └─ transmittance_lut_cs.hlsl
│     Input: Planet radius, atmosphere thickness, Rayleigh/Mie coefficients
│     Output: Optical depth T(r, mu) per (altitude, zenith angle) pair
│
├─ MultiScatterLUT (32x32 RGBA16F 2D) [Hillaire 2020 optimization]
│  └─ multi_scatter_lut_cs.hlsl
│     Input: TransmittanceLUT
│     Output: Fast 2D approximation of cumulative multiple scattering
│     Purpose: Fallback for SM5/DX11, accelerates AP volume ray march
│
└─ SingleScatteringLUT (256x128x32 RGBA16F 3D)
   └─ single_scattering_lut_cs.hlsl
      Input: TransmittanceLUT, sun direction, Rayleigh/Mie phase
      Output: deltaRayleigh3D, deltaMie3D, scatteringLut3D (packed R.rgb + M.r)
      Technique: 50-step Frostbite integration per (r, mu, mu_s, nu)

ITERATIVE ACCUMULATION (4 orders):
│
├─ Order 1 — Direct Irradiance
│  └─ irradiance_lut_cs.hlsl (order=1)
│     Input: TransmittanceLUT
│     Output: T(r, mu_s) * max(mu_s, 0) for ground irradiance lookup
│
├─ Orders 2-4 Loop:
│  │
│  ├─ ScatteringDensity (256x128x32 RGBA16F 3D)
│  │  └─ scattering_density_cs.hlsl
│  │     Inputs: SingleScatteringLUT, PreviousScatteringLUT, IrradianceLUT
│  │     Computation: 16-direction Fibonacci sphere integration
│  │     Rayleigh+Mie phase weighting per direction
│  │     Ground-reflected irradiance contribution
│  │     Output: Density texture for accumulation
│  │
│  ├─ IndirectIrradiance (64x16 RGBA16F 2D)
│  │  └─ irradiance_lut_cs.hlsl (order>1)
│  │     Input: ScatteringDensity from previous step
│  │     Computation: 32-direction hemisphere integration
│  │     Output: E(r, mu_s) for ground irradiance at order N
│  │
│  └─ IndirectScattering (256x128x32 RGBA16F 3D)
│     └─ indirect_scattering_cs.hlsl
│        Input: ScatteringDensity, TransmittanceLUT
│        Technique: 50-step ray integration of density
│        Output: Additive accumulation into scatteringLut3D
│
└─ RESULT: scatteringLut3D contains ALL orders (single + multi-scatter)
```

### Shader Responsibilities

#### transmittance_lut_cs.hlsl
- **Purpose**: Precompute atmospheric optical depth (transmittance)
- **Input**: Planet radius `r`, atmosphere max radius `r_top`, Rayleigh/Mie coeff
- **Output**: 256x64 RGBA16F 2D texture
  - Dimension: (height, view zenith angle μ)
  - Value: T(r, μ) = exp(-optical_depth)
- **Technique**: Ray-sphere intersection + numerical integration
- **Cite**: @ref Bruneton 2017 Section 2

#### single_scattering_lut_cs.hlsl
- **Purpose**: Precompute direct single-scattered light along view rays
- **Input**:
  - TransmittanceLUT (optical depth lookup)
  - Sun direction, sun intensity
  - Rayleigh phase `f_R(theta)`, Mie phase `f_M(theta)`
  - Rayleigh coefficient `β_R`, Mie coefficient `β_M`
- **Output**: 256x128x32 RGBA16F 3D texture
  - Dimensions: (r, μ, μ_s, ν) where:
    - r = altitude from planet center
    - μ = view zenith angle (cos(theta_v))
    - μ_s = sun zenith angle (cos(theta_s))
    - ν = azimuth angle between view and sun
  - Layout: R.rgb = Rayleigh scattering (linear RGB), A = Mie scattering (monochrome)
- **Integration**: 50-step Frostbite analytical integration along ray
- **Cite**: @ref Bruneton 2017 Section 3, @ref Hillaire 2015 "Physically Based and Unified Volumetric Rendering"

#### scattering_density_cs.hlsl
- **Purpose**: Compute scattering density for N-th order (N ≥ 2)
- **Input**:
  - SingleScatteringLUT (order 1 scattered light)
  - PreviousScatteringLUT[order-1] (multiple scatter up to N-1)
  - IrradianceLUT (ground-reflected component)
  - Ozone absorption parameters
- **Output**: 256x128x32 RGBA16F 3D texture
  - Same dimensions as SingleScatteringLUT
  - Value: Density of scattered light at (r, μ, μ_s, ν) for order N
- **Integration**: 16-direction Fibonacci sphere sampling
  - Rayleigh phase for order 2, combined phase for order > 2
  - Ground reflection via irradiance LUT lookup
  - Ozone absorption along incoming ray
- **Cite**: @ref Bruneton 2017 Section 4

#### indirect_scattering_cs.hlsl
- **Purpose**: Accumulate scattered light from scattering density
- **Input**: ScatteringDensity from current order, TransmittanceLUT
- **Output**: 256x128x32 RGBA16F 3D texture (additive accumulation)
  - Same dimensions as SingleScatteringLUT
  - Value: Integral of density along view ray (same computation as single scattering)
- **Integration**: 50-step Frostbite ray march of ScatteringDensity
- **Technique**: Ray-sphere intersection + exponential transmittance weighting
- **Cite**: @ref Bruneton 2017 Section 4 (algorithm same as single scattering, but input is density)

#### irradiance_lut_cs.hlsl (dual-mode)
- **Purpose**: Precompute ground irradiance for direct sun (order 1) and scattered light (order N)
- **Input**:
  - For order 1: TransmittanceLUT only
  - For order N: ScatteringDensity from current order
- **Output**: 64x16 RGBA16F 2D texture
  - Dimensions: (ground altitude, sun zenith angle μ_s)
  - Value: E(r, μ_s) irradiance on ground plane
- **Order 1 (Direct)**: T(r_ground, μ_s) * max(μ_s, 0)
  - Direct sun contribution, cosine weight for horizontal surface
- **Order N (Indirect)**: 32-direction Fibonacci hemisphere integral
  - Incoming scattered light from all directions
  - Weighted by cos(theta) for Lambertian ground
  - Rayleigh/Mie phase for order 2, combined for order > 2
- **Cite**: @ref Bruneton 2017 Section 3

---

## Runtime Usage

### Sky Rendering (sky_atmosphere_ps.hlsl)

**Path 1: LUT Lookup (SM6.6, ~0.01ms)**
```hlsl
// Single 4D texture lookup via 3D LUT
float3 rayleigh = SampleScatteringTexture4(scatteringLut3D, r, mu, muS, nu);
float mie = rayleigh.w;  // Packed in alpha

// Phase function application at sample time
float3 L = rayleigh * PhaseRayleigh(theta) + mie * PhaseMie(theta);
```

**Path 2: Ray March (SM5, fallback, ~0.3ms)**
```hlsl
// Kept in else block for compatibility
// 32-step march with analytical transmittance
for (int i = 0; i < 32; i++) {
    // Sample density along ray, integrate with transmittance
}
```

**Sun Disk**: Analytical (unchanged), additive composite

**Ground Contribution**:
- LUT: `GetIrradianceTextureUvFromRMuS()` → 64x16 lookup → Lambertian reflection
- March: Direct Lambertian from sun transmittance

### Sky Cubemap Capture (sky_cubemap_cs.hlsl)

**Purpose**: Bake sky into 6-face RGBA16F cubemap for IBL

**Path 1: LUT Lookup (~0.05ms per face, 16x faster)**
```hlsl
// Per-pixel ray direction → GetSkyRadiance() LUT lookup
float3 radiance = GetSkyRadiance(eyePos, rayDir, sunDir, sunIntensity);
```

**Path 2: Ray March (fallback, ~0.8ms per face)**
- 16-step march kept in else block
- Uses direct optical depth + phase integration

### Aerial Perspective Volume (aerial_perspective_volume_cs.hlsl)

**Purpose**: Per-voxel inscatter + transmittance for deferred lighting

**Path 1: LUT Lookup (SM6.6, ~0.05ms, ~16x faster)**
```hlsl
// Dual 4D lookup: camera→voxel and voxel→infinity
float3 skyRadiance = GetSkyRadianceToPoint(cameraPos, voxelPos, sunDir);
float3 transmittance = GetTransmittance(cameraPos, voxelPos, sunDir);
```

**Path 2: Ray March (SM5 fallback, ~0.8ms)**
- 8-step march per voxel
- Phase 1 multi-scatter 2D LUT applied per step (Hillaire 2020)

**Integration into Deferred Lighting**:
```hlsl
// Sample AP volume at fragment position
float3 inscatter = APVolume.SampleLevel(pos, 0);
float3 transmittance = APVolume.SampleLevel(pos, 1);  // W channel
sceneColor = sceneColor * transmittance + inscatter;
```

---

## GPU Resources

### Texture Allocations

| Texture | Size | Format | Purpose | RW Mode |
|---------|------|--------|---------|---------|
| **TransmittanceLUT** | 256x64 | RGBA16F | Optical depth T(r,mu) | CS UAV → PS SRV |
| **MultiScatterLUT** | 32x32 | RGBA16F | Fast 2D multi-scatter (Hillaire 2020 fallback) | CS UAV → CS SRV |
| **ScatteringLUT3D** | 256x128x32 | RGBA16F | Bruneton full scattering S(r,mu,mu_s,nu), all orders | CS UAV (accumulate) → PS/CS SRV |
| **DeltaRayleigh3D** | 256x128x32 | RGBA16F | Temp: current-order Rayleigh delta | CS UAV |
| **DeltaMie3D** | 256x128x32 | RGBA16F | Temp: current-order Mie delta | CS UAV |
| **ScatteringDensity3D** | 256x128x32 | RGBA16F | Temp: density for integration in next pass | CS UAV |
| **IrradianceLUT** | 64x16 | RGBA16F | Ground irradiance E(r,mu_s) | CS UAV (accumulate) → PS SRV |
| **DeltaIrradiance** | 64x16 | RGBA16F | Temp: current-order irradiance delta | CS UAV |
| **APVolume** | 32x32x16 | RGBA16F | Per-voxel inscatter.rgb + transmittance.a | CS UAV → PS SRV |

### Memory Footprint
- TransmittanceLUT: 256 × 64 × 8 bytes = 131 KB
- ScatteringLUT3D: 256 × 128 × 32 × 8 bytes = 268 MB (largest)
- IrradianceLUT: 64 × 16 × 8 bytes = 8 KB
- APVolume: 32 × 32 × 16 × 8 bytes = 256 KB
- **Total**: ~269 MB (streaming candidate for memory-constrained platforms)

---

## Key Functions (atmosphere_lut.hlsli)

### Coordinate Mapping

**GetRMuMuSNuFromScatteringTextureUvwz()**
- **Input**: 4D texture coordinates (u, v, w, z) normalized to [0, 1)^4
- **Output**: Physical parameters (r, μ, μ_s, ν)
- **Purpose**: Convert texture space → shader computation space
- **Cite**: @ref Bruneton 2017 Appendix A

**GetScatteringTextureUvwzFromRMuMuSNu()**
- **Inverse**: Physical parameters → texture coordinates
- **Used by**: Precomputation shaders (maps dispatch thread ID to physical space)

**GetRMuMuSNuFromScatteringTextureFragCoord()**
- **Input**: Fragment coordinate + texture dimensions
- **Output**: Physical parameters at fragment center
- **Used by**: Raster variant of precomputation

### Transmittance Queries

**GetTransmittance(r1, r2, mu)**
- **Purpose**: Compute transmittance from altitude r1 to r2 at zenith angle μ
- **Returns**: exp(-optical_depth)
- **Used by**: All shaders needing ray opacity

**GetTransmittanceToSun(r, mu_s)**
- **Purpose**: Transmittance from altitude r looking toward sun (zenith μ_s)
- **Used by**: Density integration, irradiance computation
- **Cite**: @ref Bruneton 2017 Equation 5

### Scattering Lookups

**SampleScatteringTexture(scatteringLut3D, r, mu, muS, nu)**
- **Purpose**: 3D texture lookup with normalized coordinates
- **Returns**: (rayleigh.rgb, mie) at (r, μ, μ_s, ν)
- **Technique**: Trilinear interpolation

**SampleScatteringTexture4(scatteringLut3D, r, mu, muS, nu)**
- **Extended 4D Lookup**: Handles ν wrapping (azimuth is periodic)
- **Optimization**: Branches for ν < 0.5 (direct) vs ν >= 0.5 (opposite hemisphere)
- **Used by**: Sky rendering, AP volume
- **Cite**: @ref Bruneton 2017 Section 5 lookup strategy

### Sky Radiance Functions

**GetSkyRadiance(cameraPos, rayDir, sunDir, sunIntensity)**
- **Purpose**: Infinite-distance sky color lookup
- **Computation**:
  1. Ray-sphere intersection (ray origin → atmosphere exit)
  2. Compute (r, μ, μ_s, ν) at exit point
  3. SampleScatteringTexture4() + phase function
  4. Apply sun disk analytically
  5. Add ground reflection if ray hits planet
- **Returns**: RGB radiance
- **Cite**: @ref Bruneton 2017 Section 5

**GetSkyRadianceToPoint(cameraPos, pointPos, sunDir)**
- **Purpose**: Finite-distance inscattering + transmittance (for AP volume)
- **Computation**:
  1. Compute (r_c, μ_c, μ_s, ν_c) at camera
  2. Compute (r_p, μ_p, μ_s, ν_p) at target point
  3. Dual lookup: SampleScatteringTexture4(camera) − SampleScatteringTexture4(point) * transmittance
  4. Result is inscattered light visible at camera from point's direction
- **Returns**: (inscatter.rgb, transmittance) packed in RGBA
- **Cite**: @ref Bruneton 2017 Section 5

### Ground Queries

**GetIrradianceTextureUvFromRMuS(r, muS)**
- **Purpose**: Map physical coordinates to 2D irradiance LUT UV
- **Input**: Altitude r, sun zenith μ_s
- **Output**: (u, v) for IrradianceLUT sampling
- **Used by**: Sky ground reflection, height fog

**RayIntersectsGround(rayOrigin, rayDir, planetRadius)**
- **Purpose**: Test if view ray hits planet (not outer space)
- **Returns**: Boolean + intersection distance
- **Used by**: GetSkyRadiance() to branch ground vs sky

---

## Precomputation Orchestration (atmosphere_renderer.cpp)

### Dirty-Flag Driven

```cpp
if (scatteringLut3DDirty) {
    // Full precomputation pipeline
    ENQUEUE_PASS(TransmittanceLUT);
    ENQUEUE_PASS(SingleScatteringLUT);
    ENQUEUE_PASS(DirectIrradiance);

    for (int order = 2; order <= 4; order++) {
        ENQUEUE_PASS(ScatteringDensity);
        ENQUEUE_PASS(IndirectIrradiance);
        ENQUEUE_PASS(IndirectScattering);
    }

    scatteringLut3DDirty = false;
}
```

### Trigger Conditions

| Condition | Action |
|-----------|--------|
| Startup | Full precomputation (once) |
| SkyAtmosphereComponent changed | Set `scatteringLut3DDirty` |
| Sun direction changed | Set `scatteringLut3DDirty` |
| Sun intensity changed | Set `scatteringLut3DDirty` |
| Sky parameter (rayleigh/mie) changed | Set `scatteringLut3DDirty` |

### Constant Buffer: AtmospherePrecomputeCB

```hlsl
struct AtmospherePrecomputeCB {
    float planetRadius;          // km
    float atmosphereHeight;      // km
    float sunZenithAngle;        // radians
    uint scatteringOrder;        // 1, 2, 3, or 4

    float3 rayleighCoeff;        // Rayleigh scattering coefficient
    float mieCoeff;              // Mie scattering coefficient

    float3 rayleighAbsorption;   // Absorption cross-section
    float mieAbsorption;         // Mie absorption

    float3 ozoneAbsorption;      // Ozone layer absorption
    float ozoneDensityHeight;    // km

    float groundAlbedo;          // Lambertian ground reflectance [0, 1]
    float muSMin;                // Minimum sun zenith cos for computation

    // ... texture descriptor indices (bindless)
};
```

---

## Performance

### Precomputation Time (one-time at startup)

| Shader | Dispatch | Time |
|--------|----------|------|
| TransmittanceLUT | 256x64x1 | ~0.5ms |
| SingleScatteringLUT | 256x128x32 | ~2ms |
| DirectIrradiance | 64x16x1 | ~0.1ms |
| ScatteringDensity (×4) | 256x128x32 | ~2ms each |
| IndirectIrradiance (×4) | 64x16x1 | ~0.1ms each |
| IndirectScattering (×4) | 256x128x32 | ~2ms each |
| **Total** | | ~26ms |

### Runtime Performance

| Use Case | LUT Path | March Path | Speedup |
|----------|----------|-----------|---------|
| Sky PS per-pixel | 0.01ms (LUT lookup) | 0.3ms (32-step march) | ~30x |
| Sky cubemap per-face | 0.05ms (6 faces) | 0.8ms (16-step march) | ~16x |
| AP volume per-voxel | 0.05ms dual lookup (32³) | 0.8ms (8-step march) | ~16x |

### Memory vs Speed Trade-off

- **ScatteringLUT3D**: 268 MB (large) → 30x sky speedup, 16x AP speedup
- **MultiScatterLUT**: 8 KB (tiny) → 2x AP fallback speedup
- **Total precomputation**: ~26ms (one-time, hidden by loading screen)

---

## Fallback Paths (SM5 / DX11 Compatibility)

### When ScatteringLUT3D unavailable

| Pass | LUT Path | Fallback |
|------|----------|----------|
| sky_atmosphere_ps | SampleScatteringTexture4() | 32-step ray march |
| sky_cubemap_cs | GetSkyRadiance() LUT | 16-step ray march |
| aerial_perspective_volume_cs | GetSkyRadianceToPoint() dual lookup | 8-step march + MultiScatterLUT sampling |

Fallback condition: `if (scatteringLut3DIdx == 0xFFFFFFFF) { /* march */ }`

---

## Cite & References

- @ref Bruneton, Neyret 2008 "Precomputed Atmospheric Scattering"
- @ref Bruneton, Neyret 2017 "Precomputed Atmospheric Scattering (Updated)" — `github.com/ebruneton/precomputed_atmospheric_scattering`
- @ref Hillaire 2015 "Physically Based and Unified Volumetric Rendering in Frostbite" — multi-scatter integration, density integration
- @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" — multi-scatter 2D LUT, optimizations

---

## Related Docs

- **Atmosphere System (overview)**: `docs/specs/rendering/atmosphere.md`
- **Shader Reference**: `docs/shaders.md` (Atmosphere & Sky section, 15 shaders)
- **Height Fog**: `docs/specs/rendering/atmosphere.md#height-fog`
- **Sky Rendering**: `docs/specs/rendering/atmosphere.md#sky-atmosphere-pass`

