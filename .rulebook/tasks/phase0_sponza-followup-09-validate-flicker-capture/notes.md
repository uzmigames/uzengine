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

Caveat recorded in the package: pose W `shadow_wall` sits at the measurement floor (mean radiance
1.7e-4, mean frame-to-frame change 1.2e-6). It is a good noise reference but a poor stand-in for
"shadowed walls"; followup-12 should add a shadowed wall that receives bounce light.

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
* VFX, particle, trail, LOD, significance, frustum-cull and collision suites — stale expectations
  against the in-flight VFX work, e.g. `UZ_VFXLodSettings.DefaultValues_DistanceZeroIsFifty`
  expects 50 while the current default is 5000.
* `ProjectLoaderTest.CvarsBlock_NumbersAndBoolsParsed`, `SceneLoaderTest.*`, `LuaSceneAPITest.*`,
  `RuntimeViewportTest.*`, `HeightFogComponent.*`, `RenderingParity.HeightFogDefaults_*`,
  `SkyAtmosphereComponent.*` — expectation drift (e.g. the loader now returns CVar overrides in a
  different order).

Diagnosis of the two constant-buffer/prefiltered-mip failures is recorded here; fixing them belongs
to followup-14 item 1.6, which owns them. This task did not touch the VFX files.

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

**Two criteria added to `flicker/criteria.py` with tests (25 checks now):**
`recovery_censored` — a recovery equal to the capture length did not recover, and two censored values
are not equal recoveries; `recovery_tolerance_usable` — a recovery tolerance below the region's own
noise can never be met. Both came from `shadow_wall`, whose G6 "pass" was 82 of 82 frames in both
conditions with a tolerance of 3.2e-06 against an oscillation of 8.7e-06.

**Still open in this task:** capture identity. `runner.capture_identity` now inherits the executable's
content hash from each capture's own manifest, and reports use it; older reports written before that
change still carry the analysis-time stamp.
