# Proposal: phase0-3.2.0-atmosphere-scattering-lut

## Why

Current sky rendering uses a 32-step per-pixel ray march in `sky_atmosphere_ps.hlsl`, which
is ~32x more expensive than a single LUT lookup. The aerial perspective volume does not
sample the multi-scatter LUT, making distant objects too dark. Multi-scattering uses the
Hillaire 2020 isotropic approximation (32x32 2D) instead of the more accurate Bruneton 2017
iterative approach (4+ orders via 3D textures).

Analysis of [takram/three-atmosphere](https://github.com/takram-design-engineering/three-geospatial/tree/main/packages/atmosphere)
(MIT/BSD-3 licensed, Bruneton 2017 faithful implementation) reveals that a precomputed 4D
scattering LUT (256x128x32 RGBA16F, ~4MB VRAM) eliminates the per-pixel sky march entirely
while providing exact multi-scattering. This is the single biggest performance win available
for the atmosphere system.

## Reference Implementation

- **Source**: https://github.com/takram-design-engineering/three-geospatial/tree/main/packages/atmosphere
- **License**: MIT (shaders: BSD-3-Clause from Bruneton reference)
- **Analysis**: `docs/analysis/rendering/three-geospatial-atmosphere-comparison.md`
- **Original paper**: Bruneton & Neyret 2008/2017 "Precomputed Atmospheric Scattering"

## What Changes

### Phase 1: Multi-Scatter in AP Volume (P0 — trivial fix)
Add `MultiScatterLUT.SampleLevel()` in `aerial_perspective_volume_cs.hlsl` per voxel step.
Distant objects currently lack multi-scattered light, appearing darker than they should.

### Phase 2: Bruneton 3D Scattering LUT Precomputation (P1 — core)
Implement the full Bruneton 2017 precomputation pipeline:
1. Single Rayleigh scattering (3D): numerical integration using transmittance LUT
2. Single Mie scattering (3D): same, packed as Mie red + Rayleigh RGB in RGBA
3. Scattering density (3D): per-order density computation
4. Multiple scattering (3D): accumulated iteratively for 4 orders
5. Indirect irradiance (2D): accumulated per order

Output: 256x128x32 RGBA16F 3D texture (~4MB) containing total scattering (all orders).
Precomputed once at startup (~50ms), recomputed only when atmosphere parameters change.

### Phase 3: Sky Rendering via LUT Lookup (P1 — replaces per-pixel march)
Replace the 32-step ray march in `sky_atmosphere_ps.hlsl` with a single `GetSkyRadiance()`
function that performs a 4D texture lookup into the precomputed scattering LUT.
The 4D parameterization (r, mu, mu_s, nu) is packed into the 3D texture as:
- width = nu_size(8) x mu_s_size(32) = 256
- height = mu_size(128)
- depth = r_size(32)

### Phase 4: Irradiance LUT (P2 — ground lighting)
Add 64x16 RGBA16F irradiance LUT storing direct + indirect sky irradiance as a function
of (altitude, sun_zenith). Enables accurate ground color without ray marching.

### Phase 5: AP Volume Upgrade (P2 — use scattering LUT)
Update `aerial_perspective_volume_cs.hlsl` to sample the 3D scattering LUT instead of
inline ray marching. Each voxel becomes a single LUT lookup pair (camera + point),
eliminating the 8-step march per voxel.

## Impact
- Affected specs: `docs/specs/rendering/atmosphere.md`
- Affected code:
  - MODIFY: `aerial_perspective_volume_cs.hlsl` (Phase 1+5)
  - MODIFY: `sky_atmosphere_ps.hlsl` (Phase 3 — replace march with LUT lookup)
  - MODIFY: `atmosphere_renderer.h/cpp` (manage new LUTs, precompute pipeline)
  - MODIFY: `cloud_pass.cpp` (use scattering LUT for cloud aerial perspective)
  - NEW: `single_scattering_lut_cs.hlsl` (Phase 2)
  - NEW: `scattering_density_cs.hlsl` (Phase 2)
  - NEW: `multiple_scattering_lut_cs.hlsl` (Phase 2)
  - NEW: `irradiance_lut_cs.hlsl` (Phase 4)
- Breaking change: NO (existing atmosphere visuals preserved, new LUTs added)
- User benefit: ~32x faster sky rendering, more accurate multi-scatter, correct distant object brightness

## Key References

| Paper | Year | Technique |
|-------|------|-----------|
| Bruneton & Neyret, "Precomputed Atmospheric Scattering" | 2008 | Original LUT precomputation framework |
| Bruneton, "A Qualitative and Quantitative Evaluation..." (demo) | 2017 | Updated 4D parameterization, reference C++ impl |
| Hillaire, "A Scalable and Production Ready Sky and Atmosphere" | 2020 | Multi-scatter 2D approx (our current approach) |
| takram/three-atmosphere (MIT) | 2025 | Reference implementation of Bruneton 2017 in WebGL |
