

## The door records failed their own gate — an instrumentation defect (2026-09-15)

G1 required the recorded glass colour and opacity, blended over the recorded opaque composite, to
reproduce the pixel the TAA received. It failed on **97.6 %** of the pixel-frames the pass recorded as
drawn, every one at relative error 1.0 and always with the prediction far above the image.

It was the instrumentation. At the failing pixel-frames the image matches the **opaque composite**, not
the glass: median luminance of the recorded glass 13.7, of the image 1.31e-05, of the composite
1.32e-05. At the 7,792 pixel-frames that pass, the image matches the glass to 4.0e-4 (the half-precision
store) and not the composite. The glass fragments behind opaque geometry were being recorded as drawn.

**Why.** The records are a UAV write from the pixel shader. A pixel shader that writes a UAV loses the
implicit early depth test unless it declares `[earlydepthstencil]`, because the runtime has to run it to
honour the write; the translucent shader did not declare it. So it ran for fragments that then failed
the depth test, recorded them, and was discarded. The image was never affected. The coverage record was.

**What survives from the run.** Every recorded pixel-frame carried shading model 1 and opacity 1.0,
which confirms the source reading that the door glass takes the standard branch. Nothing else is read
from it: no constancy result, no coverage classes.

**Fix.** `[earlydepthstencil]` on the translucent shader. That restores the early depth test the shader
had before it was instrumented; it has no `discard`, `clip` or depth output, so what is visible should
not change. "Should" is checked, not assumed: the re-run carries an image-invariance control against
this run's retained capture, and it goes into a new directory so that capture is not overwritten.

