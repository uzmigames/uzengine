"""Two claims about the replay's error that were asserted rather than measured (2026-09-10).

Review, after the three-step isolation:

* "coordinates exonerated **completely**" overstates it. The position error is small but not zero
  (max 1.2e-04 px), and what matters is how much *that* displacement changes the read where the
  image has a steep gradient — which was never computed.
* "the chained error grows like a **random walk**" was inferred from a resemblance between the
  observed growth and sqrt(n), on *maxima*. That is not evidence of independence. Signed errors and
  their temporal correlation are what would show it.

Both are answered from the preserved dump, with no capture:

  position budget   |grad(history)| * |delta position| per sample — an upper bound on the read
                    error the residual displacement can explain — against the observed read error,
                    reported overall and on the high-gradient pixels where it matters
  error structure   the *signed* per-frame error: its bias, its spread, and its lag-1..8
                    autocorrelation per pixel. Independent errors decorrelate immediately; a bias
                    or a slowly varying error does not, and would accumulate linearly rather than
                    as sqrt(n)

Usage: python scripts/analyze_replay_error_structure.py [--raw DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
from run_full_recurrence_replay import (  # noqa: E402
    MARGIN, RECT, as_history, catmull_rom_5tap, luma)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw', default=os.path.join(
        runner.REPO, '.screenshots', 'flicker-elimination', '15-isolate-clamp-bounds', 'raw',
        'history-read'))
    ap.add_argument('--out', default=os.path.join(
        runner.REPO, '.screenshots', 'flicker-elimination', '15-isolate-clamp-bounds'))
    args = ap.parse_args()
    if not os.path.isdir(args.raw):
        raise SystemExit(f'no raw dump at {args.raw}')

    files = sorted(int(n.split('_')[1].split('.')[0]) for n in os.listdir(args.raw)
                   if n.startswith('region_'))
    seq = regiondump.load_sequence(args.raw, files[0], files[-1])
    rect = [f.rect(RECT[0]) for f in seq]
    history_rec = np.stack([r.channel('taa_history')[..., :3] for r in rect]).astype(np.float64)
    output_rec = np.stack([r.channel('taa_output')[..., :3] for r in rect]).astype(np.float64)
    vel_x = np.stack([r.channel('taa_filtered')[..., 3] for r in rect]).astype(np.float64)
    vel_y = np.stack([r.channel('taa_history')[..., 3] for r in rect]).astype(np.float64)
    sample = np.stack([r.channel('taa_history_sample') for r in rect]).astype(np.float64)

    h, w = history_rec.shape[1:3]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    inner = (slice(MARGIN, h - MARGIN), slice(MARGIN, w - MARGIN))
    rec_px = sample[..., 0] - RECT[1]
    rec_py = sample[..., 1] - RECT[2]
    own_px = xx[None] + 0.5 - vel_x
    own_py = yy[None] + 0.5 - vel_y

    pred = np.stack([catmull_rom_5tap(as_history(output_rec[i - 1]), rec_px[i], rec_py[i])
                     for i in range(1, len(seq))])
    signed = luma(pred) - luma(history_rec[1:])            # signed, in radiance
    signed_in = signed[(slice(None),) + inner]
    observed = np.abs(signed_in)

    # ---- position budget ----------------------------------------------------
    dpos = np.sqrt((own_px - rec_px) ** 2 + (own_py - rec_py) ** 2)[1:][(slice(None),) + inner]
    gy, gx = np.gradient(luma(history_rec), axis=(1, 2))
    gmag = np.sqrt(gx * gx + gy * gy)[1:][(slice(None),) + inner]
    budget = gmag * dpos
    conv_grad = np.sqrt(gx * gx + gy * gy)[1:].mean(axis=0)[inner]
    hi = conv_grad >= np.percentile(conv_grad, 99)
    hi_t = np.broadcast_to(hi, observed.shape)

    def share(num, den):
        d = float(den.mean())
        return float(num.mean()) / d if d else None

    report = {'frames': len(seq), 'raw': args.raw, 'position_budget': {
        'displacement_mean_px': float(dpos.mean()), 'displacement_max_px': float(dpos.max()),
        'budget_mean': float(budget.mean()), 'budget_p99': float(np.percentile(budget, 99)),
        'observed_mean': float(observed.mean()), 'observed_p99': float(np.percentile(observed, 99)),
        'share_of_observed_error_mean': share(budget, observed),
        'share_on_top_1pct_gradient': share(budget[hi_t], observed[hi_t]),
        'observed_on_top_1pct_gradient': float(observed[hi_t].mean()),
        'budget_on_top_1pct_gradient': float(budget[hi_t].mean()),
    }}

    # ---- error structure ----------------------------------------------------
    per_pixel_bias = signed_in.mean(axis=0)
    centred = signed_in - per_pixel_bias[None]
    denom = (centred * centred).mean(axis=0)
    lags = {}
    for lag in (1, 2, 4, 8):
        a, b = centred[:-lag], centred[lag:]
        num = (a * b).mean(axis=0)
        ac = np.where(denom > 1e-30, num / np.maximum(denom, 1e-30), 0.0)
        lags[f'lag_{lag}'] = float(ac.mean())
    report['error_structure'] = {
        'signed_mean': float(signed_in.mean()),
        'signed_abs_mean': float(observed.mean()),
        'per_pixel_bias_abs_mean': float(np.abs(per_pixel_bias).mean()),
        'bias_over_abs_error': float(np.abs(per_pixel_bias).mean() / max(1e-30, observed.mean())),
        'autocorrelation': lags,
    }

    pb = report['position_budget']
    es = report['error_structure']
    print(f"  position budget: displacement {pb['displacement_mean_px']:.2e} px mean "
          f"({pb['displacement_max_px']:.2e} max)")
    print(f"    |grad| x |dpos| explains {pb['share_of_observed_error_mean'] * 100:.2f} % of the "
          f"observed read error overall, {pb['share_on_top_1pct_gradient'] * 100:.2f} % on the "
          f"top 1 % gradient pixels ({pb['budget_on_top_1pct_gradient']:.2e} against an observed "
          f"{pb['observed_on_top_1pct_gradient']:.2e})")
    print(f"  error structure: signed mean {es['signed_mean']:.2e} vs |error| mean "
          f"{es['signed_abs_mean']:.2e}; per-pixel bias is "
          f"{es['bias_over_abs_error'] * 100:.1f} % of the typical error")
    print('    autocorrelation of the centred error: '
          + ', '.join(f"{k} {v:+.3f}" for k, v in es['autocorrelation'].items()))

    path = os.path.join(args.out, 'replay-error-structure.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
