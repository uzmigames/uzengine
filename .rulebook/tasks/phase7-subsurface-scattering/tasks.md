## 1. Material Format and GBuffer Encoding

- [ ] 1.1 Add `SSS_ENABLED` boolean flag to the `.uzmat` material format definition in `src/assets/MaterialAsset.h`; default value `false`; serialized as `sss_enabled = 1` in the `.uzmat` INI section
- [ ] 1.2 Add `sss_width` (float, meters, default 0.01) and `sss_color` (float3, default (1.0, 0.85, 0.72) for skin) to the `.uzmat` format and `MaterialAsset` struct
- [ ] 1.3 In the GBuffer pass pixel shader, set stencil bit 7 to `1` for every fragment from a material with `SSS_ENABLED = true`; all other materials write stencil bit 7 as `0`
- [ ] 1.4 Implement `MaterialAsset::is_sss()` accessor in `src/assets/MaterialAsset.cpp` returning `sss_enabled && sss_width > 0.0f`
- [ ] 1.5 Update the asset import pipeline in `src/assets/MaterialAsset.cpp` to read `sss_enabled`, `sss_width`, and `sss_color` from `.uzmat` files; validate `sss_width` is positive, log a warning and clamp to 0.001 if zero or negative

## 2. CSubsurfaceMaterial Tag Component

- [ ] 2.1 Define `CSubsurfaceMaterial` tag component (zero-size ECS tag) in `src/ecs/components/CSubsurfaceMaterial.h`
- [ ] 2.2 Auto-add `CSubsurfaceMaterial` to any entity whose `CMeshRenderer` references a material with `SSS_ENABLED = true` during scene load and material assignment
- [ ] 2.3 Remove `CSubsurfaceMaterial` from an entity when its material changes to one with `SSS_ENABLED = false`
- [ ] 2.4 In `SSSBlurPass::setup()`, query the ECS for entities with `CSubsurfaceMaterial`; if the query returns zero results, skip both the horizontal and vertical blur dispatches for that frame entirely

## 3. SSS Blur Pass — Horizontal + Vertical

- [ ] 3.1 Create `src/renderer/passes/SSSBlurPass.cpp` implementing a two-pass separable Gaussian blur: first dispatch applies the horizontal kernel, second dispatch applies the vertical kernel; both operate on the GBuffer diffuse irradiance channel
- [ ] 3.2 Create `shaders/sss/SSSBlurPass.hlsl` with two entry points `CS_BlurH` and `CS_BlurV`; each pass reads the stencil buffer to skip pixels where stencil bit 7 is `0` (non-SSS pixels), outputting them unchanged
- [ ] 3.3 Implement the six-lobe sum-of-Gaussians kernel in `SSSBlurPass.hlsl`: precompute six `(weight, variance)` pairs at shader compile time using the Penner skin diffusion profile constants; accumulate `sum += weight * sample_color * depth_correction_weight`
- [ ] 3.4 Implement depth correction for kernel width: compute `kernel_radius_px = (sss_width_world / view_depth) * (projection_scale)` in the pixel shader to remap world-space blur width to screen-space pixel radius dynamically per-pixel
- [ ] 3.5 Implement depth discontinuity suppression: for each kernel sample, compute `depth_weight = exp(-abs(sample_linear_depth - center_linear_depth) / depth_threshold)` where `depth_threshold = 0.05 * sss_width_world`; multiply sample contribution by `depth_weight`
- [ ] 3.6 Bind the stencil SRV, GBuffer diffuse RT, and depth buffer as shader inputs; write blurred result to a separate `SSSBlurredDiffuse` RGBA16F RT allocated in `SSSBlurPass.cpp`

## 4. SSS Resolve Pass

- [ ] 4.1 Create `shaders/sss/SSSResolve.hlsl`: full-screen pass that reads the `SSSBlurredDiffuse` RT and composites it back into the HDR scene color buffer — `scene_color += blurred_diffuse * sss_color` only on stencil-bit-7 pixels
- [ ] 4.2 In `SSSResolve.hlsl`, bind `sss_color` per-material from a constant buffer populated with the material's `sss_color` field; for scenes with multiple SSS materials, use a material ID written to a GBuffer channel to look up per-material parameters from a structured buffer
- [ ] 4.3 Insert `SSSBlurPass` (horizontal) → `SSSBlurPass` (vertical) → `SSSResolve` into the render graph after the lighting accumulation pass and before the tonemapping pass in `RenderGraph.cpp`

## 5. Editor Inspector Integration

- [ ] 5.1 In the material Inspector widget (`src/editor/panels/InspectorPanel.cpp`), show the SSS controls section only when `material->is_sss()` returns true or when `SSS_ENABLED` checkbox is checked; hide when unchecked
- [ ] 5.2 SSS Inspector controls: `SSS_ENABLED` checkbox, `sss_width` float slider (0.001–0.1 m with 3-decimal precision label showing meters), `sss_color` RGB color picker with default skin tone swatch

## 6. Testing

- [ ] 6.1 Write `tests/renderer/test_sss_blur.cpp`: test stencil masking — pixel with stencil bit 7 = 0 is passed through unchanged (output equals input), pixel with stencil bit 7 = 1 is blurred (output differs from input with known kernel)
- [ ] 6.2 Test depth-corrected kernel width: for `sss_width = 0.01 m` and view depth 2 m, compute expected screen-space kernel radius; verify the blur kernel straddles that many pixels using the projection formula
- [ ] 6.3 Test depth discontinuity suppression: surface at depth 2.0 m with a background pixel at depth 10.0 m included in the kernel window; verify the background pixel contributes weight < 0.01 to the foreground pixel's blur
- [ ] 6.4 Test `CSubsurfaceMaterial` auto-add: assign a material with `SSS_ENABLED = true` to an entity; verify `CSubsurfaceMaterial` is present; swap material to `SSS_ENABLED = false`; verify `CSubsurfaceMaterial` is removed
- [ ] 6.5 Test pass skip: create a scene with zero `CSubsurfaceMaterial` entities; verify `SSSBlurPass::dispatch()` is called 0 times (use a dispatch-count mock)
- [ ] 6.6 Test `MaterialAsset` serialization: write `.uzmat` with `sss_enabled = 1`, `sss_width = 0.015`, `sss_color = (1.0, 0.8, 0.7)`; load it; verify all three fields
- [ ] 6.7 Test `sss_width` validation: load `.uzmat` with `sss_width = 0.0`; verify it is clamped to 0.001 and a warning is logged
- [ ] 6.8 Test `SSSResolve` compositing: manually set `SSSBlurredDiffuse` to a known value; run `SSSResolve`; verify output HDR buffer equals `original + blurred * sss_color` within float epsilon
- [ ] 6.9 Verify ≥95% line and branch coverage on `SSSBlurPass.cpp`, `MaterialAsset.cpp` (SSS paths), and the `CSubsurfaceMaterial` auto-add/remove logic

## 7. Documentation

- [ ] 7.1 Update `docs/specs/renderer.md`: document the SSS stencil bit 7 convention, the two-pass blur architecture, the six-lobe Penner kernel, and the render graph insertion point
- [ ] 7.2 Update `docs/specs/shader-library.md`: document `SSSBlurPass.hlsl` inputs/outputs, stencil SRV binding, and the `SSSBlurredDiffuse` RT format
- [ ] 7.3 Update `docs/specs/asset-pipeline.md`: document `sss_enabled`, `sss_width`, `sss_color` `.uzmat` keys and validation rules
- [ ] 7.4 Update `docs/specs/editor-panels.md`: document the conditional SSS Inspector section and the default skin-tone swatch behavior