**C0 was miscalibrated as written (2026-09-15, recorded before reading the re-run's log).** The
image-invariance control added for the fix demanded per-pixel, per-frame agreement to 1e-6 with the
pre-fix capture. That assumed bit-reproducibility across processes, which this ledger already
disproves: identical configurations differ in up to 795 of 786 432 recorded values, by up to 1.6e-2,
and only aggregates over a run reproduce (to about 1e-13). A per-pixel C0 can therefore fail on process
noise alone. The threshold came from a stale note, not from the ledger. Corrected definition, applied
to the same two retained captures (pre-fix `32-door-consumption/r0`, post-fix
`33-door-consumption-earlyz/r0`), with the quantity the ledger says is reproducible:

* C0 passes when the whole-region `taa_output` mean energy AND its oscillation each differ by no more
  than 1e-6 relative between the two captures. A visibility change of the kind at issue — glass
  appearing at pixels where it was hidden, luminance about 13.7 against 1.3e-05 — would move the
  region energy by orders of magnitude, so this can fail.
* Per-pixel disagreement (count of pixel-frames that differ, largest difference) is reported beside
  it for context, against the ledger's cross-process envelope, and does not decide C0.

The as-written C0 outcome is reported too, whatever it was.

**C0 failed again as redefined (2026-09-15).** On aggregates, between the pre-fix and post-fix
captures: `taa_output` energy 1.166905e-02 -> 1.166902e-02 (2.16e-06 relative) and oscillation
1.758937e-04 -> 1.758935e-04 (1.24e-06), against 1e-06. **C0 fails and is not regraded; the study
stays stopped by its own rule.** As written per pixel it had failed with max relative 1.0 and 91.46 %
of 6 422 528 pixel-frames bit-identical.

Descriptive, and not used to pass anything: after the fix the pass records 7 792 drawn pixel-frames
over 137 pixels (before: 323 056 over 2 640), none outside the pre-fix record, and 7 792 is also the
count of pre-fix pixel-frames that passed G1. Per-pixel differences are rarer where hidden glass was
recorded (4.5 % of 315 264) than elsewhere (8.8 %), and their share grows over the sequence from 4 % to
10 %; the largest is 3.8e-04 in luminance.

**Neither version of C0 had a measured tolerance.** Its 1e-06 was never set against a
same-configuration process floor on this region. That floor is measured next: two more post-fix
processes, identical configuration, retained, giving three post-vs-post pairs to set against the
three pre-vs-post pairs. Rule, fixed before the captures, for READING the floor only — it does not
change C0's verdict:

* for each aggregate (energy, oscillation): **separated** if the largest post-vs-post difference is
  below a tenth of the smallest pre-vs-post difference; **overlapping** if it is at least that
  smallest pre-vs-post difference;
* either aggregate separated -> the fix moved the image beyond process noise, a change to explain;
* both overlapping -> C0 at 1e-06 could not tell the fix from process noise: non-discriminating,
  not passed;
* anything else -> inconclusive.

**The process floor: C0 could not discriminate (2026-09-15).** Two more post-fix processes
(`34-c0-process-floor`), same binary (fnv1a64:83d15259f19c92f4) and configuration as `33/r0`:

| pair | energy (relative) | oscillation (relative) | pixel-frames differing |
|---|---|---|---|
| post 33r0 vs post 34r0 | 2.30e-06 | 2.43e-04 | 18.84 % |
| post 33r0 vs post 34r1 | 9.73e-09 | 9.40e-08 | 0.50 % |
| post 34r0 vs post 34r1 | 2.31e-06 | 2.43e-04 | 19.17 % |
| pre 32r0 vs post 33r0 | 2.16e-06 | 1.24e-06 | 8.54 % |
| pre 32r0 vs post 34r0 | 4.46e-06 | 2.42e-04 | 22.06 % |
| pre 32r0 vs post 34r1 | 2.15e-06 | 1.14e-06 | 9.00 % |

By the rule fixed before the captures, both aggregates **overlap** (largest post-vs-post against
smallest pre-vs-post: energy 2.31e-06 against 2.15e-06, oscillation 2.43e-04 against 1.14e-06). C0 at
1e-06 could not tell the fix from process noise. **C0 stays FAIL: non-discriminating, not passed.** The
door-consumption study has therefore produced no G1, S2, S4 or S5 result after the fix.

Two facts from the floor, as measured. (1) Processes of one configuration are not always close in
aggregate: 33r0 and 34r1 agree to about 1e-08, while 34r0 differs from both by 2.3e-06 in energy and
2.4e-04 in oscillation. (2) That oscillation spread is 4.3e-08 absolute, two orders under
`rear_door`'s followup-09 floor of 8.9e-06, so decisions taken against that floor are not touched by
it; tolerances set tighter than a measured same-configuration floor are.

**Proceeding needs a decision, not another result.** The study stops on C0 by its own rule. Getting
past it means either a C0 tolerance taken from this floor, or accepting the fix on the source argument
and the coverage record without an image-invariance gate. Both would be chosen after C0's failure was
seen, so neither is taken here.


## Door reading, v3 — review findings and pre-registration (2026-09-15, before its captures)

**Review findings, recorded as found.** In the v2 study and the shared criteria:

* C0 was optional: without `--compare-with` the analysis ran with C0 = None, and nothing marked the
  report incomplete.
* A C0 failure exited before writing the report, after two repeats' dumps had already been released,
  so the failure that most needed auditing survived only in a log.
* G1 compared luminance while claiming to reproduce the pixel's colour; channel errors could cancel.
* SRV indices and the shading-model id were compared with a relative tolerance instead of exactly.
* Non-finite rejection was incomplete: `exceeds_scale(inf, 1, 0.1)` returned True, and a series
  holding one NaN read as "no parity separation".
* The translucent record holds one layer per pixel, while G1's formula reconstructs one layer over
  the opaque background. `[earlydepthstencil]` settles visibility against depth; it does not make
  one record a full composition.
* **Correction:** v2's message "the instrumentation altered the image" over-claimed. A failed C0
  showed only that equivalence was not demonstrated at that tolerance. The observed difference fits
  inside the measured process variability, and that does not prove equivalence either.

**Fixed before the captures.** Every criterion in `criteria.py` rejects non-finite input, with a test
for each case above and a negative control (`test_criteria_logic.py`). The translucent pass records
a layer count (LIGHTING_EXTRA +10, cleared by the deferred pass). It is a read-modify-write, so
overlap between primitives of one draw call can be undercounted, never overcounted. The reader now
raises when a channel is newer than the file, with a test. The study was rewritten; its docstring
holds the full design. In summary:

* **C0, an equivalence test, runs inside every study run.** Three repeats with `--region-fields taa`
  (records off) against three with `components` (records on), interleaved, one binary, every dump
  retained. Quantities: `taa_raw` and `taa_output`, over the region and over the glass mask, with
  per-channel mean energy and luminance oscillation. Margins come from rules already in use: energy
  within 1 % (candidate criterion C5); oscillation within max(3 × 95 % half-width, 5 % of the mean)
  of the records-off repeats (followup-09's floor rule, about 8.9e-6 for `taa_output` on the region;
  applying it to `taa_raw` and the glass mask is a transfer, stated as such).
* **C0 per quantity:** EQUIVALENT when every off × on difference is within the margin; DIFFERENT when
  every one exceeds both the margin and the spread within a condition; otherwise INCONCLUSIVE.
  **C0 status:** PASSED / NOT EQUIVALENT / INCONCLUSIVE / NOT RUN, and anything but PASSED stops the
  study.
* **What C0 does and does not show:** it validates the visual cost of the records, with
  `[earlydepthstencil]` present in both conditions. It does not isolate the effect of adding the
  attribute, and will not be presented as doing so.
* **Gates, in order:** G0 the pass drew in every repeat; O1 the layer and coverage records agree;
  O2 relevant overlap (two or more layers AND last-layer opacity < 1 — at opacity 1 the blend
  multiplies what lies beneath by zero) is excluded and listed; G1 reproduces `taa_raw` PER RGB
  CHANNEL to 2e-3 relative.
* **The reading**, at pixels drawn in every frame and at pixels drawn in some frames:
  * coverage: the classes, jitter-index locking, and agreement across repeats;
  * material: opacity, albedo, albedo SRV index, shading-model id, emission;
  * lighting: IBL diffuse, irradiance SRV index, normal, N.L;
  * produced colour: the source colour;
  * identifiers compared exactly, real quantities constant at <= 1e-6 relative, each varying quantity
    tested for jitter-index locking;
  * S5: oscillation before and after the TAA, with energy and std share per class.
* **Reports:** written on every exit, with `status` and `stopped_at`.

**Verification before the v3 captures (2026-09-15).** `test_criteria_logic.py` 28 passed (three
new: the reproduced mistakes, eleven non-finite cases all rejected, a finite negative control);
`test_region_format.py` 12 passed (new: a channel newer than the file raises; the older file still
reads its own last record); build exit 0 with the layer-count shader text present in the binary;
`test_binary_layout.py` 6 passed (file 47 records, layout (2, 5, 0, 0, 11, 29), all 47 channels
readable). One incident: the first build failed to install (AccessDenied) because
`test_correction_gates.py`, started alongside it, was capturing with the executable. That script is a
GPU capture suite, not a unit test; it was stopped by a 300 s timeout twice and has **not** been run to
completion against the stricter criteria. Its only use of them is one finite `exceeds_scale` call. The
two partial temporary captures it left were removed.

## Door reading, v3 — result (2026-09-15)

`35-door-reading`, status **complete**, no restrictions. Six captures on one binary
(fnv1a64:cf8b6a4b4c80e0ec), every dump retained (7.2 GB), report `door_reading.json`. Ledger H-45.

| gate | result |
|---|---|
| C0 equivalence, records off vs on | **PASSED** — all 16 quantities EQUIVALENT. Region energy differs by at most 2.5e-06 relative (margin 1 %); `taa_output` region oscillation by 3.5e-10 absolute (margin 8.8e-06); every difference is the size of the spread within a condition |
| G0 drawn in every repeat | 137 pixels in each |
| O1 layer record vs coverage | 0 disagreements in every repeat |
| O2 overlap | no pixel-frame with two or more layers; nothing excluded |
| G1 per RGB channel | worst 6.75e-04 (blue, frame 237); 0 of 23 376 pixel-frames over 2e-3 |

**What varies: coverage, and nothing else recorded.**

* **Coverage.** Of the 137 glass pixels, 12 are drawn in every frame and 125 in some frames only. All
  125 repeat their coverage exactly with the jitter index, and the pattern is identical in all three
  repeats.
* **Material:** constant at every glass pixel — opacity 1, albedo, albedo SRV index 0, shading-model
  id 1, emission.
* **Lighting:** constant — IBL diffuse, irradiance SRV index 37176, normal, N.L.
* **Produced colour:** constant. Identifiers match exactly; real inputs stay within 1.4e-07 relative
  (one to two float32 ULPs), under the 1e-06 bar.
* **Contrast.** At the 125 partly covered pixels, `taa_raw` luminance is 13.72 when the glass is drawn
  and 3.18e-05 when it is not.

| class | pixels | oscillation before TAA | after TAA | energy before | after | share of summed std after TAA |
|---|---|---|---|---|---|---|
| region | 50 176 | 1.662e-02 | 1.759e-04 | 2.698e-02 | 1.167e-02 | 100 % |
| drawn every frame | 12 | 5.3e-15 | 5.37e-02 | 13.72 | 3.40 | 7.3 % |
| drawn in some frames | 125 | 5.71 | 1.91e-02 | 5.36 | 0.614 | 27.1 % |
| never drawn | 50 039 | 2.40e-03 | 1.16e-04 | 1.04e-02 | 9.35e-03 | 65.6 % |

**What this shows, and what it does not.**

* **Nothing was frozen**, so no cause is demonstrated.
* **The 12 always-drawn pixels** receive a constant input (5.3e-15) and still oscillate after the TAA
  (5.37e-02). Their output variation therefore comes from what the TAA combines with that input
  (neighbourhood, history, jitter weights), not from their own shading. Their output energy is 3.40
  against an input of 13.72.
* **The never-drawn pixels** carry their own pre-TAA variation (2.40e-03), so this study does not
  establish how much of their output oscillation comes from the glass. The shares are a distribution,
  not causal shares.
* **Limits:** the layer count can undercount overlap inside one draw call. C0 validates the records'
  visual cost with `[earlydepthstencil]` in both conditions; it does not isolate the attribute's
  effect.

**Intervention chosen (step 4), not implemented.** The factor that varies is the glass's coverage, so
the intervention acts on coverage alone: render the forward translucent pass with a fixed, unjittered
projection, leaving the opaque passes and the TAA as they are. Material, lighting and the produced
colour are constant, so reducing the glass emission would change magnitude, not variation. The TAA's
own clip was already rejected on motion (H-42). Before running it, the pre-registration must settle:

* **Which edge moves.** Coverage can change with the glass's own rasterisation or with the depth test
  against jittered opaque geometry, and a fixed translucent projection only fixes the first. The
  coverage record measures this directly.
* **Costs:** the glass edge loses TAA anti-aliasing; the unjittered glass is offset against the
  jittered depth by up to half a pixel; energy (C5) and motion (G6).
* **The reading, stated before the numbers:** if the oscillation falls, that demonstrates the part
  coverage variation plays; if it does not, that does not show coverage plays no part.

**Task 12 stays open.** No result here corrects the flicker.


**Corrections from review (2026-09-15).**

* **The intervention choice is withdrawn.** "Render the translucent pass unjittered" is not a choice
  the data make. Saying it was the only option acting on the factor that varies was wrong. Stabilising
  the coverage is one family of options; improving how the TAA reconstructs and accumulates that
  coverage is another, and the reading does not choose between them. An unjittered translucent pass
  would also change three things at once — coverage, alignment with the jittered depth, and the
  position the TAA expects to reconstruct — so it is not a single-factor intervention. It can become
  a diagnostic only if the rasterisation turns out to be what moves, and only with those misalignments
  recorded, since a fall in oscillation could come from them too.
* **The energy drop at the 12 always-drawn pixels (13.72 in, 3.40 out) does not by itself show a loss
  of energy.** The filter can move energy from bright pixels into their neighbours. Whether it did is
  measured over the glass and its neighbourhood (below), against the cycle-averaged raw input — the
  reference followup-14 used, where `rear_door` held 42 % of the correct bright energy.
* **Validation is still pending.** `test_correction_gates.py` has not run to completion. The criteria,
  format and layout tests do not replace it.
* **C0's scope.** It holds for the records with `[earlydepthstencil]` present, exactly as declared.

## Next step, pre-registered before its captures and before its analysis (2026-09-15)

**1. Rasterisation or depth test, at the 125 partly covered pixels** (`run_door_coverage_depth.py`,
`36-door-coverage-depth`). A short diagnostic, not a correction.

* **The switch.** A new diagnostic CVar, `r.Translucency.DepthTest` (default 1), builds the translucent
  pipeline without a depth test when set to 0. Every rasterised translucent fragment then reaches the
  shader and is recorded. The layer record also stores the last layer's depth (`+10.y`).
* **Captures.** Frames 237..268 (four jitter cycles), `components` fields, two repeats per condition,
  interleaved, dumps retained. Nothing from the depth-off image is read.
* **Gates, in order:**
  * D0: one binary, and the CVar value confirmed in each manifest.
  * D1: default coverage identical to H-45's at the same frames, and layer/depth records consistent.
  * D2: the switch acted (depth-off draws something depth-on does not).
  * D3: depth-on coverage is a subset of depth-off.
  * D4: repeats identical.
  * D5: recorded depth agrees between conditions to 1e-6 where both draw one layer.
* **Classification.** At each target pixel-frame not drawn with the depth test:
  * ABSENT: no fragment without the test either — not rasterised.
  * HIDDEN: exactly one fragment, at a depth inside the pixel's own glass band (its drawn-frame depth
    range widened by its largest step to a drawn neighbour) — present, rejected by the depth test.
  * OTHER: outside the band, or two or more layers — counted, never merged.
