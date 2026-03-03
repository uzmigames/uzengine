## 1. CMake SDK Integration

- [ ] 1.1 Add `FetchContent` or vcpkg entries in `CMakeLists.txt` for DLSS 3 (`nvsdk_ngx`), FSR 3 (`amd-fidelityfx-sdk`, MIT license), and XeSS (`intel-xess`) SDKs; gate each on a CMake option (`UZ_ENABLE_DLSS`, `UZ_ENABLE_FSR`, `UZ_ENABLE_XESS`, all `ON` by default)
- [ ] 1.2 Link `nvsdk_ngx_d.lib` (debug) / `nvsdk_ngx_s.lib` (release) into the renderer module for DLSS; link FSR3 static libs; link `libxess.lib` for XeSS
- [ ] 1.3 Copy DLSS runtime DLLs (`nvngx_dlss.dll`) to the build output directory via a CMake post-build step; similarly copy XeSS `libxess.dll`
- [ ] 1.4 Add `#include` guards so builds without DLSS/XeSS SDK installations still compile using only FSR3 (always-available fallback)

## 2. Upsampler Abstraction Layer

- [ ] 2.1 Create `src/renderer/upsampler/IUpsampler.h` defining pure-virtual interface `IUpsampler` with methods: `init(device, resolution_info)`, `evaluate(color_rt, depth_rt, motion_vectors_rt, output_rt, jitter_offset, delta_time_ms, reset)`, `get_render_resolution(output_resolution, quality_mode) -> uint2`, `shutdown()`
- [ ] 2.2 Implement `DLSSUpsampler` in `src/renderer/upsampler/dlss_upsampler.cpp` wrapping NVSDK NGX `NVSDK_NGX_D3D12_Init`, `NVSDK_NGX_D3D12_CreateFeature(NVSDK_NGX_Feature_SuperSampling)`, and `NVSDK_NGX_D3D12_EvaluateFeature`
- [ ] 2.3 Implement `FSRUpsampler` in `src/renderer/upsampler/fsr_upsampler.cpp` wrapping FidelityFX FSR3 `ffxFsr3UpscalerCreate` and `ffxFsr3UpscalerDispatch`
- [ ] 2.4 Implement `XeSSUpsampler` in `src/renderer/upsampler/xess_upsampler.cpp` wrapping `xessD3D12CreateContext`, `xessD3D12BuildPipelines`, and `xessD3D12Execute`
- [ ] 2.5 Implement `UpsamplerFactory::create(device, adapter_vendor_id) -> std::unique_ptr<IUpsampler>` in `src/renderer/upsampler/upsampler_factory.cpp`: check adapter vendor ID (0x10DE = NVIDIA → DLSS; 0x8086 = Intel → XeSS; all others or DLSS init failure → FSR)

## 3. Resolution Scale and Quality Modes

- [ ] 3.1 Define `UpsamplerQualityMode` enum in `src/renderer/upsampler/IUpsampler.h`: `Performance = 0` (0.50x), `Balanced = 1` (0.67x), `Quality = 2` (0.77x), `Native = 3` (1.0x)
- [ ] 3.2 Add `upsampler_quality_mode` key to project settings INI; implement `ProjectSettings::get_upsampler_quality()` returning `UpsamplerQualityMode`
- [ ] 3.3 In `src/renderer/renderer.cpp` `Renderer::init()`, call `IUpsampler::get_render_resolution(output_resolution, quality_mode)` to determine the internal render resolution; allocate all GBuffer RTs and the motion vector RT at this reduced resolution
- [ ] 3.4 Add Editor UI toggle in the Scene Settings Inspector "Rendering" section: a dropdown listing all four quality modes; changing it triggers `Renderer::resize_render_targets()` without a scene reload

## 4. Per-Frame Jitter Injection

