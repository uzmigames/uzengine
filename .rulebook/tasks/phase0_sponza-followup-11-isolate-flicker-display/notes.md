# Notes — phase0_sponza-followup-11-isolate-flicker-display

2026-09-09. Diagnostic task: separate what the renderer computes from what the display shows. No
default changed; nothing committed.

Evidence package: `.screenshots/flicker-elimination/11-isolate-flicker-display/`
(`report.md`, `display-study.json`, `manifests/`). Ledger rows H-02b, H-08b, H-15, H-16.

## The replay is validated before it is used (1.1)

`scripts/flicker/display_model.py` implements the production chain of `tonemap_ps.hlsl`
(bloom composite → exposure → saturation → contrast → ACES Hill fit → sRGB). With bloom off the
transform is per-pixel, and the replay reproduces the engine's PNG to **1 code value, on 100 % of
the pixels**. Grain, vignette, chromatic aberration and the LUT are off by default and the replay
states that rather than ignoring it.

## Units on the same pixels (1.2)

Fixed exposure 1.0, 24 frames, ROI 192x512:

| mask | pixels | linear Rec.709 | its std | resolve YCoCg Y | display code | its std |
|---|---|---|---|---|---|---|
| bright pane | 27 712 | 8.251 | 0.0211 | 31.81 | 233.4 | 0.044 |
| dark bars | 64 441 | 0.00394 | 0.00024 | 0.0152 | 2.39 | 0.204 |
| boundary | 6 151 | 1.927 | 0.0544 | 7.43 | 182.4 | 1.576 |
| whole ROI | 98 304 | 2.449 | 0.0095 | 9.44 | 78.8 | 0.245 |

The resolve's YCoCg Y is about 3.9x the Rec.709 luminance on this content: two different
quantities that the earlier packages sometimes reported side by side without saying so.

## Exposure (1.3)

The identical HDR sequence, replayed:

| exposure | display mean | display std | px > 40 / frame | px > 8 / frame |
|---|---|---|---|---|
| 0.5x | 72.66 | 0.201 | 0.7 | 223 |
| 1x | 78.79 | 0.245 | 1.7 | 438 |
| 2x | 83.37 | 0.310 | 22.0 | 930 |

On the GPU the same three exposures leave the linear records **bit-identical**, so the count column
is a property of the display transform alone: a 4x exposure change multiplies the px > 40 count by
31x with the scene untouched (ledger H-15).

Auto-exposure settles at **66.31** on this pose and swings 0.03 % across the measured sequence, so
with the camera still it sets the gain and does not itself oscillate. Its frame-time dependence
(followup-09) is what makes it a confound *between* runs of different cost.

## Bloom (1.4)

Recorded radiance is bit-identical with bloom on and off, so everything below is display-side:

| run | dark floor (code) | dark p99 | bright (code) | contrast bright/dark | px > 40 / frame | px > 8 / frame |
|---|---|---|---|---|---|---|
| bloom off | 2.37 | 79.6 | 233.3 | 98.5 | 1.7 | 677 |
| bloom on | 5.70 | 83.8 | 233.8 | 41.0 | 1.3 | 408 |

Bloom lifts the dark floor 2.4x and halves the local contrast. Its effect on the threshold count is
**not one-directional**: here (exposure 1.0) it lowers the count, while the 2026-09-09 package
measured the opposite at the auto-exposure operating point of ~67. A count that changes sign with
the exposure cannot stand for the oscillation (ledger H-16, inconclusive as stated).

## `r.TAA.PreExposeWeights` (1.5)

The flag multiplies the resolve's HDR weights by the frame exposure, so **at a fixed exposure of 1.0
it is a no-op by construction** — the first run of this study measured exactly that and the check
failed, which is why it was repeated at the operating exposure with a control at the same exposure:

