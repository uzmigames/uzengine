## 1. Implementation

- [x] 1.1 Implement luminance histogram compute shader (`exposure_histogram_cs.hlsl`) — 64 bins, log2 luminance range [-10, +20], Rec.709 weights, LDS groupshared accumulation (16x16 thread groups), global UAV merge. Near-black exclusion (lum < 1e-5).
- [x] 1.2 Implement percentile filter + target EV compute shader (`exposure_average_cs.hlsl`) — UE5 ComputeAverageLuminanceWithoutOutlier pattern. Skip lowest 10% and highest 10% (UE5 Scene.cpp:459-460). Weighted average in log2 space → exp2 → avg luminance. Clamp to [minBrightness, maxBrightness].
- [x] 1.3 Implement temporal adaptation — asymmetric: speedUp=3.0 f-stops/sec (Scene.cpp:484), speedDown=1.0 (Scene.cpp:485). Exponential blend: `alpha = 1 - exp(-dt / adaptSpeed)`. First-frame snap to target. Persistent exposure buffer across frames.
- [x] 1.4 Implement exposure multiplier — `targetExposure = exposureBiasLinear * 0.18 / avgLum` (Reinhard key-value). Applied in tonemap: `color *= exposure`. UE5 middle grey = 0.18 (PostProcessEyeAdaptation.usf:174).
- [x] 1.5 Implement manual exposure override — when `manualExposure > 0`, skip histogram/adaptation, compute `exposure = 0.18 * pow(2, bias) / manualLuminance`. No GPU dispatch.
- [ ] 1.6 Implement metering mask (optional) — center-weighted metering via texture.
- [ ] 1.7 Implement histogram debug visualization — 64-bin bar chart overlay.
- [ ] 1.8 Add AutoExposurePassDesc to PostProcessStack — enabled/disabled, method, bias, min/max brightness, speeds, percentiles, manual override.
- [x] 1.9 Register auto exposure passes in render graph — histogram → average/adapt dispatched in SceneRenderer::RenderFrame(). Histogram buffer cleared via CopyBuffer from zero-filled upload buffer each frame. Exposure buffer persistent (UAV, readback copy for CPU).
- [x] 1.10 Wire exposure into existing tonemap pass — `perFrame.exposure = autoExposure->Execute(...)` feeds into PerFrameCB. Tonemap reads `exposure` field, applies `color *= exposure` before ACES curve.
- [ ] 1.11 Create auto exposure demo — scene with bright and dark areas.

## 2. Visual Verification

- [ ] 2.1 Screenshot: dark room properly exposed
- [ ] 2.2 Screenshot: bright outdoor properly exposed
- [ ] 2.3 Screenshot sequence: dark→bright transition showing gradual adaptation
- [ ] 2.4 Screenshot: histogram debug view
- [ ] 2.5 Screenshot: manual exposure override
- [x] 2.6 Regression: existing demos still work — pbr_test runs with auto exposure active, no errors.

## 3. Documentation

- [x] 3.1 Document auto exposure parameters with UE5 equivalents — `docs/analysis/rendering/ue5-auto-exposure-quick-reference.md` (209 lines, all constants with UE5 file:line citations).
- [ ] 3.2 Add EV/luminance relationship explanation for artists
- [ ] 3.3 Document histogram debug view and exposure tuning workflow
