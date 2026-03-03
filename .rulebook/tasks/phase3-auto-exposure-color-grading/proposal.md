# Proposal: phase2-auto-exposure-color-grading

## Why
The engine renders in HDR but applies a fixed exposure value, causing outdoor scenes to blow out and dark interiors to appear near-black. Any game with exterior-to-interior transitions requires automatic exposure adaptation (eye adaptation) to remain visually correct. Additionally, without LUT-based color grading, game artists have no way to apply per-scene color palettes or cinematic looks — this is a baseline requirement for any shipped title.

## What Changes
- Add a 256-bin luminance histogram compute shader (`histogram_build.hlsl`) that samples the HDR scene color buffer each frame
- Add a histogram reduce pass (`histogram_reduce.hlsl`) that computes weighted average log-luminance and writes to a 1x1 buffer
- Implement eye adaptation via a lerp between previous and target exposure values using `adapt_speed_up` and `adapt_speed_down` CVars, stored in a persistent GPU buffer across frames
- Add a 64x64x64 3D texture LUT loader in the asset pipeline (`src/assets/lut_loader.cpp`); `.cube` file format support
- Write a tonemapping + LUT application pass (`tonemapping_pass.hlsl`) that supports operator selection: ACES, Reinhard, and Uncharted2, driven by a CVar `r.tonemap_op`
- Expose `.uzscene` fields `auto_exposure_min_ev`, `auto_exposure_max_ev`, `color_grading_lut` per-scene

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/screen-space-effects.md`, `docs/specs/asset-pipeline.md`
- Affected code: `src/renderer/passes/tonemapping_pass.cpp`, `src/shaders/histogram_build.hlsl`, `src/shaders/tonemapping_pass.hlsl`, `src/assets/lut_loader.cpp`, `src/scene/scene_loader.cpp`
- Breaking change: NO
- User benefit: Games with indoor/outdoor transitions look correct automatically; artists can apply a color grade LUT to any scene without code changes
