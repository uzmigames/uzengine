# Notes — phase0_sponza-followup-10-isolate-taa-window

2026-09-09. Diagnostic task: separate the window residual into the stages that produce it, one
factor at a time, against a reference that is not "the previous frame". No default changed; every
variant is a CVar that defaults to the production behaviour. Nothing committed.

Evidence package: `.screenshots/flicker-elimination/10-isolate-taa-window/`
(`report.md`, `taa-model-validation.json`, `reconstruction-study.json`, `window-experiments.json`,
`raw-reference.json`, `motion-check.json`, `manifests/`). Ledger rows H-04b, H-11..H-14.

## The two defects are independent

The window has **two** problems and they respond to different things:

| | what it is | measured | responds to |
|---|---|---|---|
| energy bias | the converged image is darker than the correctly resampled one | whole ROI 0.919 of the cycle-averaged raw input; an analytic 1.5-px bright bar loses **48.4 %** | the HDR weighting inside the reconstruction (0.919 → 0.980; bar 48.4 % → 20.6 %) |
| temporal residual | the converged image keeps moving with the jitter phase | ROI std 0.0095, boundary std 0.0486 radiance | the jitter (zero offset → 0.00001), the variance clamp (~35 %), and only weakly the blend |

A constant jitter offset removes the residual completely and still loses a third of the bar's
energy, which is what makes them independent rather than two views of one effect.

## Where the movement enters (H-11)

Temporal standard deviation of the linear radiance, per stage, same capture:

| stage | bright pane | dark bars | boundary | whole ROI |
|---|---|---|---|---|
| raw sample (no accumulation) | 0.00000 | 0.00002 | 3.501 | 0.183 |
| plus-filtered colour | 0.248 | 0.005 | 1.322 | 0.146 |
| reprojected history | 0.023 | 0.0002 | 0.049 | 0.0096 |
| clamped history | 0.018 | 0.0002 | 0.045 | 0.0079 |
| TAA output | 0.023 | 0.0002 | 0.049 | 0.0095 |

The input is perfectly stable on the pane and swings by 3.5 radiance at the bars, because the
jittered footprint crosses the edge. The jitter-weighted plus filter then spreads part of that swing
onto pixels whose own sample never changes (bright 0.000 → 0.248), and the accumulation attenuates
everything by about 25x. So the residual visible on the pane is **made by the reconstruction**, not
by the accumulation and not by the input.

## One factor at a time (H-04b, H-12, H-13, H-14)

GPU, pose W, still camera, fixed exposure, jitter 8, frozen masks, 24 frames per variant:

| variant | ROI energy vs reference | ROI std | boundary std | clamp active | share of change on clamped frames |
|---|---|---|---|---|---|
| baseline jitter 8 | 0.919 | 0.00952 | 0.0486 | 6.1 % | 41.3 % |
| jitter 4 | 0.914 | 0.00528 | 0.0269 | 5.6 % | 37.0 % |
| jitter 16 | 0.921 | 0.01298 | 0.0541 | 6.2 % | 43.3 % |
| jitter 1 (constant Halton23(1)) | 0.922 | 0.00000 | 0.00001 | 3.1 % | 3.0 % |
| zero jitter (`JitterScale=0`) | 0.920 | 0.00000 | 0.00001 | 3.2 % | 2.9 % |
| reconstruction 1 (no HDR weighting) | **0.980** | 0.00615 | 0.0584 | 6.3 % | 21.0 % |
| reconstruction 2 (centre tap only) | 0.948 | 0.00447 | 0.0849 | 5.6 % | 14.4 % |
| reconstruction 3 (full 3x3) | 0.914 | 0.01064 | 0.0427 | 9.2 % | 43.9 % |
| clamp 1 (none) | 0.902 | 0.00490 | 0.0314 | 0 % | 0 % |
| clamp 2 (variance box only) | 0.919 | 0.00959 | 0.0486 | 19.9 % | 35.8 % |
| clamp 3 (AABB only) | 0.916 | 0.00862 | 0.0315 | 5.5 % | 38.6 % |
| blend 1 (fixed weight) | 0.919 | 0.00874 | 0.0480 | 29.6 % | 43.4 % |
| reconstruction 1 + blend 1 | 0.978 | 0.00475 | 0.0572 | 27.4 % | 14.8 % |

