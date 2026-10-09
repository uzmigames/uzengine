"""Re-run of the display study's step 5 (auto-exposure vs fixed exposure), with three discriminating
configurations. Pre-registered 2026-10-09, before any of these captures existed.

Why. `scripts/run_display_study.py` (followup-11, close-out run of 2026-10-09 on the Debug build of
2cc2e85c) failed one of its eight checks: "auto-exposure leaves the linear HDR input indistinguishable
from the fixed-exposure run". The max |delta| read INDISTINGUISHABLE; the mean |delta| read SEPARATED
(between 3.3e-6..5.4e-6, within 1.5e-11..2.8e-6, on a mean radiance of 2.15: a uniform shift of about
2.5 parts per million). The study's own protocol (`indistinguishable()` docstring) says: re-run that
comparison with more repeats, and read it as a change only if it fails again. This script is that re-run.

Configurations (Sponza pose w, the study's ROI, frames and TAA settings, bloom off; `--repeats` processes
each, interleaved round-robin as the study does):
  A  fixed     r.AutoExposure 0, r.Exposure 1.0                    (the study's fixed baseline)
  B  auto      the scene's auto-exposure (exposure ~71.07, 0.5 x ~142.1) (the study's step 5)
  C  fixed71   r.AutoExposure 0, r.Exposure 71.07                  (the auto value, held constant, no passes)
  D  pinned    auto-exposure passes running, MinBrightness = MaxBrightness = 0.18/142.14, so the computed
               exposure is clamped to the same constant every frame (the passes without the swing)

Pre-registered reading (each comparison by the study's `indistinguishable()`):
  1. Primary: B vs A. If it passes, the study's failure was chance and the check passes on re-run.
     If it fails again, the auto-exposure configuration changes the linear HDR input; then:
  2. Mechanism (read only when 1 fails):
     - value:    C vs A fails, D vs C passes, B vs D passes  -> the exposure VALUE reaches the HDR input
     - dispatch: C vs A passes, D vs C fails                  -> running the auto-exposure passes does
                                                                 (timing or resource state, not the value)
     - swing:    C vs A passes, D vs C passes, B vs D fails   -> the per-frame CHANGE of exposure does
     - otherwise: unexplained, reported as such.
  The effect size is reported as the mean signed delta relative to the mean radiance.

Usage: python scripts/run_display_exposure_rerun.py --out DIR [--repeats 5]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_display_study as study  # noqa: E402

AUTO_INTERNAL = 142.14   # the auto-exposure's own value on this pose (71.07 / the scene's r.Exposure 0.5)
SCENE_MANUAL = 0.5       # sponza-flicker-w.uzscene sets r.Exposure 0.5 on top of auto-exposure


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', required=True)
    ap.add_argument('--repeats', type=int, default=5)
    args = ap.parse_args()
    if os.path.exists(os.path.join(args.out, 'exposure-rerun.json')):
        print(f'{args.out} already holds exposure-rerun.json (evidence is not overwritten)', file=sys.stderr)
        return 2
    study.runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix='uz_exposure_rerun_')
    base = {'r.TAA.JitterSequenceLength': 8, 'r.Bloom': 0}
    clamp = 0.18 / AUTO_INTERNAL
    plan = [
        ('fixed', {**base, 'r.AutoExposure': 0, 'r.Exposure': 1.0}, None),
        ('auto', dict(base), None),
        ('fixed71', {**base, 'r.AutoExposure': 0, 'r.Exposure': AUTO_INTERNAL * SCENE_MANUAL}, None),
        ('pinned', {**base, 'r.AutoExposure.MinBrightness': clamp, 'r.AutoExposure.MaxBrightness': clamp}, None),
    ]
    block = study.take_interleaved(work, args.out, False, plan, args.repeats)
    rgb = {label: [run['rgb'] for _, run in got] for label, got in block.items()}
    exposure = {label: [float(np.mean(run['exposure'])) for _, run in got] for label, got in block.items()}
    swing = {label: [float(np.ptp(run['exposure'])) for _, run in got] for label, got in block.items()}

    def compare(a, b):
        eq = study.indistinguishable(rgb[a], rgb[b])
        ma = np.mean([x.mean() for x in rgb[a]])
        mb = np.mean([x.mean() for x in rgb[b]])
        eq['relative_mean_shift'] = float((mb - ma) / ma) if ma else float('nan')
        return eq

    res = {k: compare(*k.split('_vs_')[::-1]) for k in ('auto_vs_fixed', 'fixed71_vs_fixed', 'pinned_vs_fixed71', 'auto_vs_pinned')}
    ok = {k: v['ok'] for k, v in res.items()}
    if ok['auto_vs_fixed']:
        reading = 'chance: the auto-exposure comparison passes on re-run'
    elif not ok['fixed71_vs_fixed'] and ok['pinned_vs_fixed71'] and ok['auto_vs_pinned']:
        reading = 'value: the exposure value reaches the linear HDR input'
    elif ok['fixed71_vs_fixed'] and not ok['pinned_vs_fixed71']:
        reading = 'dispatch: running the auto-exposure passes changes the linear HDR input'
    elif ok['fixed71_vs_fixed'] and ok['pinned_vs_fixed71'] and not ok['auto_vs_pinned']:
        reading = 'swing: the per-frame change of exposure reaches the linear HDR input'
    else:
        reading = 'unexplained: the pattern matches none of the pre-registered mechanisms'
    payload = {'harness': study.runner.harness_stamp(), 'repeats': args.repeats, 'roi': study.ROI,
               'first_frame': study.FIRST, 'last_frame': study.LAST, 'plan': [[l, c] for l, c, _ in plan],
               'exposure_mean_per_process': exposure, 'exposure_swing_per_process': swing,
               'comparisons': res, 'reading': reading}
    with open(os.path.join(args.out, 'exposure-rerun.json'), 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=1)
    for k, v in res.items():
        print(f"{k}: {'PASS' if v['ok'] else 'FAIL'} (relative mean shift {v['relative_mean_shift']:+.2e}) {v['detail']}")
    print('exposure per process:', {k: [round(x, 3) for x in v] for k, v in exposure.items()})
    print('reading:', reading)
    return 0


if __name__ == '__main__':
    sys.exit(main())
