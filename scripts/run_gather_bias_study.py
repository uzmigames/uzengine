"""H-34: is the shipping gather's energy deficit a sample-count convergence effect, or not?

H-34 reported the 8-sample gather sitting 8.2 % and 17.2 % below an independent reference, and
attributed it to the shipping rotation covering the rotation space badly. That attribution has a
consequence that can be checked, and a control that can refute it.

Pre-registered before the captures:

  P1  The shipping scheme's region mean rises with sample count, 4 -> 8 -> 32, monotonically. An
      estimator whose mean depends on how many samples it draws is biased at the low counts.
  P2  **The control.** The independent scheme's region mean does NOT depend on sample count — flat
      across 4 -> 8 -> 32 within 2 %. An unbiased estimator's expectation is the same at every
      count; only its variance changes. If the independent scheme's mean also rises, then the
      count-dependence is common to all of them and belongs to something else entirely (the trace,
      the origin offset, the plane test), and H-34's attribution to the rotation is wrong.
  P3  The shipping scheme's gap to the reference shrinks as the count rises: |bias| at 32 samples
      below |bias| at 8.

P2 is the criterion that makes this worth running. P1 and P3 alone would be consistent with any
count-dependent effect at all.

The reference is the independent scheme at 32 samples, from the same captures.

Usage: python scripts/run_gather_bias_study.py [--frames 128]
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
COUNTS = (4, 8, 32)
MODES = {0: 'shipping', 2: 'independent'}
P2_FLATNESS = 0.02

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def luma(rgb):
    return rgb @ REC709


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '25-gather-bias'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    print(f'H-34 — does the energy deficit converge with sample count? frames {FIRST}..{last}')
    print(f'  pre-registered: P1 shipping mean rises with count; P2 independent mean flat within '
          f'{P2_FLATNESS:.0%} (the control); P3 shipping gap shrinks')

    means = {}
    for pose_key, (project, roi) in POSES.items():
        name, x0, y0, w, h = roi
        ys, xs = np.arange(0, h, 2), np.arange(0, w, 2)
        for mode in MODES:
            for count in COUNTS:
                work = os.path.join(args.out, pose_key, f'mode{mode}_s{count}')
                if args.reuse:
                    runner.require_region_dir(os.path.join(work, 'region'),
                                              f'{pose_key} {MODES[mode]} {count} samples')
                if not args.reuse:
                    print(f'  ... {pose_key} {MODES[mode]} {count} samples', flush=True)
                    res = runner.run_capture(
                        work, os.path.join(runner.REPO, 'scripts', project),
                        last - SCENE_START + 8, regions=[roi], region_fields='gather',
                        region_first=FIRST, region_last=last,
                        cvars={**BASE, 'r.GI.FinalGather.SampleRotation': mode,
                               'r.GI.FinalGather.Samples': count},
                        timing_first=FIRST, clean=True)
                    if res['exit_code'] != 0:
                        raise RuntimeError(f'{pose_key} mode {mode} s{count}: '
                                           f'exit {res["exit_code"]}')
                seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, last)
                cur = np.stack([luma(f.rect(name).channel('gather_current')[np.ix_(ys, xs)][..., :3])
                                for f in seq])
                # confirm the count the shader used, from the capture itself
                rays = np.stack([f.rect(name).channel('gather_first_ray')[np.ix_(ys, xs)][..., 3]
                                 for f in seq])
                seen = sorted(set(rays.ravel().astype(int).tolist()))
                if seen != [count]:
                    raise SystemExit(f'{pose_key} mode {mode} s{count}: the shader reported sample '
                                     f'counts {seen}, not [{count}]. The CVar did not take; nothing '
                                     'is reported.')
                means[(pose_key, mode, count)] = float(cur.mean())

    print(f'\n  {"pose":>5}{"scheme":>14}' + ''.join(f'{c:>12}' for c in COUNTS))
    for pose_key in POSES:
        for mode in MODES:
            row = ''.join(f'{means[(pose_key, mode, c)]:>12.6f}' for c in COUNTS)
            print(f'  {pose_key:>5}{MODES[mode]:>14}{row}')

    print()
    # P1
    p1 = {}
    for pose_key in POSES:
        v = [means[(pose_key, 0, c)] for c in COUNTS]
        p1[pose_key] = all(v[i] < v[i + 1] for i in range(len(v) - 1))
    check(all(p1.values()), 'P1 the shipping scheme\'s mean rises with sample count',
          '; '.join(f'{p} {" < ".join(f"{means[(p, 0, c)]:.6f}" for c in COUNTS)}' for p in POSES))

    # P2 — the control
    p2 = {}
    for pose_key in POSES:
        v = [means[(pose_key, 2, c)] for c in COUNTS]
        p2[pose_key] = (max(v) / min(v) - 1.0) if min(v) > 0 else float('inf')
    check(max(p2.values()) <= P2_FLATNESS,
          f'P2 control: the independent scheme\'s mean is flat within {P2_FLATNESS:.0%} '
          f'across sample counts',
          '; '.join(f'{p} spread {v:.2%}' for p, v in p2.items())
          + ' — if this failed, the count-dependence is not about the rotation')

    # P3
    p3, detail = {}, []
    for pose_key in POSES:
        ref = means[(pose_key, 2, 32)]
        b8 = abs(means[(pose_key, 0, 8)] / ref - 1.0)
        b32 = abs(means[(pose_key, 0, 32)] / ref - 1.0)
        p3[pose_key] = b32 < b8
        detail.append(f'{pose_key} {b8:.2%} -> {b32:.2%}')
    check(all(p3.values()), 'P3 the shipping scheme\'s gap to the reference shrinks with count',
          '; '.join(detail))

    report = {'first_frame': FIRST, 'frames': args.frames, 'counts': list(COUNTS),
              'schemes': MODES, 'p2_flatness': P2_FLATNESS,
              'means': {f'{p}_mode{m}_s{c}': v for (p, m, c), v in means.items()},
              'passed': PASS, 'failed': FAIL}
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, 'bias.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n{len(PASS)} passed, {len(FAIL)} failed  ->  {path}')
    for f in FAIL:
        print(f'  FAILED: {f}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
