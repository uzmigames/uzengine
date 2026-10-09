# Notes - phase0_sponza-followup-14-validate-flicker-correction

2026-09-09. Correction task: implement the smallest reversible change addressing a demonstrated
mechanism, measure it against gates frozen beforehand, and adopt it only if every gate passes.
**No default was changed and nothing was committed.**

Evidence package: `.screenshots/flicker-elimination/14-validate-flicker-correction/`
(`report.md`, `gates.json`, `stages.json`, `motion.json`, `matrix.json`, `materials.json`,
`material-classes.json`, `correction-pilot.json`, `manifests/`, `masks/`, `csv/`, `frames/`,
`montage-*.jpg`). Ledger rows H-21, H-22, H-23, H-24.

## Result: the candidate is more accurate and still fails its own gates

`r.TAA.ReconstructionHdrWeightExponent` (default 1.0 = production) softens the HDR weighting in the
TAA reconstruction filter. Exponent 0 removes it and is bit-identical to `ReconstructionMode=1`.

| | baseline | exponent 0.5 | exponent 0.0 |
|---|---|---|---|
| W `window` residual | 9.5215e-03 | 6.8673e-03 (-27.9 %) | 6.1541e-03 (-35.4 %) |
| error vs the resampled reference | 0.2199 | 0.1543 | 0.0726 |
| bright-side energy (1.0 correct) | 0.9715 | 0.9846 | 0.9947 |
| black pixels (correct 1.4e-06) | 0.00258 | 0.00652 | 0.01502 |
| recovery after a cut | 16 frames | 41 | 60 |
| frame time | 20.91 ms | - | 20.76 ms |

Four of seven gates fail (G1, G2, G6, G7), so by the rule frozen before the candidate existed
the default stays at 1.0 and the change remains opt-in. G7 fails only through its "G6 holds" clause: its own conditions all pass - the accumulated image moves *towards* the instantaneous input (no blur), the recorded exposure is identical, and the active lighting-path bitmask is identical in every ROI. It is not independent evidence of cheating, it is a restatement of G6.

**Correction (2026-10-08).** The lighting-path clause rests on one capture per variant, `materials.json`, never repeated. The ledger's layout-bug audit lists its path bits as invalid ("Results marked invalid" table), but the file was written on 2026-09-09 at 21:43 local, before that bug's window (2026-09-10 08:13 to 09:55 local), and its path bits ([3]) are the value the post-fix slot dump reads (ledger H-29); see tasks.md 1.5 and ledger row H-23c. G7 still fails through G6; the verdict is unchanged.

## What the measurement had to survive

* **The gates were frozen first**, in the ledger, including a note predicting that G2 would
  conflict with the defect being an energy *loss*. That prediction was half right: the candidate
  does restore bright-side energy, but most of the ROI-mean change lands on dark pixels.
* **The primary metric turned out to be the wrong question.** `std_mean` measures stability, not
  correctness. Pose A `rear_door` was initially read as a severe regression (+120 % residual,
  +79 % energy); measured against the cycle-averaged raw input it is the opposite - the baseline
  holds only 42 % of the correct
  bright energy there and the candidate raises it to
  74 %, halving the
  per-pixel error. The gate still fails and is reported as failing; the interpretation is corrected
  rather than the threshold.
* **The renderer is not bit-reproducible across processes.** Four captures of an identical
  configuration differ in up to 795 of 786 432 recorded values, by up to 1.6e-2 in linear radiance
  (`scripts/diagnose_cvar_ulp.py`). Aggregates are unaffected - the three gate repeats agree to
  ~1e-13 - but two harness assertions that assumed bit-identity failed spuriously and were
  rewritten to compare against a measured per-configuration floor. Recorded as a confound.
* **Interrupted captures are now loud.** Closing the render window ends a run with exit code 0
  after a few frames; `runner.run_capture` now raises with that explanation instead of letting a
  downstream `no region_*.bin` error confuse the cause. The engine's `headless` mode only creates a
  device and exits, so captures need a real window.
* **Pose C had to be brought to the same pipeline.** Its project renders with TAA off; the first
  attempt was rejected by the capture integrity check (`taa_stamp_missing` on every frame) rather
  than analysed. Capture-time overrides give it the W/A configuration; the project file is
  untouched.

## Robustness (item 1.5)

| condition | window residual, baseline -> exponent 0 |
|---|---|
| production | 9.5215e-03 -> 6.1541e-03 (-35.4 %) |
| 50 % internal resolution | 1.7374e-02 -> 1.1878e-02 (-31.6 %) |
| reflections off | -32.9 % |
| 16 reflection samples | -35.4 % |
| AABB-only clamp | -28.7 % |
| TAA off (negative control) | identical, both exactly 0 |

