"""Verdicts for the gather-freeze study, recomputed from the preserved dumps (no capture).

The study's first pass conflated two different things: "the intervention did not apply" and "the
intervention applied and changed nothing here". Only the first is a control failure; the second is
the result. Validity is therefore established from the run manifest (the CVar reached the engine)
plus the path and exposure controls, and the step test at the midway freeze becomes evidence of
*effect size*, not a gate.

Reads `raw/` and `manifests/` written by `scripts/rendering/run_gather_freeze_study.py`.

Usage: python scripts/rendering/report_gather_freeze.py
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

CVAR = 'r.GI.FinalGather.FreezeAfterFrame'
THRESHOLDS = {'shadow_wall': 4.7e-7, 'rear_door': 8.9e-6, 'right_shadow': 2.1e-5}
ROIS = {'W': ['shadow_wall'], 'A': ['rear_door', 'right_shadow']}


def load(raw, roi):
    files = sorted(int(n.split('_')[1].split('.')[0]) for n in os.listdir(raw)
                   if n.startswith('region_'))
    seq = regiondump.load_sequence(raw, files[0], files[-1])
    return {
        'out': np.stack([f.rect(roi).luminance('taa_output') for f in seq]).astype(np.float64),
        'paths': sorted(int(v) for v in np.unique(np.stack(
            [f.rect(roi).channel('emissive_and_mis')[..., 3].view(np.uint32) for f in seq]))),
        'exposure': np.stack([f.rect(roi).channel('taa_output')[..., 3]
                              for f in seq]).astype(np.float64),
        'first': files[0], 'count': len(files),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '16-gather-freeze'))
    args = ap.parse_args()
    report = {'poses': {}}

    for pose, rois in ROIS.items():
        man = {}
        for v in ('baseline', 'gather_frozen', 'frozen_midway'):
            p = os.path.join(args.out, 'manifests', f'{pose}-{v}.json')
            man[v] = json.load(open(p, encoding='utf-8')).get('cvars', {}).get(CVAR)
        applied = (man['baseline'] == -1 and man['gather_frozen'] is not None
                   and man['gather_frozen'] >= 0 and man['frozen_midway'] is not None
                   and man['frozen_midway'] > man['gather_frozen'])
        entry = {'cvar_in_manifests': man, 'intervention_applied': bool(applied), 'rois': {}}

        for roi in rois:
            b = load(os.path.join(args.out, 'raw', f'{pose}-baseline'), roi)
            f = load(os.path.join(args.out, 'raw', f'{pose}-gather_frozen'), roi)
            m = load(os.path.join(args.out, 'raw', f'{pose}-frozen_midway'), roi)
            half = m['count'] // 2
            step = np.abs(np.diff(m['out'], axis=0)).mean(axis=(1, 2))
            before, after = float(step[:half - 2].mean()), float(step[half + 2:].mean())
            osc_b = float(b['out'].std(axis=0).mean())
            osc_f = float(f['out'].std(axis=0).mean())
            drop = osc_b - osc_f
            valid = (applied and b['paths'] == f['paths']
                     and float(np.abs(b['exposure'] - f['exposure']).max()) < 1e-9)
            if not valid:
                verdict = 'invalid — the intervention did not apply, or it changed the path'
            elif drop > 0.5 * osc_b and drop > THRESHOLDS[roi]:
                verdict = 'dominant cause of this region residual'
            elif drop > THRESHOLDS[roi]:
                verdict = 'contributes, not dominant'
            else:
                verdict = 'no measurable contribution here'
            entry['rois'][roi] = {
                'path_identical': b['paths'] == f['paths'],
                'step_before_midway': before, 'step_after_midway': after,
                'step_change_pct': (after / before - 1) * 100 if before else None,
                'oscillation_baseline': osc_b, 'oscillation_frozen': osc_f,
                'oscillation_change_pct': (osc_f / osc_b - 1) * 100 if osc_b else None,
                'threshold': THRESHOLDS[roi], 'drop': drop, 'verdict': verdict,
            }
            e = entry['rois'][roi]
            print(f"  {pose} {roi:13s} step {before:.3e} -> {after:.3e} "
                  f"({e['step_change_pct']:+.1f} %), oscillation "
                  f"{e['oscillation_change_pct']:+.1f} % -> {verdict}")
        report['poses'][pose] = entry
        print(f"      intervention applied: {applied} (CVar per run: {man})")

    report['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'gather-freeze-verdicts.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
