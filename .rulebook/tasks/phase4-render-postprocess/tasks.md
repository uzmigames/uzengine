## 1. Prerequisites

- [ ] 1.1 Confirm `phase1-render-graph` is complete — `RgPass`, `RenderGraph::compile/execute` work
- [ ] 1.2 Confirm `phase2-pbr-renderer` is complete — HDR color buffer (`DXGI_FORMAT_R16G16B16A16_FLOAT`) available as persistent `RgResource`
- [ ] 1.3 Read `docs/specs/render-graph.md` — understand pass declaration, resource binding, compute pass support
- [ ] 1.4 Read `docs/specs/shader-library.md` — understand `GlobalParams` cbuffer at b15, include utilities

## 2. Post-Process Pipeline Setup

- [ ] 2.1 Create `src/engine/renderer/passes/post_process_stack.h/cpp` — `PostProcessStack` that registers all enabled passes into `RenderGraph`
- [ ] 2.2 Read post-process settings from `.uzproj` `[post_process]` section: `bloom_enabled`, `bloom_threshold`, `bloom_intensity`, `tone_mapping_mode`, `exposure`, `chromatic_aberration_intensity`, `vignette_intensity`, `color_lut_path`
- [ ] 2.3 Register enabled passes in order: AutoExposure → Bloom → ToneMapping → ChromaticAberration → Vignette → ColorLut → Present
- [ ] 2.4 Implement `.uzproj` hot-reload: `PostProcessStack::reload_settings()` on file change, re-registers passes

## 3. Bloom Pass

- [ ] 3.1 Create `src/engine/renderer/passes/bloom_pass.h/cpp` and `shaders/bloom.hlsl`
- [ ] 3.2 Implement luminance threshold pass (compute): sample HDR color, output pixels above `bloom_threshold`
- [ ] 3.3 Implement dual Kawase downsample chain: 5 levels of half-resolution downsampling
- [ ] 3.4 Implement dual Kawase upsample chain: 5 levels of bilinear upsampling with blur kernel
- [ ] 3.5 Implement additive composite pass: HDR color + bloom_intensity × bloom_buffer → output
- [ ] 3.6 Register as 3 `RgPass` entries: threshold, downsample, upsample+composite

## 4. Auto Exposure Pass

- [ ] 4.1 Create `src/engine/renderer/passes/auto_exposure_pass.h/cpp` and `shaders/auto_exposure.hlsl`
- [ ] 4.2 Implement luminance histogram compute shader: 64-bin log-luminance histogram from HDR color buffer
- [ ] 4.3 Implement histogram reduce pass: compute weighted average luminance
- [ ] 4.4 Implement exposure adaptation: lerp current exposure toward target over `adaptation_speed` seconds
- [ ] 4.5 Store exposure in persistent `RgBuffer` (one float); read by tone mapping pass
- [ ] 4.6 Config: `exposure_min`, `exposure_max`, `adaptation_speed` from `.uzproj`

## 5. Tone Mapping Pass

- [ ] 5.1 Create `src/engine/renderer/passes/tone_mapping_pass.h/cpp` and `shaders/tone_mapping.hlsl`
- [ ] 5.2 Implement ACES filmic curve (Hill/Narkowicz approximation)
- [ ] 5.3 Implement Reinhard curve (extended with white point parameter)
- [ ] 5.4 Implement AgX curve (Blender-style neutral look)
- [ ] 5.5 Apply auto-exposure value from `AutoExposurePass` persistent buffer
- [ ] 5.6 Output `DXGI_FORMAT_R8G8B8A8_UNORM` (LDR) to next pass
- [ ] 5.7 Config: `tone_mapping_mode` enum, manual `exposure` override when auto-exposure disabled

## 6. Chromatic Aberration Pass

- [ ] 6.1 Create `src/engine/renderer/passes/chromatic_aberration_pass.h/cpp` and `shaders/chromatic_aberration.hlsl`
- [ ] 6.2 Implement RGB channel barrel distortion: sample R/G/B at slightly offset UVs (radial from center)
- [ ] 6.3 Config: `intensity` (0.0–0.01 typical), applied only when > 0.0
- [ ] 6.4 Skip pass (cull) when `intensity == 0.0` via render graph dead pass culling

## 7. Vignette Pass

- [ ] 7.1 Create `src/engine/renderer/passes/vignette_pass.h/cpp` and `shaders/vignette.hlsl`
- [ ] 7.2 Implement radial vignette: `color *= smoothstep(outer, inner, 1.0 - distance(uv, 0.5))`
- [ ] 7.3 Config: `intensity` (0.0–1.0), `inner_radius`, `outer_radius`
- [ ] 7.4 Skip pass when `intensity == 0.0`

## 8. Color LUT Pass

- [ ] 8.1 Create `src/engine/renderer/passes/color_lut_pass.h/cpp` and `shaders/color_lut.hlsl`
- [ ] 8.2 Load 3D LUT texture from `color_lut_path` (32×32×32 RGBA, DDS format)
- [ ] 8.3 Implement trilinear sample of 3D LUT using LDR color as UVW coordinates
- [ ] 8.4 Lerp between identity and LUT by `lut_intensity` parameter (0.0–1.0)
- [ ] 8.5 Skip pass when `color_lut_path` is empty

## 9. Testing

- [ ] 9.1 Unit test: each shader compiles without errors under DX12 DXC
- [ ] 9.2 Unit test: `PostProcessStack` with all passes disabled → present pass still executes
- [ ] 9.3 Unit test: bloom threshold pass output contains only pixels above threshold
- [ ] 9.4 Unit test: tone mapping output is in [0, 1] range for all input HDR values
- [ ] 9.5 Unit test: auto exposure adapts toward target luminance over simulated frames
- [ ] 9.6 Integration test: full frame with all passes enabled — DX12 validation layer reports zero errors
- [ ] 9.7 Integration test: `.uzproj` hot-reload disables bloom → bloom pass removed from next frame
- [ ] 9.8 Visual test: rendered frame with ACES tone mapping matches reference screenshot within PSNR threshold

## 10. Documentation

- [ ] 10.1 Update `docs/specs/renderer.md` — "Post-Process Stack" section with pass order and config
- [ ] 10.2 Update `docs/specs/screen-space-effects.md` — link to render graph for implementation details
- [ ] 10.3 Update `docs/guides/shader-authoring.md` — "Adding Custom Post-Process Passes" section
