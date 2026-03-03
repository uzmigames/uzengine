# Proposal: phase2-ibl-pipeline

## Why

The Phase 2 PBR lighting shader calls `SampleEnvironmentIrradiance()` and `SamplePrefilteredEnv()` — functions that reference textures with no actual data behind them. Metallic surfaces appear pitch-black in shadow (no ambient). Glossy surfaces have no environment reflections. PBR is visually broken without IBL. This is the most visible rendering gap in the entire engine.

## What Changes

**Offline bakers (compute shaders run at asset import time)**:
- Equirectangular HDR → Cubemap (8×8 threadgroup dispatch)
- Cosine-weighted irradiance convolution → 64×64 irradiance cubemap
- GGX importance sampling → prefiltered env map (7 roughness mip levels)
- Split-sum BRDF integration → 512×512 RG16F LUT

**Asset format**: `.uzenv` compiled to DDS BC6H in `.uzpak`

**Runtime shader integration**: `ComputeIBL()` in `pbr_lighting.hlsli` combining irradiance + prefiltered + BRDF LUT via split-sum approximation

**Scene config**: `[environment] ibl = "environments/studio.uzenv"` in `.uzscene`

**IPC handlers**: `scene.set_ibl`, `scene.get_ibl_info`

## Impact

- Affected specs: `docs/specs/rendering/ibl-pipeline.md`, `docs/specs/rendering/renderer.md`
- Affected code: PBR lighting shader, asset importer, scene loader, render pass
- Breaking change: NO (adds ambient lighting — existing scenes get correct ambient instead of black)
- User benefit: Physically correct PBR ambient lighting; metallic/glossy materials look correct; HDR environment support