* **Per pixel:** raster edge, depth edge, or mixed. No winner threshold. A fixed projection could act
  only on ABSENT pixel-frames and is a later, separately pre-registered step, taken only if
  rasterisation matters.

**2. Energy over the glass and its neighbourhood** (`run_door_energy_neighbourhood.py`, existing
`35-door-reading` data).

* **Reference:** the cycle-averaged raw input.
* **Masks:** glass G; Chebyshev rings 1..8; neighbourhoods N_1, N_2, N_4, N_8, N_16; far background
  beyond 16 px; the rectangle.
* **Rule:** C5's 1 % per mask, in every repeat (CONSERVED / NOT CONSERVED / INCONCLUSIVE).
* **Reading, fixed now:**
  * G not conserved but some N_k with k <= 8 conserved: redistribution within k px.
  * G and every N_k up to 16 not conserved with the same sign: not recovered within 16 px.
  * Otherwise: inconclusive.
  * The far background is reported; if it is not conserved, ring sums mix two effects and the
    background-corrected recovery is descriptive only.
* **Descriptive TAA stages:** raw, reconstruction (`taa_filtered`), history, clamped history and
  output — energy and oscillation for the glass classes, rings 1..4 and the far background.
* **Check:** the records-off captures repeat the energy verdicts.

