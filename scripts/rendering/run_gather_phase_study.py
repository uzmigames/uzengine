"""H-31 step 2: separate the sampling phase from the parity of the frame.

`r.GI.SDF.SampleFrameOffset` shifts the diffuse gather's sample sequence by whole frames and
nothing else — not the jitter, not the TAA, not a resource binding. The question it settles is
which of two things the alternation is attached to:

    the sampling      shifting the sequence by one frame SWAPS the bright and dark groups
    the frame index   shifting the sequence leaves the groups where they are

Pre-registered before the captures, so nothing here is chosen after seeing a number:

  P1  With offset 0 the region-mean series separates by parity: all odd-frame region means above
      all even-frame ones, or all below. That is the quantity H-31 is about. Per individual gather
      pixel the ranges overlap heavily, because one pixel is stochastic frame to frame; both
      figures are reported so the two are never confused for each other.
  P2  With offset 1 the region-mean series still separates — the alternation moves rather than
      merely disappearing, which a control that damaged the sampling would also produce.
  P3  The group that was bright at offset 0 is the dark one at offset 1, for at least 95 % of
      gather pixels. This is the discriminating outcome.
  P4  The alternation keeps its size: the ratio of group means at offset 1 is within 10 % of the
      ratio at offset 0. A phase shift moves the pattern, it does not shrink it.
  P5  The region's mean radiance is preserved to within 2 %, over the whole sequence and within
      each parity group. Swapping the labels must not be achieved by extinguishing the lighting —
      this is the energy check, and it is what would catch a control that merely darkened the image.

An offset of 2 is captured as well, as a null: two frames is a whole parity cycle, so it must leave
the groups exactly where offset 0 put them. If offset 2 also swaps, the intervention is doing
something other than shifting the phase and none of the above can be read.

Three repeats per offset, and two poses. The criteria are unchanged from the first single-capture
run. Every criterion is applied to the WORST repeat, never to their mean, so a spread that straddles
a threshold fails rather than averaging its way through; the spread is reported alongside, since
repeats of a deterministic renderer should agree closely and a wide one would be its own finding.

The study refuses to report at all unless the capture proves the intervention fired: the trace
records the frame index the shader saw, and the observed shift must equal the requested one.

Usage: python scripts/rendering/run_gather_phase_study.py [--frames 128] [--repeats 3] [--poses W,A]
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
JITTER_PERIOD = 8
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': JITTER_PERIOD}
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)

#: `shadow_wall` is the region followup-12 attributed to diffuse indirect. The pose-A region is a
#: different camera and different geometry, so the mechanism is asked to reappear somewhere it was
#: not found.
POSES = {
    'W': ('sponza-flicker-w.uzproject', ('shadow_wall', 150, 260, 64, 64)),
    'A': ('sponza-flicker-a.uzproject', ('right_shadow', 1472, 464, 64, 64)),
}

SWAP_FRACTION = 0.95
RATIO_TOLERANCE = 0.10
ENERGY_TOLERANCE = 0.02


def luma(rgb):
    return rgb @ REC709


def measure(region_dir, first, last, name, ys, xs):
    seq = regiondump.load_sequence(region_dir, first, last)
    frames = np.array([f.frame for f in seq])
    cur = np.stack([luma(f.rect(name).channel('gather_current')[np.ix_(ys, xs)][..., :3]) for f in seq])
    taa = np.stack([luma(f.rect(name).channel('taa_output')[..., :3]) for f in seq])
    seen = np.stack([f.rect(name).channel('gather_srv_a')[np.ix_(ys, xs)][..., 0] for f in seq])
    seen = seen.copy().view(np.uint32)
    shift = seen.astype(np.int64) - frames[:, None, None].astype(np.int64)
    odd, even = frames % 2 == 1, frames % 2 == 0
    series = cur.mean(axis=(1, 2))
    return {
        'observed_shift': sorted(set(shift.ravel().tolist())),
        'series_odd': series[odd], 'series_even': series[even],
        'odd_mean': cur[odd].mean(axis=0), 'even_mean': cur[even].mean(axis=0),
        'odd_min': cur[odd].min(axis=0), 'odd_max': cur[odd].max(axis=0),
        'even_min': cur[even].min(axis=0), 'even_max': cur[even].max(axis=0),
        'region_mean': float(cur.mean()), 'odd_region': float(cur[odd].mean()),
        'even_region': float(cur[even].mean()),
        'taa_oscillation': float(taa.std(axis=0).mean()),
    }


def disjoint(m):
    """Per-pixel: fraction of gather pixels whose odd and even ranges do not overlap."""
    return float(((m['odd_min'] > m['even_max']) | (m['even_min'] > m['odd_max'])).mean())


def separated(m):
    """Region mean: do the odd-frame means and the even-frame means separate?"""
    o, e = m['series_odd'], m['series_even']
    return bool(o.min() > e.max() or e.min() > o.max())


def ratio(m):
    hi, lo = max(m['odd_region'], m['even_region']), min(m['odd_region'], m['even_region'])
    return hi / lo if lo > 0 else float('inf')


def evaluate(pose_key, roi, data, repeats):
    """Applies the criteria to the worst repeat of each offset."""
    passed, failed = [], []

    def check(ok, what, detail=''):
        (passed if ok else failed).append(what)
        print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)

    for offset in (0, 1, 2):
        for k, m in enumerate(data[offset]):
            if m['observed_shift'] != [offset]:
                raise SystemExit(
                    f'{pose_key} offset {offset} repeat {k}: the trace shader saw a frame-index '
                    f'shift of {m["observed_shift"]}, not [{offset}]. The intervention did not '
                    'fire, so nothing would mean anything and nothing is reported.')
    print(f'  gate — every repeat saw the requested shift')

    print(f'\n  {"offset":>7}{"rep":>5}{"odd mean":>13}{"even mean":>13}{"ratio":>9}'
          f'{"region mean":>14}{"per-pixel":>11}{"separated":>11}')
    for offset in (0, 1, 2):
        for k, m in enumerate(data[offset]):
            print(f'  {offset:>7}{k:>5}{m["odd_region"]:>13.6f}{m["even_region"]:>13.6f}'
                  f'{ratio(m):>9.3f}{m["region_mean"]:>14.6f}{disjoint(m):>10.1%}'
                  f'{str(separated(m)):>11}')

    spread = {o: (float(np.std([m['region_mean'] for m in data[o]]))
                  / max(float(np.mean([m['region_mean'] for m in data[o]])), 1e-12))
              for o in (0, 1, 2)}
    print(f'\n  repeat spread of the region mean: '
          + ', '.join(f'offset {o} {spread[o]:.2%}' for o in (0, 1, 2)))

    print()
    check(all(separated(m) for m in data[0]),
          'P1 offset 0: the region-mean series separates by parity, in every repeat',
          f'per-pixel {min(disjoint(m) for m in data[0]):.1%}..'
          f'{max(disjoint(m) for m in data[0]):.1%}')
    check(all(separated(m) for m in data[1]),
          'P2 offset 1: it still separates — the alternation moved, it did not vanish',
          f'per-pixel {min(disjoint(m) for m in data[1]):.1%}..'
          f'{max(disjoint(m) for m in data[1]):.1%}')

    # every repeat of offset 1 against every repeat of offset 0: the worst pair decides
    swaps = []
    for m0 in data[0]:
        b0 = m0['odd_mean'] > m0['even_mean']
        for m1 in data[1]:
            swaps.append(float((b0 != (m1['odd_mean'] > m1['even_mean'])).mean()))
    check(min(swaps) >= SWAP_FRACTION,
          'P3 offset 1 swaps which parity is bright — the alternation follows the sampling',
          f'worst pair {min(swaps):.1%}, best {max(swaps):.1%}, required {SWAP_FRACTION:.0%}')

    r0 = [ratio(m) for m in data[0]]
    r1 = [ratio(m) for m in data[1]]
    worst_ratio = max(abs(b / a - 1.0) for a in r0 for b in r1)
    check(worst_ratio <= RATIO_TOLERANCE, 'P4 the alternation keeps its size',
          f'ratio {np.mean(r0):.3f} -> {np.mean(r1):.3f}, worst pair {worst_ratio:+.1%}, '
          f'allowed {RATIO_TOLERANCE:.0%}')

    def worst_energy(key):
        a = [m[key] for m in data[0]]
        b = [m[key] for m in data[1]]
        return max(abs(y / x - 1.0) for x in a for y in b)

    hi_key = 'odd_region' if data[0][0]['odd_region'] > data[0][0]['even_region'] else 'even_region'
    lo_key = 'even_region' if hi_key == 'odd_region' else 'odd_region'
    # after the swap the bright group is the other parity, so compare group magnitudes not labels
    def group_mag(m, bright):
        vals = (m['odd_region'], m['even_region'])
        return max(vals) if bright else min(vals)
    e_all = worst_energy('region_mean')
    e_hi = max(abs(group_mag(y, True) / group_mag(x, True) - 1.0) for x in data[0] for y in data[1])
    e_lo = max(abs(group_mag(y, False) / group_mag(x, False) - 1.0) for x in data[0] for y in data[1])
    check(max(e_all, e_hi, e_lo) <= ENERGY_TOLERANCE,
          'P5 energy is preserved — the groups swapped, the lighting was not extinguished',
          f'region {e_all:.2%}, bright group {e_hi:.2%}, dark group {e_lo:.2%}, '
          f'allowed {ENERGY_TOLERANCE:.0%}')

    swaps2 = []
    for m0 in data[0]:
        b0 = m0['odd_mean'] > m0['even_mean']
        for m2 in data[2]:
            swaps2.append(float((b0 != (m2['odd_mean'] > m2['even_mean'])).mean()))
    check(max(swaps2) <= 1.0 - SWAP_FRACTION,
          'null: offset 2 is a whole parity cycle and must NOT swap',
          f'worst {max(swaps2):.1%}, required at most {1 - SWAP_FRACTION:.0%}')

    print(f'  {len(passed)} passed, {len(failed)} failed')
    return {
        'roi': roi, 'passed': passed, 'failed': failed,
        'repeat_spread': spread,
        'swap_worst': min(swaps), 'swap_best': max(swaps), 'null_swap_worst': max(swaps2),
        'ratio': {'0': r0, '1': r1},
        'offsets': {str(o): [{k: v for k, v in m.items()
                              if not isinstance(v, (np.ndarray, list))} for m in data[o]]
                    for o in (0, 1, 2)},
        'observed_shift': {str(o): data[o][0]['observed_shift'] for o in (0, 1, 2)},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--poses', default='W,A')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '20-gather-phase'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()

    last = FIRST + args.frames - 1
    print(f'H-31 step 2 — sampling phase vs frame parity, frames {FIRST}..{last}')
    print(f'  criteria fixed before capture: swap >= {SWAP_FRACTION:.0%}, ratio within '
          f'{RATIO_TOLERANCE:.0%}, energy within {ENERGY_TOLERANCE:.0%}; '
          f'{args.repeats} repeats per offset, worst repeat decides')

    reports = {}
    for pose_key in args.poses.split(','):
        project, roi = POSES[pose_key]
        name, x0, y0, w, h = roi
        ys, xs = np.arange(0, h, 2), np.arange(0, w, 2)
        print(f'\n== pose {pose_key}, ROI {roi}')
        data = {}
        for offset in (0, 1, 2):
            reps = []
            for k in range(args.repeats):
                work = os.path.join(args.out, pose_key, f'offset_{offset}_r{k}')
                if not args.reuse:
                    print(f'  ... capturing offset {offset} repeat {k}', flush=True)
                    res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', project),
                                             last - SCENE_START + 8, regions=[roi],
                                             region_fields='gather', region_first=FIRST,
                                             region_last=last,
                                             cvars={**BASE, 'r.GI.SDF.SampleFrameOffset': offset},
                                             timing_first=FIRST, clean=True)
                    if res['exit_code'] != 0:
                        raise RuntimeError(f'{pose_key} offset {offset} r{k}: '
                                           f'engine exited {res["exit_code"]}')
                reps.append(measure(os.path.join(work, 'region'), FIRST, last, name, ys, xs))
            data[offset] = reps
        reports[pose_key] = evaluate(pose_key, roi, data, args.repeats)

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, 'phase_study.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump({'first_frame': FIRST, 'frames': args.frames, 'repeats': args.repeats,
                   'criteria': {'swap_fraction': SWAP_FRACTION,
                                'ratio_tolerance': RATIO_TOLERANCE,
                                'energy_tolerance': ENERGY_TOLERANCE},
                   'poses': reports}, fh, indent=2)
    failed = {k: r['failed'] for k, r in reports.items() if r['failed']}
    print(f'\n-> {path}')
    if failed:
        print(f'  FAILED: {failed}')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