Readings:

* the residual **grows with the jitter length** (4 → 0.0053, 8 → 0.0095, 16 → 0.0130) and vanishes
  with a constant offset. Jitter length 1 and `JitterScale=0` agree, which also confirms that
  Halton23(1) behaves as a constant offset and not as "no jitter";
* the clamp contributes about 35 % of the residual and it is the **variance box** that does it: the
  AABB alone behaves like no clamp at all (0.0315 vs 0.0314);
* the blend contributes ~1 % of the residual, although it changes how often the clamp fires;
* `r.TAA=0` is **unmeasurable with this instrument** (the resolve writes no records); the no-TAA
  image is read instead from the `taa_raw` record of a TAA-on run.

Per-mask energy ratios are redistribution, not loss: the dark bars read 69x (production) to 381x
(no HDR weighting) the radiance of their own raw samples, because the filter moves pane light into
them. Only the whole-ROI column is an energy statement.

## Analytic fixtures against a converged reference (H-12)

The offline model (`scripts/flicker/taa_model.py`) reproduces the shader's reconstruction, clamp and
blend to **2e-7** relative error on captured records, so a variant tried in the model is the same
arithmetic the GPU runs. Against the exact box-integral reference of a 1.5-px bright bar:

| variant | energy error | temporal std | converged peak (reference 6.0) |
|---|---|---|---|
| production | −48.4 % | 0.0052 | 3.19 |
| no HDR weighting | −20.6 % | 0.0064 | 4.58 |
| centre tap only | −11.8 % | 0.0056 | 5.53 |
| full 3x3 | −53.4 % | 0.0050 | 2.85 |

The same bar at `bright = 1.0` loses 16.6 % instead of 48.4 %: the loss scales with contrast, which
is what an HDR-weighted mean does. Widening the filter makes it worse, narrowing it makes it better.

## Motion, disocclusion and a cut (H-13, H-14)

`scripts/sponza-flicker-w-motion.lua`: still, a 0.5 cm/frame strafe that sweeps the bars across the
pane, still again, then a one-frame cut **back to the first pose**, so the content is identical on
both sides of the cut and the recovery is measured against the converged phase-1 image.

| variant | static std | distance to the instantaneous image | motion lag | recovery | peak error after the cut | residual after 80 frames |
|---|---|---|---|---|---|---|
| baseline | 0.00990 | 0.271 | 0.082 | 1 frame | 0.237 | 0.011 |
| clamp 3 (AABB only) | 0.00889 | 0.271 | 0.083 | 4 frames | 0.299 | 0.021 |
| clamp 1 (no clamp) | 0.00561 | 0.306 | 0.148 | **not recovered in 80** | 2.370 | 0.525 |
| reconstruction 1 | 0.00690 | **0.194** | 0.077 | 16 frames | 0.390 | 0.026 |
| reconstruction 1 + clamp 3 | 0.00693 | 0.194 | 0.077 | 39 frames | 0.470 | 0.053 |

`clamp 1` is the textbook case the checklist warns about: the best static number in the table, bought
with history that never clears. Rejected.

`reconstruction 1` lowers the residual by 30 %, recovers 6 % of the ROI energy, and moves the
accumulated image *closer* to the instantaneous one (0.271 → 0.194), so the gain is not blur. Its
costs are honest and recorded: recovery after a cut goes from 1 to 16 frames, the residual 80 frames
after a cut is 2.4x the baseline, and more pane light bleeds into the dark bars.