**Correction (2026-10-08).** The "TAA off" row is the vacuous control described under "Three of my own" below: with `r.TAA=0` the `output` record is never written, so both variants recorded all zeros. The row was removed from the matrix; the real TAA-off control is in `scripts/test_correction_gates.py`. `report.md` (git-ignored, not edited) still carries the sentence that described it.

Material coverage came from the recorded G-buffer: every measured ROI is rough (roughness p05
0.50-0.60, median 0.73-0.80) with normal-mapped surfaces (shading-normal spread 13-22 deg) and
metallic pixels present (43 % of the W window ROI). Mirror and glossy classes do not exist in the
scene - 13 glossy pixels in total, no mirror pixels - so they were forced with
`r.Material.RoughnessOverride` / `MetallicOverride` and measured separately
(`material-classes.json`), with each capture asserting the override actually reached the G-buffer.

**Correction (2026-10-08).** The metallic share (43 % of the W window ROI) comes from one frame of one capture, `materials.json`. The ledger's layout-bug audit lists its metallic counts and path bits as invalid ("Results marked invalid" table), but the file predates that bug's window (tasks.md 1.5), so the figure stands on that single capture, as do the roughness and normal figures, which read LIGHTING records. The "metallic px" and "active lighting paths" columns of `report.md` (git-ignored, not edited) come from the same file.

## What this does and does not say

* The mechanism is real and the change is an accuracy improvement, not blur: the accumulated image
  moves *towards* the instantaneous input (0.2623
  -> 0.1875).
* It is not adoptable as it stands: it fails three frozen gates, and its cost concentrates on
  near-black pixels and on recovery from an invalid history.
* The visual defect is **not closed**. Roughly 65 % of the window residual is untouched, and both
  clamp routes are now rejected (H-13 rejected no-clamp; H-23 rejects AABB-only, which on its own
  moves recovery from 16 to 48 frames).
* The next experiment is named: ledger **H-24**, a reversible-tonemap reconstruction.

**Correction (2026-10-08).** H-23's AABB-only figure (recovery 16 -> 48 frames) is one run, never repeated (see "`stage_motion` gave extra variants a single run" below); independently of it, the AABB-only clamp moves the static window residual by only -9.5 % (`matrix.json`), short of G1. H-24 was rejected on paper the same day (review item 1 below); the window's open next steps are in known-issues, "Static TAA residual".

## What an independent review changed (2026-09-09)

This task was reported complete before it was. A re-inspection of the code, the report, the
aggregates and the tests found six defects; three more of the same kind surfaced while fixing them.
The verdict — the candidate fails its gates and stays opt-in — survives all nine. What changed is
that the evidence now supports the claims made from it.

### The six from the review

1. **H-24 rested on false algebra.** It proposed a reversible-tonemap reconstruction as an
   *unbiased* next mechanism. Averaging in `c/(1+Y)` and inverting is exactly a weighted mean with
   weights `1/(1+Y)` — the same family as the shader's `1/(4 + exposure*Y)`, with a smaller
   constant, so *more* aggressive. On samples 0 and 8 it returns 0.8 where the linear mean is 4.
   Verified to 2.4e-13 over 20 000 random cases. H-24 is now rejected on paper; H-24b records that
   the exponent sweep already spans the family (its unbiased endpoint still leaves 65 % of the
   residual, so the remainder is not weighting bias); H-25 names a genuinely different mechanism —
   the clamp *bounds* being recomputed from jittered samples every frame.
2. **A gate could pass on missing evidence.** `paths_equal is not False` let `None` through, and
   the report printed "lighting paths identical: None" while scoring G7 as verified. Gates are now
   tri-state; missing evidence reads inconclusive. The absent capture was taken, and the clause is
   now `True` for all three variants.

   **Correction (2026-10-08):** the capture taken for this clause is `materials.json`, one capture per variant. The ledger's layout-bug audit later listed its path bits as invalid ("Results marked invalid" table), but the file predates that bug's window (tasks.md 1.5; ledger row H-23c), so the `True` stands on that single capture. G7 still fails through G6.