## Energy over the glass and its neighbourhood — result (2026-09-15)

`35-door-reading/door_energy_neighbourhood.json`, status complete, ledger H-46. The glass is 33 px
from the rectangle's border, so no mask is clipped.

| mask | pixels | output / cycle-averaged raw input | C5 (1 %) |
|---|---|---|---|
| glass | 137 | 0.1409 | NOT CONSERVED |
| N_1 / N_2 / N_4 / N_8 | 539 / 1 051 / 1 878 / 3 112 | 0.1460 / 0.1460 / 0.1460 / 0.1461 | NOT CONSERVED |
| N_16 | 5 439 | 0.1465 | NOT CONSERVED |
| far background (> 16 px) | 44 431 | 0.8940 | NOT CONSERVED |
| rectangle | 50 176 | 0.4326 | NOT CONSERVED |

The three repeats agree to four digits, and the records-off captures give the same verdicts.

**Reading, by the pre-registered rule: not recovered within 16 px.** The drop at the 12 pixels is not
a redistribution to the neighbours:

* **Ring 1** does receive energy — its output averages 1.06e-02 per pixel against a reference of
  1.9e-05. Corrected by the far background's ratio, that is +4.27 against a glass deficit of 717.3,
  or 0.6 %.
* **Rings 2 to 8** receive nothing.
* **The far background** is itself below its reference (0.894), so the corrected recovery is
  descriptive. Without the correction the verdict is the same: N_16 holds 14.65 % against 14.09 % for
  the glass.

