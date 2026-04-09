# PCSS (Percentage Closer Soft Shadows) — UE5 Parity

## Priority: P2 — Quality tier upgrade for realistic variable-penumbra shadows

## Summary

UzEngine uses fixed-kernel PCF (3x3 or 5x5) which produces uniform shadow softness regardless of distance from the occluder. Real shadows have variable penumbra — sharp near the contact point, soft far away. PCSS (Percentage Closer Soft Shadows) achieves this by estimating blocker distance and adapting the filter kernel size. UE5 implements PCSS as an optional quality tier (FilterMethod=1) using a three-stage pipeline: blocker search (16 Sobol samples), penumbra estimation, and adaptive PCF (32 Sobol samples).

## UE5 Reference

### CVars
**Source**: `ShadowRendering.cpp:180-192`

| CVar | Default | Description |
|------|---------|-------------|
| `r.Shadow.FilterMethod` | 0 (PCF) | 0=Uniform PCF, 1=PCSS |
| `r.Shadow.MaxSoftKernelSize` | 40 | Max kernel radius in pixels |
| `r.Shadow.CSMSplitPenumbraScale` | 0.5 | Per-cascade penumbra scaling |

### Algorithm (3 stages)
**Source**: `ShadowPercentageCloserFiltering.ush`

1. **Blocker Search** (16 Sobol samples):
   - Sample shadow map in a disk around the pixel
   - Find average depth of occluders (blockers)
   - SearchRadius = MaxSoftKernelSize / ShadowMapResolution

2. **Penumbra Estimation**:
   - `PenumbraSize = TanLightSourceAngle * (pixelDepth - avgBlockerDepth)`
   - Larger light source → wider penumbra
   - Closer to occluder → sharper shadow

3. **Adaptive PCF** (32 Sobol samples):
   - Filter radius = min(PenumbraSize, MaxSoftKernelSize)
   - Elliptical kernel for anisotropic penumbra
   - Sobol low-discrepancy sampling for noise reduction

### Light Source Size
- Directional lights: `TanLightSourceAngle` from `LightSourceAngle` property (~0.53° for sun)
- Spot lights: derived from cone angle + source radius
- Point lights: NOT supported for PCSS in UE5

### Performance
- PCSS is ~1.33x cost of standard PCF (48 total texture samples vs ~25)
- At 1080p: ~0.3ms additional GPU cost
- Quad-sharing optimization saves 10-20%

### Known UE5 Limitations
- `CSMSplitPenumbraScale` CVar exists but value is **never passed to shader** (incomplete feature)
- Point light PCSS not implemented
- Compile-time feature toggles only (no runtime permutation)

## Impact

- **Files modified**: shadow_common.hlsli (main change), constant_buffers.h, scene_renderer.cpp, cascade.h
- **New files**: pcss.hlsli (PCSS algorithm include)
- **Performance**: +0.3ms GPU at 1080p when enabled (optional quality tier)
- **Visual**: Dramatically more realistic shadows — variable penumbra based on occluder distance
- **Dependency**: Benefits from phase0-0.1.0-csm-ue5-parity (per-cascade bias)

## Acceptance Criteria

1. PCSS mode selectable via `ShadowConfig::filterMethod` (0=PCF, 1=PCSS)
2. Blocker search with 16 Sobol low-discrepancy samples
3. Penumbra estimation using `TanLightSourceAngle * (pixelDepth - blockerDepth)`
4. Adaptive PCF with 32 Sobol samples, kernel capped at `MaxSoftKernelSize`
5. Light source angle configurable per directional light
6. Shadows sharp near contact, soft far from occluder (variable penumbra)
7. Fallback to standard PCF when PCSS disabled (zero cost)
8. Demo: object casting shadow on surface — PCSS vs PCF comparison
9. No regression when PCSS disabled
