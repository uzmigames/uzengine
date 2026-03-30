# Proposal: phase1_falcor-volumetric-media

## Why
UzEngine has height fog and volumetric clouds but no general-purpose heterogeneous volume rendering. Falcor has `GridVolume` with OpenVDB-style density grids, Woodcock (delta) tracking for unbiased transmittance estimation, and Henyey-Greenstein phase functions. Fire, smoke, explosions, underwater caustics, god-ray volumes, and atmospheric dust all require heterogeneous volumes — effects where density varies in 3D space and cannot be modeled by analytic fog. Without this, UzEngine cannot render any VFX that involves volumetric effects beyond uniform height fog. @ref Novak et al. 2018 "Monte Carlo Methods for Volumetric Light Transport Simulation".

## What Changes
- `VolumeAsset` class: loads 3D density textures from raw binary (uint8 or float16, up to 256³) or from flat slice arrays. Uploads as `Texture3D<float>` to GPU. Stores `sigma_a` (absorption), `sigma_s` (scattering), `sigma_t = sigma_a + sigma_s`, and `g` (anisotropy) as volume-wide constants.
- `CVolumeComponent` ECS component: references `VolumeAsset`, provides world-space OBB transform (position + half-extents + orientation), `sigmaA`, `sigmaS`, `anisotropy` (Henyey-Greenstein g), and `maxDensity` for Woodcock tracking majorant
- `volume_raymarch_cs.hlsl`: primary volume rendering shader — ray/OBB intersection, Woodcock delta tracking for transmittance, single-scattering integral with Henyey-Greenstein phase function, shadow ray for direct light contribution, Beer-Lambert extinction. @ref Novak 2018 Section 3.3
- `VolumePass` render graph pass: executes after deferred lighting, before post-process. Reads scene depth to stop rays at opaque surfaces. Composites volume contribution front-to-back over scene color.
- Temporal reprojection: project previous frame's volume radiance estimate via motion vectors to reduce noise per-frame (exponential moving average, alpha=0.1)
- Affected files: new `volume_asset.h/.cpp`, new `volume_component.h`, new `volume_raymarch_cs.hlsl`, new `volume_pass.h/.cpp`, `engine_loop.cpp`

## Impact
- Affected specs: new `docs/specs/rendering/volumetric-media.md`
- Affected code: new volume asset/component/shader/pass files, `engine_loop.cpp`, scene gather (CVolumeComponent collection)
- Breaking change: NO — purely additive; height fog and clouds are unaffected
- User benefit: Fire, smoke, explosions, underwater caustics, god-ray volumes, and dust particles all become renderable; physically based transmittance estimation (Woodcock tracking) produces unbiased results without view-dependent artifacts
