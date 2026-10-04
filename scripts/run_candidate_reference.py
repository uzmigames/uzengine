"""C4: error against an independent reference, and which condition the reference favours.

The core evaluation rejected the candidate, and C5 — energy within 1 % of baseline — failed by
+7 % on the wall and +20 % on pose A. C5 as pre-registered takes the baseline's energy as correct.
That assumption is exactly what an independent reference can check, because "the candidate moved the
energy" and "the candidate removed a bias in the baseline" are indistinguishable from C5 alone.

First attempt, 2026-09-11: a 32-sample gather averaged over 128 static frames, one per (mode, pose),
required to agree within 1 %. They did not — 1.56 % on the wall and 3.75 % on pose A. At 32 samples
the mean still depends on the rotation sequence, so raising the sample count of the same scheme does
not produce a scheme-independent reference, and that attempt could not adjudicate. Recorded rather
than discarded: it is the evidence that the reference had to be built differently.

Second attempt, used here: rotation mode 2, an independent hashed rotation per (pixel, frame). That
is an unbiased Monte Carlo Cranley-Patterson rotation, so averaged over 128 frames at 32 samples it
estimates the integral both other modes approximate, and it is neither condition's sequence. It is a
reference generator only — independent per-frame rotations maximise temporal noise and must not ship.

Reported for each condition:
  bias        temporal mean of the 8-sample gather against the reference, relative
  rms         per-pixel RMS over frames of (gather - reference), relative to the reference — this
              is C4's criterion, lower is better
Neither number revises the core verdict. The candidate was rejected against criteria fixed before
it existed, and that stands. What this can do is say whether C5 was the right criterion, which is
recorded as a dated follow-up rather than a retroactive change.

Usage: python scripts/run_candidate_reference.py [--frames 128]
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

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
POSES = {
    'W': ('sponza-flicker-w.uzproject', ('shadow_wall', 150, 260, 64, 64)),
    'A': ('sponza-flicker-a.uzproject', ('right_shadow', 1472, 464, 64, 64)),
}
REFERENCE_SAMPLES = 32
REFERENCE_AGREEMENT = 0.01


def luma(rgb):
    return rgb @ REC709


def gather_series(region_dir, first, last, name, ys, xs):
    seq = regiondump.load_sequence(region_dir, first, last)
    return np.stack([luma(f.rect(name).channel('gather_current')[np.ix_(ys, xs)][..., :3])
                     for f in seq])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '24-candidate-reference'))
    ap.add_argument('--static', default=os.path.join(runner.REPO, '.screenshots',
                                                     'flicker-elimination', '22-candidate', 'static'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    print(f'C4 — error against a {REFERENCE_SAMPLES}-sample reference, frames {FIRST}..{last}')

    refs, out = {}, {}
    for pose_key, (project, roi) in POSES.items():
        name, x0, y0, w, h = roi
        ys, xs = np.arange(0, h, 2), np.arange(0, w, 2)
        per_mode_ref = {}
        for mode in (0, 1, 2):
            work = os.path.join(args.out, pose_key, f'ref_mode{mode}')
            if not args.reuse:
                print(f'  ... {pose_key} reference, rotation mode {mode}, '
                      f'{REFERENCE_SAMPLES} samples', flush=True)
                res = runner.run_capture(
                    work, os.path.join(runner.REPO, 'scripts', project), last - SCENE_START + 8,
                    regions=[roi], region_fields='gather', region_first=FIRST, region_last=last,
                    cvars={**BASE, 'r.GI.FinalGather.SampleRotation': mode,
                           'r.GI.FinalGather.Samples': REFERENCE_SAMPLES},
                    timing_first=FIRST, clean=True)
                if res['exit_code'] != 0:
                    raise RuntimeError(f'{pose_key} ref mode {mode}: exit {res["exit_code"]}')
            per_mode_ref[mode] = gather_series(os.path.join(work, 'region'), FIRST, last,
                                               name, ys, xs).mean(axis=0)

        a, b, c = per_mode_ref[0], per_mode_ref[1], per_mode_ref[2]
        agree = abs(float(a.mean()) / float(b.mean()) - 1.0)
        print(f'  {pose_key}: the two scheme-dependent {REFERENCE_SAMPLES}-sample means agree to '
              f'{agree:.2%} (mode 0 {a.mean():.6f}, mode 1 {b.mean():.6f}) — '
              f'{"above" if agree > REFERENCE_AGREEMENT else "within"} the '
              f'{REFERENCE_AGREEMENT:.0%} gate, so they are reported but not used')
        print(f'       independent reference (mode 2, {REFERENCE_SAMPLES} samples x '
              f'{args.frames} frames): {c.mean():.6f}')
        refs[pose_key] = c
        agree_ref = {'scheme_dependent_agreement': agree,
                     'independent_mean': float(c.mean()),
                     'mode0_32spp': float(a.mean()), 'mode1_32spp': float(b.mean())}
        refs[pose_key + '_meta'] = agree_ref

        for mode in (0, 1):
            region = os.path.join(args.static, pose_key, f'mode{mode}_r0', 'region')
            if not os.path.isdir(region):
                print(f'  skip {pose_key} mode {mode}: no 8-sample capture at {region}')
                continue
            series = gather_series(region, FIRST, last, name, ys, xs)
            ref = refs[pose_key]
            floor = max(float(ref.mean()) * 1e-3, 1e-9)
            bias = float(series.mean()) / float(ref.mean()) - 1.0
            rms = float(np.sqrt(((series - ref[None]) ** 2).mean(axis=0)).mean()
                        / max(float(ref.mean()), floor))
            out[f'{pose_key}_mode{mode}'] = {'bias': bias, 'rms_relative': rms,
                                             'scheme_dependent_agreement': agree,
                                             'reference_mean': float(ref.mean()),
                                             'condition_mean': float(series.mean())}

    print(f'\n  {"pose":>5}{"mode":>6}{"condition mean":>16}{"reference":>12}{"bias":>10}{"RMS":>10}')
    for key, v in out.items():
        pose_key, mode = key.split('_mode')
        print(f'  {pose_key:>5}{mode:>6}{v["condition_mean"]:>16.6f}{v["reference_mean"]:>12.6f}'
              f'{v["bias"]:>+9.2%}{v["rms_relative"]:>10.3f}')

    print()
    for pose_key in POSES:
        a = out.get(f'{pose_key}_mode0')
        b = out.get(f'{pose_key}_mode1')
        if not a or not b:
            continue
        closer = 'candidate' if abs(b['bias']) < abs(a['bias']) else 'baseline'
        rms_better = 'candidate' if b['rms_relative'] < a['rms_relative'] else 'baseline'
        print(f'  {pose_key}: closer in energy to the reference — {closer} '
              f'(baseline {a["bias"]:+.2%}, candidate {b["bias"]:+.2%}); '
              f'lower RMS — {rms_better} ({a["rms_relative"]:.3f} vs {b["rms_relative"]:.3f})')

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, 'reference.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump({'first_frame': FIRST, 'frames': args.frames,
                   'reference_samples': REFERENCE_SAMPLES,
                   'reference_agreement_threshold': REFERENCE_AGREEMENT,
                   'reference_meta': {k: v for k, v in refs.items() if k.endswith('_meta')},
                   'results': out}, fh, indent=2)
    print(f'\n  -> {path}')
    print('  This does not revise the core verdict; it tests whether C5 assumed the right thing.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