**Where it happens (descriptive):**

| mask | energy: reference → reconstruction → history → output | oscillation: same stages |
|---|---|---|
| glass drawn every frame | 13.72 → 4.07 → 3.40 → 3.40 | 5.3e-15 → 1.99 → 5.37e-02 → 5.37e-02 |
| glass drawn in some frames | 5.36 → 0.878 → 0.614 → 0.614 | 5.71 → 0.731 → 1.92e-02 → 1.92e-02 |
| ring 1 | 1.9e-05 → 1.95e-02 → 1.06e-02 → 1.06e-02 | 2.3e-05 → 2.66e-02 → 1.07e-03 → 1.07e-03 |
| far background | 1.166e-02 → 1.141e-02 → 1.042e-02 → 1.042e-02 | 2.69e-03 → 1.81e-03 → 1.19e-04 → 1.19e-04 |

* **At the pixels drawn every frame**, the energy drop and the oscillation both first appear in the
  reconstruction (`taa_filtered`), whose input is constant. History accumulation then damps the
  oscillation.
* **In the far background**, the reconstruction keeps 97.8 % of the energy and the step to history
  takes it to 89.4 %.
* **Limits:** this localises the effect and does not explain it. No reconstruction setting was varied
  here; H-12 and H-22 varied them, on other masks. It supports the review's point that the
  reconstruction at the edges must be observed, and does not yet choose between stabilising coverage
  and changing the reconstruction.

## Rasterisation or depth test — result (2026-09-15)

`36-door-coverage-depth/coverage_depth.json`, status complete, ledger H-47. Four captures on one binary
(fnv1a64:31922544b00f6728), dumps retained (1.8 GB).

| gate | result |
|---|---|
| D0 identity and switch | one binary; `r.Translucency.DepthTest` confirmed 1 / 0 in every manifest |
| D1 default unchanged | depth-on coverage identical to H-45 at frames 237..268 (0 pixel-frames differ); layer and depth records consistent in both conditions |
| D2 the switch acted | without the depth test 2 640 pixels are drawn, against 137 with it — the same 2 640 that v1 recorded without early depth testing |
| D3 superset | 0 violations in both repeats |
| D4 repeats | coverage identical in both conditions |
| D5 depth record | identical where both conditions draw one layer (3 896 pixel-frames, worst relative 0) |

**Classification**, at the 2 436 pixel-frames in which one of the 125 partly covered pixels is not
drawn, identical in both repeats:

| class | pixel-frames | share |
|---|---|---|
| HIDDEN — the glass fragment exists, and the depth test rejects it | 2 376 | 97.5 % |
| ABSENT — no fragment is rasterised | 4 | 0.2 % |
| OTHER — one layer outside the pixel's band | 56 | 2.3 % |
| OTHER — two or more layers | 0 | 0 % |

**Per pixel:** 122 depth edge, 0 raster edge, 1 mixed ((33, 163), holding all 4 ABSENT pixel-frames),
and 2 with OTHER only ((54, 139), (54, 151)). The bands are narrow — median width 1.3e-07, largest
neighbour step 3.5e-07 — so a HIDDEN match places the fragment on the pixel's own plane. Two target
pixels have no drawn neighbour and keep only their own range. The report does not say whether they
are the two OTHER pixels, and OTHER was not reclassified.

**Reading.** What moves the glass coverage is the depth test, not the rasterisation: in 97.5 % of the
frames in which the glass is missing from these pixels, its fragment exists and the opaque depth at
the pixel rejects it, and that depth changes with the jitter.

* **Step 2 is not indicated.** A fixed translucent projection could act on at most the 4 ABSENT
  pixel-frames, so the condition for testing it is not met, and it was not built.
* **Not identified:** which opaque surface hides the glass.
* **Still open:** stabilising the coverage — now a question about the opaque depth edge, not the
  translucent pass — or changing how the TAA reconstructs and accumulates it (H-46). No intervention
  is chosen.

**Still pending:** `test_correction_gates.py` has not run to completion.


## Review decision: no correction is chosen (2026-09-16)

H-46 and H-47 located two effects; neither shows which intervention fixes both without introducing a
bias. The task stays open and no intervention is built.

* **Next diagnostic:** identify the opaque surface that rejects the glass, and compare its depth with
  the glass fragment's depth along the jitter.
* **A second line, conceptually parallel:** the TAA reconstruction with a controlled input.
* **"Unjittered translucency" stays unjustified** and is not to be built now. It changes coverage,
  depth alignment and the position the TAA reconstructs at once.
* **Two standing limits on how the evidence may be used:** C0 holds only in the scope declared, and
  the 12 always-drawn pixels are not proof of global energy conservation.

## The surface that rejects the glass, and what moves (2026-09-16)

`scripts/run_door_occluder_depth.py`, `41-door-occluder-depth`, all four controls passing. Two
conditions, one process each, 32 frames, pose A: `depth_on` (shipping) and `depth_off`
(`r.Translucency.DepthTest=0`, which records every rasterised fragment, its image never read).