**Nothing was adopted.** followup-14 owns that decision and needs repeats, the other poses, and a
partial-weighting variant that trades bleed against energy.

## Instrumentation added (diagnostics only, production defaults unchanged)

* `r.TAA.JitterScale` (default 1) — 0 gives accumulation with no sub-pixel offset, which
  `JitterSequenceLength=1` cannot express because Halton23(1) is a constant (0, −0.167) px offset.
* `r.TAA.ReconstructionMode` (default 0) — 1 spatial weights only, 2 centre tap only, 3 full 3x3.
* `r.TAA.ClampMode` (default 0) — 1 none, 2 variance box only, 3 AABB only.
* `r.TAA.BlendMode` (default 0) — 1 fixed `r.TAA.CurrentFrameWeight`.
* Region-dump field group `taa+extra` (3 records): the clip box, the centre depth, the velocity
  magnitude and the four plus-filter tap lumas — enough to recompute the resolve offline.
* `scripts/sponza-flicker-w-motion.{lua,uzscene,uzproject}` — the still/strafe/still/cut sequence.

## Scope and limits

* One run per variant (the renderer is deterministic at a given frame index, so a repeat reproduces
  the same frames; repeats across *poses* are what followup-14 needs).
* Pose W only. The other regions are followup-12's.
* "Surface identity" in item 1.3 is captured as depth, normal, roughness and view distance; there is
  no material or instance ID in the GBuffer to record, and adding one was out of scope here.
* The disocclusion measurement is the off-screen history fraction, which is 0 throughout this
  sequence: this TAA has no depth or normal based history rejection at all, so the clamp is its only
  defence. That is a finding, not a gap in the measurement.


## Record reconciled (2026-09-15)

The checklist reads 10/10 and the index shows this task complete. That is accurate and is recorded
here explicitly, because a ticked checklist next to a long notes file invites the reading that
something was left half-done.

**Concluded.** The TAA window's contribution to the still-camera residual was isolated and measured;
the constant-jitter variant removes the residual but costs a third of the bar's contrast, so it was
not adopted. Nothing here is awaiting a further experiment.

**Carried elsewhere, deliberately.** The moving-camera behaviour is followup-13's, and the TAA
output-stage knobs were taken up again in followup-12 on 2026-09-15 — where `r.TAA.CurrentFrameWeight`
turned out to be a dead CVar (declared, never read, the constant buffer taking a hardcoded 0.04) and
was fixed. That defect lived in this task's subject area and is noted here so the two records agree;
it is not a reason to reopen this task.

**Not re-run.** No experiment from this task was repeated to produce this note.


## Correction (2026-10-08)

Closure review against the code, the git history and the evidence package. Nothing was re-run for
this note. The text above stands as written and is corrected here.

* **"Nothing committed" (top of this file).** Everything this task added (the four diagnostic
  CVars, the `taa+extra` records, `scripts/flicker/taa_model.py`, `validate_taa_model.py`,
  `test_taa_modes.py`, the `run_taa_*.py` harnesses, the motion sequence, the docs and this task
  folder) landed in `7eea6b95` (2026-10-04).
* **Jitter length 1** is the constant (0, −0.167) px = Halton23(1) on every frame, as written
  above. `tasks.md` 1.2 said (0.125, 0.278), which is Halton23(5); corrected there.
* **"The variance clamp (~35 %)" and "the AABB alone behaves like no clamp at all (0.0315 vs
  0.0314)"** are boundary-mask figures (5 127 px). Per mask, production / variance box only / AABB
  only / no clamp: boundary 0.04855 / 0.04855 / 0.03147 / 0.03144; bright 0.02317 / 0.02342 /
  0.02314 / 0.01072; whole ROI 0.00952 / 0.00959 / 0.00862 / 0.00490. Over the whole window the
  clamp contributes 48.5 % and the AABB alone keeps 80 % of it; on the bright pane either box alone
  keeps all of it. The two boxes are redundant everywhere except on the boundary (superseding ledger
  row H-04c, handed over by this review).
