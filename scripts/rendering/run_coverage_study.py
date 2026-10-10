"""The two conditions item 1.4 asks for that were held fixed instead of tested (followup-14).

Review finding (2026-09-09): the checklist requires exposure transitions and clipmap crossings, and
the task held both fixed - auto-exposure off, no boundary-crossing trajectory - on the grounds that
followup-11 and followup-13 own those systems. That is not sufficient: those tasks characterised the
systems, not their *interaction with this candidate*. This measures the interaction.

  exposure   the hold/strafe/hold/cut sequence with `r.AutoExposure=1`, so the exposure moves while
             the TAA history is being rebuilt. The recorded per-frame exposure is compared between
             variants: if the candidate changes the image, auto-exposure sees a different histogram
             and the two runs diverge in a way that has nothing to do with the reconstruction.
  clipmap    the one-axis-at-a-time boundary trajectory of followup-13, with the recentre events
             taken from the engine log, comparing the motion-compensated residual at events against
             elsewhere for both variants.

Usage: python scripts/rendering/run_coverage_study.py [--out DIR] [--repeats 2]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
import run_gdf_motion_study as gdf  # noqa: E402

CVAR = 'r.TAA.ReconstructionHdrWeightExponent'
SCENE_START = 37
VARIANTS = [('baseline', 1.0), ('candidate_0.0', 0.0)]

EXP_PROJECT = 'sponza-flicker-w-motion.uzproject'
EXP_ROI = ('window', 770, 150, 192, 512)
EXP_FIRST = SCENE_START + 40
EXP_LAST = SCENE_START + 382
EXP_PHASES = {'hold1': (60, 118), 'strafe': (125, 238), 'hold2': (245, 298),
              'after_cut': (303, 378)}


def capture(work, label, project, frames, regions, fields, first, last, cvars):
    res = runner.run_capture(os.path.join(work, label),
                             os.path.join(runner.REPO, 'scripts', 'rendering', project), frames,
                             regions=regions, region_fields=fields, region_first=first,
                             region_last=last, cvars=cvars, timing_first=first)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    return res


def exposure_study(work, out):
    payload = {'phases': EXP_PHASES, 'cvars': {'r.AutoExposure': 1}, 'variants': {}}
    for vname, exponent in VARIANTS:
        print(f'  ... exposure transitions, {vname}', flush=True)
        res = capture(work, f'exp_{vname}'.replace('.', 'p'), EXP_PROJECT,
                      EXP_LAST - SCENE_START + 8, [EXP_ROI], 'taa', EXP_FIRST, EXP_LAST,
                      {'r.AutoExposure': 1, 'r.TAA.JitterSequenceLength': 8,
                       CVAR: f'{exponent:g}'})
        seq = regiondump.load_sequence(res['region_dir'], EXP_FIRST, EXP_LAST)
        rel = np.array([f.frame - SCENE_START for f in seq])
        out_l = np.stack([f.rect(EXP_ROI[0]).luminance('taa_output') for f in seq]).astype(np.float64)
        # The *display* exposure, from the region file header. The 4th component of the records is
        # a different quantity — the exposure the TAA HDR weights used, which is
        # `r.TAA.ExposureScale` and is constant at 1.0 — and reading it made the first version of
        # this study report a flat 1.00 and no transition at all, on a run where auto-exposure had
        # in fact climbed to ~68.
        exposure = np.array([float(f.exposure) for f in seq])
        if exposure.max() - exposure.min() < 1e-6:
            raise RuntimeError('the exposure never moved, so this run does not test an exposure '
                               f'transition (constant {exposure[0]:.4f})')
        entry = {'exposure_min': float(exposure.min()), 'exposure_max': float(exposure.max()),
                 'exposure_first': float(exposure[0]), 'exposure_last': float(exposure[-1]),
                 'phases': {}}
        for name, (lo, hi) in EXP_PHASES.items():
            idx = np.flatnonzero((rel >= lo) & (rel <= hi))
            entry['phases'][name] = {
                'std_mean': float(out_l[idx].std(axis=0).mean()),
                'mean': float(out_l[idx].mean()),
                'exposure_mean': float(exposure[idx].mean()),
                'exposure_range': float(exposure[idx].max() - exposure[idx].min()),
            }
        entry['exposure_series'] = [float(v) for v in exposure]
        payload['variants'][vname] = entry
        p = entry['phases']
        print(f"      exposure {entry['exposure_min']:.2f}..{entry['exposure_max']:.2f}; "
              f"after-cut std {p['after_cut']['std_mean']:.4e}, "
              f"exposure range there {p['after_cut']['exposure_range']:.3f}", flush=True)
        shutil.rmtree(res['region_dir'], ignore_errors=True)

    b = payload['variants']['baseline']
    c = payload['variants']['candidate_0.0']
    be, ce = np.array(b['exposure_series']), np.array(c['exposure_series'])
    payload['exposure_divergence_max'] = float(np.abs(be - ce).max())
    payload['exposure_divergence_rel'] = float(np.abs(be - ce).max() / max(1e-9, be.mean()))
    print(f"      exposure divergence between variants: "
          f"{payload['exposure_divergence_max']:.4f} "
          f"({payload['exposure_divergence_rel'] * 100:.2f} % of the mean)")
    return payload


def clipmap_study(work, out):
    payload = {'project': 'sponza-gdf-boundary.uzproject', 'rois': gdf.ROIS, 'variants': {}}
    for vname, exponent in VARIANTS:
        print(f'  ... clipmap crossings, {vname}', flush=True)
        res = capture(work, f'gdf_{vname}'.replace('.', 'p'), 'sponza-gdf-boundary.uzproject',
                      gdf.LAST - SCENE_START + 8, gdf.ROIS, 'motion', gdf.FIRST, gdf.LAST,
                      {**gdf.BASE, CVAR: f'{exponent:g}'})
        frames = regiondump.load_sequence(res['region_dir'], gdf.FIRST, gdf.LAST)
        events = gdf.recentre_events(res['log'])
        entry = {'events': len(events), 'rois': {}}
        for roi in gdf.ROIS:
            resid, cov, means = gdf.motion_compensated_series(frames, roi)
            st = gdf.event_stats(frames, resid, events)
            st['coverage_mean'] = float(np.nanmean(cov[1:]))
            entry['rois'][roi[0]] = st
        payload['variants'][vname] = entry
        for rn, st in entry['rois'].items():
            print(f"      {rn}: {entry['events']} events, residual near {st['residual_near_event']:.3e} "
                  f"vs far {st['residual_far']:.3e} ({st['ratio']:.2f}x), "
                  f"coverage {st['coverage_mean'] * 100:.0f} %", flush=True)
        shutil.rmtree(res['region_dir'], ignore_errors=True)
    return payload


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '14-validate-flicker-correction'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_coverage')
    payload = {'exposure': exposure_study(work, args.out),
               'clipmap': clipmap_study(work, args.out),
               'source_fingerprint': runner.source_fingerprint()}
    path = os.path.join(args.out, 'coverage.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