| control | result |
|---|---|
| V0 the opaque depth record agrees with the independent geometry record | non-zero where there is geometry 100.00 %, zero where there is sky 100.00 % |
| V1 where the glass passed, it is nearer | 1564 / 1564 |
| V1b where the glass was rejected, it is NOT nearer | 2432 / 2432 |
| V2 the depth buffer is identical in both conditions | worst 0.000e+00 |

**What moves is the opaque geometry, not the glass.** At the 125 partly covered pixels, over the
frames where the glass is rejected versus the frames where it passes:

| quantity | median shift | max |
|---|---|---|
| opaque depth (reversed-Z) | +1.300e-03 | 1.313e-03 |
| glass fragment depth (reversed-Z) | +6.453e-09 | 2.160e-07 |
| opaque radial distance | +2842.95 cm | 2852.40 cm |

The glass fragment sits still to within 2.2e-07 of clip depth. The opaque record swings the whole way
from "nothing there" to "a surface at about 28.4 m".

**The two states of these pixels.**

* **Glass visible (1564 pixel-frames):** there is NO opaque geometry behind it — 0 % have a geometry
  record, the depth is the far plane, and the margin is the glass against nothing (+1.288e-03). The
  pixel is seeing sky through the doorway.
* **Glass rejected (2432 pixel-frames):** 100 % have geometry, at a median depth of 1.300e-03 against
  the glass's 1.288e-03 — the opaque surface is in front by 1.169e-05 of reversed-Z depth.

**The surface, as far as the dump identifies it:** in the rejecting frames its normal is ~(0, 1, 0) at
2840–2850 cm, consistent across the pixels (the largest groups are 24 and 15 pixels at those two
distance buckets). In the passing frames there is no surface at all, so the normal is (0, 0, 0) and
the angle between the two states is undefined — the script reported `nan`, which is the correct answer
to a comparison with nothing, and is now labelled as such.

**So the mechanism of the coverage change is an opaque silhouette against the sky**, crossing these
pixels as the jitter moves the sample position, roughly 26–30 cm in front of the glass. It is not the
glass's own rasterisation, and it is not the glass moving.

**Limits, fixed before the numbers were read.** This is the mechanism by which coverage changes, not a
cause of the flicker, and it says nothing about what any intervention would do. One pose, one
rectangle, 32 frames, one process per condition. No depth is converted to centimetres for the glass:
the near plane is not confirmed, and the comparison does not need it.

**Two earlier attempts failed this study's own V1 and are kept, because each one shows a way the
comparison can be wrong.**

* **Attempt 1** compared the fragment's clip depth with the opaque RADIAL DISTANCE in centimetres
  through an unprojection: 0 of 1564, with the glass apparently up to 2871 cm behind the surface it
  was drawn in front of. A matrix convention, a near plane and a depth encoding all sat between the
  two quantities.
* **Attempt 2** compared it with the TAA's centre depth, which is in the right space but is dilated to
  the nearest of a five-sample cross for reprojection, so at a glass pixel it can belong to a
  neighbour: 0 of 1564 again, deficit 1.32e-05 — exactly what a neighbour at nearly the same depth
  looks like.
* The fix was not to loosen V1 but to record the right number: the deferred pass now writes the
  pixel's OWN opaque depth into the translucent record's free z component.
* **A third guard was wrong too, and its failure was informative.** V0 first demanded a non-zero
  opaque depth nearly everywhere; it read 60.90 % and stopped the study. The zeros were not a stale
  binary — they are the far plane where the pixel sees sky, which is exactly the state this study went
  on to describe. V0 now checks the record against the geometry record instead of against a fraction.


## Review decision after the occluder diagnostic (2026-09-16)

The new evidence identifies the alternation between opaque and sky in the depth test at the 125
pixels. On its own it does not demonstrate a defect in that coverage, and it does not justify either
"unjittered translucency" or altering the opaque coverage. **No intervention is chosen.**

**Next step: diagnose the TAA reconstruction.** A capture with TAA_EXTRA that records, in the SAME
frames: the centre input, every tap the filter actually uses, their weights, the jitter, the
normalisation, and the reconstructed result. Then:

1. recompute the reconstruction from those records and check it against the shader's own output;
2. separate the contribution of the NEIGHBOUR VALUES from that of the WEIGHTS by controlled
   recombinations of the captured data — a mathematical diagnostic, not proof that any visual change
   would work.

A constant input at the 12 always-drawn pixels is not a constant input to the FILTER: its neighbours
can vary, and that distinction is what this measurement must resolve. Pose, region and repeats stay
explicit; nothing is generalised beyond them.

## TAA reconstruction study — pre-registration (2026-09-16, before the captures)

`scripts/run_taa_reconstruction_study.py`. The full design is its docstring; the commitments:

* **Instrumentation.** TAA_EXTRA grows from 4 to 11 records: the five plus taps (up, left, centre,
  right, down) as linear RGB with, in w, the weight the shader ACTUALLY applied to each; the jitter
  offset the filter consumed, the sum of the weights and the spatial multiplier; the exposure scale,
  the HDR weight exponent and the reconstruction mode. Layout (2, 5, 1, 11, 11, 29).