* **"Nothing was adopted. followup-14 owns that decision"**: decided, the candidate was rejected.
  `r.TAA.ReconstructionHdrWeightExponent=0` is the same arithmetic as `ReconstructionMode=1`.
  Measured 3 x (200 + 256) frames on poses W, A and C, it reproduces this task's numbers (9.5215e-03
  / 6.1541e-03 against 0.00952 / 0.00615) and fails G1, G2, G6 and G7 (ledger H-21..H-23), so the
  default stays 1.0 and the knob is opt-in. followup-14 was reopened on 2026-09-16 for its own
  harness (`test_correction_gates.py`) and is in progress; its notes say no verdict about the
  candidate rests on that.
* **The motion table** used a recovery tolerance of 2 % of the phase-1 reference's 99th percentile
  (`run_taa_motion_check.py`; 0.166 on the baseline) over the ROI 770,200,192,256. followup-14's G6
  uses 2 % of the converged mean (0.049) over 770,150,192,512, so the two recovery counts are not
  comparable: the same variant reads 1 -> 16 frames here and 16 -> 60 there. Reconstruction 2
  (centre tap only, −53 % static) and reconstruction 1 + blend 1 (−50 %) never ran this sequence,
  and no reason was recorded. Clamp 3 got no verdict here; followup-14 rejected it in H-23 on one
  run per variant (its notes, 2026-09-15: "stage_motion gave extra variants a single run"), not
  re-run since; independently of that, its static residual falls only 9.5 % against G1's 50 %
  (followup-14 `matrix.json`: 8.6205e-03 against 9.5215e-03, matching 0.00862 here), which rejects
  it as a correction on its own. The cut back to the same pose measures recovery from invalidated
  history, not newly revealed surfaces. Disocclusion does occur in the sequence: the strafe's
  parallax covers and uncovers the bright surfaces behind the bars
  (`sponza-flicker-w-motion.lua:10-11`). On pose W at frame 237
  (`15-isolate-clamp-bounds/raw/full-recurrence`, masks by this task's rule,
  `run_taa_window_experiments.py:95-97`, over frames 237-244) the own-pixel view distance
  (`lighting_normal.w`) has a median of 252 cm on the dark mask against 361 cm on the bright mask:
  the bars stand about 1.1 m in front of the lit surfaces. The TAA's recorded centre depth
  (`taa_clip_min.w`) is not used for this: it is the nearest of the centre and four diagonal taps
  2 px away (`taa_resolve_cs.hlsl:257-286`), not the pixel's own depth. No metric isolates those
  pixels (the off-screen history fraction is 0 throughout), so "a disocclusion" in item 1.7 was
  exercised but never measured as such.
