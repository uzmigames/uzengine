## 1. Screen Percentage Infrastructure

- [x] 1.1 Add `m_renderWidth`/`m_renderHeight` (internal render res) alongside `m_width`/`m_height` (display res) in `scene_renderer.h`. Compute from `m_screenPercentage` (float, 10-200, default 100). Added `SetScreenPercentage()`, `GetScreenPercentage()`, `GetRenderWidth()`, `GetRenderHeight()` public API.
- [ ] 1.2 Add `r.ScreenPercentage` CVar (float, default 100.0) readable from config. @source UE5 LegacyScreenPercentageDriver.cpp:10-61
- [x] 1.3 Resize GBuffer (albedo, normal, depth, velocity, positionWS) to `renderWidth × renderHeight` instead of `displayWidth × displayHeight` in `gbuffer.cpp`. Done in Init() and Resize().
- [x] 1.4 Resize HDR scene color (`m_hdrSceneColor`) to `renderWidth × renderHeight`. Done in Init() and Resize().
- [x] 1.5 Keep backbuffer, tonemap output, and overlay targets at `displayWidth × displayHeight`. TAA history at display res. FXAA/CAS at display res.
- [x] 1.6 Update `SceneRenderer::Resize()` to recompute `m_renderWidth/Height` from screen percentage and recreate render targets at new render resolution. SetScreenPercentage() triggers Resize().
- [x] 1.7 Pass both `renderWidth/Height` and `displayWidth/Height` to all passes that need viewport dimensions. FrameContext gains renderWidth/renderHeight. Geometry/lighting passes use renderWidth. Tonemap/CAS/FXAA use vpWidth (display).

## 2. Camera Jitter (Halton Sequence)

- [ ] 2.1 Create `halton.h` with `HaltonSequence(index, base)` function. @source UE5 Halton.ush / SceneView.cpp TemporalJitterPixels
- [ ] 2.2 Add `m_jitterX`/`m_jitterY` (sub-pixel offset in [-0.5, 0.5]) to `Camera` class
- [ ] 2.3 Add `Camera::SetJitter(float jx, float jy)` that applies jitter to projection matrix as a translation in clip space: `proj[2][0] += jx * 2.0/renderWidth`, `proj[2][1] += jy * 2.0/renderHeight`
- [ ] 2.4 In `RenderFrame()`, advance Halton index each frame, compute jitter from `Halton(frameIndex, 2)` and `Halton(frameIndex, 3)`, call `camera.SetJitter(jx, jy)`. Only jitter when screen percentage < 100 or TAA enabled
- [ ] 2.5 Upload jittered `viewProj` to `PerFrameCB`. Store un-jittered `viewProj` separately for effects that need stable projection (skybox, UI)
- [ ] 2.6 Add `jitterOffset` (float2) to `PerFrameCB` — shaders need it for TAA resolve

## 3. Previous Frame ViewProj (Motion Vectors Support)

- [ ] 3.1 Add `prevViewProj` (Mat4, 64 bytes) to `PerFrameCB` using space from `_pad2[35]`. Reduce padding accordingly
- [ ] 3.2 Store `m_prevViewProj` in `SceneRenderer`, update each frame BEFORE computing new viewProj
- [ ] 3.3 Upload `prevViewProj` to CB every frame. First frame: `prevViewProj = viewProj` (no motion)

## 4. Velocity Pass (Wire Existing Implementation)

- [ ] 4.1 Call `AddVelocityPass()` in `RenderFrame()` after GBuffer pass and before Lighting pass
- [ ] 4.2 Create `VelocityCB` each frame with `invViewProj` (current) + `prevViewProj` (previous). Upload to CB ring
- [ ] 4.3 Verify `velocity_ps.hlsl` correctly computes per-pixel motion vectors from depth reprojection
- [ ] 4.4 Verify velocity texture (RG16F) contains valid motion vectors via GPU readback diagnostic

## 5. Temporal Upscaler Interface

- [ ] 5.1 Create `temporal_upscaler.h` with `ITemporalUpscaler` abstract interface: `Init()`, `Upscale()`, `OnResize()`, `GetName()`, `GetMinScreenPercentage()`, `GetMaxScreenPercentage()`. @source UE5 TemporalUpscaler.h — ITemporalUpscaler
- [ ] 5.2 Create `upscaler_factory.h` with `CreateUpscaler(UpscalerType)` — returns TSR (default), TAA (legacy), or null for FSR2/DLSS (future plugin)
- [ ] 5.3 Add `UpscalerType` enum: `None`, `TSR`, `TAA`, `FSR2`, `DLSS`, `XeSS`. TSR is the default, TAA is legacy/lightweight fallback
- [ ] 5.4 Add `r.AntiAliasingMethod` CVar (int: 0=None, 1=TAA, 2=TSR). Default = 2 (TSR). @source UE5 — TSR replaced TAA as default in UE5.0+
- [ ] 5.5 Add `r.TSR.History.ScreenPercentage` CVar (float, 100-200, default 100). History buffer can be LARGER than output for extra detail accumulation. @source UE5 TemporalSuperResolution.cpp

