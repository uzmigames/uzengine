"""Baseline against the pre-registered sampling candidate.

Criteria, requirements and thresholds come from
`docs/analysis/gather-sampling-candidate-preregistration.md`, written before the candidate existed.
Nothing here may loosen them; this script only applies them.

Phase `core` covers the criteria that one static capture can answer, plus both controls:

  fired     the capture must show the rotation mode the shader used, and mode 1's rotation must
            differ from mode 0's. A CVar that was passed but did nothing is void.
  control   mode 0, after the code change, must reproduce the pre-change baseline. The phase study's
            offset-0 region means are the reference; a difference means the change is not confined
            to what it claims.
  C1        period-2 share of the gather input's region-mean variance, <= 10 %
  C2        the region-mean series must no longer separate by parity, in every repeat
  C3        total temporal variance of that series, <= baseline. This is the criterion that stops
            period-2 being exported to other frequencies
  C5        energy: region mean within 1 % of baseline, per ROI, both poses
  C8        cost: GPU time of the SSGI passes, <= baseline + 2 %. Measured in captures with the
            region dump OFF, because the dump's own diagnostic probe ray would otherwise be
            measured instead of the candidate

Phase `full` adds C4 (error against a 32-sample reference), C6 (convergence from a cold history),
C7 (motion) and C9 (the TAA-output symptom at the full followup-09 ROIs).

Every criterion is applied to the worst repeat, never to a mean.

Usage: python scripts/run_candidate_evaluation.py [--phase core] [--frames 128] [--repeats 3]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import criteria as crit  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
JITTER_PERIOD = 8
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': JITTER_PERIOD}
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)

POSES = {
    'W': ('sponza-flicker-w.uzproject', ('shadow_wall', 150, 260, 64, 64)),
    'A': ('sponza-flicker-a.uzproject', ('right_shadow', 1472, 464, 64, 64)),
}

#: pre-registered thresholds — see the pre-registration document
C1_PERIOD2_SHARE = 0.10
C5_ENERGY = 0.01
C8_COST = 0.02
CONTROL_TOLERANCE = 1e-6     # mode 0 must reproduce the pre-change baseline

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def luma(rgb):
    return rgb @ REC709


# period_share lives in flicker/criteria.py: the local copy summed the one-sided
# spectrum without weighting paired bins, which inflated period 2.
period_share = crit.period_share


def measure(region_dir, first, last, name, ys, xs):
    seq = regiondump.load_sequence(region_dir, first, last)
    frames = np.array([f.frame for f in seq])
    cur = np.stack([luma(f.rect(name).channel('gather_current')[np.ix_(ys, xs)][..., :3]) for f in seq])
    rot = np.stack([f.rect(name).channel('gather_rotation')[np.ix_(ys, xs)] for f in seq])
    mode = rot[..., 2].copy().view(np.uint32)
    series = cur.mean(axis=(1, 2))
    odd, even = frames % 2 == 1, frames % 2 == 0
    return {
        'modes_seen': sorted(set(mode.ravel().tolist())),
        'rotation_x_mean': float(rot[..., 0].mean()),
        'rotation_parity_gap': abs(float(rot[..., 0][odd].mean()) - float(rot[..., 0][even].mean())),
        'series': series,
        'separates': bool(series[odd].min() > series[even].max()
                          or series[even].min() > series[odd].max()),
        'period2_share': period_share(series, 2),
        'total_variance': float(series.var()),
        'region_mean': float(cur.mean()),
        'odd_region': float(series[odd].mean()), 'even_region': float(series[even].mean()),
    }


def ssgi_cost(manifest_path):
    with open(manifest_path, encoding='utf-8') as fh:
        m = json.load(fh)
    return float(m.get('gpu_ms', {}).get('SSGI', float('nan')))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--phase', default='core', choices=('core', 'full'))
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '22-candidate'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()

    last = FIRST + args.frames - 1
    print('candidate evaluation — criteria from the pre-registration, applied to the worst repeat')
    print(f'  frames {FIRST}..{last}, {args.repeats} repeats, C1 <= {C1_PERIOD2_SHARE:.0%}, '
          f'C5 within {C5_ENERGY:.0%}, C8 <= +{C8_COST:.0%}')

    data = {}
    for pose_key in ('W', 'A'):
        project, roi = POSES[pose_key]
        name, x0, y0, w, h = roi
        ys, xs = np.arange(0, h, 2), np.arange(0, w, 2)
        for mode in (0, 1):
            reps = []
            for k in runner.reuse_repeat_range(args.repeats, args.reuse):
                work = os.path.join(args.out, 'static', pose_key, f'mode{mode}_r{k}')
                if not args.reuse:
                    print(f'  ... {pose_key} mode {mode} repeat {k}', flush=True)
                    res = runner.run_capture(
                        work, os.path.join(runner.REPO, 'scripts', project),
                        last - SCENE_START + 8, regions=[roi], region_fields='gather',
                        region_first=FIRST, region_last=last,
                        cvars={**BASE, 'r.GI.FinalGather.SampleRotation': mode},
                        timing_first=FIRST, clean=True)
                    if res['exit_code'] != 0:
                        raise RuntimeError(f'{pose_key} mode {mode} r{k}: exit {res["exit_code"]}')
                if args.reuse:
                    runner.require_region_dir(os.path.join(work, 'region'),
                                              f'{pose_key} mode {mode} repeat {k}')
                reps.append(measure(os.path.join(work, 'region'), FIRST, last, name, ys, xs))
                runner.release_repeat_dump(work, k)
            data[(pose_key, mode)] = reps

    # ---- cost, with the diagnostic off ------------------------------------
    cost = {}
    for mode in (0, 1):
        work = os.path.join(args.out, 'cost', f'mode{mode}')
        if not args.reuse:
            print(f'  ... cost mode {mode} (region dump off)', flush=True)
            res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', POSES['W'][0]),
                                     last - SCENE_START + 8,
                                     cvars={**BASE, 'r.GI.FinalGather.SampleRotation': mode},
                                     timing_first=FIRST, clean=True)
            if res['exit_code'] != 0:
                raise RuntimeError(f'cost mode {mode}: exit {res["exit_code"]}')
        cost[mode] = ssgi_cost(os.path.join(work, 'manifest.json'))

    # ---- the controls ------------------------------------------------------
    print()
    fired_ok, fired_detail = True, []
    for (pose_key, mode), reps in data.items():
        for k, m in enumerate(reps):
            if m['modes_seen'] != [mode]:
                fired_ok = False
                fired_detail.append(f'{pose_key} mode {mode} r{k} saw {m["modes_seen"]}')
    gap0 = max(m['rotation_parity_gap'] for (p, mo), r in data.items() if mo == 0 for m in r)
    gap1 = max(m['rotation_parity_gap'] for (p, mo), r in data.items() if mo == 1 for m in r)
    check(fired_ok and gap1 < gap0,
          'fired: the capture shows the mode the shader used, and the candidate\'s rotation differs',
          '; '.join(fired_detail) or f'parity gap of the rotation: mode 0 {gap0:.4f}, '
                                     f'mode 1 {gap1:.4f}')

    # mode 0 must reproduce the pre-change baseline (the phase study's offset 0)
    phase_path = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '20-gather-phase',
                              'phase_study.json')
    if os.path.exists(phase_path):
        with open(phase_path, encoding='utf-8') as fh:
            phase = json.load(fh)
        worst, detail = 0.0, []
        for pose_key in ('W', 'A'):
            want = phase['poses'][pose_key]['offsets']['0'][0]['region_mean']
            got = data[(pose_key, 0)][0]['region_mean']
            rel = abs(got / want - 1.0)
            worst = max(worst, rel)
            detail.append(f'{pose_key} {want:.9f} -> {got:.9f} ({rel:.2e})')
        check(worst <= CONTROL_TOLERANCE,
              'control: mode 0 reproduces the pre-change baseline',
              '; '.join(detail))
    else:
        check(False, 'control: mode 0 reproduces the pre-change baseline',
              f'the phase study report is missing at {phase_path}')

    # ---- the criteria ------------------------------------------------------
    print(f'\n  {"pose":>5}{"mode":>6}{"rep":>5}{"period2":>10}{"separates":>11}'
          f'{"variance":>13}{"region mean":>14}')
    for pose_key in ('W', 'A'):
        for mode in (0, 1):
            for k, m in enumerate(data[(pose_key, mode)]):
                print(f'  {pose_key:>5}{mode:>6}{k:>5}{m["period2_share"]:>9.1%}'
                      f'{str(m["separates"]):>11}{m["total_variance"]:>13.4e}'
                      f'{m["region_mean"]:>14.6f}')

    print()
    worst_p2 = max(m['period2_share'] for pk in ('W', 'A') for m in data[(pk, 1)])
    check(worst_p2 <= C1_PERIOD2_SHARE,
          f'C1 period-2 share of the candidate <= {C1_PERIOD2_SHARE:.0%}',
          f'worst {worst_p2:.1%}; baseline worst '
          f'{max(m["period2_share"] for pk in ("W", "A") for m in data[(pk, 0)]):.1%}')

    sep1 = [(pk, k) for pk in ('W', 'A') for k, m in enumerate(data[(pk, 1)]) if m['separates']]
    check(not sep1, 'C2 the candidate\'s region-mean series no longer separates by parity',
          f'still separating: {sep1}' if sep1 else 'no repeat separates')

    c3 = {}
    for pk in ('W', 'A'):
        b = max(m['total_variance'] for m in data[(pk, 0)])
        c = max(m['total_variance'] for m in data[(pk, 1)])
        c3[pk] = (b, c)
    check(all(c <= b for b, c in c3.values()),
          'C3 total temporal variance does not exceed baseline',
          '; '.join(f'{pk} {b:.3e} -> {c:.3e} ({c / b - 1:+.1%})' for pk, (b, c) in c3.items()))

    c5 = {}
    for pk in ('W', 'A'):
        b = float(np.mean([m['region_mean'] for m in data[(pk, 0)]]))
        worst = max(abs(m['region_mean'] / b - 1.0) for m in data[(pk, 1)])
        c5[pk] = worst
    check(max(c5.values()) <= C5_ENERGY,
          f'C5 energy within {C5_ENERGY:.0%} of baseline, per ROI',
          '; '.join(f'{pk} {v:+.2%}' for pk, v in c5.items()))

    rel_cost = cost[1] / cost[0] - 1.0 if cost[0] else float('nan')
    check(rel_cost <= C8_COST, f'C8 SSGI GPU cost <= baseline + {C8_COST:.0%}',
          f'{cost[0]:.3f} ms -> {cost[1]:.3f} ms ({rel_cost:+.2%})')

    report = {
        'first_frame': FIRST, 'frames': args.frames, 'repeats': args.repeats, 'phase': args.phase,
        'thresholds': {'C1': C1_PERIOD2_SHARE, 'C5': C5_ENERGY, 'C8': C8_COST},
        'static': {f'{pk}_mode{mo}': [{k: v for k, v in m.items()
                                       if not isinstance(v, (np.ndarray, list))} for m in reps]
                   for (pk, mo), reps in data.items()},
        'cost_ms': cost, 'passed': PASS, 'failed': FAIL,
    }
    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, f'candidate_{args.phase}.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n{len(PASS)} passed, {len(FAIL)} failed  ->  {path}')
    for f in FAIL:
        print(f'  FAILED: {f}')
    print('\n  not covered by this phase: C4 reference error, C6 convergence, C7 motion, '
          'C9 the TAA-output symptom. The candidate is not certified until those run.')
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
