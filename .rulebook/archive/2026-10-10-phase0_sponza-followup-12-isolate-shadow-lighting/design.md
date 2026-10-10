# Design: Isolate lighting flicker on shadow walls and rear door

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
Write run artifacts under .screenshots/flicker-elimination/12-isolate-shadow-lighting/ with manifest.json, report.md, full-resolution linear dumps, exact per-frame CSV, annotated masks, comparison plots and run logs. Keep reusable harness code in scripts/. Link historical .screenshots/flicker-region and flicker-jitter evidence but account for the corrected PNG render indices. Do not overwrite previous packages. Repository documentation stays in English.

## Scope and completion
All checklist items start pending. No rendering implementation or capture execution is included in task creation itself. No commits, unrelated VFX changes, automatic archival or default visual tuning are authorized by this plan. Preserve old calibration backups. Diagnose known test failures instead of blanket filtering.

## Implementation and results (2026-09-09)

New instrumentation: a `lighting_extra` region-dump field group (4 records — direct light after
shadowing + shadow visibility, diffuse indirect after AO + AO, specular indirect after the BRDF +
metallic, emissive + environment MIS + the active-path bitmask), the diagnostic CVars
`r.GI.Reflections.FreezeSeed` (covers the deferred reflections and the world gather) and
`r.GI.RadianceCache.FreezeAfterFrame`, and `scripts/rendering/run_pass_attribution.py`,
`scripts/rendering/run_shadow_lighting_study.py`, `scripts/rendering/test_lighting_components.py`.

The path bitmask earned its place immediately: the first version of the radiance-cache freeze
skipped `UpdateProbes` entirely, which left `HasProducedResult()` false and silently switched the
consumer to the sky-SH fallback. The recorded paths showed neither the gather nor the reflections
running, so the control was rejected and rewritten to hold the converged buffer instead.

Results: the window and the right-hand shadowed arcade are forward translucent geometry (0.0 % and
0.9 % of their colour explained by the deferred components); the rear door is mixed; the shadowed
walls and the sunlit floor are deferred and have different sources from each other. Full tables in
notes.md and `.screenshots/flicker-elimination/12-isolate-shadow-lighting/report.md`; ledger rows
H-06b, H-17, H-18, H-19.

**Correction (2026-10-08).** "Full tables in notes.md" is wrong: the committed notes.md (7eea6b95,
the only version in git) begins with the 2026-09-15 door section and holds none of the 2026-09-09
tables. They are in `.screenshots/flicker-elimination/12-isolate-shadow-lighting/report.md` and
`shadow-lighting-study.json` (git-ignored, local only) and, in summary, in ledger rows H-06b, H-17,
H-18 and H-19, whose rounded ratios H-06c restates. The diagnostics added after this section are
listed in `docs/guides/build-and-deployment.md`. The gather work of 2026-09-10..14
(`r.GI.FinalGather.FreezeAfterFrame`, `r.GI.FinalGather.SampleRotation`, `r.GI.SDF.SampleFrameOffset`,
`--component-replay`) is recorded in ledger H-28..H-36 and
`docs/analysis/rendering-arch/flicker/gather-sampling-candidate-preregistration.md`, not in notes.md; the door work
(`r.Translucency.DepthTest`, the translucent and plus-filter records) is in notes.md and ledger
H-43..H-48. The closure record (2026-10-08) ends notes.md.
