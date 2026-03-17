## 1. Implementation

- [x] 1.1 Implement vignette in final composite shader — implemented in tonemap_ps.hlsl using UE5 ComputeVignetteMask (PostProcessCommon.ush:142-150). Uses cosine-fourth-power natural vignetting law: tan2Angle=dot(pos*intensity, pos*intensity), cos4Angle=1/(tan2Angle+1)^2. Applied after tonemapping. Default intensity 0.4 (UE5 Scene.cpp:505).
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
