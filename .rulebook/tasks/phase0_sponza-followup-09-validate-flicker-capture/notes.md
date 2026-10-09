# Notes — phase0_sponza-followup-09-validate-flicker-capture

2026-09-09. The task is diagnostic: build a capture and measurement path that can be trusted, and
measure the baseline it will be compared against. No rendering behaviour was changed; the only
engine changes are diagnostics and a manifest. Nothing was committed.

Evidence package: `.screenshots/flicker-elimination/09-validate-flicker-capture/`
(`report.md`, `baselines.json`, `field-check-W.json`, `csv/`, `masks/`, `manifests/`).
Ledger: `docs/analysis/flicker-hypothesis-ledger.md`.

## 1.1 Field semantics — verified against data, not against the shader source

`scripts/run_flicker_field_check.py`, pose W, 32 frames, fixed exposure, bloom off. All 8 checks pass.

| claim | how it was verified | result |
|---|---|---|
| the rectangle's coordinates address the same pixels as the PNG | bin pixels by quantised linear RGB and measure the spread of the display value inside each bin; a shifted crop must be worse | aligned spread 0.005 code values over 310 bins vs 5.0, 14.0, 29.6, 63.8 when shifted by (0,1), (1,0), (0,3), (3,0) |
| the records are linear scene radiance, not pre-exposed | run at `r.Exposure=0.25` and `4.0` and compare, with a repeated identical run as the noise floor | records bit-identical (delta 0, noise floor 0); mean energy ratio 1.0000 |
| the header exposure is the tonemap exposure | compare with the manifest | 0.25 and 4.0 as requested |
| `taa_output.w` is a different quantity | read both | weight exposure 1.0 (`r.TAA.ExposureScale`) next to the frame exposure |
| the record identities are what the header says | recompute the resolve's HDR-weighted blend from the recorded filtered colour, clamped history, blend weight and weight exposure | reproduces the recorded output to 9.2e-07 relative error |
| units | Rec.709 luminance and the resolve's 4x-scaled YCoCg Y are provided separately | tested in `test_flicker_metrics.py` |

