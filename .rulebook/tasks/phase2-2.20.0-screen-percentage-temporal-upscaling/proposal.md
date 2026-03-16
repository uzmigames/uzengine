# Screen Percentage + Temporal Upscaling — UE5/Unity/Godot Parity

## Priority: P0 — Prerequisite for 4K rendering, TAA, and all temporal effects

## Summary

No modern engine renders at native 4K. UE5, Unity, and Godot all render at 50-67% internal resolution and upscale via temporal reconstruction (TSR, TAA upscale, FSR 2.0). UzEngine currently has NO screen percentage system — render targets are always sized to the swapchain resolution. This makes 4K rendering 2-4x more expensive than necessary and blocks TAA, motion blur, SSR, and any temporal effect that depends on jitter + history accumulation.

This task implements the full pipeline: screen percentage → jittered camera → velocity buffer → temporal accumulation → upscale → post-process at display resolution.

## Reference Engines

### UE5 — Temporal Super Resolution (TSR)
- **Source**: `TemporalSuperResolution.cpp`, `LegacyScreenPercentageDriver.cpp`
- **Pattern**: `ViewRect` (render res) vs `UnscaledViewRect` (display res). Screen percentage = render/display × 100
- **Default**: Auto-compute based on pixel budget. Typical: 67% (1440p → 4K)
- **Jitter**: Halton sequence, applied to projection matrix per frame
- **History**: 16 accumulated samples, YCoCg neighborhood clamp, motion vector reprojection
- **Dynamic Resolution**: GPU frame time heuristic, scales screen percentage per-frame
- **Third-party**: `ITemporalUpscaler` interface for DLSS/FSR/XeSS plugins

### Unity — TAA Temporal Upscale
- **Pattern**: Render scale float multiplier (0.5-2.0) applied to width × height
- **Default**: 100% (native). Recommended: 67% + TAA upscale
- **Dynamic Resolution**: `DynamicResolutionHandler` — scale down instant, scale up slow (360 frames)
- **Post-process**: Runs at FINAL resolution (after upscale), not internal

### Godot 4 — FSR 2.0
- **Source**: `renderer_viewport.cpp`, `fsr2.cpp`
- **Pattern**: `rendering/scaling_3d/scale` (0.5-2.0) + mode enum (Bilinear/FSR1/FSR2)
- **FSR 2.0**: 9-pass temporal algorithm, requires motion vectors + camera jitter
- **Fallback chain**: FSR2 → FSR1 → Bilinear (deterministic at init)
- **Mipmap bias**: Auto-compensated via `log₂(scale)`

## Current UzEngine State

| Component | Status |
|-----------|--------|
| Screen Percentage | **Not implemented** — all targets = swapchain size |
| Velocity buffer | **Texture exists** (GBuffer RG16F) but `AddVelocityPass()` never called |
| Camera jitter | **Not implemented** — no Halton, no jitter on projection |
| prevViewProj | **Not in PerFrameCB** — space exists in `_pad2[35]` |
| TAA / TSR | **Not implemented** — task `phase3a-3.14.0` exists but not started |
| Tonemap | Writes directly to backbuffer — no intermediate buffer |
| Dynamic Resolution | **Not implemented** |
| FSR/DLSS/XeSS | **Not implemented** |

## Architecture

### Pipeline Order (after implementation)

```
1. Shadow Pass            (render res)
2. GBuffer Pass           (render res — may be smaller than display)
3. Velocity Pass          (render res — fills motion vectors)
4. Lighting Pass          (render res)
5. Atmosphere + Fog       (render res)
6. TAA / Temporal Upscale (render res → display res)
7. Tonemap                (display res)
8. FXAA (optional)        (display res)
9. Overlay                (display res)
```

### Key Concept: Two Resolutions

```
renderWidth  = displayWidth  * screenPercentage / 100
renderHeight = displayHeight * screenPercentage / 100

GBuffer, HDR scene color, velocity → sized at (renderWidth, renderHeight)
History buffers, tonemap output, backbuffer → sized at (displayWidth, displayHeight)
```

### Upscaler Interface (pluggable)

```cpp
class ITemporalUpscaler {
    virtual void Init(IRHIDevice*, uint32_t renderW, uint32_t renderH,
                      uint32_t displayW, uint32_t displayH) = 0;
    virtual void Upscale(IRHICommandList*, RHITextureHandle sceneColor,
                         RHITextureHandle velocity, RHITextureHandle depth,
                         const Mat4& jitteredViewProj, const Mat4& prevViewProj,
                         float deltaTime, RHITextureHandle output) = 0;
    virtual void OnResize(uint32_t renderW, uint32_t renderH,
                          uint32_t displayW, uint32_t displayH) = 0;
};
```

Implementations:
- `TSRUpscaler` (built-in, DEFAULT — UE5 parity, 3-pass: velocity dilate + history reject + resolve)
- `TAAUpscaler` (built-in, legacy lightweight fallback — single-pass, ~0.3ms)
- `FSR2Upscaler` (future, AMD SDK plugin)
- `DLSSUpscaler` (future, NVIDIA SDK plugin)

**UzEngine always defaults to TSR** — matching UE5's decision to replace TAA with TSR as the standard AA method. TAA exists only as a low-cost fallback for low-end hardware.

## Impact

- **Files modified**: ~15-20 (scene_renderer, camera, gbuffer, constant_buffers, velocity_pass, new upscaler files)
- **New files**: ~8 (screen_percentage.h, temporal_upscaler.h, taa_upscaler.cpp/h, halton.h, dynamic_resolution.h/cpp)
- **Shaders**: ~3 (taa_resolve.hlsl, velocity_ps.hlsl updates, mipmap bias in material sampling)
- **Performance**: 2-4x faster at 4K (render at 1440p, upscale). TAA adds ~0.5ms.
- **Visual**: Dramatically better AA quality, smoother motion, temporal stability
- **Dependencies**: Prerequisite for motion blur (3.18.0), SSR (3.16.0), and any temporal effect

## Acceptance Criteria

1. Screen percentage CVar (`r.ScreenPercentage`) controls internal render resolution (10-200%)
2. GBuffer + HDR scene color sized at render resolution, not display resolution
3. Halton jitter applied to camera projection each frame (8-16 phase sequence)
4. Velocity pass fills motion vectors from current + previous ViewProj
5. TAA temporal accumulation: YCoCg clamp, motion vector reprojection, history ping-pong
6. TAA resolve outputs at display resolution (upscale integrated)
7. Tonemap + FXAA run at display resolution (after upscale)
8. `ITemporalUpscaler` interface allows plugging FSR2/DLSS later
9. **TSR is the default upscaler** — 3-pass architecture matching UE5 (velocity dilation, shading rejection, spatial+temporal resolve)
10. TAA exists as lightweight fallback (single-pass, no velocity dilation, ~0.3ms)
11. Dynamic resolution adjusts screen percentage based on GPU frame time
12. Mipmap bias auto-compensated: `log₂(screenPercentage / 100)` applied to samplers
13. Visual: 67% screen percentage + TSR looks near-identical to native at 4K
14. No regression at 100% screen percentage (bypass upscaler, behaves as today)
15. `r.AntiAliasingMethod` CVar: 0=None, 1=TAA, 2=TSR (default=2)
