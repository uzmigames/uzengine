# Design: Validate an evidence-selected flicker correction

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
Write run artifacts under .screenshots/flicker-elimination/14-validate-flicker-correction/ with manifest.json, report.md, full-resolution linear dumps, exact per-frame CSV, annotated masks, comparison plots and run logs. Keep reusable harness code in scripts/. Link historical .screenshots/flicker-region and flicker-jitter evidence but account for the corrected PNG render indices. Do not overwrite previous packages. Repository documentation stays in English.

## Scope and completion
All checklist items start pending. No rendering implementation or capture execution is included in task creation itself. No commits, unrelated VFX changes, automatic archival or default visual tuning are authorized by this plan. Preserve old calibration backups. Diagnose known test failures instead of blanket filtering.

## Implementation and results (2026-09-09)

### What was implemented
`r.TAA.ReconstructionHdrWeightExponent` (default **1.0 = production**, 0 = no HDR weighting;
`taa_upscaler.cpp` and `taa_resolve_cs.hlsl`). It raises the reconstruction filter's HDR weight to
a power, so the intervention is continuous and exponent 0 is bit-identical to the existing
`r.TAA.ReconstructionMode=1`. Smallest reversible change addressing the demonstrated mechanism of
ledger H-11/H-12. Two further diagnostic CVars, `r.Material.RoughnessOverride` and
`r.Material.MetallicOverride` (both default -1 = off), force a material class into the G-buffer so
item 1.5's mirror/glossy coverage could be measured at all: Sponza contains no such surfaces.

### The gates
Frozen in the hypothesis ledger before the candidate existed, and not revised. **Both exponent 0.0
and 0.5 fail G1, G2, G6 and G7**; G3, G4 and G5 pass. The default is unchanged and the correction
is opt-in.

G7 fails only through its "G6 holds" clause: its own conditions all pass - the accumulated image moves *towards* the instantaneous input (no blur), the recorded exposure is identical, and the active lighting-path bitmask is identical in every ROI. It is not independent evidence of cheating, it is a restatement of G6.

**Correction (2026-10-08).** The "active lighting-path bitmask is identical in every ROI" clause rests on one capture per variant, `materials.json`, never repeated. The ledger's layout-bug audit lists its path bits as invalid (`docs/analysis/rendering-arch/flicker/flicker-hypothesis-ledger.md`, "Audit of the layout bug", "Results marked invalid" table), but the file was written on 2026-09-09 at 21:43 local, before that bug's window opened (TAA_EXTRA grew on 2026-09-10 at 08:13 local), and its path bits ([3]) are the value the post-fix slot dump reads (ledger H-29); the listing is corrected in ledger row H-23c (tasks.md 1.5). G7 still fails through its G6 clause; the verdict is unchanged.

| gate | threshold | exponent 0.5 | exponent 0.0 |
|---|---|---|---|
| G1 residual | -50 % on pose W `window` | -27.9 % FAIL | -35.4 % FAIL |
| G2 energy | within 2 % | +2.87 % FAIL | +6.63 % FAIL |
| G3 bright side | p99 within +5 % | -11.0 % pass | -62.0 % pass |
| G6 motion | recovery <= baseline + 5 frames | 41 frames FAIL | 60 frames FAIL |

### The finding that matters more than the verdict
Measured against the **correctly resampled reference** (the cycle-averaged raw input) rather than
against the baseline, the candidate is more accurate on every ROI measured:

| ROI | baseline error | exponent 0 | bright-side energy (1.0 = correct) |
|---|---|---|---|
| W `window` | 0.2199 | 0.0726 | 0.9715 -> 0.9947 |
| A `rear_door` | 0.0157 | 0.0080 | 0.4202 -> 0.7446 |
| C `small_window` | 0.2522 | 0.0812 | 0.9509 -> 0.9909 |

**G1 was mis-specified for the question it was meant to answer.** `std_mean` measures temporal
stability, not correctness: a bright feature reconstructed at its correct energy carries more
absolute variance than one crushed to 42 % of it, so restoring it *raises* the metric. On pose A
`rear_door` the candidate halves the error against the reference while doubling `std_mean`. The
gate was frozen and it fails, and it is reported as failed - but a future gate for this defect
should be stated against a reference, not against the previous frame.

The candidate's real costs are the ones G6 and H-22 name: light bleeding onto pixels whose correct
value is ~0 (about 6x more), and recovery after a cut going 16 to 60
frames.

### What stays open
The visual defect is **not** closed. About 65 % of the window residual is untouched by this
mechanism, and both clamp routes are now rejected (H-13, H-23). The next discriminating experiment
is recorded as ledger row **H-24**: a reversible-tonemap reconstruction, which is the variance
reduction the HDR weight approximates but is unbiased for the filter.

**Correction (2026-10-08).** Two statements above are out of date. (1) The AABB-only half of "both clamp routes are now rejected" rests on one motion run each of `baseline_aabb` and `combined_exp0_aabb` (`motion.json`, 2026-09-09, never repeated); on the static window residual alone they reach only -9.5 % and -35.4 % (`matrix.json`), short of the -50 % G1 asks for. (2) H-24 was rejected on paper the same day: the reversible tonemap is the same `1/(k + Y)` weighting (ledger H-24, H-24b); H-25, H-25b, H-26 and H-27 followed. The window's open next steps are listed in `docs/analysis/known-issues-and-partial-implementations.md`, "Static TAA residual", and since 2026-10-09 are owned by `phase0_sponza-flicker-correction`.