3. **The reference could not support the claims.** Everything was measured against the
   cycle-averaged raw input, built from the same jitter offsets under examination, and "not blur"
   rested on the output approaching the *instantaneous* sample. An independent reference was built
   — un-accumulated `taa_raw` at `r.ScreenPercentage=200`, averaged over a jitter cycle — and
   sharpness measured on real content as gradient energy:

   | | error vs reference | energy (1.0 correct) | gradient ratio |
   |---|---|---|---|
   | W `window` baseline | 0.2375 | 0.9198 | 0.9578 |
   | W `window` exponent 0 | 0.1452 | 0.9807 | 0.9804 |
   | A `rear_door` baseline | 0.0164 | 0.4441 | 0.2019 |
   | A `rear_door` exponent 0 | 0.0128 | 0.7933 | 0.6561 |
   | the cycle-average proxy, W | 0.1096 | 1.0010 | 0.9923 |

   The candidate is **sharper** than the baseline, not blurrier, and closer to the reference on both
   poses. On `rear_door` the baseline keeps only 20 % of the
   reference's gradient energy. And the proxy the review flagged as circular agrees with the
   independent reference to 0.1 % in energy on pose W, so the
   conclusions drawn against it stand.
4. **Two required conditions were never tested.** Exposure transitions and clipmap crossings were
   held fixed. Measured now: with auto-exposure the exposure moves over
   66.1–73.2 and the two variants' curves diverge by
   0.11 % of the mean; on the boundary trajectory both variants show 24 recentre
   events with identical near/far ratios. Separately, G5 is written against GPU time and only CPU
   wall time was ever recorded: GPU total 17.437 -> 17.316 ms (-0.69 %), the TAA pass
   0.0563 -> 0.0555 ms (-1.33 %).
5. **G6 read one repeat of three.** It now takes the worst case across every repeat and reports the
   spread.
6. **`.metadata.json` said `pending`** while every item was ticked.

### Three of my own, all the same failure

Each was a control that "passed" because it measured nothing — the same shape as the vacuous mirror
test found earlier in this task:

* **the TAA-off control compared two all-zero buffers.** The `output` record is written by the TAA
  resolve, so with `r.TAA=0` nothing writes it. It now runs on the deferred lighting components of
  a sunlit floor and asserts its own power first: floor 1.2e-07 against a signal of 0.239, i.e.
  0.00 % of it. The matrix row that shared the flaw was removed with the reason recorded;

  **Correction (2026-10-08):** that floor (1.2e-07) is from one pair of the run of 2026-09-09 23:25 local (`test_gates8.log`), which the ledger's layout-bug audit lists as invalid ("Results marked invalid" table) although it predates that bug's window (2026-09-10 08:13 to 09:55 local) and read the same 0.23884 signal as the runs after the fix. Re-run after the fix: floor 1.218e-04 on that signal (0.05 %), with the CVar's max |delta| 0 on 2026-09-10 and 1.182e-05 on 2026-09-16.

* **the exposure study read the wrong exposure.** The 4th record component is the exposure the TAA
  *weights* use (`r.TAA.ExposureScale`, constant 1.0), not the display exposure, so a run where
  auto-exposure climbed to ~68 was reported as a flat 1.00 with no transition. The region file
  header carried the right value all along;
* **the first spatial reference was empty** for the same reason as the first: `r.TAA=0` plus the
  `output` record. The script now refuses a reference that is all zeros.

Two instrumentation defects came out of the same work: the run manifest recorded the *window* size
as the render resolution, so a 50 % capture claimed 1920x1080 while the log showed 960x540; and the
`gpu_ms` field added for G5 stayed empty because per-pass GPU profiling is off by default and has no
CVar. Both fixed in `runtime/tools/uzengine.cpp`.

### What this says about the method

Four of the nine defects were controls or references that could not have failed. A control that
cannot fail is worse than no control, because it is reported as evidence. The harness now states a
control's power before its verdict, and `runner.run_capture` refuses a capture that was cut short.

## Test run (items 1.6 and 2.3)

`cd runtime && zig build test` -> **7628 tests, 7473 passed, 85 failed**. Every failure was
classified rather than filtered; none is in an area this task changed (TAA resolve, the material
overrides, the region dump, the analysis stack).

