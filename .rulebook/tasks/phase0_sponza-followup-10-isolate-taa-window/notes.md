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
