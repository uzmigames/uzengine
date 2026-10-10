"""The no-TAA reference for the window ROI (followup-10 item 1.2).

`--region-fields` records the raw pre-TAA scene colour (`taa_raw`) alongside the resolved output, and
that raw series is exactly what the display would show with `r.TAA=0` — the same jittered samples,
without accumulation. Capturing it from a TAA-on run avoids the blind spot that a `r.TAA=0` run has:
with the resolve disabled the instrument writes no resolve records at all.

It also reports the same statistics for the plus-filtered colour, so the residual can be attributed
between "the input moves" (raw), "the reconstruction moves" (filtered) and "the accumulation moves"
(output).

Usage: python scripts/rendering/run_taa_raw_reference.py [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 24
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject')
ROI = ('window', 770, 150, 192, 512)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '10-isolate-taa-window'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_taa_raw_reference')

    res = runner.run_capture(work, PROJECT, LAST - SCENE_START + 8, regions=[ROI],
                             region_fields='taa+extra', region_first=FIRST, region_last=LAST,
                             cvars={'r.AutoExposure': 0, 'r.Exposure': 1.0,
                                    'r.TAA.JitterSequenceLength': 8},
                             timing_first=FIRST)
    if res['exit_code'] != 0:
        raise SystemExit(f'engine exited {res["exit_code"]}')
    frames = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
    stacks = {name: np.stack([f.rect(ROI[0]).luminance(name) for f in frames]).astype(np.float64)
              for name in ('taa_raw', 'taa_filtered', 'taa_history', 'taa_clamped', 'taa_output')}
    raw = stacks['taa_raw']
    peak = np.percentile(raw.max(axis=0), 99)
    masks = {'bright': raw.min(axis=0) >= 0.9 * peak,
             'dark': raw.max(axis=0) <= 0.05 * peak}
    masks['boundary'] = ~masks['bright'] & ~masks['dark']
    masks['all'] = np.ones_like(masks['bright'], dtype=bool)

    out = {'roi': ROI, 'frames': [f.frame for f in frames], 'stages': {},
           'masks': {k: int(v.sum()) for k, v in masks.items()},
           'manifest': res['manifest'], 'source_fingerprint': runner.source_fingerprint()}
    print(f'{"stage":14s} ' + ' '.join(f'{m:>12s}' for m in masks))
    for stage, s in stacks.items():
        row = {}
        for m, mask in masks.items():
            v = s[:, mask]
            row[m] = {'std_mean': float(v.std(axis=0).mean()), 'mean': float(v.mean()),
                      'abs_diff_mean': float(np.abs(np.diff(v, axis=0)).mean())}
        out['stages'][stage] = row
        print(f'{stage:14s} ' + ' '.join(f'{row[m]["std_mean"]:12.5f}' for m in masks))
    print('\n(std of the linear Rec.709 radiance per stage; `taa_raw` is the no-TAA image)')

    path = os.path.join(args.out, 'raw-reference.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=2)
    shutil.rmtree(res['region_dir'], ignore_errors=True)
    print('wrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