**Correction (2026-10-08).** "delta 0, noise floor 0" in the second row describes that pair of
processes only. Separate processes are not bit-reproducible (ledger, "The renderer is not
bit-reproducible across processes"); on 2026-10-05 an exposure pair differed by up to 1.0e-3 while its
repeat pair agreed exactly, which failed `test_region_capture.py` check 3. The claim holds on the run
aggregate (Closing review).

**Reconciliation of the 2026-09-09 wording.** The report described "bright constant-centre pixels"
(linear) and "dark bars" (display). Both are true and they are *disjoint pixel sets* on the two
sides of the same edge. Measured at pose W, fixed exposure 1.0, bloom off, 32 frames:

| population | pixels | linear std | linear mean | display mean | max display jump |
|---|---|---|---|---|---|
| pane (raw bright every frame) | 29 156 | 0.0232 | 8.046 | 232.6 | 4.1 |
| bar (raw dark every frame) | 64 021 | 0.00018 | 0.0025 | 1.8 | 37.3 |
| edge (raw toggles with the jitter) | 5 127 | 0.0487 | 1.177 | 165.0 | 73.6 |
| top 1 % by linear std | 984 | 0.375 | 7.197 | 230.2 | 17.6 |
| display \|dL\| > 40 | 13 | 0.0255 | 0.039 | 35.3 | 73.6 |

Overlap between the top 1 % linear pixels and the display crossings: **0 px**.

## 1.2 Frame integrity — GPU stamps

Region dump v2. Every rectangle is stamped by the deferred lighting pass and by the TAA resolve with
the frame index the GPU rendered; the readback compares both with the frame the CPU believes it is
writing and flags the file. `scripts/test_region_capture.py` (23 checks, all pass) measures:

* the requested range is complete, unique and stamp-matched, including the frames flushed at
  shutdown (`files == requested range`);
* the lighting (render) and TAA (output) domains coincide at 100 % screen percentage, and the file
  records both so a mismatch is visible;
* with `r.TAA=0` the resolve never writes: all 13 files are produced **and flagged**
  `taa_stamp_missing`, and the reader refuses the sequence — the failure mode the earlier format had
  (returning last frame's records) is now impossible;
* the raw centre sample is identical between frames sharing a jitter index (4.3e-06) and different
  between neighbours (0.203), which is measured rather than assumed;
* instrumentation neutrality: **at fixed exposure the presented image is bit-identical** with and
  without the dump; cost 125.3 ms vs 126.0 ms per frame with screenshots active in both arms
  (23.2–23.7 ms without screenshots, from the baseline manifests).
  *Correction (2026-10-08):* bit-identical in every recorded run, but not something two processes
  guarantee; check 5 now compares the run aggregate and reports bit-identity (Closing review).

**Confound found here:** with auto-exposure on, the same comparison differs by 1 code value.
Auto-exposure adapts with `dt` (`auto_exposure.cpp`), so any change of cost moves the displayed
image. Every display-referred comparison in this programme must fix the exposure. Bloom does the
same spatially: with bloom on, the display value is not a per-pixel function of the recorded
radiance (rank correlation 0.645 vs 0.710 with bloom off).

## 1.3 Metric validation — synthetic sequences

`scripts/test_flicker_metrics.py`, 22 checks, all pass. Constant sequences report **undefined**
autocorrelation (not 0 or 1); a sinusoid's amplitude is recovered within 2 % and its period exactly
(4, 6, 8, 12, 16); a one-pixel translation of an edge is classified as `translation` with 99 % of the
change explained, while the same edge with a brightness change is classified as `lighting`; counts
are exact and strictly-greater; a 2x2 subsample times four is shown to disagree with the exact count;
masked samples are excluded; missing, duplicated and renumbered frames all fail the integrity check;
a version-1 file is refused.

## 1.4 Run manifest

`--run-manifest <path>`: executable path and FNV-1a-64 hash, project, scene, output and render
resolution, output encoding, frames rendered, CPU frame-time statistics (mean/min/p50/p95/max from
`--timing-first`), effective exposure, camera pose, region-dump file and rejection counts, the CLI
arguments, and every registered CVar plus the subset that differs from its default.
`scripts/flicker/runner.py:source_fingerprint` adds a SHA-256 over 183 shader and analysis sources.
Every capture in the package carries its manifest in `manifests/`.

## 1.5 Baselines

Two poses, three independent runs each, 200 warm-up frames then 256 measured frames (237..492),
camera still, full resolution, no subsampling. Primary metric `std_mean` = mean over ROI pixels of
the temporal standard deviation of the TAA output's linear Rec.709 luminance.

| pose / ROI | mean radiance | std_mean | std_mean / mean | period | run-to-run range |
|---|---|---|---|---|---|
| W / `window` | 2.449 | 9.52e-03 | 0.39 % | 8 | 0.0014 % |
| W / `shadow_wall` | 1.66e-04 | 9.46e-06 | 5.69 % | **24** | 0.0027 % |
| A / `rear_door` | 1.17e-02 | 1.77e-04 | 1.52 % | 8 | 0.0051 % |
| A / `right_shadow` | 3.72e-02 | 4.20e-04 | 1.13 % | 8 | 0.0028 % |
| A / `lit_floor` | 2.37e-01 | 9.99e-04 | 0.42 % | 8 | 0.0015 % |

New results that the earlier packages did not have:

* **the static residual is not confined to the window** — the rear door, the right shadow and even
  the sunlit floor all carry a period-8 residual (ledger H-10, rejects the "window only" reading);
* **the deeply shadowed wall has a different period (24, in all three runs)**, which matches the
  GTAO temporal pattern rather than the jitter cycle (ledger H-09, open, with the discriminating
  experiment named for followup-12);
* **relative amplitude rises as a region darkens**, so absolute radiance differences must not be
  compared across regions;
* **run-to-run reproducibility is 0.001–0.005 %** because the renderer is deterministic at a given
  frame index: three runs measure process reproducibility, not stochastic noise.
  *Correction (2026-10-08):* the reason given is wrong. The renderer is not bit-reproducible across
  processes (ledger, "The renderer is not bit-reproducible across processes": up to 795 of 786 432
  recorded values differ, by up to 1.6e-2; its 2026-09-15 correction adds that aggregates can differ
  too). The 0.001–0.005 % is a measured property of these aggregates, so the thresholds of 1.6
  stand, for the 2026-09-09 binary they were measured on (Closing review).

Caveat recorded in the package: pose W `shadow_wall` sits at the measurement floor (mean radiance
1.7e-4, mean frame-to-frame change 1.2e-6). It is a good noise reference but a poor stand-in for
"shadowed walls"; followup-12 should add a shadowed wall that receives bounce light.

*Note (2026-10-08):* followup-12 added `wall_bounce`, but it is darker than `shadow_wall` and also at
the measurement floor, so the caveat is still open there (Closing review).

## 1.6 Tolerances and ledger

`docs/analysis/flicker-hypothesis-ledger.md` holds the protocol, the predeclared decision rule and
ten rows (H-01..H-10). Thresholds (a later task may only call a difference an effect above these):
W `window` 4.8e-4, W `shadow_wall` 4.7e-7, A `rear_door` 8.9e-6, A `right_shadow` 2.1e-5,
A `lit_floor` 5.0e-5 — all `max(3 x 95 % half-width, 5 % of the mean)`, the second term binding.

Carried hypotheses were re-checked against rule 4 (no rejection on a post-tonemap threshold count
alone). H-01 was supported only by display counts; it is now supported in linear radiance at jitter
length 8 (per-frame series peaks at lag 8 with r 0.74; frames sharing a jitter index differ by 1.4 %
of the neighbouring-frame difference, and by 0.003 % before TAA). Lengths 1/4/16 remain display-only
and are marked as such. H-02 and H-08 were already decided on linear metrics.

## Engine changes (diagnostics only)

* Region dump v2: multiple named rectangles (`--regions`), field presets (`--region-fields`), GPU
  frame stamps, a readback pass that runs independently of TAA, a shutdown flush, and a self-
  describing file format. Record layout documented in
  `runtime/shaders/runtime/common/region_dump.hlsli`.
* `--run-manifest`, `--timing-first`.
* `--screenshot-frames` file naming already fixed on 2026-09-09 (file N = render N).
* `scripts/test_region_dump.ps1` was removed: it validated the version-1 format, which no longer
  exists. `scripts/test_region_capture.py` replaces it with 23 behavioural checks.

## 2.3 Test results

| suite | result |
|---|---|
| `python scripts/test_flicker_metrics.py` | 22 passed |
| `python scripts/test_region_capture.py` | 23 passed (9 engine captures) |
| `runtime/build/bin/test_runtime.exe` | 7628 tests, **86 failures — all pre-existing and outside this task's code** |

Restricting the C++ suite to the code this task touched
(`TaaJitter*:*PerFrameCB*:*ConstantBuffer*:*GiGpu*:*RadianceCache*:*SceneRenderer*:*Upscaler*:*Taa*`)
leaves exactly two failures, both stale expectations that contradict the committed source:

* `BuildPerFrameCB_StructSize.PerFrameCB_Is768Bytes` — expects 768 bytes while the committed
  `constant_buffers.h` already asserts `sizeof(PerFrameCB) == 1024`.
* `PerFrameCB_IBL.PrefilteredMipLevelsDefaultIsFive` — expects 5; the struct default is 8 and
  `r.IBL.PrefilteredMipLevels` defaults to 6.

The other 84 failures were sampled and characterised, none in code this task touched:

* 26 `ShaderCrossBackend/ShaderCrossBackendTest.Compile/*` — "Shader source not found on disk"; the
  fixture searches two and three levels up from the executable and does not cover every shader
  directory. `fullscreen_vs` in the same suite passes, so it is a fixture path problem, not a
  compilation problem. (`SPIRV_SM60_deferred_lighting_ps` fails for the same missing-file reason,
  not because of the region-dump include added by this task.)
  *Correction (2026-10-08):* the suite has 21 cases and 13 fail ("26" counted each failure twice,
  inline and in gtest's closing list). The fixture does find the three `lighting/` shaders; what it
  lacks is `gbuffer/` in its subdirectory list (`test_shader_cross_backend.cpp:56-57`), where
  `mesh_vs`, `mesh_ps`, `gbuffer_ps` and `gbuffer_debug_ps` have lived since bd4db199 (2026-03-31).
  And `SPIRV_SM60_deferred_lighting_ps` does not fail for a missing file: its source is found and does
  not compile without `UZ_BINDLESS`. `shadow.hlsli` uses `shadowAtlasTexture` (lines 156 and 251
  today), which no non-bindless block declares, and `deferred_lighting_ps.hlsl:1381` reads
  `gBufIndices` outside `#ifdef UZ_BINDLESS` (since 1d2e97da, 2026-04-04). followup-14 recorded the
  compile error; it is unrelated to the region-dump include.
* VFX, particle, trail, LOD, significance, frustum-cull and collision suites — stale expectations
  against the in-flight VFX work, e.g. `UZ_VFXLodSettings.DefaultValues_DistanceZeroIsFifty`
  expects 50 while the current default is 5000.
* `ProjectLoaderTest.CvarsBlock_NumbersAndBoolsParsed`, `SceneLoaderTest.*`, `LuaSceneAPITest.*`,
  `RuntimeViewportTest.*`, `HeightFogComponent.*`, `RenderingParity.HeightFogDefaults_*`,
  `SkyAtmosphereComponent.*` — expectation drift (e.g. the loader now returns CVar overrides in a
  different order).

Diagnosis of the two constant-buffer/prefiltered-mip failures is recorded here; fixing them belongs
to followup-14 item 1.6, which owns them. This task did not touch the VFX files.

*Correction (2026-10-08):* followup-14 item 1.6 diagnosed both tests again and changed neither ("not
changed here", followup-14 notes, "Test run (items 1.6 and 2.3)"). Both still failed in the full run
of 2026-10-08 12:45 and were fixed later that day by 8246ab5c (Closing review).

## Capture reliability, targeted pass (2026-09-15)

Scoped work on the three things that kept going wrong, without rebuilding the harness.

**Wrong automatic criteria.** The decision rules now live in `scripts/flicker/criteria.py` and are
exercised by `scripts/test_criteria_logic.py` (17 checks, each with the input it must accept AND the
input it must reject). The rules encode the mistakes actually made here: a one-sided "no worsening"
that an improvement cannot fail, a two-sided control that movement in either direction fails, a
relative gap that never divides by whichever value happens to be smaller, per-pixel-then-mean
oscillation rather than the reverse, and `exceeds_scale` for the case that bit hardest — "100x the
repeatability floor" degenerates to "> 1e-10" on a deterministic renderer, where the floor is
genuinely zero. That vacuous gate in `test_correction_gates.py` is fixed to a fraction of the
signal's own scale, with the floor still reported for when it is not zero.

*Correction (2026-10-08):* the floor is not genuinely zero: the renderer is not bit-reproducible
across processes (ledger, corrected 2026-09-15). The fix described here, a fraction of the signal's
own scale, is what keeps the gate meaningful either way.

**Preserving what is needed.** One retention rule, in `runner`: `RETAINED_REPEAT = 0`,
`release_repeat_dump()` and `require_region_dir()`. `reuse_repeat_range()` makes `--reuse` re-read
what was actually retained and say that it covers one repeat rather than the worst of several —
previously reuse asked for dumps the same script had deleted.

**Reproducibility.** `runner.harness_stamp()` records the binary's path, size and modification time
in each study's report, because a full evaluation once ran against an engine that did not contain the
change under test. `run_capture` also now fails on two things it used to pass through: a shader that
failed to compile (shaders compile at load, not in `zig build`, so a broken one looks like a clean
build) and `DXGI_ERROR_DEVICE_REMOVED`, which struck mid-batch and surfaced only as a raw exit code.

## A re-analysis overwrote full-run reports (2026-09-15)

`--reuse` re-reads only the retained repeat, and three studies wrote that single-repeat re-analysis
under the same filename as the full run: `door_stages.json`, `wall_output.json` and
`wall_motion_gate.json`. Each time, three-repeat evidence on disk was silently replaced by one-repeat
evidence. No verdict changed — repeats in this renderer are bit-identical — but a report whose
contents no longer match what it claims to cover is exactly the failure this task exists to stop, and
it was introduced while fixing retention.

*Correction (2026-10-08):* "repeats in this renderer are bit-identical" does not hold in general
(ledger, corrected 2026-09-15). No verdict changed because the three-repeat logs give the same
verdicts as the one-repeat JSONs (`29-wall-output`: `clip_wider` 8.0432e-06 in both;
`28-door-stages`: only `no_translucency` believable in both).

**Fixed as a rule, not per script.** `runner.report_path(out, name, reuse)` sends a reuse run to
`<name>.reuse.json`, and the five studies that write reports use it.

**Evidence preserved.** The three-repeat run logs, which carry the worst-repeat tables the overwritten
JSON used to hold, are kept beside the reports:

* `28-door-stages/door_stages.three_repeats.log`
* `29-wall-output/wall_output.three_repeats.log`
* `31-wall-motion-gate/wall_motion_gate.three_repeats_pre_censoring.log`

The door and wall-output JSONs on disk are therefore the one-repeat reuse version and are **not**
re-captured, since the review asked not to restart studies; their three-repeat results are in those
logs. The motion gate is re-run in full so its report carries all three repeats together with the
censoring verdict.

*Correction (2026-10-08):* the rule is not universal. 22 study scripts call `report_path` today, but
`scripts/run_candidate_evaluation.py` re-reads one repeat on `--reuse` (line 132) and writes the fixed
name `candidate_<phase>.json` (line 252), so a reuse run there would still replace a three-repeat
report (`22-candidate/candidate_core.json`). The door and wall-output JSONs also still declare
`"repeats": 3` while holding the one-repeat re-analysis; `.screenshots/` is git-ignored, so they are
noted here and not rewritten.

**Two criteria added to `flicker/criteria.py` with tests (25 checks now):**
`recovery_censored` — a recovery equal to the capture length did not recover, and two censored values
are not equal recoveries; `recovery_tolerance_usable` — a recovery tolerance below the region's own
noise can never be met. Both came from `shadow_wall`, whose G6 "pass" was 82 of 82 frames in both
conditions with a tolerance of 3.2e-06 against an oscillation of 8.7e-06.

**Still open in this task:** capture identity. `runner.capture_identity` now inherits the executable's
content hash from each capture's own manifest, and reports use it; older reports written before that
change still carry the analysis-time stamp.

*Closed (2026-10-08):* see Closing review.

## Closing review (2026-10-08)

What was still open, checked against the tree (HEAD 8e3470f8; 8246ab5c, committed later the same day,
fixed the two stale gtests) and the data on disk. Nothing here ran the engine.

**Capture identity: resolved for the flicker studies.** `runner.capture_identity`
(`scripts/flicker/runner.py:110-135`) copies the `executable` block of the capture's own
`manifest.json`, which holds the whole-file FNV-1a-64 the engine computes (`HashFile`,
`runtime/tools/uzengine.cpp:590-602`, written by `WriteRunManifest` at line 626); 30 study scripts
call it. Of the 69 report JSONs under `.screenshots/flicker-elimination/` that carry an executable
identity, 62 inherit it from a manifest and 7 do not: `28-door-stages/door_stages.json` and
`30-firefly-reassessment/fireflies.json` (path, size and mtime, written on 2026-09-15 before
`capture_identity` existed) and `36-door-coverage-depth/occluder_depth.json`, the three
`37-debug-views*/debug_views.json` and `46-gdf-sampling-side/gdf_sampling_side_accum.json` (an
analysis-time stamp). The capture directories behind all seven keep their `manifest.json` with the
engine hash, so the identity is recoverable; `.screenshots/` is git-ignored and the reports were left
as written. Residuals:

* `runner._hash_file` (`scripts/flicker/runner.py:66`) stops at 64 MiB although its docstring
  promises the file's SHA-256. Every stamp with a `sha256` written from 2026-09-15 to 2026-10-05
  records an exe of 80.0-81.1 MB, so those hashes cover a prefix. The exe has been about 11.6 MB since
  2026-10-07 at the latest (11 555 840 bytes in the `sweep-b` stamp of that day, 11 572 224 bytes for
  the build of 2026-10-08 15:25, with a separate 73 MB `uzengine.pdb`) and is hashed whole.
* `scripts/run_gdf_sampling_side_accum.py:157` re-reads earlier captures and stamps only the binary
  present at analysis time; `scripts/run_debug_view_check.py:86` stamps before capturing, in the same
  run.
* `scripts/run_candidate_evaluation.py` can still overwrite a full report on `--reuse` (above).
* `capture_identity` is opt-in: the October lighting harnesses' reports (e.g.
  `test-env-mis/env_mis.json`, `test-ddgi-dedup/ddgi_dedup.json`, `test-split-sum/split_sum.json`)
  carry no executable identity, although their capture directories keep the manifests.

The first three need small changes in shared or other tasks' scripts; the closing records them, and
the known-issues flicker entry lists them until they are applied.

**Two more residuals of this task's scripts (found in the review of this closing, 2026-10-08).**

* `scripts/test_flicker_metrics.py` has 26 checks today (followup-13 added four reprojection checks)
  and 11 of them error out (run on 2026-10-08, no engine; first recorded on 2026-09-18 in followup-01's
  notes, "Support in the black regions — result"). Every fixture that writes a format-2/3 file
  carrying only the LIGHTING, TAA and OUTPUT groups (fields 3, 5 and 4) is refused by
  `regiondump.resolve_layout` (`scripts/flicker/regiondump.py:209-215`): all three `LEGACY_LAYOUTS`
  match its record count, although they agree on every group the file carries. Real format-2/3
  captures with those masks are unreadable for the same reason. followup-01's closing hands over the
  reader change (accept the matches when they agree on every carried group); on a copy of
  `scripts/flicker/` it restores 26/26, with `test_region_format.py` 12/12 and `test_criteria_logic.py`
  28/28 unchanged.
* `scripts/report_flicker_baselines.py:97-102` writes a fixed paragraph into every `report.md` it
  generates: the runs "agree to within 0.005 %" and "the renderer is deterministic at a given frame
  index". A re-measured package would repeat both whatever its data say, so the paragraph has to state
  the measured range and drop the determinism claim before the baselines are re-captured.

**Fixes handed over with this closing** (none applied here; shared or other tasks' scripts):

* `scripts/flicker/runner.py:66-78`: `_hash_file` hashes the whole file (drop `limit`; the only
  caller is `harness_stamp`, line 103).
* `scripts/run_gdf_sampling_side_accum.py`, in `run()` before `rows = []` (line 101):
  `report['captures'] = [{'name': c['name'], 'repeat': c['repeat'], 'identity': c.get('identity') or
  runner.capture_identity(c['dir'])} for c in rep['captures']]`; the `harness` stamp stays.
* `scripts/run_debug_view_check.py:100-101`: each mode's row also gets `'identity':
  runner.capture_identity(work)`; the report-level stamp (line 86) stays.
* `scripts/run_candidate_evaluation.py:252`: `path = runner.report_path(args.out,
  f'candidate_{args.phase}.json', args.reuse)`.
* `scripts/flicker/regiondump.py:209-211` (the change followup-01's closing hands over): keep the
  legacy matches, and reject only when they disagree on a group the file carries
  (`carried = {tuple(n for i, n in enumerate(c) if fields & (1 << i)) for c in matches}`, then
  `if len(carried) != 1:`).
* `scripts/report_flicker_baselines.py:97-102`: print the largest measured relative range instead of
  "0.005 %" and drop "the renderer is deterministic at a given frame index".

**`scripts/test_region_capture.py`: checks 3 and 5 no longer require per-value identity between two
processes.** Check 3 judged the exposure pair's maximum |delta| against one repeat pair's maximum
(`<= max(noise, 1e-4) * 4 + 1e-6`), and check 5 required `max(diffs) == 0` on the PNGs of two
processes. Two processes can land in states whose records differ (ledger), and check 3 failed that way
on 2026-10-05: exposure pair 1.0e-3 apart, repeat pair identical. Both now judge the run aggregate,
mean |delta| below `UNCHANGED_FRACTION` = 1e-3 of the signal's own mean (check 5 over the frame and
each dumped rectangle, a rectangle judged against the larger of its own level and the frame's), and
report the maximum, the repeat-run floor and bit-identity. Proved without the engine on the two work
dirs the harness kept on 2026-10-05 (merge build `fnv1a64:7c934918de920a4a`, A.10 build
`fnv1a64:431f6f66a74eab48`):

| case | old criterion | new criterion |
|---|---|---|
| today's `main()` replayed on each kept set (`capture()` returns the kept dirs) | — | 23/23 and 23/23 |
| the harness's own pairs, both sets | ok | ok (the pairs are bit-identical) |
| the 2026-10-05 failure mode rebuilt from the kept data: floor pair identical, compared pair in two states | FAIL (3.84e-3 and 7.83e-3 against 4.0e-4) | ok (4.8e-7 and 1.6e-6 of the mean record 1.981) |
| the logged failing run (max 1.0e-3, floor 0; dir not kept) | FAIL | ok: the mean is at most the max, 5.1e-4 of 1.981 |
| synthetic pre-exposure: all records / red only / 1 % / 0.1 % of pixels; NaN records | — | FAIL 0.94 / 0.68 / 0.037 / 0.0037; FAIL |
| auto-exposure pair, a real cross-process display difference used as a stand-in | FAIL (max 1 code value) | ok (frame 4.6e-4 on 40.4 and 6.9e-4 on 40.3 code values) |
| merge vs A.10 build at fixed exposure | FAIL (max 6) | FAIL (frame 8.0e-3 on 5.31, 1.5e-3) |
| synthetic dump side effects: window +1 code value / window zeroed / black door lit to 10 / 0.1 % sparse +1 flips | — | FAIL / FAIL / FAIL / ok |

**Determinism.** The renderer is not bit-reproducible across processes (ledger, corrected
2026-09-15). The statements in this file and in `design.md` that it is deterministic, or that the
run-to-run floor is zero, are corrected in place. The repeatability figures and the thresholds of 1.6
are measurements of aggregates and stand, for the 2026-09-09 binary only: nothing re-measured them
after the October engine changes (b8ff5077 sky capture, 975fc3c9 mesh winding, 513e2983 decals,
c14215ed split-sum, among others). The package's `report.md` repeats the claim; it is git-ignored and
was left as written.

**Where each leftover lives now.**

* H-09 (lag 24 on `shadow_wall`): its prediction, the same origin as the window residual, is rejected
  by H-17 (deferred, 97.5 % diffuse indirect; the final gather's per-frame sampling with about 40 %
  from GTAO) and H-28c (freezing the gather output takes the wall from 8.59e-06 to 1.56e-06, about
  82 %; the 100.1 % there is relative to the original intervention's drop); H-36 shows that the
  gather's frame-parity structure (H-31) is not what drives it. The lag-24 / GTAO attribution itself
  was never tested: H-17's GTAO-off runs were 16 frames, shorter than one period, and the 6 x 4
  pattern is hardcoded (`gtao_cs.hlsl:241-243`). A superseding ledger row, H-09b, records this; the
  period test goes to the proposed `phase0_sponza-flicker-correction` (followup-12, which closed the
  same day, re-homed it there: its notes, "Closure record (2026-10-08)").
* The shadowed-wall caveat of 1.5: followup-12 added `wall_bounce` (620,300,128x384), but it is
  darker than `shadow_wall` (radiance 5.55e-05 vs 1.64e-04, temporal std 1.58e-06 vs 3.82e-06,
  `12-isolate-shadow-lighting/report.md`), followup-12 item 1.7 puts both at the measurement floor,
  and the ledger has no threshold for it. No shadowed-wall ROI above the floor exists; open,
  owner: the proposed `phase0_sponza-flicker-correction` (re-homed by followup-12's closure).
* Baselines and thresholds on the current build: open, owner: the proposed
  `phase0_sponza-flicker-correction` (re-homed by followup-12's closure), which needs them before any
  further flicker decision; the fixed paragraph of `report_flicker_baselines.py` (above) is corrected
  first.
* `PerFrameCB_Is768Bytes` and `PrefilteredMipLevelsDefaultIsFive`: fixed on 2026-10-08 by 8246ab5c.
  The 13 `ShaderCrossBackend` failures: known-issues "Stale unit tests"; no existing task owns the
  fixes, so one (`phase0_test-suite-preexisting-failures`) is proposed with the closing.
* `test_flicker_metrics.py`'s 11 errors: the shared reader change handed over by followup-01's
  closing; the known-issues flicker entry lists it, with followup-14 as owner, until it is applied.
  Then `python scripts/test_flicker_metrics.py` (no engine) has to read 26/26.
* The other pre-existing failures belong to their areas: the VFX, particle and editor suites to the
  VFX work in the working tree, the fog and sky defaults as recorded in
  `docs/analysis/lighting-block-a-2026-10-05.md`. None is in code this task touched.

**Update (2026-10-09, final check of this closing; nothing here ran the engine).**

* Owners now exist (created 2026-10-09, status pending): `phase0_sponza-flicker-correction` holds
  the shadowed-wall ROI above the floor (its item 1.1), the baselines and per-ROI thresholds on the
  current build (1.2) and the GTAO period test of H-09b (1.3); `phase0_preexisting-test-failures`
  holds the 13 `ShaderCrossBackend` failures (1.4). The latter is the task proposed above as
  `phase0_test-suite-preexisting-failures`. followup-12's `.metadata.json` still reads in-progress;
  its "Closure record (2026-10-08)" is what re-homed the flicker items.
* The first five "Fixes handed over with this closing" were applied by the closing integration of
  2026-10-08 (uncommitted on 2026-10-09), and the known-issues flicker entry records them as fixed; in
  the working tree `capture_identity` sits at `scripts/flicker/runner.py:105-130`. Run on 2026-10-09
  without the engine: `test_flicker_metrics.py` 26/26 (11 errors with the reader at HEAD 5fcd62e5),
  `test_region_format.py` 12/12, `test_criteria_logic.py` 34/34, and today's
  `test_region_capture.main()` replayed on both kept sets, 23/23 and 23/23 again. Still open:
  `scripts/report_flicker_baselines.py:97-102`, owner `phase0_sponza-flicker-correction` (item 1.2),
  to be fixed before its re-measurement.
* The register suggested for the non-bindless `shadowAtlasTexture` (known-issues "Stale unit tests")
  has to be t3, not t5: the SM5 path binds the cascade atlas at t3
  (`runtime/engine/renderer/core/params/shadow_data_params.cpp:13`) and the IBL cubes and LUT at
  t5-t7 (`runtime/engine/renderer/core/params/per_frame_params.cpp:10-12`,
  `runtime/engine/renderer/passes/lighting/lighting_pass.cpp:116-122`); t3 is free in the shader's
  non-bindless block (`deferred_lighting_ps.hlsl:51-58`).
* The census above counts reports that carry the inherited block or a stamp. Three 2026-09-09
  reports embed the whole capture manifest instead (`09-validate-flicker-capture/field-check-W.json`,
  `10-isolate-taa-window/raw-reference.json` and `taa-model-validation.json`), so they carry the
  engine's hash as well; the seven listed above remain the only reports without it.
* Left before closing: `python scripts/test_region_capture.py` on the current build. Its changed checks
  3 and 5 have run only on the captures kept on 2026-10-05.

## Close-out runs (2026-10-09) — the task stays open

On the Debug build of 2cc2e85c (uzengine.exe 2026-10-09 08:53, fnv1a64:c4cf401970c78226): `scripts/test_region_capture.py` gave **22/23** (`.screenshots/flicker-elimination/followup-closeout-20261009/f09-region-capture.log`). The failing
check, "at fixed exposure the dump leaves the presented image unchanged (run aggregate)", compared one
process with the dump against one without on a fixed bound: frame 0.013 code values on 5.14, 2.5e-3 of the
level against 1e-3, with no floor measured in this setting.

The check was amended before its next run (the amendment is in the script's NEUTRALITY_REPEATS comment):
three interleaved processes per condition, judged by the rule the programme adopted on 2026-10-08 for its
other equivalence claims. That run (`--keep`, `.screenshots/flicker-elimination/followup-closeout-20261009/f09-region-capture-repeats.log`) gave 22/23 again:
the dump is indistinguishable from process variation on every region, but the frame is unpowered, because
processes of one condition differed by up to 80 code values at frames 62-64.

The cause is measured: the one process whose scene pre-load took 43 frames instead of 44 differs from every
other by up to 80 code values (mean 0.15), while among the 44-frame processes the dump-on and dump-off runs
were bit-identical in one pair and at most one code value apart in the rest. The dump changes nothing; the
loader's thread timing chooses the state. Owner of the fix: `phase0_capture-preload-determinism` (ledger
H-73). This task closes when that task's build gives 23/23 here.
