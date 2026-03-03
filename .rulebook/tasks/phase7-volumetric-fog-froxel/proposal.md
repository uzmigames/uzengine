# Proposal: phase3-volumetric-fog-froxel

## Why
Without atmospheric depth cues, all scenes look like objects floating in a void — there is no sense of air, distance, or environment. Volumetric fog with god-ray shafts, haze, and light scattering makes scenes feel grounded and atmospheric. The froxel (frustum-voxel) approach subdivides the camera frustum into a 3D grid and integrates scattering per-cell on the GPU, which is both efficient and physically grounded. This technique has been standard in AAA titles since 2018 and is expected by players and artists alike.

## What Changes
- Allocate a 160x90x64 froxel volume as a 3D RW texture (RGBA16F) for scatter/transmittance accumulation
- Compute shader `FroxelScatter.hlsl`: for each froxel cell, evaluate fog density from height-based gradient + animated 3D noise texture
- Compute shader `FroxelIntegrate.hlsl`: ray-march along Z slices, accumulating in-scattered light and transmittance per froxel
- Light shaft contribution from the primary directional light (sun): shadow map lookup per froxel to determine whether cell is lit
- Per-froxel color accumulation stores (in-scatter RGB, transmittance A) for the resolve pass
- Final composite pass in `VolumetricFogComposite.hlsl`: samples froxel volume by reprojected clip-space UVW and blends over the HDR scene color
- `CVolumetricFog` ECS component with fields: `density`, `anisotropy`, `height_falloff`, `noise_scale`, `noise_speed`, `scatter_color`
- Add volumetric fog toggle and parameters to the scene `.uzscene` INI format
- Editor Inspector panel exposes all `CVolumetricFog` parameters with sliders

## Impact
- Affected specs: `renderer.md`, `screen-space-effects.md`, `shader-library.md`, `point-spot-light-shadows.md`
- Affected code: `src/renderer/passes/VolumetricFogPass.cpp`, `shaders/froxel/FroxelScatter.hlsl`, `shaders/froxel/FroxelIntegrate.hlsl`, `shaders/froxel/VolumetricFogComposite.hlsl`, `src/ecs/components/CVolumetricFog.h`
- Breaking change: NO
- User benefit: Scenes gain atmospheric depth, god-ray shafts, and haze that make environments feel alive and cinematic rather than sterile. Artists can control fog density, color, and noise without any code changes.
