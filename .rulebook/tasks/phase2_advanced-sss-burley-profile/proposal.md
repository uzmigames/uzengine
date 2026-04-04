# Proposal: Advanced SSS — Burley Diffusion Profile (Phase 2)

## Why
UzEngine's current SSS is wrap-diffuse + back-lighting — a cheap approximation that looks flat on skin and wax. UE5 uses separable screen-space SSS with Burley normalized diffusion profiles (Christensen & Burley 2015) for realistic skin rendering. This produces the characteristic red translucency at thin features (ears, fingers, nose) that makes skin look alive instead of plastic. The implementation requires a screen-space blur pass with per-pixel profile selection.

## What Changes

### New Systems
1. `BurleyDiffusionProfile(r, d)` — Burley 2015 normalized diffusion function in `subsurface.hlsli`
2. `SSSBlurPass` — Screen-space separable (horizontal + vertical) Gaussian-weighted SSS blur using the diffusion profile as kernel
3. `SubsurfaceProfile` asset — per-material SSS profile (scatter radius, scatter color, transmittance tint)

### Refactoring
- subsurface.hlsli: replace wrap-diffuse with profile-based evaluation for SHADING_MODEL_SUBSURFACE
- deferred_lighting_ps.hlsl: output SSS-flagged pixels to a separate target for blur
- Render graph: add SSSBlurPass after deferred lighting, before post-processing

## Impact
- Affected specs: docs/specs/rendering/shader-library.md, docs/specs/rendering/renderer.md
- Affected code: runtime/shaders/runtime/include/subsurface.hlsli, NEW runtime/shaders/runtime/postprocess/sss_blur_h_cs.hlsl, NEW runtime/shaders/runtime/postprocess/sss_blur_v_cs.hlsl, NEW runtime/engine/renderer/passes/postprocess/sss_blur_pass.cpp/.h
- Breaking change: NO — wrap-diffuse remains as fallback for non-profile materials
- User benefit: Realistic skin, wax, jade, marble, plant leaf rendering
- Enables: Character rendering quality on par with UE5
