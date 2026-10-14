# 07 — Item 1.5: shipped sweep in motion, per-frame rc_support

Date: 2026-10-13. Scope: record the deferred per-frame `rc_support` over the Sponza S3
motion sweep (201 frames, recentres at 113/157/201) under the shipped configuration,
gather on (the shipped observation) and gather off (a diagnostic control). This is a
diagnostic; it is not a pass/fail gate and makes no adoption decision.

## Constitutional limitation (from the measurement script)

`rc_support` is the **deferred** cache query, not the shipped final-gather hit-point
queries. H-62b in the shipped consumer is therefore **NOT causally judgeable here**
(`H62b_verdict: NOT JUDGEABLE without gather hit-query support instrumentation`). The
H-62b wall-flicker candidate ROI is `left_wall`; `rear_door`, `right_shadow`, `lit_floor`
are non-causal controls.

## Infra note — moving screen ROIs

A fixed screen-space ROI does not identify the same world surface each frame. The control
ROI `right_shadow` leaves geometry for frames 216–290. The runner
(`scripts/rendering/run_probe_support_motion.py`) was patched on 2026-10-13 to **record
such frames as gaps and continue** (previously it aborted with `no geometry`), which is
the honest treatment of the script's own documented `screen_roi_limitation`. The H-62b
candidate `left_wall` and the other controls cover all 201 frames with no gaps.

## Result — per-frame diffuse step (luminance) and rc_support correlation

| ROI | covered | gather=1 p99 / max step | g=1 support corr | gather=0 p99 / max step | g=0 support corr |
|---|---|---|---|---|---|
| left_wall (candidate) | 201 | 0.00095 / 0.00180 | 0.457 | 0.00099 / 0.00150 | 0.497 |
| rear_door | 201 | 0.00102 / 0.00187 | 0.620 | 0.00221 / 0.01352 | 0.102 |
| lit_floor | 201 | 0.00099 / 0.00156 | 0.335 | 0.01251 / 0.08266 | 0.016 |
| right_shadow | 126 (gap 216–290) | 0.00082 / 0.00113 | 0.325 | 0.00055 / 0.00101 | 0.195 |

## Reading

Under the shipped config (gather on), the per-frame diffuse steps at the candidate
`left_wall` are small (p99 0.00095, max 0.0018 luminance/frame) and moderately correlated
with the per-frame `rc_support` weight step (0.457). Turning the final gather **off**
markedly inflates `lit_floor` (max step 0.083, 50x) and `rear_door` (max 0.0135, 7x) while
leaving `left_wall` essentially unchanged — i.e. the shipped gather **stabilises** those
regions rather than driving flicker there. H-62b is neither confirmed nor refuted (the
measurement cannot, by construction); the candidate's measured temporal steps are small.
Recorded as a diagnostic; no defect escalation.

Report: `.screenshots/rc-probe-support-followups/item15/probe_support_motion.json`.