* **"One run per variant (the renderer is deterministic at a given frame index ...)"**: the ledger
  later corrected the premise ("The renderer is not bit-reproducible across processes", section "Two
  findings about the measurement itself"). Aggregate statistics reproduce closely but not always
  exactly (up to 2.4e-04 relative in oscillation between processes on A `rear_door`, the ledger's
  "Corrected 2026-09-15", far below the smallest effect reported here, about 1 %), and single values
  do not reproduce. Every GPU variant here remains a single run (24 measured frames per still-camera
  variant, one pass of the motion sequence per motion variant), and the package keeps summaries only
  (no dumps, CSV, masks, plots or logs; git-ignored).
* **`r.TAA.BlendMode=1` held the weight at a hardcoded 0.04 in these runs**, not at
  `r.TAA.CurrentFrameWeight`, which nothing read until the H-39 fix (followup-12; in `7eea6b95`).
  The default is 0.04, so the blend-1 numbers stand. `test_taa_modes.py` could not see this: its
  BlendMode check never set the CVar, and its jitter negative control ended in `or True`; its
  clip-box check never compared with `taa_clip_max`. All three were fixed on 2026-10-08 (see
  `tasks.md` 2.2) and have not yet been run on the GPU.
* **Record reconciled (2026-09-15).** "The index shows this task complete" was true of the generated
  index then (still ✅ at `7eea6b95` and `31b5c14a`). But the index is generated from
  `.metadata.json`, whose status has said `pending` since 2026-09-09, and the regeneration in
  `088b8f40` (2026-10-05) turned the row to ⬚, as the generated `.rulebook/tasks/README.md` shows
  (line 42 at `8246ab5c`).
  "Costs a third of the bar's contrast, so it was not adopted" misstates the reason. On the thin
  bright bar the constant offset loses 31.1 % (length 1) or 34.1 % (`JitterScale=0`) of the energy,
  against production's 48.4 %: less, not more. It was a control and never a candidate, because it
  removes the residual by removing the sub-pixel sampling the TAA anti-aliases with. "Nothing here
  is awaiting a further experiment" holds for this task's own checklist only. The window's static
  residual and the HDR-weighting energy bias stay open, and no open task item owns their next step:
  followup-12's acceptance criteria of 2026-09-17 gate the rear-door glass and the walls, not the
  window. The untested interventions (cycle-averaged or slowly filtered clamp bounds, ledger H-25b;
  an exact history read during accumulation, H-26) are recorded only in
  `known-issues-and-partial-implementations.md` and the ledger.
  *(2026-10-09: `.rulebook/tasks/phase0_sponza-flicker-correction`, created that day from
  followup-12's close-out with the window task proposed as followup-15 by the followup-10 and
  followup-14 close-outs merged in, now owns them: item 1.1 adds the W window to the acceptance
  criteria, 1.3 runs the untested interventions and the AABB motion repeats, 1.4 asks for the
  judgement on the residual. As created, its item 1.3 does not list the cut-sequence runs of
  reconstruction 2 and reconstruction 1 + blend 1 (`tasks.md` 1.7) or the H-01b period at jitter
  lengths 4 and 16.)*
* **H-01 hand-off.** The ledger's H-01 waited for this task to repeat jitter lengths 1/4/16 in
  linear units. Item 1.2 measured the linear amplitude (ROI std 0.00528 / 0.00952 / 0.01298 at
  4 / 8 / 16, 0.00000 at length 1) but not the period, and no row recorded it. This review hands the
  ledger a superseding row H-01b, with the period at 4 and 16 left open.
* **Stale unit tests.** Item 2.3 handed them to followup-14 item 1.6, which diagnosed both and
  changed neither. They were fixed on 2026-10-08 in `8246ab5c`, outside this task:
  `PerFrameCB_Is768Bytes` was removed and `PrefilteredMipLevelsDefaultIsFive` became
  `PrefilteredMipLevelsDefaultMatchesTheBaker`. Its commit message records the filtered gtest run
  at 47/47; not re-run for this note (see `tasks.md` 2.3).

## Close-out run (2026-10-09)

`python scripts/test_taa_modes.py` on the Debug build of 2cc2e85c (uzengine.exe 2026-10-09 08:53, fnv1a64:c4cf401970c78226): **12 passed, 0 failed** (`.screenshots/flicker-elimination/followup-closeout-20261009/f10-taa-modes.log`).
The three checks fixed on 2026-10-08 read: production cycles through 8 distinct offsets, none (0, 0);
`r.TAA.BlendMode=1` uses the given `r.TAA.CurrentFrameWeight` 0.1 with a maximum deviation of 0.00e+00
(so the CVar is live, no H-39 regression); the clamped history lies inside the recorded clip box, both
bounds, on 100.000 % of 12 288 pixels. Item 2.3 re-ticked; the task closes with the 2026-10-08
resolution. The optional motion repeats and the H-01b period stay with the proposed followup-15.
