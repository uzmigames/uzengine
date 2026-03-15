---
name: Frostbite Volumetric Rendering Analysis
description: Complete SIGGRAPH 2015 technical analysis with energy-conservative integration formula, froxel grid architecture, and 10-week UzEngine adaptation roadmap
type: project
---

## Frostbite Volumetric Rendering (SIGGRAPH 2015)

**Document**: `/docs/analysis/rendering/frostbite-volumetric-rendering.md` (755 lines)

### Core Technical Contributions

1. **Froxel Architecture**: Frustum-aligned 3D textures (8×8×64 default) aligned with tiled-deferred light tiles
   - Enables light culling reuse
   - O(1) per-tile light list lookup during froxel scattering

2. **Energy-Conservative Integration** (CRITICAL FIX):
   - **WRONG** (naive): Treats scattering as discrete samples, loses energy at high extinction
   - **CORRECT** formula: $\int_0^D e^{-\sigma_t \cdot x} \cdot S \, dx = S \cdot \frac{1 - e^{-\sigma_t \cdot D}}{\sigma_t}$
   - Without this: At σ_t = 5000, naive approach loses 95%+ energy
   - Fixes light leaking with volumetric shadows

3. **Participating Media Material Model**:
   - 3 material properties: extinction σ_t, albedo ρ (σ_s/σ_t), phase g (Henyey-Greenstein)
   - V-Buffer format: RGBA16F×2 (scattering RGB + extinction, emissive RGB + phase)
   - Artists author in intuitive space: {albedo, extinction} OR {σ_a, σ_s}

4. **3-Step Pipeline**:
   - Material voxelization (fog + height fog + local volumes → V-Buffer)
   - Froxel light scattering (sun + indirect SH + local lights → scatter buffer)
   - Final accumulation (energy-conservative ray-march → final fog)

5. **Temporal Integration**: Halton jitter + 5% exponential moving average blend
   - Crucial for stability at sparse (8×8) froxel resolution
   - Reduces aliasing under camera motion

6. **Unified Shadow System**: Visibility = opaque_shadow × volumetric_shadow
   - Volumetric shadow maps: 32³ clip-map per light, ray-marched extinction
   - Particle voxelization: uint cascaded extinction (InterlockedAdd), then convert+add float16

### Performance (PS4 900p)

| Config | Material | Scattering | Final | Total |
|--------|----------|-----------|-------|-------|
| 8×8    | 0.45 ms  | 2.00 ms   | 0.40 ms | **2.95 ms** |
| 16×16  | 0.15 ms  | 0.50 ms   | 0.08 ms | **0.83 ms** |

Breakdown (8×8):
- Local lights: 1.10 ms
- Sun: 0.50 ms
- Temporal: 0.40 ms

### UzEngine Implementation (Phases 3.5a-f, ~10 weeks)

**Adopt**: Froxel architecture, energy-conservative formula, PM material model, temporal jitter, unified shadows

**Simplify Phase 1**: Single-lobe phase g (no multi-lobe), default particle voxel (2×2×2), sun volumetric shadows only (no local), fixed SH probe

**Files to create**:
- `runtime/engine/renderer/volumetric/` — froxel_grid, material, voxelizer, scattering, accumulator, shadows
- `runtime/engine/renderer/shaders/volumetric/` — voxelize, scattering, accumulate, shadow_gen, apply

### Critical Equations

- **Transmittance**: $T_r(D) = e^{-\sigma_t \cdot D}$
- **Single-scattering**: $L_i = T_r \cdot L_s + \int_0^D T_r \cdot \sigma_t \cdot L_{\text{scat}} \, dd$
- **Scattered light**: $L_{\text{scat}} = \rho \cdot f_v(g) \cdot V \cdot L_{\text{in}}$
- **Energy-conservative**: $\int_0^D e^{-\sigma_t \cdot x} \cdot S \, dx = S \cdot \frac{1 - e^{-\sigma_t \cdot D}}{\sigma_t}$ ← MEMORIZE THIS

### Why It Matters for UzEngine

- Production-quality volumetric effects (fog, smoke, atmosphere interaction)
- Unified with sky atmosphere system (both froxel-based fog)
- Decouples material from lighting (same fog works with sun/local lights correctly)
- Validated against physically-based path tracer (Mitsuba)
