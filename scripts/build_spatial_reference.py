"""An independent converged spatial reference, and what it says about "not blur" (followup-14).

Everything in this task so far was measured against the **cycle-averaged raw input**: the mean of
the eight jittered pre-TAA samples of a pixel. Review finding (2026-09-09): that is an approximation
of the correctly resampled image, not the image itself. It is an 8-sample Monte Carlo estimate
built from the *same* jitter offsets the candidate is accused of being sensitive to, so it cannot
by itself decide whether a change is more correct or merely more aligned with that sampling
pattern. The claim "the gain is not blur" rested on the output moving towards the instantaneous
input, which is weaker still: the instantaneous sample carries jitter-dependent coverage of its own.

This builds a reference that shares neither property:

* `r.ScreenPercentage=200` — the frame is rendered at 3840x2160, so each output pixel integrates
  four *spatially distinct* geometric samples rather than eight temporally distinct sub-pixel
  offsets of the same 1x grid;
* the **un-accumulated** `taa_raw` record, averaged over one full jitter cycle — the scene colour
  before any history is blended in, so nothing is inherited across frames.

The first attempt used `r.TAA=0` and the `output` record. That record is written by the TAA resolve,
so with the pass disabled it was all zeros and the reference was empty — the same failure mode as
the vacuous mirror test earlier in this task. Probing also showed the TAA records are written at
*output* resolution even at 200 %, so the rectangle must not be scaled.

and then reports, for the baseline and for the candidate:

* per-pixel error against that reference (the accuracy question);
* a sharpness measure computed on real scene content — mean gradient magnitude, and the gradient
  ratio against the reference — because a blurred image loses gradient energy. The analytic thin
  bar covers one synthetic feature; this covers the actual ROI.

**Caveat, stated because it is not a ground truth:** rendering at 2x also changes the sampling
density of the GI, AO and reflection passes, so the reference differs from the 1x image for reasons
other than resampling. It is an *independent* reference, not an exact one, and the numbers below are
comparisons of two candidates against the same imperfect yardstick.

Usage: python scripts/build_spatial_reference.py [--out DIR]
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

CVAR = 'r.TAA.ReconstructionHdrWeightExponent'
SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 24
LAST = FIRST + FRAMES - 1
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}

POSES = {
    'W': ('sponza-flicker-w.uzproject', ('window', 768, 152, 192, 512), {}),
    'A': ('sponza-flicker-a.uzproject', ('rear_door', 960, 592, 224, 224), {}),
}
SUPERSAMPLE = 2


def capture(work, label, project, roi, cvars, fields, frames_first, frames_last):
    res = runner.run_capture(os.path.join(work, label),
                             os.path.join(runner.REPO, 'scripts', project),
                             frames_last - SCENE_START + 8, regions=[roi], region_fields=fields,
                             region_first=frames_first, region_last=frames_last,
                             cvars=cvars, timing_first=frames_first)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    return res


def gradient_energy(img):
    """Mean gradient magnitude — a blurred image has less of it."""
    gy, gx = np.gradient(img.astype(np.float64))
    return float(np.sqrt(gx * gx + gy * gy).mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '14-validate-flicker-correction'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_spatial_reference')
    payload = {'supersample': SUPERSAMPLE, 'frames': FRAMES, 'poses': {}}

    for pose_key, (project, roi, extra) in POSES.items():
        name, x, y, w, h = roi
        print(f'  ... {pose_key} reference at {SUPERSAMPLE}x, un-accumulated', flush=True)
        ref_res = capture(work, f'{pose_key}_reference', project, roi,
                          {**BASE, **extra, 'r.ScreenPercentage': 100 * SUPERSAMPLE},
                          'taa', FIRST, FIRST + 7)
        manifest = ref_res['manifest'] or {}
        render = (manifest.get('resolution') or {}).get('render') or []
        if render and render[0] != 1920 * SUPERSAMPLE:
            raise RuntimeError(f'{pose_key}: asked for {SUPERSAMPLE}x but the manifest reports '
                               f'render resolution {render} — the reference would be no better '
                               f'than the 1x runs')
        seq = regiondump.load_sequence(ref_res['region_dir'], FIRST, FIRST + 7)
        reference = np.stack([f.rect(name).luminance('taa_raw')
                              for f in seq]).astype(np.float64).mean(0)
        if not np.any(reference > 0):
            raise RuntimeError(f'{pose_key}: the reference is empty — nothing wrote the record')
        entry = {'roi': roi, 'reference_resolution': manifest.get('resolution'),
                 'reference_gradient': gradient_energy(reference),
                 'reference_mean': float(reference.mean()), 'variants': {}}
        shutil.rmtree(ref_res['region_dir'], ignore_errors=True)

        for vname, exponent in (('baseline', 1.0), ('candidate_0.0', 0.0)):
            print(f'  ... {pose_key} {vname}', flush=True)
            res = capture(work, f'{pose_key}_{vname}'.replace('.', 'p'), project, roi,
                          {**BASE, **extra, CVAR: f'{exponent:g}'}, 'taa', FIRST, LAST)
            seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
            out = np.stack([f.rect(name).luminance('taa_output') for f in seq]).astype(np.float64)
            raw = np.stack([f.rect(name).luminance('taa_raw') for f in seq]).astype(np.float64)
            cycle = raw.reshape(-1, 8, *raw.shape[1:]).mean(axis=1).mean(axis=0)
            conv = out.mean(axis=0)
            entry['variants'][vname] = {
                'error_vs_spatial_reference': float(np.abs(conv - reference).mean()),
                'rms_vs_spatial_reference': float(np.sqrt(((conv - reference) ** 2).mean())),
                'energy_vs_spatial_reference': float(conv.mean() / reference.mean()),
                'gradient_energy': gradient_energy(conv),
                'gradient_ratio_vs_reference': gradient_energy(conv) / entry['reference_gradient'],
                # how good a proxy the cycle-averaged raw input actually is
                'cycle_mean_error_vs_spatial_reference': float(np.abs(cycle - reference).mean()),
                'cycle_mean_energy_vs_spatial_reference': float(cycle.mean() / reference.mean()),
                'cycle_mean_gradient_ratio': gradient_energy(cycle) / entry['reference_gradient'],
            }
            v = entry['variants'][vname]
            print(f"      error vs spatial reference {v['error_vs_spatial_reference']:.4f}, "
                  f"energy {v['energy_vs_spatial_reference']:.4f}, "
                  f"gradient ratio {v['gradient_ratio_vs_reference']:.4f}", flush=True)
            shutil.rmtree(res['region_dir'], ignore_errors=True)
        payload['poses'][pose_key] = entry

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'spatial-reference.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