- [ ] 4.1 Implement `HaltonSequence` class in `src/renderer/upsampler/halton.h` with `next(base2, base3) -> float2` returning the next element in the Halton(2, 3) sequence; reset the sequence index to 0 on upsampler `reset` signal
- [ ] 4.2 In `Renderer::begin_frame()`, call `HaltonSequence::next()` to get the current frame's sub-pixel jitter offset in [−0.5, 0.5] range scaled to render resolution pixels
- [ ] 4.3 Apply the jitter by offsetting `CameraSystem`'s projection matrix `[2][0]` and `[2][1]` elements by `jitter.x / render_width` and `jitter.y / render_height` before uploading to the `CameraParams` cbuffer
- [ ] 4.4 Unjitter world-space positions in the GBuffer and motion vector passes by subtracting the jitter from the clip-space output — required for correct motion vector computation

## 5. Motion Vector Buffer

- [ ] 5.1 Allocate the screen-space motion vector buffer as a `RG16F` texture at the internal render resolution in `src/renderer/renderer.cpp`; bind as UAV during the GBuffer pass
- [ ] 5.2 In the GBuffer vertex shader, compute and output `current_ndc_pos` and `prev_ndc_pos` for each vertex using the current and previous frame's unjittered view-projection matrices
- [ ] 5.3 In the GBuffer pixel shader, write `motion_vector = current_ndc.xy - prev_ndc.xy` (in NDC [-1,1] space, not UV [0,1]) to the RG16F render target; the upsampler SDKs expect NDC-space vectors
- [ ] 5.4 Pass the motion vector buffer to `IUpsampler::evaluate()` alongside color and depth each frame

## 6. TAA Coexistence / Fallback

- [ ] 6.1 In `RenderGraph.cpp`, gate the TAA pass behind `upsampler == nullptr` — when any upsampler is active, skip TAA and use the upsampler output RT directly as the resolved color
- [ ] 6.2 Preserve the TAA pass for `UpsamplerQualityMode::Native` and for hardware that fails all three upsampler init paths; document this fallback in the render graph comments

## 7. Testing

- [ ] 7.1 Write `tests/renderer/test_upsampler.cpp`: test `UpsamplerFactory::create()` with vendor ID `0x10DE` — verify it attempts DLSS init; on stub DLL failure, falls back to FSR
- [ ] 7.2 Test `UpsamplerFactory::create()` with vendor ID `0x8086` — verify XeSS init is attempted before FSR fallback
- [ ] 7.3 Test `UpsamplerFactory::create()` with unknown vendor ID `0x1234` — verify FSR is selected directly without attempting DLSS or XeSS
- [ ] 7.4 Test `HaltonSequence::next()` for the first 16 values: verify they match the known Halton(2, 3) sequence (precomputed reference values hardcoded in the test)
- [ ] 7.5 Test render resolution computation: `Quality` mode on a 3840x2160 output → internal resolution should be 2954x1662 (0.77x, within ±1 pixel rounding)
- [ ] 7.6 Test jitter injection: verify the projection matrix offset values for the first Halton sample are within expected bounds (`|jitter_x| < 0.5 / render_width`)
- [ ] 7.7 Test that `RenderGraph` skips the TAA pass when `Renderer::upsampler_` is not null
- [ ] 7.8 Test that quality mode change via the Inspector dropdown calls `Renderer::resize_render_targets()` exactly once and allocates RTs at the new render resolution
- [ ] 7.9 Verify ≥95% line and branch coverage on `upsampler_factory.cpp`, `halton.h`, and the render resolution / jitter injection paths in `renderer.cpp`

## 8. Documentation

- [ ] 8.1 Update `docs/specs/renderer.md`: document upsampler selection logic by vendor ID, render resolution pipeline, motion vector RT format and coordinate space, and render graph TAA bypass
- [ ] 8.2 Update `docs/specs/screen-space-effects.md`: document all four quality modes with their scale factors, the Halton jitter sequence, and the unjitter requirement for motion vectors
- [ ] 8.3 Update `docs/specs/camera-system.md`: document jitter application to the projection matrix and the unjitter correction in GBuffer/motion-vector outputs
