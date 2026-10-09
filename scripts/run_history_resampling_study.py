"""Why is the history resampled with a static camera, and is that where the residual lives? (H-26)

The H-25 screening (`scripts/run_clamp_bounds_study.py`) rejected its own hypothesis: fixing the
clamp bounds removes essentially all of the clamp's contribution, but that contribution is only
about 11 % of the residual, and per pixel the residual co-locates with the *history-resampling*
perturbation (r = 0.73 / 0.83) far more than with clamp activity (r = 0.10 / 0.03).

That perturbation should not exist. The shader fetches the history with a 5-tap Catmull-Rom at
`historyUV = uv - motionVector`; at a texel centre with zero motion the basis collapses to
`w1 = 1` and the fetch is exact. So a static camera should resample nothing. Two things are
measured here rather than argued:

  velocity     the recorded per-pixel velocity (the region dump stores velocity.x and .y in the
               4th components of `taa_filtered` and `taa_history`). If it is non-zero with a static
               camera — in particular if it is jitter-sized, under a pixel — then the history is
               resampled every frame at a sub-pixel offset, which is a jitter-locked perturbation
               and would explain why H-01 found the residual's period equal to the jitter sequence
               length.
  confounding  the residual and the resampling perturbation are both larger at high-contrast edges,
               so a raw correlation between them proves little. The partial correlation controlling
               for the spatial gradient of the converged image is reported next to the raw one, and
               the gradient's own correlation with each.

**Preregistered decision rule.** H-26 is supported only if (a) the recorded velocity is non-zero on
a static camera at a scale that forces resampling, and (b) the residual still tracks the resampling
perturbation after controlling for the spatial gradient. If the partial correlation collapses, the
co-location was the gradient all along and the hypothesis is rejected — the mechanism would then
have to be sought in what makes edges unstable, not in the history fetch.

Usage: python scripts/run_history_resampling_study.py [--out DIR]
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
FRAMES = 32
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 128, 256)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0}
#: Jitter length 1 is a *constant* Halton23(1) offset, not zero jitter (followup-09); JitterScale=0
#: is the only way to remove the sub-pixel offset while leaving the rest of the pipeline alone.
VARIANTS = [
    ('jitter 8 (production)', {'r.TAA.JitterSequenceLength': 8}),
    ('jitter 16', {'r.TAA.JitterSequenceLength': 16}),
    ('no sub-pixel offset (JitterScale=0)', {'r.TAA.JitterSequenceLength': 8,
                                             'r.TAA.JitterScale': 0.0}),
]


def luma(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def partial_corr(x, y, z):
    """Correlation of x and y with the linear effect of z removed from both."""
    x, y, z = x.ravel(), y.ravel(), z.ravel()
    if min(x.std(), y.std(), z.std()) < 1e-15:
        return None

    def resid(a):
        beta = np.cov(a, z)[0, 1] / max(1e-30, z.var())
        return a - beta * z
    rx, ry = resid(x), resid(y)
    if rx.std() < 1e-15 or ry.std() < 1e-15:
        return None
    return float(np.corrcoef(rx, ry)[0, 1])


def corr(a, b):
    a, b = a.ravel(), b.ravel()
    if a.std() < 1e-15 or b.std() < 1e-15:
        return None
    return float(np.corrcoef(a, b)[0, 1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '15-isolate-clamp-bounds'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_history_resampling')
    payload = {'roi': ROI, 'frames': FRAMES, 'first_frame': FIRST, 'variants': {}}

    for vname, cvars in VARIANTS:
        print(f'  ... {vname}', flush=True)
        res = runner.run_capture(os.path.join(work, vname.replace(' ', '_').replace('=', '')),
                                 PROJECT, LAST - SCENE_START + 8, regions=[ROI],
                                 region_fields='taa', region_first=FIRST, region_last=LAST,
                                 cvars={**BASE, **cvars}, timing_first=FIRST)
        if res['exit_code'] != 0:
            raise RuntimeError(f'{vname}: engine exited {res["exit_code"]}')
        seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
        rect = [f.rect(ROI[0]) for f in seq]
        out = np.stack([r.channel('taa_output')[..., :3] for r in rect]).astype(np.float64)
        hist = np.stack([r.channel('taa_history')[..., :3] for r in rect]).astype(np.float64)
        vx = np.stack([r.channel('taa_filtered')[..., 3] for r in rect]).astype(np.float64)
        vy = np.stack([r.channel('taa_history')[..., 3] for r in rect]).astype(np.float64)
        shutil.copy(res['manifest_path'],
                    os.path.join(args.out, 'manifests',
                                 vname.replace(' ', '_').replace('=', '') + '.json'))
        shutil.rmtree(res['region_dir'], ignore_errors=True)

        speed = np.sqrt(vx * vx + vy * vy)
        out_l = luma(out)
        resid = out_l.std(axis=0)
        hist_delta = np.abs(hist[1:] - out[:-1]).max(axis=-1).mean(axis=0)
        gy, gx = np.gradient(out_l.mean(axis=0))
        grad = np.sqrt(gx * gx + gy * gy)

        entry = {
            'cvars': cvars,
            'velocity': {
                'mean_px': float(speed.mean()), 'max_px': float(speed.max()),
                'p99_px': float(np.percentile(speed, 99)),
                'nonzero_fraction': float((speed > 1e-6).mean()),
                'subpixel_fraction': float(((speed > 1e-6) & (speed < 1.0)).mean()),
            },
            'residual_mean': float(resid.mean()),
            'history_resampling_mean': float(hist_delta.mean()),
            'history_resampling_max': float(hist_delta.max()),
            'corr_residual_resampling': corr(resid, hist_delta),
            'corr_residual_gradient': corr(resid, grad),
            'corr_resampling_gradient': corr(hist_delta, grad),
            'partial_corr_residual_resampling_given_gradient':
                partial_corr(resid, hist_delta, grad),
        }
        payload['variants'][vname] = entry
        v = entry['velocity']
        print(f"      velocity: mean {v['mean_px']:.4f} px, p99 {v['p99_px']:.4f}, max "
              f"{v['max_px']:.4f}; non-zero on {v['nonzero_fraction'] * 100:.1f} % of samples "
              f"({v['subpixel_fraction'] * 100:.1f} % sub-pixel)")
        print(f"      residual {entry['residual_mean']:.4e}, resampling "
              f"{entry['history_resampling_mean']:.4e}")
        print(f"      r(residual, resampling) = {entry['corr_residual_resampling']}, "
              f"r(residual, gradient) = {entry['corr_residual_gradient']}, "
              f"partial = {entry['partial_corr_residual_resampling_given_gradient']}", flush=True)

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'history-resampling.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