| group | count | diagnosis |
|---|---|---|
| VFX / Vfx editor suites | ~55 | The user's own uncommitted VFX work, untouched by this task. Pre-existing. |
| `ShaderCrossBackend` | 13 | 12 fail with *"Shader source not found on disk"* - a path lookup relative to the executable - and they include `mesh_vs.hlsl` and `mesh_ps.hlsl`, which this task never touched, so the cause is environmental rather than a shader change. The 13th, `SPIRV_SM60_deferred_lighting_ps`, fails on `shadowAtlasTexture` and `gBufIndices` being undeclared: bindless SM6.6 constructs that do not exist on the SM6.0 SPIRV path. |
| `HeightFog*`, `RenderingParity`, `SkyAtmosphereComponent` | 6 | Component defaults unrelated to this work. Pre-existing. |
| `Particle*`, `GPUParticleBuffer`, `TrailAssetUV`, `TurbulenceModule`, `UZ_Collision*` | ~10 | VFX runtime, pre-existing. |
| `SceneLoaderTest`, `ProjectLoaderTest`, `LuaSceneAPITest` | 3 | Loader/scripting defaults, pre-existing. |
| **`BuildPerFrameCB_StructSize.PerFrameCB_Is768Bytes`** | 1 | **Named in the checklist.** Stale: commit `29c83ba5` grew `PerFrameCB` to 1024 bytes (4 x 256) and the header carries its own `static_assert(sizeof(PerFrameCB) == 1024)`, with fields in use out to offset 1008. The invariant that matters, D3D12's 256-byte constant-buffer alignment, is already enforced there. Suggested fix: assert `sizeof(PerFrameCB) % 256 == 0`. Not changed here - it belongs to the constant-buffer area. |
| **`PerFrameCB_IBL.PrefilteredMipLevelsDefaultIsFive`** | 1 | **Named in the checklist.** Stale: commit `6985ffac` deliberately raised the prefiltered chain to 8 in `IBLBaker::kPrefilteredMipLevels` and in all three CB structs, which agree with each other. Suggested fix: expect `IBLBaker::kPrefilteredMipLevels` instead of the literal 5. Not changed here - it belongs to the IBL area. |

**Correction (2026-10-09).** The `ShaderCrossBackend` row is misdiagnosed: its 12 "Shader source not found on disk" failures are a
defect of the test fixture, not of the environment. The fixture's subdirectory list
(`runtime/tests/engine/shader/test_shader_cross_backend.cpp:56-57`) has no `gbuffer/`, where `mesh_vs`, `mesh_ps`, `gbuffer_ps`
and `gbuffer_debug_ps` have lived since bd4db199 (known-issues, "Stale unit tests"). The failures of this table other than the
two named ones are owned by `phase0_preexisting-test-failures` (created 2026-10-09; `ShaderCrossBackend` is its item 1.4, the
default-drift tests item 1.3), except the VFX and editor suites, which stay with the VFX work in progress.

This task added two fields to `PerFrameCB` / `PerFrameParams`, inside the existing 12 bytes of tail
padding, so the struct stays 1024 bytes and both `static_assert`s in the headers still hold - the
build would have failed otherwise. The G-buffer change is proven at runtime rather than only by
compilation: the forced roughness is read back from the recorded G-buffer in every capture of
`scripts/run_material_class_study.py` (0.767 -> 0.050 as requested), and the capture asserts it.

Behavioural harnesses, all passing:

* `scripts/test_correction_gates.py` - **10 / 10**, including the two negative controls (with TAA
  off the CVar changes nothing; omitting the CVar matches setting it to its default) and the
  equivalence of exponent 0 with `r.TAA.ReconstructionMode=1` (bit-identical).

  **Correction (2026-10-08):** this 10 / 10 is the run of 2026-09-09 17:19 local (`test_gates4.log`, scratchpad of session 9d44760c), the latest when these lines were written (17:30). In it exponent 0 and `ReconstructionMode=1` did match bit for bit (max |delta| 0), but not in general across processes: a later run measured 1.247e-02 between them, exactly on its floor (`test_gates5.log`; tasks.md 1.3). Its omitted-vs-1.0 control passed exactly on its single-pair floor (3.815e-04 against 3.815e-04), and its TAA-off control was the all-zero comparison rebuilt later (above). The 10 / 10 and 11 / 11 runs later that night (23:07, 23:25) passed the omitted-vs-1.0 control exactly on its floor too (3.366e-03), and the 11 / 11 the exponent-0 check as well (2.707e-07 against 2.707e-07).

* `scripts/validate_taa_model.py --exponents 1,0.5` - the offline model reproduces the GPU to a max
  relative error of 2.1e-07 at both exponents.
* `scripts/diagnose_cvar_ulp.py` - characterises the process-to-process reproducibility floor.

## Closure (2026-09-10)

This task is closed as an **evaluation completed with the candidate rejected**, which is not the
same as the defect being corrected. `r.TAA.ReconstructionHdrWeightExponent` stays opt-in at its
production default of 1.0; four of seven preregistered gates fail.

What the evaluation established, and what it did not:

* the candidate is *more accurate* against an independent spatial reference on every ROI measured,
  and sharper rather than blurrier — the objection that it worked by blurring is answered;
* it fails G1, G2, G6 and G7, and its cost concentrates on near-black pixels and on recovery from
  an invalid history;
* G1 measures stability where the question is accuracy, and G5 was written for GPU time while CPU
  time was reported. Both are recorded in the ledger; neither threshold was moved.

Work that belongs to follow-ups rather than here:

* **followup-12 (open)** — the shadowed walls and the rear door: freezing the gather output while
  keeping the lighting path, and instrumenting the translucent contribution on the door and the
  right shadow. Nothing in followup-14 or the H-25/26/27 work touches these regions.
* **followup-13 (open)** — motion and the GDF: the third repeat, separating a change of the field
  from a change of its sampling, and testing direct-shadow updates.
  *(2026-10-08: followup-13 closes. The third repeat ran on 2026-09-15. The field/sampling split is
  moot because the recentre excess was a mask artefact (ledger H-07c). The direct-shadow update
  control goes to `phase0_sponza-flicker-correction` (ledger H-18b, H-20b), proposed at followup-12's
  close-out the same day and created 2026-10-09.)*
* **ledger H-26** — the history read is not eliminated; it needs an exact-read substitution during
  a run, not an instantaneous comparison.
* **ledger H-25b scope** — bound stabilisation was rejected for a single frozen jitter phase.
  Averaging the bounds over a cycle, or filtering them slowly, are untested and different.
  *(2026-10-09: every open item of this list now has one owner, `phase0_sponza-flicker-correction`
  (created 2026-10-09). followup-12's close-out of 2026-10-08 hands it the walls, the rear door and
  the right shadow, and its item 1.3 holds the exact history read (H-26) and the cycle-averaged or
  slowly filtered bounds (H-25b scope).)*

## Instrumentation added

* `r.TAA.ReconstructionHdrWeightExponent` (default 1.0), `r.Material.RoughnessOverride` and
  `r.Material.MetallicOverride` (default -1 = off).
* `scripts/run_correction_pilot.py`, `run_correction_validation.py` (stages gates / stages /
  materials / motion / matrix), `report_correction_validation.py`, `run_material_class_study.py`,
  `make_correction_montage.py`, `diagnose_cvar_ulp.py`, `test_correction_gates.py`.
* `scripts/flicker/taa_model.py` gained the exponent and `validate_taa_model.py` now validates the
  model against the GPU at more than one exponent (max relative error 2.1e-07 at both).
* `regiondump.check_sequence` / `load_sequence` / `analysis.analyse_run` gained
  `allow_taa_missing`, used only for the deliberate TAA-off control.


## Closed (2026-09-15)

This task is an **evaluation that reached a verdict**, and it is closed as such rather than left open
because no correction shipped from it.

**What it delivered.** `r.TAA.ReconstructionHdrWeightExponent` was implemented, measured against
gates fixed beforehand, and **rejected**: it fails G1, G2, G6 and G7. It also showed the candidate was
not achieving its effect by blurring — against an independent 2x spatial reference it is more
accurate and sharper on every ROI — so the rejection rests on the gates it failed, not on a suspicion
it did not deserve. The default is unchanged and the knob remains opt-in.

**Why that is a finished task and not an open one.** The question asked was whether that candidate
should ship. It was answered, with the evidence preserved. A rejected candidate is a result.

**What outlived it.** The gate machinery built here (pre-registered criteria, negative controls that
can fail, an independent spatial reference) is what later tasks reused, and two errors found in it —
G7 accepting missing evidence, G6 reading only the first repeat — were fixed and are recorded above.
The criteria rules have since moved to `scripts/flicker/criteria.py` with tests of their own
(`scripts/test_criteria_logic.py`), so the class of mistake this task hit is now caught by a test
rather than by review.


## `stage_motion` gave extra variants a single run (2026-09-15)

`run_correction_validation.py` had `reps = repeats if not extra else 1` in `stage_motion`, so every
motion variant added through `MOTION_EXTRA` was measured once and compared against a baseline measured
several times. That is the same defect fixed in `run_gdf_motion_study.py` on 2026-09-15, found again
while reusing this file's `motion_stats` for the `VarianceClipGamma` motion gate. Fixed: every variant
gets every repeat.

