# Notes — phase0_sponza-followup-13-isolate-gdf-motion

2026-09-09. Diagnostic task: decide whether the lighting steps seen while moving are caused by the
global distance field recentring, using a comparison that follows the same surface instead of the
same screen pixel. No default changed; nothing committed.

Evidence package: `.screenshots/flicker-elimination/13-isolate-gdf-motion/`
(`gdf-motion-study.json`, `csv/`, `manifests/`). Ledger rows H-07b, H-20.

## Result: supported, and this is the first demonstrated cause in the programme

Both criteria the task set for claiming causality are met:

* **the discontinuity follows the grid.** Shifting the voxel snapping phase by half a voxel
  (`r.GI.GDF.CenterPhaseOffset = 9.765625`) moves the event frames (baseline 78, 98, 117, 137 … vs
  shifted 88, 108, 127 …) with the camera path unchanged, and the elevation moves with them;
* **it disappears in the fixed-centre control.** `r.GI.GDF.FreezeCenter=1` produces **no events at
  all**, and the residual away from events is unchanged (2.48e-6 vs 2.35e-6 on the shadowed wall,
  1.42e-5 vs 1.35e-5 on the lit wall), so the ordinary motion residual is not made of recentres.

| quantity | baseline | grid phase shifted | centres frozen |
|---|---|---|---|
| recentre events | 24 (clipmap 0: 20, clipmap 1: 4; x 10, y 8, z 6) | 20 | 0 |
| motion-compensated residual at events / elsewhere, shadowed wall | 3.17e-6 / 2.35e-6 (**1.35x**) | 3.57e-6 / 2.35e-6 (**1.52x**) | — / 2.48e-6 |
| the same, lit wall | 1.78e-5 / 1.35e-5 (**1.31x**) | 2.67e-5 / 1.44e-5 (**1.86x**) | — / 1.42e-5 |
| signed step of the ROI mean, shadowed wall | \|step\| 3.45e-5 vs 5.41e-6 elsewhere (**6.4x**) | 2.82e-5 vs 6.61e-6 (**4.3x**) | no events |
| the same, lit wall | 1.10e-4 vs 3.37e-5 (**3.3x**) | 1.99e-4 vs 4.39e-5 (**4.5x**) | no events |
| sign of the step | mixed (53 % one way) | mixed (53–59 %) | — |

In relative terms the step at an event is about **24 % of the shadowed wall's mean radiance**
(3.45e-5 on 1.46e-4) and about **9 % of the lit wall's** (1.10e-4 on 1.26e-3), against ordinary
frame-to-frame changes of 4 % and 3 % in the same windows. Events happen roughly every 35 frames of
movement at 1 cm/frame (clipmap 0 recentres every 19.53 cm). The step has no consistent sign, so it
is a discontinuity, not a drift.

## How the measurement was made honest

* **World-space tracking.** Each pixel's world position is reconstructed from its recorded view
  distance and the recorded unjittered view-projection (region dump format 3 now carries the
  matrices and the camera), then projected into the previous frame; the residual is taken there.
  Pixels that leave the rectangle, land at a different distance (more than 2 cm or 1 %) or whose
  normal turns by more than about 18 degrees are rejected. Valid coverage is reported with every
  number: **88 %** of the rectangle through the moving segments.