## 6. TSR — Temporal Super Resolution (Default Upscaler, UE5 Parity)

- [ ] 6.1 Create `tsr_upscaler.h/cpp` implementing `ITemporalUpscaler`. This is the PRIMARY upscaler, matching UE5's default. @source UE5 TemporalSuperResolution.cpp
- [ ] 6.2 Allocate 2 history buffers (ping-pong) at `displayRes * r.TSR.History.ScreenPercentage / 100` (RGBA16F). @source UE5 TSR uses history at 100-200% of output for super-resolution accumulation
- [ ] 6.3 Create `tsr_resolve_cs.hlsl` compute shader with 3 sub-passes matching UE5 TSR architecture: (a) dilate velocity, (b) reject history, (c) spatial+temporal resolve. @source UE5 TemporalSuperResolution.cpp — TSR uses 3 dispatch groups
- [ ] 6.4 **Velocity dilation**: 3x3 neighborhood search for closest depth, use that pixel's motion vector. Prevents thin geometry from losing motion coverage. @source UE5 TSR Pass 1
- [ ] 6.5 **History rejection via shading rejection**: Compare current frame luminance vs reprojected history luminance. If difference exceeds threshold, mark pixel as rejected. @source UE5 r.TSR.ShadingRejection.Flickering — detects moire/flicker patterns
- [ ] 6.6 **YCoCg neighborhood clamp**: Convert current 3x3 neighborhood to YCoCg, compute AABB, clamp reprojected history to this AABB. Primary ghosting prevention. @source UE5 TemporalSuperResolution.cpp
- [ ] 6.7 **Responsive accumulation**: Blend factor adapts per-pixel based on (a) velocity magnitude, (b) rejection flag, (c) disocclusion. Static pixels: alpha=0.04 (accumulate 25 frames). Fast motion: alpha=0.2 (5 frames). @source UE5 r.TSR.History.SampleCount=16
- [ ] 6.8 **Sub-pixel jitter compensation**: Unjitter current frame by subtracting jitter offset before blending. History is already in un-jittered space. @source UE5 — jitter is removed during resolve, not during rendering
- [ ] 6.9 **Disocclusion detection**: Pixels with no valid history (parallax reveal, object appearing) detected via depth discontinuity + velocity confidence. Fall back to current frame with spatial anti-aliasing filter. @source UE5 TSR — spatial filter on rejected pixels
- [ ] 6.10 **Spatial anti-aliasing filter for rejected pixels**: When history is rejected, apply a Catmull-Rom or Lanczos 2-tap filter on the current frame to provide some AA even without temporal data. @source UE5 TSR — rejected pixels still get spatial AA
- [ ] 6.11 **History update quality levels**: Support 4 quality levels (0=low to 3=high) matching UE5 `r.TSR.History.UpdateQuality`. Higher = more taps in spatial filter, tighter neighborhood clamp, better flickering detection
- [ ] 6.12 **Sharpen pass (RCAS)**: AMD CAS/RCAS sharpening as final sub-pass. Strength controlled by `r.TSR.Sharpness` CVar (0.0-1.0, default 0.0 = no sharpening). @source UE5 uses optional sharpening post-TSR

## 7. TAA — Legacy Temporal Anti-Aliasing (Lightweight Fallback)

- [ ] 7.1 Create `taa_upscaler.h/cpp` implementing `ITemporalUpscaler`. Simpler than TSR — single-pass resolve, no velocity dilation, no shading rejection. For low-end GPUs or when TSR cost is too high
- [ ] 7.2 Allocate 2 history buffers (ping-pong) at DISPLAY resolution (RGBA16F)
- [ ] 7.3 Create `taa_resolve_cs.hlsl` — single compute dispatch: reproject history via motion vectors, YCoCg clamp, blend with current frame. No multi-pass
- [ ] 7.4 Fixed blend factor: `alpha = 0.1` (10 frames accumulation). No per-pixel adaptive blending
- [ ] 7.5 No velocity dilation, no shading rejection, no spatial fallback filter — just basic temporal accumulation. ~0.3ms vs TSR's ~0.8ms

## 7. Mipmap Bias Compensation

- [x] 7.1 Compute global mipmap bias: `bias = log₂(screenPercentage / 100.0)`. At 67% → bias = -0.58, at 50% → bias = -1.0. Computed in frame_setup.cpp.
- [x] 7.2 Apply mipmap bias to all texture sampling in GBuffer shader: `SampleBias(sampler, uv, mipBias)` for all PBR texture samples in gbuffer_ps.hlsl.
- [x] 7.3 Add `mipBias` field to `PerFrameCB` at offset 480 (bytes). Declared in gbuffer_ps.hlsl via packoffset(c30).
- [x] 7.4 At 100% screen percentage, mipBias = 0 (no change from current behavior). log2(1.0) = 0.

