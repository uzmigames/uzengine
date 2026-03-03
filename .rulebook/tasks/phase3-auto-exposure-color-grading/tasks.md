## 1. Luminance Histogram Compute Shader

- [ ] 1.1 Write `src/shaders/histogram_build.hlsl`: a compute shader with 256 bins dispatched as `ceil(width/8) x ceil(height/8)` thread groups; each thread samples the HDR scene color buffer, computes log-luminance via `log2(dot(color.rgb, float3(0.2126, 0.7152, 0.0722)))`, converts to a bin index in `[0, 255]`, and performs an `InterlockedAdd` into a `RWStructuredBuffer<uint> g_Histogram[256]`
- [ ] 1.2 Write `src/shaders/histogram_reduce.hlsl`: a single-thread compute shader that reads `g_Histogram[256]`, computes weighted average log-luminance (ignoring the lowest 2% and highest 2% of pixels to exclude sky and shadow extremes), and writes target exposure EV to a `RWBuffer<float> g_TargetExposure`
- [ ] 1.3 Create `src/engine/renderer/passes/auto_exposure_pass.cpp` and `auto_exposure_pass.h` implementing `AutoExposurePass::Execute(cmd_list, scene_color_srv)` that dispatches `histogram_build.hlsl` then `histogram_reduce.hlsl`, managing their `UAV` and `SRV` descriptor handles via `BindlessManager`

## 2. Eye Adaptation

- [ ] 2.1 Allocate a persistent single-float GPU buffer `g_CurrentExposure` in `AutoExposurePass` at initialization; initialize it to a neutral EV value of `0.0` (exposure multiplier 1.0); persist it across frames by not clearing it
- [ ] 2.2 Add an eye adaptation pass: a tiny compute shader (`eye_adaptation.hlsl`) that reads `g_CurrentExposure` and `g_TargetExposure`, lerps between them using `adapt_speed_up` (default `2.0 EV/sec`) and `adapt_speed_down` (default `1.0 EV/sec`) scaled by `delta_time`, and writes the result back to `g_CurrentExposure`
- [ ] 2.3 Register CVars `r.auto_exposure_adapt_up` (float, default `2.0`) and `r.auto_exposure_adapt_down` (float, default `1.0`) for runtime tuning of adaptation speeds
- [ ] 2.4 Register CVars `r.auto_exposure_min_ev` (float, default `-8.0`) and `r.auto_exposure_max_ev` (float, default `8.0`) that clamp `g_CurrentExposure`; these are also writable per-scene via `.uzscene` fields `auto_exposure_min_ev` / `auto_exposure_max_ev`

## 3. Color Grading LUT Asset Pipeline

- [ ] 3.1 Create `src/engine/assets/lut_loader.cpp` and `lut_loader.h` implementing `LutLoader::Load(path)` that parses `.cube` LUT files (Adobe/DaVinci Resolve format): reads the `LUT_3D_SIZE` header (64 expected), then reads `64^3` RGB triplets into a `std::vector<uint16_t>` (R16G16B16 half-float)
- [ ] 3.2 Upload the parsed LUT data to a `DXGI_FORMAT_R16G16B16A16_FLOAT` 64x64x64 3D texture via `CopyTextureRegion` during `LutLoader::UploadToGpu(cmd_list)`; register the texture in `BindlessManager` and store the heap index in the scene's `color_grading_lut_index`
- [ ] 3.3 Add `.uzscene` field `color_grading_lut` (string, project-relative path to `.cube` file) parsed in `src/engine/scene/scene_loader.cpp`; when absent, bind a neutral identity LUT (ramp from (0,0,0) to (1,1,1)) that passes through colors unchanged

## 4. Tonemapping Pass

- [ ] 4.1 Write `src/shaders/tonemapping_pass.hlsl`: a fullscreen triangle pixel shader that reads `g_CurrentExposure`, applies exposure as `color *= pow(2.0, exposure_ev)`, samples the 3D LUT via `SampleLevel`, and applies the selected tonemapping operator before writing to the SDR back buffer
- [ ] 4.2 Implement three tonemapping operators in `tonemapping_pass.hlsl` selected by `r.tonemap_op` CVar (int, default `0`): `0`=ACES filmic curve, `1`=Reinhard, `2`=Uncharted2 (Hable filmic); implement each as a separate HLSL function and select via a compile-time `#define` or a dynamic branch on a root constant
- [ ] 4.3 Create `src/engine/renderer/passes/tonemapping_pass.cpp` implementing `TonemappingPass::Execute(cmd_list, hdr_srv, lut_index)` that binds `g_CurrentExposure` as a root CBV, dispatches the fullscreen triangle, and handles resource barriers for the HDR input SRV and SDR RTV output

## 5. Scene Configuration

- [ ] 5.1 Add `auto_exposure_min_ev`, `auto_exposure_max_ev` (float), and `color_grading_lut` (string) fields to the `.uzscene` JSON schema in `docs/specs/renderer.md`
- [ ] 5.2 Parse these fields in `src/engine/scene/scene_loader.cpp` and apply them to the `AutoExposurePass` and `TonemappingPass` during scene load; on scene unload, reset to default CVar values

## 6. Testing

- [ ] 6.1 Create `tests/renderer/test_auto_exposure_pass.cpp` using a mock DX12 command list recorder to verify `AutoExposurePass::Execute` dispatches the histogram build then histogram reduce shaders in order, with the correct thread group counts for a 1920x1080 input
- [ ] 6.2 Test that the histogram bin index formula maps pure-white HDR color `(1,1,1)` to bin 255 and pure-black `(0,0,0)` to bin 0
- [ ] 6.3 Test eye adaptation lerp math: with `adapt_speed_up=2.0`, `delta_time=0.5s`, `current_ev=0.0`, `target_ev=2.0`, verify the output is `1.0 EV` (half the gap in one step)
- [ ] 6.4 Test `r.auto_exposure_min_ev` clamp: force `g_TargetExposure` to `-10.0` (below the `-8.0` default minimum) and verify `g_CurrentExposure` is clamped to `-8.0` after adaptation
- [ ] 6.5 Create `tests/assets/test_lut_loader.cpp` testing `LutLoader::Load()` with a minimal 2x2x2 `.cube` file (hand-crafted test fixture) and verify the parsed RGB values match expected half-float encoding
- [ ] 6.6 Test `LutLoader::Load()` with a malformed `.cube` file missing the `LUT_3D_SIZE` header returns an error and does not allocate any memory
- [ ] 6.7 Test the identity LUT (64x64x64 ramp) passes through color `(0.5, 0.25, 0.75)` unchanged within `1e-3` tolerance after sampling
- [ ] 6.8 Test tonemapping ACES curve: input `(1.0, 1.0, 1.0)` maps to a value less than 1.0 (tone-mapped, not clipped)
- [ ] 6.9 Test `.uzscene` field parsing: load a scene JSON with `"auto_exposure_min_ev": -4.0` and verify the `AutoExposurePass` clamp is set to `-4.0 EV`
- [ ] 6.10 Test that absent `color_grading_lut` in `.uzscene` falls back to the identity LUT and no error is logged
- [ ] 6.11 Run coverage -- all new files in `src/engine/renderer/passes/`, `src/engine/assets/`, and `src/engine/scene/` must reach >= 95% line and branch coverage before marking complete