* **Two bugs in the tracking were found and fixed before any conclusion.** Unprojecting at a single
  mid depth lands only about 9 cm from the camera under the engine's reversed-Z projection, which
  made the ray direction wildly sensitive: the round trip was off by 21 px. Using two well separated
  depths brought it to 1–2 px, and doing the tracking as a *displacement* between the two
  projections of the same point cancels the rest (the jitter, and a sub-centimetre difference
  between the recorded camera position and the matrix's own centre of projection). Coverage went
  from 10 % to 88 % and the measured ratios changed sign of interpretation — the first version would
  have reported "no effect at events".
* **Repeatability.** Two baseline runs agree to 0.00 % on the residual.
* **Synthetic validation.** `scripts/test_flicker_metrics.py` now checks the reprojection on a
  synthetic camera: the world round trip is exact to 0.01 px, a known lateral camera translation
  reproduces the analytic parallax to within 5 %, and a deliberately wrong matrix does not track.

## What this does and does not say

* It is about the **moving** camera. The still-camera residuals of followup-09..12 are untouched by
  this: freezing the centres leaves them unchanged.
* It is measured on two deferred-lit regions of pose W. The translucent regions of followup-12 are
  not covered here.
* The steps have no consistent sign and the trajectory used is a slow 1 cm/frame slide; faster
  motion crosses boundaries more often and was not measured.
* Cause *within* the recentre is not isolated: a recentre both re-voxelises the clipmap and shifts
  the sampling origin of everything that traces it. Distinguishing those two is the next experiment
  and belongs to a correction task, not here.

## Instrumentation added

* `r.GI.GDF.FreezeCenter` (default off) and `r.GI.GDF.CenterPhaseOffset` (default 0) —
  `GlobalDistanceField::SetDiagnosticCentreControls`.
* Region dump format **3**: a 144-byte camera block (unjittered view-projection, the previous
  frame's, camera position) after the header, plus the `motion` field preset (lighting records and
  the TAA output, 48 bytes per pixel). `regiondump.world_positions()` and `.project()` read it.
* `scripts/sponza-gdf-boundary.{lua,uzscene,uzproject}` — six segments crossing one axis at a time
  in both directions, each with a stop before and after.
* `scripts/run_gdf_motion_study.py`, `scripts/test_gdf_controls.py`.

## Repeats completed, and the recentre split (2026-09-15)

**The repeats were incomplete and are now done.** `run_gdf_motion_study.py` had
`repeats = args.repeats if label == 'baseline' else 1`: only the baseline was repeated, so every
variant was a single measurement compared against a baseline measured three times. Every variant now
gets the repeats. The result of doing so: they are **bit-identical**, 0.0 % spread on every variant
and region, so repeats here bound nondeterminism and nothing else.

**Separating the field from the sampling.** A recentre does two things at once — requantises the
clipmap onto a new lattice, and moves the sampling origin of everything that traces it. A sub-voxel
phase offset separates them, because it moves where the lattice falls without moving any geometry or
changing the trajectory. The single half-voxel point became three.

| variant | events | `shadow_wall` | `lit_wall` |
|---|---|---|---|
| baseline | 13 | 1.346x | 1.314x |
| centres frozen | 0 | — | — |
| phase +1/4 voxel | 10 | 0.936x | 1.543x |
| phase +1/2 voxel | 11 | 1.523x | 1.864x |
| phase +3/4 voxel | 10 | 2.129x | 1.294x |

Freezing the centres removes the events and leaves the ordinary residual where it was (2.476e-06
against the baseline's 2.353e-06), which confirms the earlier finding that the motion residual is not
made of recentres.

**The excess is sensitive to the phase of the grid.** *(Corrected 2026-09-15: this heading first read "the excess tracks the lattice, not the event", which claims a separation the measurement does not deliver. Phase sensitivity is what was measured; the phase also changes the event set, and no control separates the two mechanisms.)* An origin shift is the same geometric displacement
whatever the phase, so an origin-driven excess ought to be roughly phase-invariant. It is not — it
swings 0.94x to 2.13x on the shadowed wall with the geometry and trajectory untouched. That points at
requantisation of the field.

**What limits that.** Changing the phase also changes which frames recentre and how many (13, 10, 11,
10 in window), so the near-event frame subsets differ between conditions. The claim that survives is
that the excess is **not invariant to lattice phase**; no individual per-phase number should be read
as a measurement of anything on its own.


## Review decision: stays open (2026-09-16)

The results so far do not let anyone choose between the field's requantisation and the sampling
origin's shift. Separating them needs controls that move ONE of the two while holding the other
fixed; the phase sweep moves both, because changing the phase also changes which frames recentre and
how many. No explanation is adopted until such a control exists.


## Review decision (2026-09-16)

Proceed with the defined controls: separate the field's requantisation from the sampling origin's
shift with controls that move ONE of the two while the other stays fixed. This does not depend on any
TAA intervention.

## The field alone, across a recentre — pre-registration (2026-09-16, before the run)

`scripts/run_gdf_field_compare.py`, engine diagnostic `r.GI.GDF.Diagnostic.FieldCompare` (default off).
A recentre re-composites clipmap 0 at a centre snapped to the voxel lattice AND moves the origin
everything samples with; the phase sweep moved both. This looks at the field alone: on each recentre
the engine reads the raw R16F distance volume back immediately before the recomposite and immediately
after it, and compares them at the SAME world positions — the centre moves by a whole number of voxels,
so every old voxel still inside the clipmap lands exactly on a new voxel
(i_new = i_old + (c_old - c_new) / voxel). One log line per event; no region dump.

* **Capture:** the boundary trajectory (`sponza-gdf-boundary.uzproject`), 120 warm-up frames then six
  140-frame segments each crossing one grid axis — the trajectory the motion study measured, so its
  recentres are the events already known. 2 repeats.
* **F0:** at least one event per repeat, and every shift a whole number of voxels (residual
  < 1e-3 voxel); otherwise the comparison is meaningless and the study stops.
* **F1:** non-finite voxels < 0.1 % of the compared volume.
* **R, per event:** shift, overlap, fraction bit-identical, mean / p99 / max |delta| in cm, fraction
  changed by more than 1 % of a voxel (voxel = 19.53 cm at 256^3 over 2500 cm).
* **Reading, fixed now:** IDENTICAL if every event is bit-identical on the overlap (the composite is
  deterministic in world space and the recentre changes nothing in the overlapping field);
  REQUANTISED if any event has max |delta| > 1 % of a voxel (the field does change at fixed world
  points — a candidate for the excess, not yet its cause); NEGLIGIBLE if differences exist but all
  are <= 1 % of a voxel.
* **What it cannot say:** whether a field change, if any, is what the lighting reacts to. That needs
  the other half — a recomposite in place, without a move — which is the next control.

## The field alone, across a recentre — result (2026-09-16)

`44-gdf-field-compare`, status complete, 2 repeats, ledger H-49. F0 and F1 passed: 21 clipmap-0
events per repeat (the 58 "GI recentre" lines per repeat are the radiance cache's own recentres, a
different system), every shift a whole number of voxels (residual 0), no non-finite voxels.

**By the pre-registered rule the verdict is REQUANTISED** — and it is carried entirely by one kind of
event. The two event kinds must be read apart, and are:

| events | shift (voxels) | overlap | identical | mean abs delta | max abs delta |
|---|---|---|---|---|---|
| 40 runtime recentres (20 per repeat) | one axis, +-1 | 16 711 680 (256 x 256 x 255) | **100.0000 %** | 0 cm | **0 cm** |
| 2 initial placements (1 per repeat) | (-8, -57, -8), from the origin (0, 0, 0) to the camera | 12 239 296 | 0.033 % | 542 cm | 1583 cm |

**The runtime recentres — the events the motion study measured — do not change the field at any world
point that stays inside the clipmap.** Old voxel (x, y, z) and new voxel (x + s, y, z) hold the same
16-bit value in every one of the 16.7 million overlapping voxels, in all 40 events. The composite is
deterministic in world space, and a one-voxel move leaves the overlapping 255/256 of the volume
bit-identical. The only field change at such a recentre is the one-voxel slab that enters at the far
edge, about 2500 cm from the centre.

**The initial placement is a different thing.** At scene start the clipmap moves 57 voxels from the
origin to the camera; the "old" field was composited at the origin, possibly before every mesh had
loaded, and its overlap with the new one is almost entirely different. It is not a runtime recentre,
it happens once, before the measured trajectory begins, and it is the sole reason the rule's verdict
reads REQUANTISED. The verdict is recorded as the rule gives it; the restriction is recorded beside it.

**What this settles for the separation.** For the recentres at issue, the field's values are not what
changes: the "requantisation onto a new lattice" leaves the overlapping field untouched. Whatever the
lighting reacts to at a recentre is therefore on the other side of the split — the origin everything
samples with (clipmap selection, the extent-relative falloff, the gather's snapped probe origin, ray
origins resolved against the field) — or in the newly exposed boundary slab. Which of those it is
remains unmeasured; this study rules the field's values out for the overlap and nothing more.

**One reporting quirk, fixed rather than left to mislead:** the engine's p99 is a histogram estimate
whose first bin is centred at 0.0098 cm; for bit-identical events it printed that value. The report
now shows 0 where the maximum is exactly 0 and marks the p99 as an estimate.

**Limits.** One trajectory, one pose family, clipmap 0 only (the finest), 2 repeats. The other three
cascades were not compared.



## Review decision (2026-09-16): measure the sampling-origin side directly

The equality on the overlap weakens the hypothesis that the field changes at the observed recentres.
**REQUANTISED is preserved as the output of the original criterion, with this rider attached wherever
it is quoted: triggered only by the initial placement; no change detected in the overlap of the
trajectory's recentres.** It is not to be used as the explanation of the motion excess.

Next isolation, the main one: **measure the sampling-origin side directly** — record the clipmap
selection, the query coordinates, the attenuation, the probe origin, and the dependence on the newly
inserted slab. Recomposing in place is an auxiliary control, not the next isolation.

## The sampling side of a recentre — pre-registration (2026-09-16, before the captures)

`scripts/run_gdf_sampling_side.py`; the full design is its docstring. What the code establishes first:
the sampler has NO attenuation — `SampleGdfSurface` is first-containing-cascade selection — and a
voxel-aligned recentre shifts the trilinear coordinates by exactly one texel, so with the field
identical on the overlap (H-49) the only sampling quantity such a recentre can change at a fixed
world point is which cascade serves a point within one voxel of the moved faces. That is measured,
not deduced: the gather now records, per pixel, where its rays ended with respect to clipmap 0
(GATHER +29: fraction of hits inside clipmap 0, fraction in a coarser cascade, minimum distance from a
hit to a clipmap-0 face in voxels, fraction escaped) and clipmap 0 as it saw it (+30).

* **Events:** three clipmap-0 recentres on the boundary trajectory, one per axis, single-voxel and
  with no clipmap-1 recentre on the same frame: frames 238 (x-), 380 (y+), 668 (z+), from the retained
  logs of the same deterministic trajectory. Windows of +-8 frames, `gather` fields, ROIs
  `shadow_wall` and `lit_wall`, 2 repeats.
* **G0** the log's clipmap-0 recentre frame equals the expected one; **G1** +30's centre changes at
  every pixel between the frame before and the event frame, and at no other consecutive pair.
* **Pairing:** frame f's pixels carried to f-1 by reprojecting their recorded world position (+9)
  with f-1's view-projection; accepted within 2 cm and normals (+11) within 0.99; coverage reported.
* **Steps:** the EVENT step (f_e vs f_e-1) against the CONTROL steps (all other consecutive pairs in
  the window) — the same motion, without the recentre.
* **R1** mean |delta| of the raw trace output (+27) at the event step against the controls;
  **R2** the fraction of paired pixels whose snapped probe origin (+15) or ray origin (+10) changed,
  at the event and at the controls; **R3** at the event step, |delta output| for pixels whose rays
  ended within one voxel of a clipmap-0 face or changed cascade fraction, against the rest, and the
  same split at the controls.
* **What it cannot say:** a cause beyond what the records name, or anything about an intervention.

## Follow-on on the same captures — pre-registration (2026-09-16, before the numbers)

The sampling-side study finds the recentre step indistinguishable from the surrounding motion steps in
the gather's RAW trace output (+27). H-40's excess was measured on the TAA output. The same captures
hold, per pixel, the accumulated output (+2) with its rejection code and sample count (+3) and the
`taa_output`. `scripts/run_gdf_sampling_side_accum.py`, same pairs and steps:

* **A1** per stage (trace, accumulated, TAA output): mean |delta| at the event step against the
  control steps' range.
* **A2** the accumulator's state at the event step against the controls: rejected fraction, mean
  sample count, fraction reset to 1, mean effective alpha.
* **Reading, fixed now:** TRACE if the trace already exceeds its controls; ACCUMULATOR if the trace
  is inside, the accumulated output above, and A2 shows a reset beyond the controls; TAA ONLY if
  trace and accumulated are inside and the TAA output is above; NONE if every stage is inside its
  controls (H-40's excess not reproduced here, reported as such); MIXED otherwise.
* **Cannot say:** why a history would be rejected; other events or poses; any intervention.

## The sampling side of a recentre — result (2026-09-16)

`46-gdf-sampling-side`, status complete, ledger H-50. Three single-voxel clipmap-0 recentres (x- at
frame 238, y+ at 380, z+ at 668), windows of +-8 frames, `gather` fields on `shadow_wall` and
`lit_wall`, 2 repeats — identical to the last digit. G0 held (the log's recentre frame is the expected
one) and G1 held (clipmap 0's recorded centre changes at every pixel at the event step and nowhere
else). Pairing by recorded world position covered 80-100 % of each rectangle.

| event / ROI | pairs (coverage) | mean abs delta of the raw trace (+27): event vs control steps | snapped probe origin moved (control max) | rays ending near a clipmap-0 face | delta near face vs elsewhere |
|---|---|---|---|---|---|
| x- 238 shadow_wall | 16 128 (98 %) | 2.45e-04 vs 2.20e-04..2.73e-04 | 0.3 % (1.0 %) | 12.9 % | 2.6e-04 vs 2.4e-04 |
| x- 238 lit_wall | 15 424 (94 %) | 1.32e-02 vs 4.69e-03..1.55e-02 | 0.8 % (2.2 %) | 17.0 % | 1.38e-02 vs 1.31e-02 |
| y+ 380 shadow_wall | 14 616 (89 %) | 4.01e-04 vs 4.14e-04..4.84e-04 (below) | 0.0 % (0.0 %) | 32.8 % | 3.1e-04 vs 4.5e-04 |
| y+ 380 lit_wall | 16 384 (100 %) | 5.84e-03 vs 5.54e-03..5.96e-03 | 0.1 % (0.9 %) | 23.1 % | 4.5e-03 vs 6.3e-03 |
| z+ 668 shadow_wall | 13 148 (80 %) | 1.27e-03 vs 1.14e-03..1.42e-03 | 0.0 % (0.0 %) | 19.3 % | 6.9e-04 vs 1.41e-03 |
| z+ 668 lit_wall | 15 872 (97 %) | 5.63e-03 vs 5.37e-03..5.80e-03 | 0.0 % (0.0 %) | 20.1 % | 4.5e-03 vs 5.9e-03 |

* **R1:** in all six cases the recentre step's change in the raw trace output sits inside — once
  below — the range of the surrounding motion steps. The per-frame trace does not react to the recentre.
* **R2:** the snapped probe origin (+15) moved at 0-0.8 % of paired pixels at the event, within the
  control maximum: it does not respond to the recentre. **The ray-origin indicator is void by my own
  design:** its threshold (0.01 cm) is below the pairing tolerance (2 cm), so it saturates at ~100 % at
  every step and says nothing. Recorded as a design error, not as a finding.
* **R3:** the change is NOT concentrated where cascade selection can change: pixels whose rays end
  within one voxel of a clipmap-0 face (13-33 %) change as much as the rest or less.

### Follow-on on the same captures: trace vs accumulator vs TAA

`run_gdf_sampling_side_accum.py`, pre-registered above. **NONE in all twelve cases.** At the event
step every stage — the raw trace (+27), the accumulated output (+2) and the TAA output — stays inside
the range of its control steps; the accumulator rejects no more history (0.0-0.3 %, control maxima
0.1-1.3 %) and resets no more sample counts than at the controls (mean counts 60-64, and 23 at z+
`shadow_wall` in event and controls alike).

**What this settles and what it does not.** At these three single-voxel clipmap-0 recentres, in these
two rectangles, nothing in the recorded pipeline — field (H-49), sampling, trace, accumulation, TAA
output — distinguishes the recentre step from the motion around it. **H-40's excess is not reproduced
here.** That is not a refutation of H-40: it aggregated many events across the whole trajectory with
its own tracking and metric, and four of those events coincide with a recentre of clipmap 1 (frames
137, 218, 399, 516), a cascade four times coarser that neither the field comparison (clipmap 0 only)
nor this study examined. Those events are the next candidate, with the field comparison extended to
clipmap 1. One trajectory, two rectangles, three events, 2 repeats.


## Clipmap 1 — pre-registration (2026-09-17, before the captures)

H-50 left the clipmap-1 recentres as the open candidate: four of the events H-40 aggregated coincide
with one, and neither H-49 (field comparison, clipmap 0 only) nor H-50 (sampling side, clipmap 0)
looked at that cascade. Clipmap 1 is four times coarser — 78.13 cm voxels against 19.53 — so a
one-voxel move there displaces the lattice by four of clipmap 0's.

The field comparison now selects its cascade (`r.GI.GDF.Diagnostic.FieldCompareCascade`, default 0);
the gates, the parser and the reading are the ones pre-registered for H-49 on 2026-09-16, unchanged.

* capture: the boundary trajectory, `--cascade 1`, 2 repeats, output `49-gdf-field-compare-c1`.
* **E0 (new, and it can fail):** the log must contain at least one `gdfClipmap=1` recentre, and every
  field-compare line must report `clipmap=1`. If the trajectory produces no clipmap-1 recentre, the
  study stops and says so — the frames 137/218/399/516 come from H-40's own tracking on an older
  build and are not assumed to reproduce; the frames actually observed are listed either way.
* F0, F1 and R as in H-49: voxel-aligned shifts (residual < 1e-3 voxel), overlap >= 50 %, non-finite
  < 0.1 %; per event the shift, overlap, identical fraction, mean/p99/max |delta| in cm and the
  fraction over 1 % of a voxel (0.78 cm here, not 0.195).
* The reading, unchanged: **IDENTICAL** (every event identical, max |delta| = 0) / **REQUANTISED**
  (some event over 1 % of a voxel) / **NEGLIGIBLE** (differences exist, all within 1 % of a voxel).

What follows from each, fixed now: IDENTICAL closes the field side for clipmap 1 exactly as H-49
closed it for clipmap 0 — the recentre changes nothing in the overlapping field, and only the sampling
side could carry anything, which would then need its own study. REQUANTISED would be the first
measured field change in this programme and becomes the candidate, reported with its magnitudes and
nothing more.

What this cannot say either way: whether the lighting reacts; that is the sampling side, and it is not
part of this study.

## Clipmap 1 — result (2026-09-17)

`49-gdf-field-compare-c1` (cascade 1, 2 repeats, identical) and `49b-gdf-field-compare-c0-control`
(cascade 0, same build, same trajectory), both status complete.

**E0 held, and the frames reproduce.** The boundary trajectory recentres clipmap 1 at frames
**137, 218, 399 and 516** on this build — the four frames H-40 flagged on its own build, unchanged.
Every compare line reported `clipmap=1`. Three of the four events produced a comparison; the
diagnostic services one readback at a time, so frames 218 and 516 were not compared (a limit of the
instrument, recorded, not a result). F0 and F1 held: shifts whole-voxel (residual 0), overlap 15.6-16.7
million voxels, non-finite below the gate.

| cascade | event | shift | identical | mean abs delta | max abs delta | over 1 % of a voxel |
|---|---|---|---|---|---|---|
| 1 | initial placement | (-2,-14,-2) | 0.00000090 | 6031.7 cm | 12884.0 cm | 99.9994 % |
| 1 | **trajectory, single voxel (frame 137)** | (-1,0,0) | **0.00000054** | **5397.8 cm** | **12447.0 cm** | **99.9982 %** |
| 1 | **trajectory, single voxel (frame 399)** | (0,-1,0) | **0.00000054** | **5396.7 cm** | **12461.0 cm** | **99.9982 %** |
| 0 (control) | initial placement | (-8,-57,-8) | 0.00033082 | 542.1 cm | 1583.0 cm | 99.9650 % |
| 0 (control) | **trajectory, single voxel — all 20 events** | ±1 on one axis | **1.00000000** | **0.0 cm** | **0.0 cm** | **0.0000 %** |

**Verdict by the pre-registered rule: REQUANTISED — and, unlike H-49, not only from the initial
placement.** The clipmap-0 control reproduces H-49 exactly on this build (20 of 20 single-voxel
trajectory recentres bit-identical, REQUANTISED coming solely from the initial placement), which is
what validates the measurement path and the new cascade selection: the same code, the same trajectory
and the same gates give 1.000000 on cascade 0 and 5.4e-07 on cascade 1.

**What is measured:** at a single-voxel recentre of clipmap 1, the field at fixed world positions is
not preserved — 9 voxels of 16.7 million keep their value, the mean absolute change is 5397 cm and the
largest 12461 cm, against a voxel of 78.13 cm. The H-50 rider ("triggered only by the initial
placement; no change detected in the overlap of the trajectory recentres") stands for clipmap 0 and
**does not carry to clipmap 1**.

**What is not measured, and must not be read into this:**
* **Why.** A change of thousands of centimetres in a cascade whose voxel is 78 cm is not a
  requantisation of the same content; the content differs. One reading — *a hypothesis, not a
  finding* — is that most of clipmap 1's volume is never written by the composite (Sponza occupies a
  small part of a 200 m box), so it holds values that are locked to the lattice and move with the
  centre. Testing that means measuring what fraction of the volume the composite writes, and it is
  the next step, not a conclusion here.
* **Whether the lighting reacts.** H-50 measured that for clipmap-0 events and found nothing at the
  trace, the accumulator or the TAA. Clipmap 1's sampling side is untouched. A field that changes is
  not by itself an excess on screen, and H-40's excess is still not reproduced.
* The p99 column of the engine's log is a histogram estimate calibrated for sub-voxel differences and
  saturates at these magnitudes; only the mean and the max are used above.
* Two of the four events were not compared, and the study covers one trajectory and one build.

**Next candidates, for deliberation — nothing chosen:** (1) how much of clipmap 1 the composite
actually writes, which would settle the stale-volume reading cheaply; (2) the H-50 sampling-side and
accumulator study applied to frames 137/399 (and 218/516 with the readback serialisation lifted);
(3) whether cascades 2 and 3 behave like 1, which the same switch now measures.


## Same texel or same world — pre-registration (2026-09-17, before the captures)

H-53 measured that a single-voxel clipmap-1 recentre does not preserve the field at the same WORLD
positions. It cannot say why, and the magnitudes (thousands of cm at a 78 cm voxel) rule out a
requantisation of the same content. Two readings remain, and one statistic separates them:

* **the content changed** — the composite produced different values; then neither the same world
  position nor the same texel keeps its value;
* **the content stayed on the lattice** — the composite produced the same values in the same texels,
  i.e. the field did not follow the world when the centre moved; then the same texel keeps its value
  while the same world position does not.

The comparison now reports both on the same pass: the existing same-world statistic and a same-texel
one (no shift, whole volume). The two populations differ (the overlap excludes the slab that left the
volume; the texel pass covers everything), so the fractions are not complementary and are reported
separately.

* captures: the boundary trajectory, `--cascade 1` and `--cascade 0`, as in H-53. 2 repeats for
  cascade 1, 1 for the control.
* **V0 (validation on a known answer):** on **cascade 0**, whose single-voxel trajectory recentres are
  already known to be bit-identical at the same world position, the same-texel fraction must be
  **below 1.0** — the field there follows the world, so the texels must have changed. A same-texel
  fraction of 1.0 on cascade 0 would mean the new statistic is measuring nothing, and the study stops.
* **R1:** on cascade 1's single-voxel trajectory recentres, the same-texel identical fraction and mean
  and max |delta|, beside the same-world ones.

  The reading, fixed now:
    LATTICE-LOCKED  same-texel identical >= 0.99 while same-world identical <= 0.01 — the field did
                    not follow the world; the recentre moved the sampling window and left the content
                    where it was.
    CONTENT CHANGED both fractions <= 0.01 — the composite produced different values; neither reading
                    above survives and the next step is what differs between the two composites.
    MIXED           anything else, reported as such.

What this cannot say either way: whether the lighting reacts, which is the sampling side and is not
part of this study; and nothing about cascades 2 and 3, which are not captured here.

## H-53 IS WITHDRAWN, and the corrected result (2026-09-17)

**The clipmap-1 result recorded above is wrong and is withdrawn. It was my defect, not the engine's.**
When I taught the comparison to select a cascade I extended the *before* snapshot to the selected
clipmap and left the *after* snapshot hard-gated to `m_clipmaps[0]`. With `--cascade 1` the comparison
therefore measured **clipmap 1's field against clipmap 0's** — two volumes whose extents differ by a
factor of four. Every number in the H-53 table for cascade 1 is that mismatch: the "mean |delta| of
5397 cm at a 78 cm voxel" was two different cascades, and the same-texel statistic added afterwards
was computed on the same mismatched pair, so it could not catch it either (it read CONTENT CHANGED,
which is exactly what comparing two different volumes looks like).

What exposed it: the magnitudes were impossible for a distance field under a 78 cm shift — a true
distance field has a gradient of at most 1, so a one-voxel move cannot change a value by more than
one voxel — and reading the code for that reason found the gate. The earlier text stays as written;
this section corrects it.

### The corrected measurement — `51-gdf-field-compare-fixed-c1` and `-c0`

With the after-snapshot taken from the same clipmap as the before-snapshot, and with the same-texel
statistic on the same pass. Cascade 1 now produces **all four** trajectory events (137, 218, 399, 516):
the after-copy no longer has to wait for clipmap 0 to be dirty, which is why H-53 saw only three.

| cascade | event | same-world identical | same-world mean / max | same-texel identical | same-texel mean |
|---|---|---|---|---|---|
| 1 | initial placement (-2,-14,-2) | 0.00285476 | 526.76 / 1556.00 cm | 0.967760 | 6.56 cm |
| **1** | **(-1,0,0), single voxel** | **1.00000000** | **0.00 / 0.00 cm** | 0.233973 | 34.71 cm |
| **1** | **(+1,0,0), single voxel** | **1.00000000** | **0.00 / 0.00 cm** | 0.233973 | 34.71 cm |
| **1** | **(0,-1,0), single voxel** | **1.00000000** | **0.00 / 0.00 cm** | 0.211559 | 38.61 cm |
| **1** | **(0,+1,0), single voxel** | **1.00000000** | **0.00 / 0.00 cm** | 0.211559 | 38.61 cm |
| 0 (control) | initial placement (-8,-57,-8) | 0.00033082 | 542.07 / 1583.00 cm | 0.501872 | 264.55 cm |
| **0** (control) | **20 single-voxel events** | **1.00000000** | **0.00 / 0.00 cm** | 0.393-0.437 | 6.80-8.06 cm |

* **Clipmap 1 behaves exactly like clipmap 0: the overlapping field is bit-identical at every
  single-voxel trajectory recentre, and REQUANTISED is triggered only by the initial placement.** The
  H-49/H-50 rider carries to clipmap 1 unchanged.
* V0 held on both cascades: the same-texel fraction is well below 1.0 everywhere (0.21-0.44 on the
  trajectory events), so the field does follow the world when the centre moves — which is what a
  same-world identity of 1.000000 means, and the statistic is not vacuous.
* The reading of the pre-registered same-texel rule is therefore neither LATTICE-LOCKED nor CONTENT
  CHANGED: same-world is 1.0 and same-texel is not, the case the rule did not need to name because it
  is the one where nothing is wrong.

**What this closes.** The field side of the clipmap-1 candidate is closed exactly as H-49 closed it
for clipmap 0: a recentre changes nothing in the overlapping field, on either cascade. Whatever H-40's
excess is, it is not the field's values, on either cascade.

**What remains open.** The sampling side at the clipmap-1 events (frames 137/218/399/516) is still
unmeasured — H-50 measured it for clipmap-0 events only. That is the remaining route, and it is the
next candidate; nothing about it is decided here.

