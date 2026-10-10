# Design: Isolate GDF recentring from camera motion

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
Write run artifacts under .screenshots/flicker-elimination/13-isolate-gdf-motion/ with manifest.json, report.md, full-resolution linear dumps, exact per-frame CSV, annotated masks, comparison plots and run logs. Keep reusable harness code in scripts/. Link historical .screenshots/flicker-region and flicker-jitter evidence but account for the corrected PNG render indices. Do not overwrite previous packages. Repository documentation stays in English.

## Scope and completion
All checklist items start pending. No rendering implementation or capture execution is included in task creation itself. No commits, unrelated VFX changes, automatic archival or default visual tuning are authorized by this plan. Preserve old calibration backups. Diagnose known test failures instead of blanket filtering.

## Implementation and results (2026-09-09)

Region dump format 3 carries the unjittered view-projection, the previous frame's and the camera
position, so `regiondump.world_positions()` and `.project()` can follow a world point across frames;
`scripts/sponza-gdf-boundary.lua` crosses one clipmap axis at a time with a stop before and after
each crossing; `r.GI.GDF.FreezeCenter` and `r.GI.GDF.CenterPhaseOffset` are the two controls that
separate the grid from the trajectory.

Result: **supported**. The motion-compensated residual is 1.31-1.35x higher at recentre frames, the
elevation follows the grid when its phase is shifted, and the fixed-centre control removes the events
without changing the residual elsewhere. The ROI mean steps by 3-6x its ordinary local change at an
event. The ordinary motion residual is *not* caused by the field (H-20, rejected).

Two tracking bugs were found and fixed before any conclusion: a single mid-depth unprojection under
reversed-Z (21 px error, 10 % coverage) and the absolute-pixel comparison that carried the jitter and
a sub-centimetre camera/matrix offset into every sample. Coverage is now 88 % and is reported with
every number. Full tables in notes.md; ledger rows H-07b and H-20.

## Correction (2026-10-08)

The result above ("supported") is withdrawn. `run_gdf_motion_study.py` placed its moving-frame mask
at FIRST + seg x 140 + 41..100 with FIRST = 157, while `scripts/sponza-gdf-boundary.lua` counts its
segments from its on_start frame (logged `base_frame=37`): the mask ran 120 frames late, 160 of its
360 frames were still, and every recentre is a moving frame. With the windows the trajectory logs,
the near-event residual is 1.09x (shadowed wall) and 0.89x (lit wall) the rest, and the fixed-centre
control, which has no recentre at all, gives the same residual at the same frames (per frame
0.99943-1.00022 of it, 16 event frames). The phase-shifted runs show the same thing: the elevation
follows the frames selected, not the grid. A recentre therefore has no measurable effect on the
motion-compensated residual in this scope (ledger H-07c supersedes H-07b; H-40b voids H-40). H-20's
rejection stands, on corrected grounds (H-20b). The 88 % coverage is the mean over all dumped frames;
on the moving frames it is 76 % and 86 %. Full numbers, method and what was fixed in the harnesses:
notes.md, "Correction (2026-10-08)".