| mask | radiance mean control → flag | temporal std control → flag | std p99 control → flag |
|---|---|---|---|
| bright | 8.251 → 7.954 | 0.0211 → 0.2423 | 0.390 → 2.757 |
| dark | 0.00394 → 0.00008 | 0.00024 → 0.00000 | 0.0051 → 0.0001 |
| boundary | 1.927 → 0.499 | 0.0544 → 0.2007 | 0.293 → 1.273 |
| whole ROI | 2.449 → 2.274 | 0.0095 → 0.0809 | 0.184 → 2.576 |

At the exposure the scene actually runs at, the flag makes the bright side oscillate 11x more and
crushes the dark side to 2 % of its radiance. The lower display counts reported for it on
2026-09-09 are that crushing, not a smaller oscillation. It stays off (ledger H-08b supersedes H-08).

## The reflections-off confound (1.6)

| quantity | reflections on | off | ratio |
|---|---|---|---|
| auto-exposure operating point | 66.31 | 4.23 | 0.06x |
| dark-side radiance | 0.00394 | 0.04404 | 11.2x |
| boundary radiance | 1.927 | 2.013 | 1.05x |
| boundary temporal std | 0.05441 | 0.05373 | 0.99x |

**`r.GI.Reflections=0` is not a subtraction.** In `deferred_lighting_ps.hlsl` the specular radiance
is initialised from the prefiltered IBL cubemap and only *overwritten* by the SDF trace when
reflections are enabled, so the control swaps an occluded traced term for an unoccluded environment
lookup. Inside this interior that makes the dark side 11x brighter, which drops the auto-exposure
operating point 16x, which is what moved the display counts on 2026-09-09. The oscillation itself is
unchanged (0.99x), so reflections are not its source — the same conclusion as H-02, for a different
and now demonstrated reason (ledger H-02b).

## Radiometric attribution table (1.6)

| stage | contribution | measured |
|---|---|---|
| upstream HDR | the oscillation exists here | boundary std 0.054 radiance on a mean of 1.93 (2.8 %); bright side 0.021 on 8.25 (0.26 %) |
| exposure | a gain that decides how many pixels cross a display threshold | identical HDR: 0.7 / 1.7 / 22.0 px > 40 per frame at 0.5x / 1x / 2x |
| auto-exposure | sets that gain (66.31) and is stable when still | swing 0.03 % over the sequence; frame-time dependent between runs |
| bloom | lifts the dark floor 2.4x, halves local contrast | count moves in either direction depending on the exposure |
| tonemap curve | compresses the bright side | the pane sits at 233 code values, and its 0.26 % radiance swing is 0.04 code values |

The visible flicker is an upstream radiance oscillation of a few per cent, amplified into visibility
by the exposure an interior needs, on pixels whose own radiance is small. No stage in this list
creates it; followup-10 showed where it is created.

## Scope and limits

* Pose W only, still camera; the moving-camera behaviour of auto-exposure is followup-13's, where
  the trajectory scenes live.
* Bloom is not replayed offline (it is spatial); its effect is measured by capturing with it on and
  off on the identical HDR sequence, which is why the replay harness asserts that the replay
  *fails* when bloom is on.
* The tonemap replay covers the operators and the settings this project uses (ACES Hill fit,
  saturation 1.0, contrast 0.65); other operators are implemented but only Hill was validated
  against the engine.


## Record reconciled (2026-09-15)

The checklist reads 9/9 and the index shows this task complete. Accurate, and stated plainly here.

**Concluded.** The display and exposure stages were cleared as sources of the still-camera residual:
auto-exposure sets the gain and is stable with the camera still, and the display path was shown not
to manufacture the oscillation. The one dependence found — auto-exposure varying with frame time
between runs — is a measurement hazard for other tasks, not a flicker source, and every flicker study
since has pinned `r.AutoExposure=0` and `r.Exposure=1.0` because of it.

**Carried elsewhere, deliberately.** Moving-camera auto-exposure is followup-13's.

**Not re-run.** No experiment from this task was repeated to produce this note.
