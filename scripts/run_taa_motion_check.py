"""Motion, disocclusion and camera-cut check for TAA variants (followup-10 item 1.7).

A variant that lowers the static residual is worthless if it does so by blurring, by lagging, or by
keeping history it should have rejected. This runs each candidate through one continuous sequence
with four phases (`scripts/sponza-flicker-w-motion.lua`) and measures, per phase:

* `static_std` — the residual with the camera still (what the candidate is trying to improve);
* `lag_vs_raw` — mean |TAA output - raw sample| while moving. A blurrier or slower filter shows a
  larger lag; the raw sample is the un-accumulated image of the same frame;
* `history_rejected` — the fraction of pixels whose reprojected history was off-screen
  (disocclusion), and the error at exactly those pixels;
* `recovery_frames` — after the one-frame cut back to the phase-1 pose, how many frames until the
  output matches the converged phase-1 image again. The content is identical on both sides of the
  cut, so this measures the history's recovery and nothing else; a variant that keeps invalid
  history (or never converges) is caught here.

Usage: python scripts/run_taa_motion_check.py [--out DIR]
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
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w-motion.uzproject')
ROI = ('window', 770, 200, 192, 256)

# Phase boundaries in relative frames (see the Lua): hold 0..120, strafe 121..240,
# hold 241..300, cut at 302, hold 303..382.
PHASES = {'hold1': (60, 118), 'strafe': (125, 238), 'hold2': (245, 298), 'after_cut': (303, 378)}
FIRST = SCENE_START + 40
LAST = SCENE_START + 382

VARIANTS = [
    ('baseline', {}),
    ('clamp 3: AABB only', {'r.TAA.ClampMode': 3}),
    ('clamp 1: no clamp', {'r.TAA.ClampMode': 1}),
    ('reconstruction 1: no HDR weighting', {'r.TAA.ReconstructionMode': 1}),
    ('reconstruction 1 + clamp 3', {'r.TAA.ReconstructionMode': 1, 'r.TAA.ClampMode': 3}),
]
BASE_CVARS = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}


def phase_slice(frames, name):
    lo, hi = PHASES[name]
    return [i for i, f in enumerate(frames) if lo <= f.frame - SCENE_START <= hi]


def analyse(region_dir):
    frames = regiondump.load_sequence(region_dir, FIRST, LAST)
    out = np.stack([f.rect(ROI[0]).luminance('taa_output') for f in frames]).astype(np.float64)
    raw = np.stack([f.rect(ROI[0]).luminance('taa_raw') for f in frames]).astype(np.float64)
    rejected = np.stack([~f.rect(ROI[0]).history_valid_mask() for f in frames])
    stats = {}
    for name in PHASES:
        idx = phase_slice(frames, name)
        if len(idx) < 4:
            continue
        o, r, rej = out[idx], raw[idx], rejected[idx]
        entry = {
            'frames': [int(frames[i].frame) for i in (idx[0], idx[-1])],
            'std_mean': float(o.std(axis=0).mean()),
            'mean': float(o.mean()),
            'lag_vs_raw': float(np.abs(o - r).mean()),
            'history_rejected_fraction': float(rej.mean()),
        }
        if rej.any():
            entry['lag_at_rejected'] = float(np.abs(o - r)[rej].mean())
        stats[name] = entry
    # Recovery after the cut, against the converged phase-1 image of the SAME content.
    idx1 = phase_slice(frames, 'hold1')
    reference = out[idx1[-16:]].mean(axis=0) if len(idx1) >= 16 else out[idx1].mean(axis=0)
    tol = 0.02 * float(np.percentile(reference, 99))
    cut_idx = [i for i, f in enumerate(frames) if f.frame - SCENE_START >= 301]
    if cut_idx:
        err = np.abs(out[cut_idx] - reference[None, :, :]).mean(axis=(1, 2))
        above = np.flatnonzero(err > tol)
        recovered = not len(above) or int(above[-1] + 1) < len(cut_idx)
        stats['recovery'] = {
            'frames': int(above[-1] + 1) if len(above) else 0,
            'recovered_within_capture': bool(recovered),
            'captured_frames_after_cut': int(len(cut_idx)),
            'tolerance': float(tol),
            'peak_error_after_cut': float(err.max()),
            'error_8_frames_after_cut': float(err[min(8, len(err) - 1)]),
            'residual_error_at_end': float(err[-1]),
        }
        stats['ghosting_after_cut'] = float(np.abs(out[cut_idx[:8]] - raw[cut_idx[:8]]).mean())
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '10-isolate-taa-window'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_taa_motion')

    results = {}
    for label, cvars in VARIANTS:
        print(f'  … {label}', flush=True)
        d = os.path.join(work, label.replace(' ', '_').replace(':', ''))
        res = runner.run_capture(d, PROJECT, LAST - SCENE_START + 8, regions=[ROI],
                                 region_fields='taa', region_first=FIRST, region_last=LAST,
                                 cvars={**BASE_CVARS, **cvars}, timing_first=FIRST)
        if res['exit_code'] != 0:
            raise RuntimeError(f'{label}: engine exited {res["exit_code"]}')
        stats = analyse(res['region_dir'])
        stats['cvars'] = cvars
        results[label] = stats
        shutil.rmtree(res['region_dir'], ignore_errors=True)
        h1, st, rec = stats['hold1'], stats['strafe'], stats.get('recovery', {})
        print(f"      static std {h1['std_mean']:.5f}  static lag {h1['lag_vs_raw']:.4f}  "
              f"motion lag {st['lag_vs_raw']:.4f}  "
              f"recovery {rec.get('frames')} frames"
              f"{'' if rec.get('recovered_within_capture') else ' (NOT recovered)'}  "
              f"peak error {rec.get('peak_error_after_cut', float('nan')):.4f}  "
              f"residual {rec.get('residual_error_at_end', float('nan')):.5f}")

    path = os.path.join(args.out, 'motion-check.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump({'roi': ROI, 'phases': PHASES, 'base_cvars': BASE_CVARS, 'results': results,
                   'source_fingerprint': runner.source_fingerprint()}, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
