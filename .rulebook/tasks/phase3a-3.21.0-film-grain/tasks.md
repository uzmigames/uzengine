## 1. Implementation

- [ ] 1.1 Generate or import 128×128 blue-noise texture — R8_UNORM, tiling-safe. Include as engine resource (embedded or loaded at init).
- [ ] 1.2 Implement film grain shader — sample blue-noise at `uv * screenSize / 128 + temporalOffset`. Temporal offset = `frame_index * 0.6180339887` (golden ratio). Convert to [-0.5, 0.5] range.
- [ ] 1.3 Implement luminance-dependent grain strength — compute pixel luminance, apply zone weighting: `grainMul = lerp(shadowIntensity, midtoneIntensity, smoothstep(shadowsMax*0.5, shadowsMax, luma)) * lerp(midtoneIntensity, highlightIntensity, smoothstep(highlightsMin, highlightsMax, luma))`. Multiply grain by `intensity * grainMul`.
- [ ] 1.4 Implement chromatic grain — offset blue-noise UV slightly per R/G/B channel (e.g., +0.5 texel, +1.0 texel) for subtle color grain. Scale by `intensity * 0.3` for chromatic component.
- [ ] 1.5 Implement grain texel scale — scale blue-noise UV by `texelSize` parameter (1.0=default, 0.5=fine grain, 2.0=coarse). UE5 default: 1.0.
- [ ] 1.6 Integrate into final composite pass — film grain applied after tonemap + color grading, in SDR space. `output = saturate(tonemapped + grain)`.
- [ ] 1.7 Add FilmGrainDesc to PostProcessStack — enabled/disabled, intensity, shadowIntensity, midtoneIntensity, highlightIntensity, shadowsMax, highlightsMin, highlightsMax, texelSize.
- [ ] 1.8 Create film grain demo — scene with and without grain, intensity slider, zoom to see grain detail.

## 2. Visual Verification

- [ ] 2.1 Screenshot: grain at intensity=0.5 — visible noise on midtones
- [ ] 2.2 Screenshot: grain at intensity=0.0 — clean image (no grain)
- [ ] 2.3 Screenshot: zoom showing blue-noise quality — no white-noise clumps
- [ ] 2.4 Screenshot: shadows have less grain than midtones (luminance-dependent)
- [ ] 2.5 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document film grain parameters with UE5 equivalents
- [ ] 3.2 Add blue-noise explanation and generation method
