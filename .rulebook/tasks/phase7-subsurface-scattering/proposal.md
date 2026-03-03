# Proposal: phase3-subsurface-scattering

## Why
Skin, wax, leaves, and milk all share the property that light penetrates the surface, scatters through the material, and exits at a different point — making edges glow and thin regions appear translucent. Without subsurface scattering, every organic material renders as flat plastic, which is immediately noticeable and breaks player immersion for any character-centric game. SSS is a mandatory feature for realistic human characters, creature skins, and organic props. The Jimenez separable screen-space approach achieves high quality at low cost by performing a depth-corrected blur in view-space on the GBuffer, making it practical for real-time use.

## What Changes
- Add `SSS_ENABLED` material flag to the `.uzmat` format and PBR GBuffer encoding; stencil bit 7 marks skin pixels during the GBuffer pass
- `SSSBlurPass.hlsl`: two-pass (horizontal + vertical) separable Gaussian blur on the diffuse irradiance GBuffer channel, operating only on stencil-masked pixels
- Blur kernel is depth-corrected: kernel width in world-space is `sss_width` meters, reprojected to screen pixels using view-space depth, preventing blur bleeding across depth discontinuities
- Diffusion profile implemented as a sum-of-Gaussians (six-lobe) approximating the Penner/Jimenez skin profile; kernel weights precomputed at engine init
- `sss_width` (float, meters) and `sss_color` (RGB tint, controls the reddish-pink subsurface hue) exposed as per-material parameters in `.uzmat` and the Inspector panel
- `SSSResolve.hlsl`: composite blurred SSS irradiance back into the HDR scene color buffer, modulated by `sss_color`
- `CSubsurfaceMaterial` tag component added to ECS to flag entities that have SSS materials, used to skip the blur pass entirely when no SSS objects are in view
- Editor Inspector shows SSS controls only when the material has `SSS_ENABLED = true`

## Impact
- Affected specs: `renderer.md`, `shader-library.md`, `asset-pipeline.md`, `editor-panels.md`
- Affected code: `src/renderer/passes/SSSBlurPass.cpp`, `shaders/sss/SSSBlurPass.hlsl`, `shaders/sss/SSSResolve.hlsl`, `src/assets/MaterialAsset.cpp`, `src/ecs/components/CSubsurfaceMaterial.h`
- Breaking change: NO
- User benefit: Character skin, creature hide, plant leaves, and translucent organic materials render with the glowing-edge subsurface look seen in AAA titles. The feature is opt-in per material and zero-cost when no SSS materials are visible in the current frame.
