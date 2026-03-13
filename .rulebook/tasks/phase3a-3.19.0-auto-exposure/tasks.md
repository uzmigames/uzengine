## 1. Implementation

- [ ] 1.1 Implement luminance histogram compute shader (`exposure_histogram_cs.hlsl`) — 64 bins covering [histLogMin, histLogMax] log2 luminance range. Each thread: compute pixel luminance (Rec.709 weights), convert to log2, find bin index, InterlockedAdd to histogram UAV. Clear histogram to 0 at frame start.
- [ ] 1.2 Implement percentile filter + target EV compute shader (`exposure_average_cs.hlsl`) — sum histogram bins, find low percentile (skip darkest 70%), high percentile (skip brightest 2%). Average remaining bins → weighted log2 luminance → target EV. Clamp to [log2(minBrightness), log2(maxBrightness)].
- [ ] 1.3 Implement temporal adaptation — in average shader: read previous frame's exposure from persistent buffer. Compute adapt speed = (targetEV > currentEV) ? speedUp : speedDown. `newEV = lerp(currentEV, targetEV, 1 - exp(-deltaTime / adaptSpeed))`. Write to exposure buffer.
- [ ] 1.4 Implement exposure multiplier — `exposure = exposureBias / (1.2 * pow(2, currentEV))`. Pass to tonemap shader as uniform. Tonemap applies `color *= exposure` before ACES curve.
- [ ] 1.5 Implement manual exposure override — when manualExposure > 0, skip histogram/adaptation, use fixed `exposure = exposureBias / (1.2 * pow(2, manualExposure))`.
- [ ] 1.6 Implement metering mask (optional) — center-weighted metering via texture. Multiply histogram contribution by mask value. Center of screen contributes more than edges.
- [ ] 1.7 Implement histogram debug visualization — render 64-bin bar chart overlay showing luminance distribution, current EV marker, percentile boundaries. Toggled via debug flag.
- [ ] 1.8 Add AutoExposurePassDesc to PostProcessStack — enabled/disabled, method (Basic/Histogram), exposureBias, minBrightness, maxBrightness, speedUp, speedDown, lowPercent, highPercent, manualExposure.
- [ ] 1.9 Register auto exposure passes in render graph — histogram → average/adapt. Run before tonemap. Exposure buffer persists across frames (not transient).
- [ ] 1.10 Wire exposure into existing tonemap pass — tonemap reads exposure buffer, applies `sceneColor *= exposure` before ACES curve.
- [ ] 1.11 Create auto exposure demo — scene with bright and dark areas, camera flying between them. Show histogram debug overlay, exposure adaptation in real-time.

## 2. Visual Verification

- [ ] 2.1 Screenshot: dark room properly exposed (shadows visible, not crushed)
- [ ] 2.2 Screenshot: bright outdoor properly exposed (highlights not clipped)
- [ ] 2.3 Screenshot sequence: dark→bright transition showing gradual adaptation
- [ ] 2.4 Screenshot: histogram debug view showing bin distribution + EV marker
- [ ] 2.5 Screenshot: manual exposure override at different EV values
- [ ] 2.6 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document auto exposure parameters with UE5 equivalents
- [ ] 3.2 Add EV/luminance relationship explanation for artists
- [ ] 3.3 Document histogram debug view and exposure tuning workflow
