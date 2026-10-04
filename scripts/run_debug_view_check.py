"""followup-06 item 4.2: do the debug views render their channel, or an error colour?

Recorded before this ran (followup-06 notes, 2026-09-16). The task's open item says `shadows` and
`lighting` render solid magenta, `lightingonly` renders black (mean 1.5) and `ao` renders a flat 65.5
with GTAO on, while `albedo`, `roughness`, `metallic`, `worldnormal` and `detaillighting` work.

Each mode is rendered from the same pose with the same settings, screenshotted, and measured in
display code values (0..255) from the PNG:

  M1  not an error colour: fewer than 1 % of pixels are pure magenta (R > 250, B > 250, G < 5).
      Before the fix `lighting` and `shadows` were solid magenta, so this can fail and did.
  M2  not black: mean luminance >= 5 code values. `lightingonly` measured 1.5.
  M3  not flat: the per-pixel standard deviation is >= 1 code value. `ao` measured a flat 65.5.
  M4  distinct: each mode differs from `albedo` by a mean absolute difference >= 5 code values, so a
      mode cannot pass by quietly showing another channel.

  controls  `albedo`, `worldnormal` and `detaillighting` already worked and must still pass M1-M3;
            `albedo` is also M4's reference, so it is exempt from M4.

A mode passes when M1, M2 and M3 hold (and M4 where it applies). The report records every number,
and the exit code is non-zero if any mode fails, so this cannot pass silently.

Usage: python scripts/run_debug_view_check.py [--frames 40] [--out DIR]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

PROJECT = 'sponza-flicker-a.uzproject'
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0}
#: mode -> whether it was reported broken before the fix (for the record, not for the criteria).
MODES = {
    'albedo': 'worked',
    'worldnormal': 'worked',
    'detaillighting': 'worked',
    'ao': 'flat 65.5 with GTAO on',
    'lighting': 'solid magenta',
    'shadows': 'solid magenta',
    'lightingonly': 'black, mean 1.5',
}
REFERENCE = 'albedo'
MAGENTA_FRACTION = 0.01
MIN_MEAN = 5.0
MIN_STD = 1.0
MIN_DISTINCT = 5.0
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


def load_png(path):
    from PIL import Image
    with Image.open(path) as im:
        return np.asarray(im.convert('RGB'), dtype=np.float64)


def measure(rgb):
    lum = rgb @ REC709
    magenta = (rgb[..., 0] > 250) & (rgb[..., 2] > 250) & (rgb[..., 1] < 5)
    return {'mean': float(lum.mean()), 'std': float(lum.std()),
            'magenta_fraction': float(magenta.mean()),
            'channel_means': [float(rgb[..., c].mean()) for c in range(3)]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=40)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '37-debug-views'))
    args = ap.parse_args()
    runner.require_exe()
    print(f'debug views — {args.frames} frames per mode, pose A, fixed exposure')
    print(f'  M1 magenta < {MAGENTA_FRACTION:.0%}, M2 mean >= {MIN_MEAN:g}, M3 std >= {MIN_STD:g}, '
          f'M4 mean |difference| from {REFERENCE} >= {MIN_DISTINCT:g} code values')

    report = {'criteria': {'magenta_fraction': MAGENTA_FRACTION, 'min_mean': MIN_MEAN,
                           'min_std': MIN_STD, 'min_distinct': MIN_DISTINCT},
              'frames': args.frames, 'harness': runner.harness_stamp(), 'modes': {}}
    images = {}
    for mode in MODES:
        work = os.path.join(args.out, mode)
        print(f'  ... {mode}', flush=True)
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', PROJECT), args.frames,
                                 cvars=BASE, screenshot_frames=str(args.frames - 1),
                                 extra_args=['--view-mode', mode], clean=True)
        if res['exit_code'] != 0:
            raise SystemExit(f'{mode}: exit {res["exit_code"]}')
        shots = sorted(glob.glob(os.path.join(work, 'frames', '*.png')))
        if not shots:
            raise SystemExit(f'{mode}: no screenshot was written')
        images[mode] = load_png(shots[-1])
        report['modes'][mode] = {'screenshot': shots[-1], 'before_the_fix': MODES[mode],
                                 **measure(images[mode])}

    ref = images[REFERENCE]
    failures = []
    print()
    for mode, row in report['modes'].items():
        row['M1_not_error_colour'] = row['magenta_fraction'] < MAGENTA_FRACTION
        row['M2_not_black'] = row['mean'] >= MIN_MEAN
        row['M3_not_flat'] = row['std'] >= MIN_STD
        if mode == REFERENCE:
            row['M4_distinct'] = None
        else:
            diff = float(np.abs(images[mode] - ref).mean())
            row['mean_abs_difference_from_reference'] = diff
            row['M4_distinct'] = diff >= MIN_DISTINCT
        checks = [v for k, v in row.items() if k.startswith('M') and v is not None]
        row['pass'] = all(checks)
        if not row['pass']:
            failures.append(mode)
        flags = ' '.join(k.split('_')[0] for k, v in row.items() if k.startswith('M') and v is False)
        print(f'  {mode:<15} mean {row["mean"]:>7.2f}  std {row["std"]:>7.2f}  magenta '
              f'{row["magenta_fraction"]:>6.1%}  '
              f'{"PASS" if row["pass"] else "FAILS (" + flags + ")"}   [before: {MODES[mode]}]')

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, 'debug_views.json')
    report['failures'] = failures
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n  -> {path}')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
