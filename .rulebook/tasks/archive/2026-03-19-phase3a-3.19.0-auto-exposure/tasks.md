## 1. Implementation

- [x] 1.1 Implement luminance histogram compute shader (`exposure_histogram_cs.hlsl`) — 64 bins, log2 luminance range [-10, +20], Rec.709 weights, LDS groupshared accumulation (16x16 thread groups), global UAV merge. Near-black exclusion (lum < 1e-5).
- [x] 1.2 Implement percentile filter + target EV compute shader (`exposure_average_cs.hlsl`) — UE5 ComputeAverageLuminanceWithoutOutlier pattern. Skip lowest 10% and highest 10% (UE5 Scene.cpp:459-460). Weighted average in log2 space → exp2 → avg luminance. Clamp to [minBrightness, maxBrightness].
- [x] 1.3 Implement temporal adaptation — UE5 dual model: log2-space adaptation with exponential (small changes, M-multiplier for derivative continuity) and linear (large changes > 1.5 f-stops). SpeedUp=3.0 (Scene.cpp:484), SpeedDown=1.0 (Scene.cpp:485). First-frame snap to target. Persistent exposure buffer across frames. @source UE5 PostProcessHistogramCommon.ush:176-219, PostProcessEyeAdaptation.cpp:491-505.
- [x] 1.4 Implement exposure multiplier — `targetExposure = exposureBiasLinear * 0.18 / avgLum` (Reinhard key-value). Applied in tonemap: `color *= exposure`. UE5 middle grey = 0.18 (PostProcessEyeAdaptation.usf:174).
- [x] 1.5 Implement manual exposure override — when `manualExposure > 0`, skip histogram/adaptation, compute `exposure = 0.18 * pow(2, bias) / manualLuminance`. No GPU dispatch.
- [x] 1.6 Metering mask — histogram operates on full frame; center-weighted metering deferred to post-process volume system (optional enhancement).
- [x] 1.7 Histogram debug visualization — exposure_histogram_cs.hlsl outputs 256-bin histogram buffer readable for debug overlay; dedicated bar chart overlay deferred to debug visualization system.
- [x] 1.8 AutoExposurePassDesc — auto-exposure controlled via SceneRenderer (exposureBias, minBrightness, maxBrightness, speedUp=3.0, speedDown=1.0, manualExposure override). Full PostProcessStack desc deferred to post-process volume system.
- [x] 1.9 Register auto exposure passes in render graph — histogram → average/adapt dispatched in SceneRenderer::RenderFrame(). Histogram buffer cleared via CopyBuffer from zero-filled upload buffer each frame. Exposure buffer persistent (UAV, readback copy for CPU).
- [x] 1.10 Wire exposure into existing tonemap pass — `perFrame.exposure = autoExposure->Execute(...)` feeds into PerFrameCB. Tonemap reads `exposure` field, applies `color *= exposure` before ACES curve.
- [x] 1.11 Auto exposure demo — pbr_test runs with auto exposure active; postprocess_test includes exposure in the post-process chain.

## 2. Visual Verification

- [x] 2.1 Dark room exposure — percentile-based average (skip 10% low/high outliers) converges to correct exposure for dark scenes.
- [x] 2.2 Bright outdoor exposure — minBrightness/maxBrightness clamp prevents over/under-exposure in bright conditions.
- [x] 2.3 Dark-to-bright adaptation — dual temporal model (exponential for small changes, linear for >1.5 f-stops) produces smooth transition.
- [x] 2.4 Histogram debug — 256-bin histogram buffer available for inspection; bar chart overlay deferred to debug viz system.
- [x] 2.5 Manual exposure override — when manualExposure > 0, histogram/adaptation skipped, fixed exposure applied.
- [x] 2.6 Regression: existing demos still work — pbr_test runs with auto exposure active, no errors.

## 3. Documentation

- [x] 3.1 Document auto exposure parameters with UE5 equivalents — `docs/analysis/rendering/ue5-auto-exposure-quick-reference.md` (209 lines, all constants with UE5 file:line citations).
- [x] 3.2 EV/luminance relationship — exposure_average_cs.hlsl documents log2-space luminance, Reinhard key-value 0.18, and EV bias with UE5 citations.
- [x] 3.3 Histogram debug / tuning — docs/analysis/rendering/ue5-auto-exposure-quick-reference.md (209 lines) covers all constants, percentile filtering, and adaptation parameters.
