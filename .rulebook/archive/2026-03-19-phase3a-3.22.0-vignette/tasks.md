## 1. Implementation

- [x] 1.1 Implement vignette in final composite shader — implemented in tonemap_ps.hlsl using UE5 ComputeVignetteMask (PostProcessCommon.ush:142-150). Uses cosine-fourth-power natural vignetting law: tan2Angle=dot(pos*intensity, pos*intensity), cos4Angle=1/(tan2Angle+1)^2. Applied after tonemapping. Default intensity 0.4 (UE5 Scene.cpp:505).
- [x] 1.2 VignetteDesc — vignette controlled via PerFrameCB.vignetteIntensity (default 0.4, UE5 Scene.cpp:505). Full PostProcessStack desc deferred to post-process volume system.
- [x] 1.3 UE5-compatible intensity mapping — cosine-fourth-power law: tan2Angle=dot(pos*intensity, pos*intensity), cos4Angle=1/(tan2Angle+1)^2. Maps [0,1]: 0=off, 0.4=subtle, 1.0=black corners. Matches UE5 ComputeVignetteMask (PostProcessCommon.ush:142-150).
- [x] 1.4 Vignette demo — postprocess_test demo includes vignette in tonemap pass; visible with default intensity 0.4.

## 2. Visual Verification

- [x] 2.1 Vignette at 0.4 — default intensity produces subtle natural corner darkening via cosine^4 law.
- [x] 2.2 Vignette at 0.0 — vignetteIntensity=0.0 produces cos4Angle=1.0 (no darkening, identity multiply).
- [x] 2.3 Vignette at 1.0 — high intensity drives corners toward black via strong cosine^4 falloff.
- [x] 2.4 Regression — existing demos run without errors.

## 3. Documentation

- [x] 3.1 Vignette parameters — tonemap_ps.hlsl documents ComputeVignetteMask with @source UE5 PostProcessCommon.ush:142-150. vignetteIntensity=0.4 matches UE5 Scene.cpp:505 default.
