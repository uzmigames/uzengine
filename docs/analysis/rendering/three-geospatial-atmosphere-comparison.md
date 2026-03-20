<!-- doc-meta
type: analysis
category: rendering/atmosphere
title: three-geospatial Atmosphere vs UzEngine — Gap Analysis
created: 2026-03-20
updated: 2026-03-20
status: complete
-->

# three-geospatial Atmosphere vs UzEngine — Gap Analysis

Comparison of [takram/three-atmosphere](https://github.com/takram-design-engineering/three-geospatial/tree/main/packages/atmosphere) (Bruneton 2017 faithful) with UzEngine's current Hillaire 2020 hybrid implementation.

---

## 1. Architecture Comparison

| Feature | UzEngine (current) | three-atmosphere | Winner |
|---------|-------------------|------------------|--------|
| **Transmittance LUT** | 256x64 RGBA16F, Bruneton 2017 | 256x64 Float32/16, identical | Tie |
| **Multi-scatter method** | Hillaire 2020 (32x32 2D, isotropic approx, geometric series) | Bruneton 2017 (iterative 4+ orders via 3D textures) | three-atmo (more accurate) |
| **Scattering LUT** | None (per-pixel march) | 256x128x32 3D RGBA (4D packed) | three-atmo |
| **Irradiance LUT** | None (implicit via cubemap) | 64x16 2D (direct + indirect) | three-atmo |
| **Sky rendering** | Per-pixel 32-step ray march | Single 4D LUT lookup (zero march) | three-atmo (faster) |
| **Sky View LUT** | None | None | Tie (neither has it) |
| **Aerial perspective** | 32x32x16 volume + inline 8-step march | Per-pixel dual 4D LUT lookup | UzEngine (volume is better for games) |
| **AP multi-scatter** | Not sampled in AP volume fill | Via 4D LUT (implicit) | three-atmo |
| **Per-frame precompute** | Transmittance + multi-scatter only on dirty flag | None (all precomputed at init) | three-atmo (zero per-frame) |
| **Sun disk** | Analytical in sky PS | LUT transmittance + smoothstep AA | Tie |
| **Moon** | None | Oren-Nayar BRDF + ray-sphere | three-atmo |
| **Stars** | None | Yale BSC 9096 stars, black body chromaticity | three-atmo |
| **Ozone** | Yes (tent distribution) | Yes (tent distribution) | Tie |
| **IBL capture** | 64x64 cubemap, 16 steps per texel | CPU-sampled SH from irradiance LUT | UzEngine (GPU cubemap is richer) |
| **Coordinate system** | Z-up game world, km atmosphere | ECEF WGS84, meters | UzEngine (simpler for games) |

---

## 2. What three-atmosphere Does Better

### 2a. Full 4D Scattering LUT (HIGH value)
three-atmosphere precomputes a full 4D scattering LUT (256x128x32 = 1M texels) following Bruneton 2017. This means:
- **Sky rendering is a single texture lookup** — no 32-step ray march per pixel
- Multi-scattering is exact (4+ iterative orders), not an isotropic approximation
- The LUT captures view-dependent scattering correctly at all sun angles

**UzEngine impact**: Our per-pixel 32-step sky march is ~32x more expensive than a single LUT lookup for the same quality. However, adding a 3D scattering LUT requires ~4MB VRAM (256x128x32 × RGBA16F = 4MB) and a significant precomputation pass.

**Recommendation**: **Implement the Bruneton 2017 scattering 3D LUT.** The VRAM cost is minimal and it would eliminate the per-pixel sky march entirely. This is the single biggest performance win available.

### 2b. Irradiance LUT (MEDIUM value)
three-atmosphere has a dedicated 64x16 irradiance LUT that stores direct + indirect irradiance at the ground as a function of (altitude, sun_zenith). This enables:
- Instant ground color lookup without ray marching
- CPU-side SH probe sampling for ambient lighting

**UzEngine impact**: We currently get ground irradiance indirectly via the IBL cubemap. A dedicated irradiance LUT would be more accurate and enable CPU-side sun color computation.

**Recommendation**: Add irradiance LUT as part of the Bruneton precomputation pipeline.

### 2c. Multi-Scatter in Aerial Perspective (HIGH value)
Our AP volume compute shader does NOT sample the multi-scatter LUT. This means distant objects lack the multi-scattered light contribution, making them appear darker than they should.

**Recommendation**: Add `MultiScatterLUT.SampleLevel()` call in `aerial_perspective_volume_cs.hlsl` per voxel step. Simple fix, high visual impact.

### 2d. CPU-Side Transmittance Sampling (LOW value)
three-atmosphere samples the transmittance LUT on CPU to compute sun light color/intensity. This avoids the GPU pipeline for setting `DirectionalLight.color`.

**UzEngine impact**: We currently pass sun color as a fixed parameter. CPU readback of the transmittance LUT would make the sun color physically accurate at all times of day. However, GPU→CPU readback has latency.

**Recommendation**: Nice-to-have, not critical. Could sample a 1x1 compute result per frame.

---

## 3. What UzEngine Does Better

### 3a. Aerial Perspective Volume (better for games)
Our 32x32x16 AP volume texture is the correct approach for games:
- Amortizes the aerial perspective computation to a low-res 3D texture
- Each pixel just does a single 3D texture sample
- Supports temporal reprojection

three-atmosphere does two 3D texture lookups per pixel (into the full scattering LUT), which is more expensive for dense scenes. The UE5/game approach wins for real-time performance.

### 3b. Game-World Coordinate System
three-atmosphere uses ECEF (Earth-Centered Earth-Fixed) coordinates with WGS84 ellipsoid — great for GIS, unnecessary complexity for games. Our Z-up km system is simpler and correct for the use case.

### 3c. GPU IBL Cubemap
Our 64x64 cubemap capture with 16 steps per texel gives richer IBL than three-atmosphere's CPU-side L0+L1 SH probe (4 coefficients only). The GPU cubemap preserves specular detail.

---

## 4. Recommended Improvements (Priority Ranked)

### P0: Add Multi-Scatter to AP Volume (1 hour)
**File**: `aerial_perspective_volume_cs.hlsl`
**Change**: Add `MultiScatterLUT.SampleLevel()` per step, accumulate multi-scattered light into inscattering.
**Impact**: Distant objects get correct brightness. Currently they're too dark.

### P1: Scattering 3D LUT (Eliminate per-pixel sky march)
**Files**: NEW `scattering_lut_cs.hlsl`, MODIFY `sky_atmosphere_ps.hlsl`, MODIFY `atmosphere_renderer.cpp`
**Change**:
1. Implement Bruneton 2017 precompute pipeline: single scattering (3D), scattering density (3D), multiple scattering (3D), accumulate iteratively.
2. Replace 32-step sky PS with single 4D LUT lookup.
**Impact**: ~32x faster sky rendering. More accurate multi-scattering.
**Cost**: ~4MB VRAM for 256x128x32 RGBA16F. One-time precomputation (~50ms).

### P2: Irradiance LUT (64x16)
**Files**: NEW `irradiance_lut_cs.hlsl`, MODIFY `atmosphere_renderer.cpp`
**Change**: Compute direct + indirect irradiance LUT during precomputation.
**Impact**: Accurate ground lighting, CPU-accessible sun/sky color.

### P3: Sky View LUT (Optional — alternative to P1)
Instead of the full 3D scattering LUT, we could add a **Sky View LUT** (Hillaire 2020 / UE5 approach):
- 192x104 2D texture, updated per frame
- 32-step march per texel (but only ~20K texels vs ~2M pixels)
- Sky PS becomes a single 2D texture lookup
**Trade-off**: Less VRAM than 3D LUT, but requires per-frame update. Middle ground.

**Recommendation**: P1 (3D scattering LUT) is better than P3 (Sky View LUT) because it's precomputed once and gives exact multi-scattering. Sky View LUT is the UE5 approach but wastes GPU time every frame.

### P4: Moon Rendering (Nice-to-have)
Add moon disc rendering in sky PS using Oren-Nayar diffuse BRDF with ray-sphere intersection. Low complexity, nice visual addition for night scenes.

---

## 5. What NOT to Adopt

| Feature | Reason to skip |
|---------|---------------|
| ECEF coordinates | Game engine doesn't need WGS84 ellipsoid math |
| Ellipsoid altitude correction | Only relevant for GIS globe rendering |
| Geometric error correction | Only relevant for terrain tile rendering |
| Pre-baked LUT loading from CDN | We precompute on GPU which is fast enough |
| Incremental async precompute | Our GPU precompute is fast (~5ms one-time) |
| `astronomy-engine` sun position | We have our own sun direction from `DirectionalLightComponent` |

---

## 6. Summary

three-atmosphere is a **faithful Bruneton 2017 implementation** optimized for a web GIS context. Its main advantage over UzEngine is the **full 4D scattering LUT** which eliminates per-pixel sky marching entirely. Our main advantages are the **AP volume** (better for games) and **GPU IBL cubemap** (richer specular).

The highest-value improvement is **adding multi-scatter to our AP volume** (trivial fix) followed by **implementing the Bruneton 3D scattering LUT** (eliminates sky march, gives exact multi-scatter). These two changes would bring our atmosphere quality to match or exceed three-atmosphere while keeping our game-optimized AP volume approach.