**What it affects in this task's record.** The G6 verdicts for `combined_exp0_aabb` and
`baseline_aabb` rested on one run each. Repeats in this renderer have since proven bit-identical, so the
numbers are unlikely to move — but "unlikely" is not "measured", and those two G6 verdicts should be
read as single-run results until they are re-run. The candidate this task rejected
(`ReconstructionHdrWeightExponent`) was a primary variant and did get its repeats, so its rejection is
unaffected.

**Correction (2026-10-08).** "Repeats in this renderer have since proven bit-identical" overstates it: the three repeats of each main motion variant agree exactly in recovery frames and to within 3e-06 relative in the error 80 frames after the cut (8.2e-08 absolute at exponent 0), not bit for bit (`motion.json`), and per-pixel records differ across processes (above). The two extra variants were never re-run, so their G6 figures remain single-run (handed to the ledger on 2026-10-08 as a row superseding H-23).

## The behavioural harness ran to completion, and its negative control failed (2026-09-16)

`scripts/test_correction_gates.py` had never been run to the end (it was stopped twice by a 300 s
timeout while other work held the GPU). It is a GPU capture suite, not a unit test, and the criteria,
format and layout tests do not substitute for it. Full run, exit code 1:

| check | result |
|---|---|
| the CVar exists and defaults to 1.0 | ok — the manifest reports 1 |
| every configuration reproduces itself across processes | ok — default 2.37e-04, explicit1 1.47e-03, exp0 1.25e-02, recon1 1.25e-02 on a mean of 3.376 |
| **negative control: omitting the CVar matches setting it to 1.0, within their floor** | **FAILS — max abs delta 3.218e-03 against a floor of 1.473e-03** |
| exponent 0 changes the image by at least 1 % of its own mean | ok — mean abs delta 2.065e-01, 6.1 % of the mean |
| exponent 0 matches `r.TAA.ReconstructionMode=1` | ok — max abs delta 3.124e-03 against a floor of 1.247e-02 |
| the residual falls monotonically as the exponent falls | ok — 1.1301e-02 > 9.4422e-03 > 8.3770e-03 > 7.8582e-03 > 7.6165e-03 |

**Correction (2026-10-08).** Two errors in this section. (1) "Had never been run to the end" holds only for the revision saved on 2026-09-15: earlier revisions ran to completion five times on 2026-09-09 (16:31 8/10, 17:19 10/10, 21:56 9/10, 23:07 10/10 and 23:25 11/11, local) and once on 2026-09-10 (10:08 local, 11/11, the re-run after the layout-bug fix). (2) The table lists 6 checks; this run executed all 11 and passed 10 (`correction_gates_full.log`, 2026-09-16 00:36, local scratchpad of session 9d44760c). The five not listed all passed: the TAA-off control's power (floor 1.218e-04 on a signal of 0.23884, 0.05 %), the TAA-off negative control (1.182e-05 against 1.218e-04), not blur (0.3314 -> 0.2355), energy (0.9252 -> 0.9818) and the bleed cost (2096x -> 12060x). `.metadata.json` repeats the error ("Five of six checks pass").

**The failure, stated plainly and not explained away.** Omitting `r.TAA.ReconstructionHdrWeightExponent`
should render exactly as setting it to its default of 1.0. It does not: the two differ by 3.218e-03
where the floor measured from each configuration against itself is 1.473e-03 — about twice the floor,
and about 0.095 % of the image mean. It is small, and it is above the bar the test set for itself.

**Not diagnosed here.** Two readings are open: the default path genuinely differs from the explicit
one, or the per-configuration floor (the max absolute difference between two captures of the same
configuration) understates this renderer's cross-process spread, which the ledger has already caught
being larger for one process in three. Distinguishing them needs repeats of both configurations, not
an argument. Recorded as failing until then; no verdict about the candidate rests on it.


## Reopened (2026-09-16)

The negative control is part of the acceptance for this task, so `complete` contradicted a failing
result. Status returns to in-progress.

**The re-test, as specified in review:** capture the `omitted` and `1.0` conditions again in
INTERLEAVED order, and compare the difference BETWEEN conditions against the variation BETWEEN
REPEATS of the same condition. The tolerance is not to be raised to accommodate the result.

## The re-test: the two conditions are indistinguishable (2026-09-16)

`scripts/run_default_vs_explicit.py`, `40-default-vs-explicit`. Four repeats of each condition,
captured INTERLEAVED (omitted, explicit1, omitted, explicit1, ...), one process each, same pose, ROI,
frames and fields the harness uses. No tolerance was introduced and none was raised: the comparison is
between-condition distances against within-condition distances.

