# Design: Separate exposure and bloom from temporal HDR error

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
Write run artifacts under .screenshots/flicker-elimination/11-isolate-flicker-display/ with manifest.json, report.md, full-resolution linear dumps, exact per-frame CSV, annotated masks, comparison plots and run logs. Keep reusable harness code in scripts/. Link historical .screenshots/flicker-region and flicker-jitter evidence but account for the corrected PNG render indices. Do not overwrite previous packages. Repository documentation stays in English.

## Scope and completion
All checklist items start pending. No rendering implementation or capture execution is included in task creation itself. No commits, unrelated VFX changes, automatic archival or default visual tuning are authorized by this plan. Preserve old calibration backups. Diagnose known test failures instead of blanket filtering.

## Implementation and results (2026-09-09)

`scripts/flicker/display_model.py` replays the production tonemap chain offline and is validated
against the engine's own PNG (1 code value, 100 % of pixels, bloom off);
`scripts/test_display_replay.py` also asserts that it *fails* with the wrong exposure, the wrong
operator, or with bloom on, so the replay is never used outside what it models.
`scripts/run_display_study.py` runs the study and writes
`.screenshots/flicker-elimination/11-isolate-flicker-display/display-study.json`.

Results: the display counts are a property of the exposure, not of the scene (identical HDR at
0.5x / 1x / 2x gives 0.7 / 1.7 / 22.0 px > 40 per frame while the records stay bit-identical);
auto-exposure settles at 66.31 on pose W and is stable when still; bloom lifts the dark floor 2.4x
and halves local contrast, with an effect on the count that changes sign with the exposure;
`r.TAA.PreExposeWeights` is a no-op at exposure 1.0 and, at the real operating point, makes the
bright side oscillate 11x more while crushing the dark side to 2 % of its radiance;
`r.GI.Reflections=0` substitutes an unoccluded IBL lookup for the occluded SDF trace, which is why
it moved the counts (dark side 11x brighter, exposure 16x lower) without changing the oscillation.

Ledger rows H-02b, H-08b, H-15, H-16 in `docs/analysis/flicker-hypothesis-ledger.md`; full tables in
notes.md and the package report.
