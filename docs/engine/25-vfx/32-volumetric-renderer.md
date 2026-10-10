# VFX volumetric particle renderer

Density-field renderer for large, low-density effects (smoke clouds, dust
banks, fog sheets). Particles splat their density into a 3D grid; a
fullscreen raymarch samples the grid with Beer–Lambert absorption and a
Henyey–Greenstein phase function. Lower overdraw than billboards when the
effect occupies a substantial screen region.

Reference: UE5 `FNiagaraVolumetricRenderer`.

## Pipeline

```
  ┌──────────────┐        splat CS       ┌───────────────┐
  │ Particle pos ├──────────────────────▶│ RWTexture3D   │
  │  + alive flag│  3×3×3 tent filter     │  <float>      │
  └──────────────┘                        └───────┬───────┘
                                                  │
                                                  ▼
                                          fullscreen PS (raymarch)
                                                  │
                                                  ▼
                                          HDR scene colour (blended)
```

## CPU helpers

`engine/vfx/modules/vfx_volumetric.h` ships the authoritative CPU-side
math. The HLSL shaders mirror it step-for-step so tests that exercise the
CPU helpers validate the GPU pipeline without requiring a device.

| Helper                               | Purpose                                      |
|--------------------------------------|----------------------------------------------|
| `VfxVolumetricWorldToVoxel`          | World → voxel index with bounds check.       |
| `VfxVolumetricVoxelToWorld`          | Voxel center → world (for debug / atlas).    |
| `VfxHenyeyGreenstein(cosθ, g)`       | Phase function; `g` clamped to (-1, 1).      |
| `VfxBeerLambertTransmittance`        | `exp(-σ·ρ·ds)`; negatives clamped to 0.     |
| `VfxVolumetricStepContribution`      | One raymarch step's in-scatter RGB.          |

## Config

```cpp
VfxVolumetricConfig cfg {
    .gridResolution     = 128,       // voxels per axis
    .halfExtent         = 50.0f,     // metres
    .gridCenter         = Vec3(0,0,0),
    .densityPerParticle = 0.1f,
    .raymarchSteps      = 48,
    .extinctionCoeff    = 1.5f,      // σ_t
    .hgAnisotropy       = 0.8f,      // forward-scattering
    .scatteringColor    = Vec3(1.0f, 0.95f, 0.85f),
};
```

- **Grid resolution** — total voxels = res³; 64 is an editor preview, 128–256
  for shipped effects. 128³ at R32F = 8 MB per emitter.
- **Half extent** — world metres; the grid covers `2·halfExtent` per axis.
- **HG anisotropy** — 0.8 is the UE5 default for forward-scattering clouds.
  Negative values back-scatter; 0 is isotropic.

## Splat pass (`vfx_volumetric_splat_cs.hlsl`)

One thread per alive particle. Each thread:

1. Maps `particle.pos → voxelIndex` via `WorldToVoxel` (identical math to
   the CPU helper).
2. Adds `densityPerParticle · weight` into the 3×3×3 tent neighbourhood.
   Tent weights are 1 at center, 0.5 at 6-faces, 0.25 at 12-edges, 0.125
   at 8-corners; sum is 8 so the per-particle density spreads naturally
   across the local region.
3. Inactive particles (`pos.w <= 0`) and out-of-bounds positions are
   skipped without atomic traffic.

## Raymarch pass (`vfx_volumetric_raymarch_ps.hlsl`)

Fullscreen pixel shader:

1. Reconstructs the world-space ray from screen UV via `invViewProj`.
2. Intersects with the grid AABB; misses return fully-transparent.
3. Marches `raymarchSteps` fixed-length samples through the volume:
   ```
   density  = volume.Sample(trilinear, uvw)
   stepT    = exp(-σ_t · density · stepSize)
   phase    = HG(cosθ_viewSun, g)
   absorp   = 1 - stepT
   inScatter += scatteringColor · phase · density · absorp · transmittance
   transmittance *= stepT
   ```
4. Early-outs when `transmittance < 0.005` (remaining march cannot
   contribute visibly).
5. Outputs `(inScatter.rgb, 1 - transmittance)` — premultiplied alpha
   composited over HDR scene colour.

## Integration with fog

The pass can be inserted either before or after the existing fog pass.
When placed **before** fog, atmospheric fog absorbs the volumetric result;
when placed **after**, the volumetric reads the fogged scene. Both are
supported — config on the render graph.

## Tests

`runtime/tests/engine/vfx/test_vfx_volumetric.cpp` — 18 cases:

- Grid mapping: center, min corner, max corner, out-of-bounds clamp,
  zero-resolution guard, voxel→world round-trip.
- Henyey–Greenstein: isotropic at g=0, integrates-to-one over sphere at
  four g values, forward peak at g>0, back peak at g<0, pole clamp.
- Beer–Lambert: zero density / zero step give full transmission, expected
  exponential decay at 3 parameter combos, negative-input clamp.
- Step contribution: zero density / zero transmittance saturates, colour
  channels match scattering colour ratios.
