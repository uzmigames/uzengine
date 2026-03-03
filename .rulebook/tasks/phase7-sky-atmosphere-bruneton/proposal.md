# Proposal: phase3-sky-atmosphere-bruneton

## Why
The current sky is a solid color or simple gradient, which immediately reads as unfinished to players and breaks immersion. Physically accurate atmospheric scattering explains why the sky is blue at noon, orange-red at sunset, and dark at night — and it does so using real physics of Rayleigh (molecules) and Mie (aerosols) scattering. The Bruneton 2017 precomputed model is the industry-standard approach used in Unreal Engine 5 and Unity HDRP because it achieves ground-truth accuracy with LUT lookups that cost less than a simple skybox sample at runtime. Implementing this enables time-of-day systems, aerial perspective for distant terrain, and a fully dynamic sky dome.

## What Changes
- Precompute transmittance LUT (256x64 R11G11B10F) encoding atmosphere optical depth from any altitude and sun angle
- Precompute multiple-scattering LUT (32x32 RGBA16F) for accurate multi-bounce illumination
- Precompute sky-view LUT (192x108 RGBA16F) per frame, updated when sun direction changes
- `SkyAtmospherePass.hlsl`: full-screen sky render sampling the three LUTs to produce HDR sky color
- Sun disk rendering with limb darkening and physically correct angular size
- Aerial perspective LUT (32x32x32 RGBA16F): accumulate transmittance+inscatter over view ray for distant geometry color tinting, applied in PBR composite pass
- Day/night integration: `CSkyAtmosphere` ECS component with `sun_direction` (driven by time-of-day float), `rayleigh_scale_height`, `mie_scale_height`, `ground_albedo`, `mie_anisotropy` parameters
- `SkyAtmospherePrecompute.hlsl`: offline (editor startup) compute shader fills all LUTs; dirty-flagged on parameter change
- Replace existing gradient sky background with the new sky pass in the render graph; old solid-color fallback remains for headless/test mode

## Impact
- Affected specs: `renderer.md`, `shader-library.md`, `screen-space-effects.md`, `editor-panels.md`
- Affected code: `src/renderer/passes/SkyAtmospherePass.cpp`, `shaders/sky/SkyAtmospherePrecompute.hlsl`, `shaders/sky/SkyAtmospherePass.hlsl`, `shaders/sky/AerialPerspective.hlsl`, `src/ecs/components/CSkyAtmosphere.h`
- Breaking change: NO
- User benefit: Scenes gain a physically accurate sky that automatically transitions from blue-day through orange-sunset to dark-night. Distant mountains and buildings receive aerial perspective haze for correct depth perception. Artists only need to set sun direction and optionally tweak scattering parameters.