* **Capture.** Pose A, `rear_door` (960, 592, 224, 224), frames 237..268, `--region-fields full`,
  2 repeats, dumps retained. Classes from the same capture's coverage: `always` (glass in every
  frame), `some` (glass in some frames), `ring1` (8-neighbours of glass, not glass), `rest`.
* **T0** the records describe the plus filter: mode 0 everywhere, positive sums, the five weights sum
  to the recorded sum within 1e-5 relative. Another mode is refused.
* **T1** the reconstruction recomputed from the records — sum of YCoCg(tap) x weight over the sum of
  weights, converted back — matches the shader's `taa_filtered` within 1e-4 relative at every
  pixel-frame. If it fails, nothing below is reported.
* **T2** the weights recomputed from (jitter, k, Y, exposure, exponent) match the recorded weights
  within 1e-4 relative, pinning the formula.
* **Recombinations** with the formula that passed T1, per pixel over the 32 frames: A actual/actual
  (must equal the record); B taps frozen at their temporal mean, weights actual; C taps actual,
  weights frozen; D both frozen (must have zero oscillation); E centre frozen, neighbours and weights
  actual. Metric: per-pixel temporal std of luminance, class mean, worst repeat; energy = class mean.
  **B and C are not additive shares of A** and are reported as what remains when one factor is held.
* **Also reported:** per class, the fraction of frames in which each neighbour tap is brighter than
  10x the never-drawn median — the neighbourhood's coverage, distinct from the centre's.
* **What it cannot say:** whether any change would look better. One pose, one rectangle, 32 frames,
  2 repeats.

## TAA reconstruction study — result (2026-09-16)

`42-taa-reconstruction`, status complete, 2 repeats, ledger H-48.

| gate | result |
|---|---|
| T0 the records describe the plus filter | mode 0 in every frame; all sums positive; five weights vs recorded sum, worst 1.66e-07 |
| T1 reconstruction recomputed from the records vs the shader's `taa_filtered` | worst relative 1.35e-06 (tolerance 1e-4) |
| T2 weights recomputed from (jitter, k, Y, exposure, exponent) vs recorded | worst relative 8.39e-07 (tolerance 1e-4) |
| D both factors frozen | oscillation 0.000e+00 in every class |

Classes from the capture's own coverage: `always` 12, `some` 125, `ring1` 402, `rest` 49 637.

**Oscillation of the reconstruction** (per-pixel temporal std of luminance, class mean, worst repeat),
with the same formula that passed T1:

| class | A actual | B values frozen, weights actual | C weights frozen, values actual | E centre frozen | energy (A) |
|---|---|---|---|---|---|
| always (12) | 1.985 | 1.681 | 1.519 | 1.985 | 4.074 |
| some (125) | 0.731 | 1.576 | 3.166 | 1.457 | 0.879 |
| ring1 (402) | 2.66e-02 | 3.10e-01 | 3.12e-01 | 2.66e-02 | 1.95e-02 |
| rest (49 637) | 1.62e-03 | 7.68e-04 | 2.02e-03 | 8.06e-04 | 1.02e-02 |

**The neighbourhood's coverage** (fraction of frames a tap is brighter than 10x the never-drawn
median, repeat 0): at the 12 always-drawn pixels the centre is glass in 100 % of frames and the
neighbours in 49 % (up), 17 % (left), 31 % (right), 49 % (down); at the 125 partly covered pixels the
centre is glass in 39 % and the neighbours in 18-31 %; at the ring, neighbours in 4-9 %.

**What the arithmetic shows.**

* **At the 12 pixels with a constant centre, the filter's input is not constant.** The neighbours
  are glass in a fifth to a half of the frames. Holding the tap VALUES at their means and letting
  only the weights vary leaves an oscillation of 1.681 (B); holding the WEIGHTS and letting the
  values vary leaves 1.519 (C). Each factor alone produces variation of the same order as the actual
  1.985. E equals A exactly, as it must when the centre is already constant.
* **At the partly covered pixels and at the ring, the two factors partly cancel.** Freezing either
  one INCREASES the oscillation: `some` 0.731 -> 1.576 (B) / 3.166 (C); `ring1` 0.0266 -> 0.310 (B)
  / 0.312 (C). In the recorded formula the weight of a tap is exp(-k|offset - jitter|^2) /
  (Y x exposure + 4)^e: when a tap turns to glass its Y rises by orders of magnitude and its weight
  falls, so its value swing arrives damped by its own weight swing. Hold the weights at their mean
  and the swing passes at mean weight. That is what C measures, and it is why A is smaller than
  either factor alone there.
* **B and C are not additive shares of A**, as pre-registered: the filter is a ratio of sums.

**Consistent in direction with an earlier measurement, and not the same experiment:** H-22 found
that removing the HDR weighting (exponent 0) raised `rear_door` residual by 119.7 %. Here, weights
frozen at their mean - a different manipulation - also raises the reconstruction's oscillation at
the partly covered pixels.

**What this does not say.** Nothing about whether any change to the filter, the weights or the
coverage would look better, and nothing beyond this pose, this rectangle, 32 frames and 2 repeats.
No intervention is chosen.



## Review decision: the diagnostic stage closes, the task stays open (2026-09-16)

