# TAA window intervention sweep (H-27 correction), 2026-10-13

User decision (2026-10-13): the pose-W window residual is to be CORRECTED, not accepted (task
item 1.4). This file records the measurement sweep that localises the residual and tests the two
interventions item 1.3 names as never-tried. Harness: `scripts/rendering/run_taa_window_experiments.py`
(reused verbatim; a second variant list was driven through the same `capture`/`analyse` so the ROI,
masks and metric are identical). ROI = pose-W `window`; masks frozen from the baseline capture:
bright pane = 29156 px, boundary (coverage-flip) = 5127 px, dark bars = 64021 px. Primary metric =
mean per-pixel temporal std of linear Rec.709 luminance over 64 frames (8 jitter cycles).
`energy_ratio_vs_reference` compares the output mean to the mean of the raw pre-TAA samples over
whole jitter cycles (1.0 = the supersampler converged to the right value).

The baseline reproduces the previously reported residual exactly: window all_std = 0.008511.

## Existing diagnostic modes

| variant | all_std | bright_std | boundary_std | bright_E | boundary_E |
|---|---:|---:|---:|---:|---:|
| baseline jitter 8 | 0.008511 | 0.020535 | 0.044181 | 0.973 | 0.306 |
| jitter 16 | 0.011561 | 0.028927 | 0.050025 | 0.975 | 0.302 |
| jitter 4 | 0.004780 | 0.011458 | 0.025079 | 0.966 | 0.316 |
| jitter 1 / zero-jitter | ~0.000004 | ~0.000001 | ~0.00002 | 0.955 | 0.56 |
| reconstruction 1 (no HDR weight) | 0.005712 | 0.007910 | 0.053896 | 0.995 | 0.768 |
| reconstruction 2 (center tap) | 0.004161 | 0.000295 | 0.078069 | 1.000 | 0.354 |
| reconstruction 3 (full 3x3) | 0.009445 | 0.024562 | 0.038643 | 0.968 | 0.296 |
| clamp 1 (no clamp) | 0.004533 | 0.009687 | 0.030253 | 0.958 | 0.274 |
| clamp 2 (variance only) | 0.008494 | 0.020480 | 0.044181 | 0.972 | 0.306 |
| clamp 3 (AABB only) | 0.007779 | 0.020511 | 0.030283 | 0.973 | 0.273 |
| blend 1 (fixed weight) | 0.007640 | 0.017697 | 0.043633 | 0.973 | 0.304 |
| reconstruction 1 + blend 1 | 0.004339 | 0.003481 | 0.052749 | 0.993 | 0.765 |

## The two interventions item 1.3 named as never-tried

| variant | all_std | bright_std | boundary_std | bright_E | verdict |
|---|---:|---:|---:|---:|---|
| exact history read (HistoryReadMode=1, point Load) | 0.008514 | 0.020549 | 0.044139 | 0.973 | **no change vs baseline** |
| cycle-averaged clamp (ClampFreezeAfterFrame=236, AverageFrames=8) | 0.011102 | 0.027487 | 0.052379 | 0.963 | **worse than baseline** |
| exact read + reconstruction 1 | 0.005719 | 0.007916 | 0.053929 | 0.995 | = reconstruction 1 alone |
| cycle-avg clamp + exact read | 0.011113 | 0.027535 | 0.052167 | 0.963 | no help |

Both named interventions are DEAD: exact/point history read changes nothing (the 5-tap Catmull-Rom
resampling is not the source), and freezing/cycle-averaging the clamp box makes the bright-pane
residual worse and loses a little energy.

## Localisation

The window residual lives in the jitter-dependent HDR-weighted reconstruction plus-filter
(taa_resolve_cs.hlsl ~210-242, HDR weight at ~95-128 and line 219/235). Evidence: dropping the HDR
weighting (reconstruction mode 1, hdrWeight->1) cuts bright_std 61% (0.020535 -> 0.007910) and fixes
the energy bias (0.973 -> 0.995); center-tap only (mode 2) nearly eliminates it (0.000295) but wrecks
boundary AA (0.044 -> 0.078). The clamp box barely matters for the bright pane (clamp 2 = baseline);
history reprojection does not matter (exact read = baseline). r.TAA.ReconstructionHdrWeightExponent
(softening pow(w,exp)) was tried previously and did not reach threshold — consistent, since even the
full drop only reaches bright_std 0.0079, still above the 0.00048 detection threshold.

Conclusion: no single existing lever brings the bright window below threshold without worsening
silhouette flicker or fireflies. A real production correction must neutralise the 8-periodic HDR-weight
modulation on coverage-stable bright pixels while keeping the HDR weighting where it suppresses
fireflies at moving / high-contrast edges. The specific mechanism is being chosen by the architect
consultant (claude-fable-5-1) from this measured data; the chosen correction + its before/after
measurement will be recorded in the next file.

Artifacts: `.screenshots/flicker-correction-20261013-taa-sweep/window-experiments.json`,
`.screenshots/flicker-correction-20261013-taa-sweep2/window-experiments.json`.