| statistic | within a condition (12 pairs) | between conditions (16 pairs) | verdict |
|---|---|---|---|
| max abs delta | 0.000e+00 .. 3.218e-03 | 9.537e-07 .. 3.218e-03 | INDISTINGUISHABLE |
| mean abs delta | 0.000e+00 .. 1.666e-07 | 2.295e-11 .. 1.662e-07 | INDISTINGUISHABLE |

**The harness's failure is explained, and it is not a difference between the conditions.** The value it
flagged — 3.218e-03 — appears here between repeats of the SAME condition: `omitted` pairs (0,1), (1,2),
(1,3) and `explicit1` pairs (0,1), (0,2), (0,3) all reach exactly it. Two captures (`omitted_r1` and
`explicit1_r0`) sit apart from their own condition's siblings by that amount. The harness measured its
floor from a single pair per configuration, and that pair happened to be a close one; with four repeats
the same-condition spread reaches the same magnitude as the cross-condition difference. Recomputing the
harness's own comparison on these captures gives 3.218e-03 against a two-capture floor of 3.218e-03 —
it passes, on the same data that failed it before, purely by which pair the floor came from.

**A provenance gap, found while checking the conditions.** The manifest cannot distinguish these two
conditions: `cli` records a handful of keys and no `--cvar` overrides, and `cvars` reports 1 for this
CVar in BOTH conditions, because setting a CVar to its own default does not make it non-default
(`cvars_non_default` omits it either way). The evidence that a condition was applied is the engine log
line `CVar applied: r.TAA.ReconstructionHdrWeightExponent = 1`, present 2x in `explicit1` and 0x in
`omitted`, and that is what the study checks. A study that identified conditions from the manifest
alone could not tell these apart.

**What this does and does not settle.** It settles the reading of the harness's negative control on
this pose and metric: the conditions are not distinguishable above the renderer's own process spread.
It does not make the harness's check correct as written — a floor from one pair understates the
spread — and it is not a general claim that omitting a CVar always equals setting it to its default.
`test_correction_gates.py` itself remains pending as a task item; this study is not a substitute for
it, and no evidence has been offered that they are the same requirement.


## Review reading of the re-test (2026-09-16)

The precise statement is: **omitted and explicit 1.0 are not distinguishable from the between-process
variability in this experiment** — this pose, this rectangle, these 16 frames, four repeats each.
Overlap of the distances does not prove universal equivalence, and the repeats only withdraw the
support for the claim of a difference; they do not establish sameness.

**The task stays open** until two things are done, with the original result preserved:

1. **Provenance:** the manifest must record the complete command line, including every `--cvar`
   override, so that a condition can be identified from the capture's own record rather than from the
   engine log.
2. **The control validated with repeats** — the harness's negative control judged against a floor
   measured from repeats, not from one pair.

`test_correction_gates.py` remains a separate pending item.

### Provenance fix implemented, verification pending (2026-09-16)

The run manifest now writes `argv` verbatim and `cvar_overrides` (every override as applied, project
file first, then the command line; `Config::FlushCvarOverrides` applies without clearing, so the list
is complete at the end of the run). Additive fields; the schema string is unchanged. To be verified on
the next capture: both fields present, and an explicit `--cvar X=<default>` visible in `argv` where
`cvars` alone could not show it.

### Provenance verified (2026-09-16)

Both manifests of `42-taa-reconstruction` carry `argv` (26 arguments, the three `--cvar` among them)
and `cvar_overrides` (11 entries: the project file's, then the command line's, in application order).
An explicit `--cvar X=<default>` is now visible in `argv` where `cvars` alone could not show it.
Item 1 of the reopening is done. Item 2 - the control judged against a floor from repeats - was
measured in `40-default-vs-explicit`; whether the harness's own floor is changed remains the separate
`test_correction_gates.py` item.



### Review reading (2026-09-16)

The provenance item is resolved in the verified scope. That does not make the task complete, and it
does not replace `test_correction_gates.py`, which remains a separate pending item.

## Close-out (2026-10-08)

**The harness floor now comes from repeats.** `scripts/test_correction_gates.py` captures every
configuration `--repeats` times (default 4, at least 3), interleaved, and judges each equivalence
check (omitted vs 1.0, exponent 0 vs `ReconstructionMode=1`, and the TAA-off control) by the rule
pre-registered for `40-default-vs-explicit`: max(between) <= max(within) on max |delta|
(`spread_verdict`). The mean |delta| verdict and the old single-pair comparison are printed beside
each verdict and are not judged. The effect check takes the smallest mean |delta| over every
default x exponent-0 pair. Cost: 27 captures at the default.

