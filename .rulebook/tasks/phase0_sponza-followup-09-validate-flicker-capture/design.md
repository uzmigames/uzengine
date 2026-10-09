# Design: Validate flicker capture and measurement

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
Write run artifacts under .screenshots/flicker-elimination/09-validate-flicker-capture/ with manifest.json, report.md, full-resolution linear dumps, exact per-frame CSV, annotated masks, comparison plots and run logs. Keep reusable harness code in scripts/. Link historical .screenshots/flicker-region and flicker-jitter evidence but account for the corrected PNG render indices. Do not overwrite previous packages. Repository documentation stays in English.

## Scope and completion
All checklist items start pending. No rendering implementation or capture execution is included in task creation itself. No commits, unrelated VFX changes, automatic archival or default visual tuning are authorized by this plan. Preserve old calibration backups. Diagnose known test failures instead of blanket filtering.

## Implementation (2026-09-09)

### Region dump v2 — the capture instrument

`uzengine.exe --region-dump <dir>` writes one file per rendered frame with per-pixel linear-HDR
records for up to eight screen rectangles:

```
--regions "window:770,150,192,512;shadow_wall:150,260,192,384"
--region-fields output|lighting|taa|full   --region-first N --region-last M
```

Files are `region_<frame>.bin` (format `UZR2`): a 64-byte header (frame, rect count, field mask,
records per pixel, flags, render and output resolution, jitter index and offset, exposure), the
rectangle table, the per-rectangle stamps, then the raw records. `scripts/flicker/regiondump.py`
reads them; `runtime/shaders/runtime/common/region_dump.hlsli` documents the record layout.

Field groups, in fixed order: `lighting` (2 records — SDF reflection radiance + roughness, world
normal + view distance, with `-1` marking "no geometry this frame"), `taa` (5 records — raw centre
sample + blend weight, plus-filtered colour + velocity.x, reprojected history + velocity.y, clamped
history + history-rejected flag, TAA output + weight exposure) and `output` (1 record — TAA output
+ weight exposure, the cheap preset for long sequences).

### Frame integrity

The GPU stamps every rectangle with the frame index it rendered (one stamp from the deferred
lighting pass, one from the TAA resolve, each written by the thread that owns the rectangle's
origin). The readback compares both stamps with the frame the CPU believes it is writing and sets
the file's flags accordingly; `regiondump.load_sequence` refuses a sequence with gaps, duplicates,
renamed frames or mismatched stamps. The readback pass runs at the end of the frame, independently
of TAA, so a run with `r.TAA=0` still produces files — flagged `taa_stamp_missing` rather than
silently carrying the previous frame's records. Frames still in flight are flushed at shutdown, so
the file set covers the whole requested range.

### Run manifest

`--run-manifest <path>` writes the executable path and hash, the project and scene, output and
render resolution, output encoding, frames rendered, CPU frame-time statistics (mean/min/p50/p95/max
from a configurable warm-up frame, `--timing-first`), the effective exposure, the camera pose, the
region-dump file and rejection counts, the CLI arguments and every registered CVar (plus the subset
that differs from its built-in default). `scripts/flicker/runner.py:source_fingerprint` adds a
SHA-256 over the shader sources and the analysis scripts.

### Measurement code

`scripts/flicker/metrics.py` and `analysis.py`. Exact full-resolution counts, undefined (not zero)
autocorrelation for a constant series, masks that exclude invalid samples, and a translation test
that separates a moving edge from a lighting change. `scripts/test_flicker_metrics.py` validates all
of it on synthetic sequences with known answers (22 checks); `scripts/test_region_capture.py` is the
behavioural harness that runs the engine and checks what the GPU actually wrote (23 checks). The
version-1 harness `scripts/test_region_dump.ps1` was removed: it validated a file format that no
longer exists.

### Verified record semantics (checklist 1.1)

Checked against captured data, not against the shader source, by
`scripts/run_flicker_field_check.py`:

* **Coordinate space** — a rectangle's screen coordinates address the same pixels as the PNG. Shown
  by rank-correlating the ROI's recorded radiance with the PNG luminance at the same coordinates and
  showing that one- and three-pixel shifts correlate worse. Bloom has to be off for this comparison,
  because it mixes neighbouring pixels; that mixing is itself measured and reported.
* **Radiometry** — the records are linear scene radiance: a 16x change of `r.Exposure` leaves them
  bit-identical (negative control against the run-to-run noise floor, which is zero — the renderer is
  deterministic at a given frame index), and the recorded values exceed the display range.
  `taa_output.w` is the TAA HDR-weight exposure (`r.TAA.ExposureScale`), a different quantity from
  the tonemap exposure in the header.
  **Correction (2026-10-08):** the run-to-run floor is not zero in general: separate processes are
  not bit-reproducible (`docs/analysis/flicker-hypothesis-ledger.md`, "The renderer is not
  bit-reproducible across processes"). The negative control holds on the run aggregate, which is
  what `scripts/test_region_capture.py` check 3 judges since this date.
* **Record identity** — the resolve's own blend equation, recomputed from the recorded filtered
  colour, clamped history, blend weight and weight exposure, reproduces the recorded output to
  9.2e-07 relative error. Any other meaning for those fields would not close that identity.
* **Units** — colours are linear RGB; Rec.709 luminance and the resolve's 4x-scaled YCoCg Y are
  provided separately and never mixed.

### Confound found while validating

Auto-exposure adapts with the frame time (`auto_exposure.cpp` uses `dt`), so **any** change in cost
moves the displayed image: instrumented and uninstrumented runs of identical settings differ by one
code value with auto-exposure on, and are bit-identical with the exposure fixed. Every
display-referred comparison in this programme fixes the exposure.

**Correction (2026-10-08):** "bit-identical with the exposure fixed" was measured on each recorded
run, not guaranteed: two processes are not bit-reproducible, so check 5 of
`scripts/test_region_capture.py` now compares the run aggregate and reports bit-identity.
