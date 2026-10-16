# Measurement preregistration, 2026-10-13

Registered before the first capture of this task. No production correction is selected.
H-27 acceptance belongs to the user. All captures run serially on the RTX 5080,
without the unavailable GPU validation layer; tests follow the final capture.

## Regions and reference

1920x1080, jitter length 8, fixed exposure 1, TAA on. Three independent
processes per condition; 200 warmup frames and 256 measured frames for still
baselines, 128 for diagnostic interventions (whole jitter cycles).

Pose A: rear_door (960,592,224,224), right_shadow (1472,464,256,256),
lit_floor (832,816,256,64). Pose W: window (770,150,192,512),
shadow_wall (150,260,192,384), and lit_wall (350,260,192,384), a wall
above the floor, carried independently rather than substituting a threshold
defined on the darker wall. If its signal is too weak, report that gate
unmeasurable; do not choose a new region from candidate results.

R is each pixel's mean pre-TAA raw radiance over complete jitter cycles.
This is an energy reference for reconstruction, not ground truth lighting.
Metrics use linear Rec.709 luminance; RGB energy is also reported.
The followup-09 floor is max(3 * Student-t 95% half width, 5% * mean),
recomputed independently per region on current-build baseline repeats.

## E1 through M2

E1: spatial mean absolute per-pixel energy error against R, normalized by
the ROI mean R, <=1% at defect regions. This replaces cancellation-prone
ROI energy ratios and the small glass-only mask as the sole acceptance test;
signed energy ratios remain diagnostic.
E2: control ROI mean energy changes <=1% from baseline.
T1: per-pixel temporal standard deviation averaged over the ROI <=50%
of baseline at each defect, and improvement exceeds the region's floor.
T2: control oscillation <=110% of baseline.
D1: mean Sobel magnitude of cycle-averaged output >=95% of baseline.
M1: spatial mean absolute error against settled pre-motion output, after
strafe stop and after cut, must recover and remain within tolerance in the
capture, <= baseline recovery +2 frames. Tolerance is fixed by the rule
max(2% * settled ROI mean, 1.5 * maximum same-error value during settled
baseline hold); calibration uses baseline only and is reused unchanged
for variants. Report calibrated values, noise and censored recoveries.
Before the first motion capture, calibration is specified to include both
settled baseline holds (before and after strafe): use the larger same-error
maximum and the larger settled mean. Moving the ROI exposes different
content, so pre-strafe noise alone cannot establish a usable tolerance at
the second pose. This uses no candidate data and changes no acceptance margin.
M2: maximum per-pixel absolute error in eight frames after cut <=110%
of baseline. Report this separately from the spatial mean recovery error.

Motion uses hold/strafe/hold/cut/hold at both W and A; A includes the door,
right shadow and floor, not only the window. Phase timing comes from the
Lua boundary log, not an assumed preload index.

## Harness controls and diagnostic questions

No-op must fail T1. A spatial 3x3 box blur of recorded output must fail D1;
report T1 too, without claiming a real correction when it fails T1 as well.
A temporal cycle-average plus spatial blur is an additional deliberately
over-smoothed control which must pass T1 and fail D1.

H-45: run records-off/on equivalence and consumption gates unchanged on
right_shadow; no interpretation past a failed gate.
H-18b: freeze CSM atlas updates and the matching cascade constants after
frame 228; report consumed shadow-visibility oscillation, output and energy.
H-09b: compare GTAO on/off; measure AO and output spectrum at periods
2, 6, 8 and 24 (GTAO sequence period is 6*4), per-pixel power shares.
Use 192 measured frames for this diagnostic: all four periods must divide
the observation window, avoiding nearest-bin leakage.
Window: compare ordinary bounds, last-frame frozen bounds, eight-frame
cycle-average frozen bounds, and exact nearest-texel history read.
Validate bounds are actually frozen and history read changes recorded data.
H-23b: repeat baseline, AABB only, and exponent-0+AABB motion three times,
with M1 calibration fixed from baseline, not each variant.

Scene SkyLight remains unchanged during current-scene measurements.
Compare source calibration with the shipping Sponza scenes and recommend
whether to migrate; any calibrated comparison uses separate scene files.
No threshold, default or H-27 acceptance decision is changed after measurement.
