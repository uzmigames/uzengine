# Design: Isolate TAA reconstruction and history on the window

## Execution order
1. phase0_sponza-followup-09-validate-flicker-capture
2. phase0_sponza-followup-10-isolate-taa-window
3. phase0_sponza-followup-11-isolate-flicker-display
4. phase0_sponza-followup-12-isolate-shadow-lighting
5. phase0_sponza-followup-13-isolate-gdf-motion
6. phase0_sponza-followup-14-validate-flicker-correction

## Shared protocol
Use full-resolution pixel counts and linear RGB/Rec709 luminance for energy; label YCoCg Y separately. Display-referred PNG thresholds are secondary. Record effective exposure and every active fallback. Preserve jitter 8 and PreExposeWeights off as production defaults during diagnosis. Length-1 Halton jitter is a constant offset, not zero.

Use at least three independent runs, each internally continuous with at least 200 warm-up frames and 256 static measurement frames; record dt and GPU timing, and verify convergence rather than assuming it. Hold exposure fixed for upstream attribution, then test adaptation independently. Freeze masks and analysis criteria before comparing candidates; never reselect each candidate's worst pixels. Maintain a 16-phase/equivalent spatially supersampled reference for analytic edges and a converged reference appropriate to each production ROI.

Motion comparisons require validated reprojection/world-space tracking, depth/normal/surface validation, disocclusion rejection and mask-coverage reporting. Track cut and post-motion convergence independently. Avoid treating fixed-screen ROI mean changes as lighting discontinuities. Keep random seeds fixed between paired controls where applicable and vary seeds/repeat runs to assess robustness.

## Hypothesis ledger
For every hypothesis store: ID; ROI; predicted effect; intervention; controlled settings; effective pipeline; stage and unit; reference; valid sample count; repeatability/confidence interval; observed result; and verdict (supported, rejected only within tested scope, or inconclusive). A null result below measurement sensitivity is inconclusive. A diagnostic task can finish with an inconclusive verdict only when its experiments were performed and the next discriminating test is specified; this does not close the visual defect.

## Artifacts and reproducibility
Write run artifacts under .screenshots/flicker-elimination/10-isolate-taa-window/ with manifest.json, report.md, full-resolution linear dumps, exact per-frame CSV, annotated masks, comparison plots and run logs. Keep reusable harness code in scripts/. Link historical .screenshots/flicker-region and flicker-jitter evidence but account for the corrected PNG render indices. Do not overwrite previous packages. Repository documentation stays in English.

## Scope and completion
All checklist items start pending. No rendering implementation or capture execution is included in task creation itself. No commits, unrelated VFX changes, automatic archival or default visual tuning are authorized by this plan. Preserve old calibration backups. Diagnose known test failures instead of blanket filtering.

## Implementation and results (2026-09-09)

### Diagnostic controls (defaults unchanged)

`r.TAA.JitterScale` (1) scales the Halton offsets, so 0 gives accumulation with no sub-pixel offset —
a control `JitterSequenceLength=1` cannot express, because Halton23(1) is a constant (0, −0.167) px
offset. `r.TAA.ReconstructionMode` (0) selects the plus filter with spatial x HDR weights, spatial
weights only, the centre tap only, or the full 3x3. `r.TAA.ClampMode` (0) selects AABB ∩ variance,
none, variance only or AABB only. `r.TAA.BlendMode` (0) selects the production blend or a fixed
`r.TAA.CurrentFrameWeight`. The region dump gained a `taa+extra` field group (clip box, centre depth,
velocity magnitude, plus-filter tap lumas) so the resolve can be recomputed offline.

### Offline model

`scripts/flicker/taa_model.py` implements the resolve and the analytic fixtures;
`scripts/validate_taa_model.py` checks it against GPU records — reconstruction 2.3e-7, clamp 1.1e-7,
blend 2.2e-7 relative error. Variants are then compared against a **converged spatial supersampling
reference** (the exact box integral of the signal over the pixel footprint), never against the
previous frame.

### Results

Two independent defects, separated by intervention: an energy bias caused by the HDR weighting
inside the reconstruction (whole ROI 0.919 of the correctly resampled input; an analytic 1.5-px
bright bar loses 48.4 %), and a temporal residual driven by the jitter (zero offset removes it
entirely) and amplified about 35 % by the variance clamp. The residual on the bright pane is created
by the reconstruction: the raw sample there has a temporal standard deviation of 0.00000 while the
filtered colour has 0.248.

`r.TAA.ReconstructionMode=1` is the best candidate so far (residual −30 %, ROI energy 0.919 → 0.980,
distance to the instantaneous image −28 %, motion lag −6 %), with recorded costs: recovery after a
camera cut 1 → 16 frames and more bleed into the dark bars. `r.TAA.ClampMode=1` is rejected: the best
static number in the study, bought with history that never clears (peak error after a cut 10x, still
46x the baseline residual after 80 frames).

Full numbers: `.screenshots/flicker-elimination/10-isolate-taa-window/report.md` and notes.md.
Ledger rows H-04b, H-11..H-14 in `docs/analysis/flicker-hypothesis-ledger.md`.

**Correction (2026-10-08).** Four statements above need scoping; the text is left as written.
(1) "Amplified about 35 % by the variance clamp" holds on the boundary mask only. Over the whole
window rectangle the clamp contributes 48.5 % of the residual (0.00952 -> 0.00490 with no clamp)
and the AABB alone keeps 80 % of that (0.00862); on the bright mask either box alone keeps all of it
(notes.md, Correction (2026-10-08); ledger H-04c).
(2) "`r.TAA.ReconstructionMode=1` is the best candidate so far" was overtaken by followup-14, as
the proposal intended. As `r.TAA.ReconstructionHdrWeightExponent=0` it fails gates G1, G2, G6 and
G7 (ledger H-21..H-23) and stays opt-in. Its cut-recovery cost there is 16 -> 60 frames, under a
tolerance of 2 % of the converged mean rather than the 2 % of the 99th percentile used here.
(3) In this task's runs `r.TAA.BlendMode=1` held the weight at a hardcoded 0.04:
`r.TAA.CurrentFrameWeight` was read by nothing until the H-39 fix (followup-12, in `7eea6b95`). The
default is 0.04, so the numbers stand.
(4) The evidence package is git-ignored and local only. It keeps `report.md`, five JSON summaries
and the run manifests, not the dumps, per-frame CSV, masks, plots and logs listed under "Artifacts
and reproducibility", and every GPU variant is a single run (24 measured frames for a still-camera
variant, one pass of the motion sequence for a motion variant) rather than the shared protocol's
three runs of 200 + 256 frames. Only the baseline and `ReconstructionMode=1` were later re-measured
under that protocol (H-21).
