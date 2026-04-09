# Volumetric Clouds

## Why

UE5 default scenes include a volumetric cloud layer that fills the sky with realistic cumulus formations. Without clouds, the sky is a flat gradient that looks empty. P3 gap — lower priority than fog/atmosphere but completes the outdoor sky look. Dependencies: sky atmosphere (3.8.0) should be complete first so clouds use atmospheric scattering for lighting.

## What Changes

### New Systems
1. **CloudNoiseGenerator** — 3D Perlin-Worley noise texture (128^3) baked via compute shader. Detail noise (32^3) for erosion.
2. **VolumetricCloudPass** — ray march through cloud layer shell. Sample density from 3D noise, Beer-Lambert extinction, single-scattering with Henyey-Greenstein phase function.
3. **CloudComponent** — parameters: altitude, thickness, coverage, density, wind speed/direction, silver lining intensity

### Refactoring
- Render graph gains VolumetricCloudPass after sky atmosphere, before post-process
- Cloud output composited with scene via depth-aware blending

## Impact
- Affected specs: `docs/engine/12-rendering/07-post-processing.md`
- Affected code: `runtime/engine/renderer/passes/`, `runtime/shaders/`
- Breaking change: NO
- User benefit: Realistic sky with cloud formations matching UE5 outdoor scenes
- Enables: God rays through clouds, cloud shadows on terrain
