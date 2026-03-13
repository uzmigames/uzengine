## 1. Implementation

- [ ] 1.1 Implement vignette in final composite shader — compute radial distance from center with aspect ratio correction: `float2 centered = (uv - 0.5) * float2(aspectRatio, 1.0); float d = length(centered);`. Apply: `float vig = saturate(1.0 - smoothstep(1.0 - intensity, 1.0 - intensity + smoothness, d));`. Multiply scene color by vignette.
- [ ] 1.2 Add VignetteDesc to PostProcessStack — enabled/disabled, intensity (0.4 UE5 default), smoothness (0.3), optional center offset (float2 for off-center vignette).
- [ ] 1.3 Implement UE5-compatible intensity mapping — UE5's VignetteIntensity=0.4 matches subtle corner darkening. Map [0,1] range: 0=off, 0.4=subtle, 0.8=heavy, 1.0=black corners.
- [ ] 1.4 Create vignette demo — toggle on/off, intensity slider, before/after comparison.

## 2. Visual Verification

- [ ] 2.1 Screenshot: vignette at intensity=0.4 — subtle corner darkening
- [ ] 2.2 Screenshot: vignette at intensity=0.0 — no effect
- [ ] 2.3 Screenshot: vignette at intensity=1.0 — heavy dark corners
- [ ] 2.4 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document vignette parameters with UE5 equivalents
