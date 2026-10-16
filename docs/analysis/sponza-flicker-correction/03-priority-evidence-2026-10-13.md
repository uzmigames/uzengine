# H-27 priority evidence, 2026-10-13

This is the requested first delivery: completed baselines and harness
controls, not a completion claim for the owed diagnostic set. H-27 acceptance
is reserved for the user. No production correction or scene recalibration
was applied.

## Build and captures

Vendored Zig 0.15.2 build: EXIT=0; `Shader integrity OK (139 shaders verified)`.
Executable identity in every final capture: `fnv1a64:d631082fedeeed66`.
Six independent serial GPU captures completed: three W and three A.
Every analysed sequence has 256 contiguous valid frames, 237 through 492,
at 1920x1080, jitter length 8, fixed exposure 1.

The initial smoke capture failed at the signed sentinel conversion, fixed
as described in 02. The next capture exposed a stale assumption about
preload length: requesting `last - 37 + 8` ended at frame 484 rather than
492 on the current build. Final runs request `last + 32` and still analyse
only the preregistered range. Failed runs remain separate and are excluded.
The baseline command printed `BASELINE_EXIT=0` and `COMPLETE baseline`.
Its monitor subscription expired, but the completed capture/report evidence
and the command's exit sentinel were collected. The priority report exited 0.

Artifacts:

- `.screenshots/flicker-correction-20261013-r2/measurement.json`
- `.screenshots/flicker-correction-20261013-r2/priority-evidence.json`
- `runtime/flicker_measurement_baseline_r2.log`
- `runtime/flicker_priority_evidence.log`
- Each capture's manifest, run log, compressed raw records and reference PNG.

## Current residual and thresholds

Primary metric: each pixel's temporal standard deviation of TAA-output
linear Rec.709 luminance, averaged over the full followup-09 ROI.
The current floor is max(3 * Student-t 95% half-width, 5% * mean).

| ROI | Mean residual, 3 repeats | Historical followup-09 threshold | Current floor | Repeat 95% half-width |
|---|---:|---:|---:|---:|
| W window | 0.008511417892 | 0.000480000000 | 0.000425570895 | 0.000000134477 |
| A right_shadow | 0.000406731113 | 0.000021000000 | 0.000020336556 | 0.000000001757 |
| A lit_floor | 0.001008235843 | 0.000050000000 | 0.000050411792 | 0.000000005419 |
| W shadow_wall | 0.000021479545 | 0.000000470000 | 0.000001073977 | 0.000000000141 |

Window repeat values: 0.008511355406, 0.008511450453, 0.008511447815.
Their full range is 9.504667444e-8, or approximately 0.00112% of the mean.
The window magnitude is 17.73 times the historical threshold and 20 times
the new floor. These are thresholds for detecting a CHANGE, not acceptance
limits on absolute flicker; the factor 20 follows from the binding 5% term.

The separate frame-to-frame metric is the spatial mean absolute luminance
step, measured from repeat 0 at every frame pair. Do not compare these
different quantities directly with the primary metric's threshold.

| ROI | Mean absolute frame step | 95th percentile | Maximum |
|---|---:|---:|---:|
| W window | 0.008729032935 | 0.012223966420 | 0.012224567123 |
| A right_shadow | 0.000428024373 | 0.000590187241 | 0.000590508513 |
| A lit_floor | 0.001359467149 | 0.001564881403 | 0.001565706218 |

At the window, filtered-input oscillation is 0.133719085; output is
0.008511418. Period 8 carries 57.913% of output variance. This is a current
jitter-8 residual measurement, not a new validation of the full H-27
period-4/8/16 model. That model remains an approximate amplitude model.

Window output retains 92.0794% of its own cycle-averaged raw-input energy;
the cancellation-free normalized absolute per-pixel energy error is 8.0499%.
R is a reconstruction reference, not ground truth for lighting.
Accepting the temporal residual does not automatically accept this energy bias.

## Controls: what failed, and the qualification

- No-op: oscillation ratio 1.0, FAIL T1 at every ROI.
- Actual 3x3 spatial blur: lit_floor oscillation ratio 0.425236, so it
  PASSES T1, but detail ratio 0.769932, so it FAILS D1 (required >=0.95).
  It therefore provides the requested reduction-with-detail-loss rejection.
- The same blur loses detail at right_shadow (ratio 0.700113), rear_door
  (0.743959), shadow_wall (0.932372) and lit_wall (0.762133).
- Temporal-plus-spatial blur: zero oscillation, PASSES T1, but FAILS D1
  on those same detail-sensitive regions.
- Qualification: window-only D1 does NOT reject this 3x3 blur:
  detail ratio 0.983645, while its spatial-only oscillation ratio 0.936299
  fails T1. The combined temporal-plus-spatial control also passes D1
  at the window alone; it is rejected by the multi-region gate. Do not
  describe this as proof that the window-only detail gate detects all blur.
  No threshold was changed after seeing this result.

The CPU measurement checks passed: 33 unit tests, 34 criterion checks and
12 region-format checks. Gtests have not been run in this first delivery;
none ran concurrently with these GPU captures.

## Recalibration recommendation and reserved decision

The flicker scenes still use SkyLight intensity and indirect intensity 1.0;
shipping `sample/content/scenes/sponza.uzscene` and `sponza-fog.uzscene` use
1.7320508 for both. Recommend migrating the flicker scenes to the same
calibration for future current-Sponza studies, after preserving this dated
legacy-scene snapshot. That changes the experiment and requires new
baselines and floors. This measurement did not silently apply it.

USER DECISION: accept the measured jitter-8 W-window residual, or require
further correction investigation. The pending clamp-cycle and exact-history
interventions can discriminate remaining mechanisms. Reconstruction changes,
lower accumulation gain and shorter jitter periods also trade against energy,
detail, recovery or anti-aliasing, so none is selected here.

The remaining H-45 right_shadow, H-18b, H-09b, window interventions,
H-23b motion captures, measured M1 calibration and owned gtests are not
reported as completed. They follow this explicitly prioritized first delivery.
