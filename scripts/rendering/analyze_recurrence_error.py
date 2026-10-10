"""Where does the replay's error live, and is the replay faithful enough to attribute with?

`run_full_recurrence_replay.py` rejects itself on a *max* relative error: one bad pixel fails the
whole check. That is the right default — an attribution built on an unfaithful replay is worthless —
but it does not say whether the replay is wrong everywhere or wrong on a handful of pixels, and the
answer changes what can be concluded and where.

This reads the raw dump the replay kept (no recapture) and reports the error distribution per stage,
plus where the error sits: against the spatial gradient, against the recorded velocity, and against
the clamp being active. If the error concentrates on a small, identifiable set of pixels, the
attribution can still be made on the complement — stated as such, with the excluded set named.

Usage: python scripts/rendering/analyze_recurrence_error.py [--raw DIR]
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
import taa_model as tm  # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_full_recurrence_replay import (  # noqa: E402
    ANALYSIS, FIRST, MARGIN, RECT, as_history, blend_and_weights, catmull_rom_5tap, clamp_ycocg,
    luma)


def stats(err, ref):
    scale = max(1e-9, float(np.abs(ref).max()))
    e = err / scale
    return {'mean': float(e.mean()), 'p50': float(np.percentile(e, 50)),
            'p99': float(np.percentile(e, 99)), 'p999': float(np.percentile(e, 99.9)),
            'max': float(e.max()),
            'fraction_above_1e_4': float((e > 1e-4).mean()),
            'fraction_above_1e_3': float((e > 1e-3).mean())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw', default=os.path.join(
        runner.REPO, '.screenshots', 'flicker-elimination', '15-isolate-clamp-bounds', 'raw',
        'full-recurrence'))
    ap.add_argument('--out', default=os.path.join(
        runner.REPO, '.screenshots', 'flicker-elimination', '15-isolate-clamp-bounds'))
    args = ap.parse_args()
    if not os.path.isdir(args.raw):
        raise SystemExit(f'no raw dump at {args.raw} — run run_full_recurrence_replay.py first')

    files = sorted(int(n.split('_')[1].split('.')[0]) for n in os.listdir(args.raw)
                   if n.startswith('region_'))
    seq = regiondump.load_sequence(args.raw, files[0], files[-1])
    rect = [f.rect(RECT[0]) for f in seq]
    g = lambda ch: np.stack([r.channel(ch)[..., :3] for r in rect]).astype(np.float64)  # noqa: E731
    filtered, history_rec = g('taa_filtered'), g('taa_history')
    clamped_rec, output_rec = g('taa_clamped'), g('taa_output')
    clip_min, clip_max = g('taa_clip_min'), g('taa_clip_max')
    blend = np.stack([r.channel('taa_raw')[..., 3] for r in rect]).astype(np.float64)
    exposure = np.stack([r.channel('taa_output')[..., 3] for r in rect]).astype(np.float64)
    vel_x = np.stack([r.channel('taa_filtered')[..., 3] for r in rect]).astype(np.float64)
    vel_y = np.stack([r.channel('taa_history')[..., 3] for r in rect]).astype(np.float64)

    h, w = filtered.shape[1:3]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    inner = (slice(MARGIN, h - MARGIN), slice(MARGIN, w - MARGIN))
    sx = xx[None] + 0.5 - vel_x
    sy = yy[None] + 0.5 - vel_y

    report = {'frames': len(seq), 'raw': args.raw, 'stages': {}}

    hist_pred = np.stack([catmull_rom_5tap(as_history(output_rec[i - 1]), sx[i], sy[i])
                          for i in range(1, len(seq))])
    he = np.abs(hist_pred - history_rec[1:])[(slice(None),) + inner].max(axis=-1)
    report['stages']['history_catmull_rom'] = stats(he, history_rec[(slice(None),) + inner])

    ce = np.abs(clamp_ycocg(history_rec, clip_min, clip_max)
                - clamped_rec)[(slice(None),) + inner].max(axis=-1)
    report['stages']['clamp'] = stats(ce, clamped_rec[(slice(None),) + inner])

    ob, _ = blend_and_weights(clamped_rec, filtered, blend, exposure)
    be = np.abs(ob - output_rec)[(slice(None),) + inner].max(axis=-1)
    report['stages']['blend'] = stats(be, output_rec[(slice(None),) + inner])

    # where does the history error live?
    conv = luma(output_rec.mean(axis=0))
    gy, gx = np.gradient(conv)
    grad = np.sqrt(gx * gx + gy * gy)[inner]
    speed = np.sqrt(vel_x * vel_x + vel_y * vel_y)[(slice(None),) + inner]
    hist_y = tm.rgb_to_ycocg(history_rec)[..., 0]
    clamped_y = tm.rgb_to_ycocg(clamped_rec)[..., 0]
    clamp_active = (np.abs(clamped_y - hist_y) > 1e-6)[(slice(None),) + inner]

    per_pixel = he.mean(axis=0)

    def corr(a, b):
        a, b = np.asarray(a).ravel(), np.asarray(b).ravel()
        if a.std() < 1e-15 or b.std() < 1e-15:
            return None
        return float(np.corrcoef(a, b)[0, 1])

    report['history_error_location'] = {
        'corr_with_spatial_gradient': corr(per_pixel, grad),
        'corr_with_velocity': corr(he.ravel(), speed[1:].ravel()),
        'corr_with_clamp_active': corr(he.ravel(), clamp_active[1:].astype(float).ravel()),
        'error_on_top_1pct_gradient_pixels': float(
            per_pixel[grad >= np.percentile(grad, 99)].mean()),
        'error_elsewhere': float(per_pixel[grad < np.percentile(grad, 99)].mean()),
    }

    print(f'  frames {len(seq)}, analysis region {ANALYSIS[0]}x{ANALYSIS[1]}')
    for name, st in report['stages'].items():
        print(f"  {name:20s} mean {st['mean']:.2e}  p99 {st['p99']:.2e}  max {st['max']:.2e}  "
              f"above 1e-4 on {st['fraction_above_1e_4'] * 100:.3f} % of samples")
    loc = report['history_error_location']
    print(f"  history error vs spatial gradient r={loc['corr_with_spatial_gradient']:.3f}; "
          f"on the top 1 % gradient pixels {loc['error_on_top_1pct_gradient_pixels']:.2e} "
          f"vs {loc['error_elsewhere']:.2e} elsewhere "
          f"({loc['error_on_top_1pct_gradient_pixels'] / max(1e-12, loc['error_elsewhere']):.0f}x)")

    path = os.path.join(args.out, 'recurrence-error.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