T1/T2 establish that the analysis reproduces what the shader consumed and computed. The results rule
out the simplistic conclusions: freezing the weights or removing the luminance weighting is not
justified, because at the partly covered pixels and at the ring the interaction between values and
weights already damps the oscillation. **B and C demonstrate sensitivity to the manipulations; they
do not attribute causal percentages to A's oscillation, and there is no basis to name either as "the
responsible".**

* No coverage or filter change is chosen.
* **Before any correction is tried, the reference and the acceptance criteria must be defined:**
  temporal stability TOGETHER WITH energy fidelity, preservation of detail, and absence of trails. A
  lower oscillation on its own can reward a darkened or blurred image.
* Task 13's defined controls and followup-01's per-probe comparison proceed; neither depends on
  choosing a TAA intervention now.


## Review decision (2026-09-16): acceptance criteria before any intervention

Fix, before any correction is tried: temporal stability; energy against a DECLARED reference; detail
preservation; trails and disocclusion. Include regions and conditions beyond the diagnostic pose. No
threshold is to be invented after a result is seen.

## Acceptance criteria for any candidate correction (2026-09-17, before any correction is tried)

Fixed now, before a candidate exists, per the review decision of 2026-09-16. They apply unchanged to
every candidate; a candidate that fails one is rejected and the result recorded. **No threshold here
is to be revised after a result is seen.** A candidate may be re-evaluated only under a new
pre-registration that states what changed and why, leaving this one and its results in place.

### The declared reference

**R = the per-pixel mean, over one complete jitter cycle, of the PRE-TAA resolved output, at the same
pixels, in the same capture** (`taa_input`; the cycle is the renderer's own 8-frame sequence). This is
the reference used already in H-46. What it is not: a ground truth, a supersampled render, or a
statement that the unfiltered image is correct. It is the renderer's own unfiltered temporal mean, so
it carries whatever the rasteriser and the lighting do; it is a reference for **the reconstruction's
energy**, and for nothing else. Every energy number below is measured against it, per ROI, over a
whole number of cycles.

### The four families

| # | criterion | threshold, fixed now | why this number, before any result |
|---|---|---|---|
| **E1** | energy at the defect: the TAA output's per-pixel temporal mean over the cycle, against R, at the glass ROI | within **1 %** | the tolerance already pre-registered as C5 in this task (H-46); using anything looser would be choosing a tolerance to fit |
| **E2** | energy elsewhere: the same quantity at each control ROI, against its own pre-correction value | within **1 %** | same tolerance, applied as non-regression: the fix must not move energy where there is no defect |
| **T1** | temporal stability at the defect: `criteria.oscillation` at the glass ROI against its pre-correction value in the same conditions | **<= 50 %** of baseline | the defect must be at least halved to count as fixed; a marginal change is not a correction. Between-process variability measured for this metric is 2.4e-4 relative, so 50 % is far outside noise |
| **T2** | temporal stability elsewhere: the same at each control ROI | **<= 110 %** of baseline | a fix that steadies the glass by damping the whole image is not accepted |
| **D1** | detail: mean Sobel gradient magnitude of the **cycle-averaged** output (stable under flicker), per ROI, against pre-correction | **>= 95 %** of baseline at every ROI, glass included | a correction may not buy stability with blur; measured on the averaged image so the baseline's own flicker does not inflate it |
| **M1** | trails / disocclusion: frames until the per-pixel error against the settled value falls under tolerance, after motion and after a cut | **<= baseline + 2 frames**, and **not censored** (`criteria.recovery_censored`) | two frames is the smallest margin that survives frame-to-frame jitter; the censoring guard exists because two censored counts already read as an equal recovery once (2026-09-15) |
| **M2** | disocclusion overshoot: max per-pixel error in the 8 frames after a cut, per ROI | **<= 110 %** of baseline max | a fix may not trade trails for a brighter halo |

### Regions and conditions — beyond the diagnostic pose

All ROIs identical across conditions and candidates; 2 repeats each; every criterion evaluated on
every condition where it applies.

| condition | project | ROIs | criteria |
|---|---|---|---|
| A, still (the diagnostic pose) | `sponza-flicker-a` | `rear_door` glass mask + `shadow_wall`, `lit_wall` as controls | E1, E2, T1, T2, D1 |
| W, motion | `sponza-flicker-w-motion` | `shadow_wall`, `lit_wall` | E2, T2, D1, M1, M2 |
| teleport (hard cut) | `sponza-teleport-camera` | `shadow_wall`, `lit_wall` | M1, M2 |

### Controls that must be able to fail

Before any real candidate is judged, the gate itself is validated on two candidates whose outcome is
fixed in advance:

* **no-op** (the build with no change): must **FAIL T1** — it does not reduce the oscillation. A gate
  that passes the no-op is measuring nothing.
* **blur** (a 3x3 box filter on the resolved output, diagnostic only): must **FAIL D1** while passing
  T1. A gate that accepts the blur is rewarding a darkened, smoothed image.

If either control does not behave as stated, the gate is broken and no candidate is judged until it is
fixed — and that fix is recorded here, not silently applied.

### What these criteria do not decide

Which correction to try, whether any is warranted at all, and anything about the DDGI/radiance-cache
flicker, which is a different mechanism (H-49/H-50/H-51) and not covered by this gate.