Why the single-pair floor had to go, from the saved logs: the omitted-vs-1.0 check passed exactly
on its floor in 4 of the 5 runs of 2026-09-09 with per-configuration floors (3.815e-04 twice,
3.366e-03 twice; the revision before them, with one global floor of 0, failed it at 2.707e-07),
failed on 2026-09-16 (3.218e-03 against 1.473e-03) and passed on 2026-09-21; the exponent-0 check
sat exactly on its floor in 3 of 6 runs; and the TAA-off control passed at exact equality on
2026-09-09 (1.192e-07 against 1.192e-07, 23:25 local, `test_gates8.log`).

**Static verification, no engine run.** `python -m py_compile` and pyflakes are clean. On the eight
captures of `40-default-vs-explicit` (the harness's pose, ROI, frames and fields), read through the
harness's own `stack()` and `equivalent()`: INDISTINGUISHABLE (between 9.537e-07 to 3.218e-03, within
0 to 3.218e-03), so the check passes. Treating the eight same-path captures as exchangeable, all 35
splits into two groups of four are INDISTINGUISHABLE on max |delta|, against 25 of 35 on mean
|delta|, which is why the mean is reported and not judged; the single-pair rule passes 4512 of its
5040 pair choices over those splits (89.5 %) and 132 of 144 on the real split. The rule can fail: one
pixel shifted by 1.5 times the within spread in every `explicit1` capture reads SEPARATED, and a
uniform +1e-3 (0.03 % of the mean) reads OVERLAPPING. `main()` driven end to end with the engine
calls replaced by those arrays (plus synthetic exponent and TAA-off arrays): 11 passed; with a real
+5e-4 difference given to the TAA-off candidate that control fails (SEPARATED) and the script exits
1; `--repeats 2` is refused. The emulation scripts are in the session scratchpad (46a4cf85, `t14/`),
local only; `t14/emulate_gates.py` predates the move of `spread_verdict` into `scripts/flicker/criteria.py`,
and `sweep/emulate_gates_rewired.py`, the same check against the rewired harness, reproduced its log on 2026-10-09.

**Before this task closes:** run the changed harness on the current build and record it here (item
2.3 is unticked until then). The two stale gtests of item 1.6 need nothing more: 8246ab5c fixed
them the same afternoon (47/47 on the commit's own filter). Optional:
`scripts/run_correction_validation.py --stage motion --repeats 3` with `--out` set to a new package
directory, to replace the single runs of the two AABB variants (the default `--out` would
overwrite this task's 2026-09-09 package). That re-run, the window's residual and the candidate fixes never tried are owned
by `phase0_sponza-flicker-correction` (created 2026-10-09, items 1.3 and 1.4), which absorbed the window-residual task this
close-out proposed.

**`report.md` is git-ignored and was not edited.** Read its G7 "lighting paths identical: True"
(both candidate tables) and the "metallic px" and "active lighting paths" columns of "Material
coverage" as one capture per variant (`materials.json`; the ledger's "Results marked invalid" table lists them as invalid, but
the file predates the layout bug's window, see tasks.md 1.5 and ledger row H-23c); its motion rows
`combined_exp0_aabb` and `baseline_aabb` as single runs; and the `taa_off` sentence under the
robustness matrix as describing a removed, vacuous row.

## Close-out runs (2026-10-09) — the task stays open

On the Debug build of 2cc2e85c (uzengine.exe 2026-10-09 08:53, fnv1a64:c4cf401970c78226): `scripts/test_correction_gates.py --repeats 4` gave **10/11** (`.screenshots/flicker-elimination/followup-closeout-20261009/f14-correction-gates.log`).
The failing check is the power precondition, "every configuration reproduces itself across processes to
well under 2 %": default and explicit1 1.92e-01, exp0 and recon1 4.52e-02 (largest of 6 repeat pairs) on a
mean of 3.107, against 0.062. Every other check passed, including the negative control.

The diagnostic (four processes of the default configuration, dumps kept,
`.screenshots/flicker-elimination/followup14-correction-gates-repeats/spread-diagnostic/diag.json`): one
process differs from the other three, which agree to 3.9e-04. At its worst pixel (frame 237) the TAA's
current-frame input is identical in both (7.6055) while the history differs by 0.189; anywhere, the input
differs by at most 4.1e-05. That process's pre-load took 43 frames, the others 44. The spread is the
loader's thread timing carried by the TAA history, not a property of the configurations compared. Owner of
the fix: `phase0_capture-preload-determinism` (ledger H-73). This task closes when that task's build gives
11/11 here.