## 8. Render Graph Integration

- [x] 8.1 TAA resolve is the upscale point. Scene color (render res) -> TAA history (display res). TAA resolve reads render-res scene color, outputs display-res history. taa_resolve_cs.hlsl updated for resolution mismatch (GetDimensions, UV-based coord mapping, display-res dispatch).
- [x] 8.2 Tonemap reads TAA output (display res). Post-process SRV chain routes TAA history SRV to tonemap.
- [x] 8.3 At 100% screen percentage, render res == display res. All paths are identity — no overhead. SampleBias(mipBias=0) == Sample. TAA dispatch/coords identical.
- [ ] 8.4 FXAA (if enabled) runs after tonemap at display resolution

## 9. Dynamic Resolution (GPU-Bound Adaptive)

- [ ] 9.1 Create `dynamic_resolution.h/cpp` with `DynamicResolutionController`
- [ ] 9.2 Track GPU frame time per frame (from RHI GPU timestamp queries)
- [ ] 9.3 Scale-down logic: if `gpuTime > targetFrameTime * 0.95` → reduce screen percentage by 5% next frame (instant response). @source UE5 DynamicRenderScaling.h
- [ ] 9.4 Scale-up logic: if `gpuTime < targetFrameTime * 0.85` for 120+ consecutive frames → increase screen percentage by 5% (conservative). @source Unity DynamicResolutionHandler — 360 frames at 60Hz
- [ ] 9.5 Clamp screen percentage to `[minScreenPercentage, maxScreenPercentage]` (default 50-100)
- [ ] 9.6 Add `r.DynamicResolution.Enabled` CVar (bool, default false)
- [ ] 9.7 Add `r.DynamicResolution.TargetFPS` CVar (float, default 60.0)
- [ ] 9.8 On resolution change, call `SceneRenderer::Resize()` to rebuild render targets at new render resolution

## 10. Lua API

- [ ] 10.1 `uz.renderer.set_screen_percentage(pct)` — set render scale (10-200)
- [ ] 10.2 `uz.renderer.get_screen_percentage()` — get current render scale
- [ ] 10.3 `uz.renderer.set_upscaler(name)` — "none", "taa", "fsr2"
- [ ] 10.4 `uz.renderer.get_upscaler()` — current upscaler name
- [ ] 10.5 `uz.renderer.set_dynamic_resolution(enabled)` — toggle adaptive scaling
- [ ] 10.6 `uz.renderer.get_render_resolution()` — returns renderWidth, renderHeight
- [ ] 10.7 `uz.renderer.get_display_resolution()` — returns displayWidth, displayHeight

## 11. Visual Verification

- [ ] 11.1 Screenshot: 100% screen percentage + no AA — baseline, must match current output exactly
- [ ] 11.2 Screenshot: 100% screen percentage + TSR — temporal stability on edges, no ghosting on fast motion
- [ ] 11.3 Screenshot: 67% screen percentage + TSR — near-native quality at 4K, primary use case
- [ ] 11.4 Screenshot: 50% screen percentage + TSR — acceptable quality, some softness on fine detail
- [ ] 11.5 Screenshot: 67% screen percentage + TAA (legacy) — compare against TSR to show TSR advantage
- [ ] 11.6 Screenshot: camera orbit with velocity vectors visualized (debug view) — verify motion vector correctness
- [ ] 11.7 Screenshot: fast camera rotation — verify TSR handles disocclusion (no ghosting trails)
- [ ] 11.8 Compare grid: native vs 67% TSR vs 67% TAA vs 50% TSR — single image, 4-cell grid
- [ ] 11.9 Verify no regression: all existing demos render correctly at 100% with upscaler=none

## 12. Documentation

- [ ] 12.1 Document screen percentage system with UE5/Unity/Godot comparison
- [ ] 12.2 Document ITemporalUpscaler interface for future FSR2/DLSS plugin authors
- [ ] 12.3 Document TSR architecture: 3-pass pipeline, velocity dilation, shading rejection, history quality levels. Reference UE5 TemporalSuperResolution.cpp
- [ ] 12.4 Document TAA vs TSR: when to use each, cost comparison (~0.3ms vs ~0.8ms), quality difference
- [ ] 12.5 Document dynamic resolution algorithm and tuning CVars
- [ ] 12.6 Document pipeline order change (where upscaler sits in render graph)
- [ ] 12.7 Add TSR/TAA quality settings guide (r.TSR.History.UpdateQuality, r.TSR.Sharpness, blend factor tuning)
