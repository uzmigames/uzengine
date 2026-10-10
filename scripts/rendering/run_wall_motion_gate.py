"""followup-12: can `r.TAA.VarianceClipGamma=2.0` pass the motion gate it was held back for?

On a still camera it reduced `shadow_wall`'s residual by 6.7 % at no cost in energy or sharpness
(ledger H-39). It was kept out of the defaults because widening the variance clip keeps more history,
and too much history shows up only when the camera moves — as ghosting during motion and as a slow
recovery after the history goes wrong. This is that test, and it invents no new gate.

**The gate is followup-14's G6, reused, not re-derived.** `motion_stats` is imported from
`run_correction_validation.py` and called as it stands, so the recovery and the error after the cut
are computed by exactly the code that judged the previous candidate:

  G6   recovery after the cut <= baseline + 5 frames, AND the error 80 frames after the cut
       <= 2x baseline. Worst candidate repeat against best baseline repeat, as G6 did.

The sequence is `sponza-flicker-w-motion`: converge at pose W, strafe 60 cm, hold, then a ONE-FRAME
CUT back to pose W, which makes the history wrong at a stroke. Too much history recovers slowly.

**Two regions, each compared only with itself.** G6 was defined on `window`, where the high-contrast
bars make lag visible, so it is applied there exactly. The candidate is for `shadow_wall`, so the same
algorithm is applied there too. Because `motion_stats` hardcodes the `window` rectangle, a local
generalisation takes the rectangle name — and before it is trusted it must reproduce `motion_stats`
bit for bit on `window`. If it does not, the `shadow_wall` numbers are not G6 and are not reported.

Every variant gets every repeat. `stage_motion` in the validation script gives extra variants a
single run (`reps = repeats if not extra else 1`), the same defect fixed in followup-13's study; it is
not repeated here.

Usage: python scripts/rendering/run_wall_motion_gate.py [--repeats 3]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'flicker'))
sys.path.insert(0, HERE)

import criteria as crit  # noqa: E402
import regiondump  # noqa: E402
import run_correction_validation as rcv  # noqa: E402
import runner  # noqa: E402

WALL_ROI = ('shadow_wall', 150, 260, 192, 384)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
VARIANTS = {
    'baseline': {},
    'clip_wider': {'r.TAA.VarianceClipGamma': 2.0},
}
G6_RECOVERY_SLACK = 5
G6_ERROR_RATIO = 2.0


def motion_stats_for(region_dir, rect_name):
    """`rcv.motion_stats` with the rectangle as a parameter. Same algorithm, line for line."""
    seq = regiondump.load_sequence(region_dir, rcv.MOTION_FIRST, rcv.MOTION_LAST)
    out_l = np.stack([f.rect(rect_name).luminance('taa_output') for f in seq]).astype(np.float64)
    raw_l = np.stack([f.rect(rect_name).luminance('taa_raw') for f in seq]).astype(np.float64)
    rel = np.array([f.frame - rcv.SCENE_START for f in seq])
    res = {'phases': {}}
    for name, (lo, hi) in rcv.MOTION_PHASES.items():
        idx = np.flatnonzero((rel >= lo) & (rel <= hi))
        res['phases'][name] = {
            'frames': [int(rel[idx[0]]), int(rel[idx[-1]])],
            'std_mean': float(out_l[idx].std(axis=0).mean()),
            'lag': float(np.abs(out_l[idx] - raw_l[idx]).mean()),
            'mean': float(out_l[idx].mean()),
        }
    hold1 = np.flatnonzero((rel >= rcv.MOTION_PHASES['hold1'][0]) & (rel <= rcv.MOTION_PHASES['hold1'][1]))
    converged = out_l[hold1].mean(axis=0)
    tol = 0.02 * float(converged.mean())
    after = np.flatnonzero(rel >= 301)
    err = np.abs(out_l[after] - converged[None]).mean(axis=(1, 2))
    bad = np.flatnonzero(err > tol)
    res['recovery'] = {
        'tolerance': tol,
        'frames': int(bad[-1] + 1) if len(bad) else 0,
        'captured_after_cut': int(len(after)),
        'peak_error': float(err.max()) if len(err) else float('nan'),
        'error_8_frames_after_cut': float(err[min(8, len(err) - 1)]) if len(err) else float('nan'),
        'error_80_frames_after_cut': float(err[min(79, len(err) - 1)]) if len(err) else float('nan'),
    }
    return res


def g6(base_runs, cand_runs):
    b_frames = [r['recovery']['frames'] for r in base_runs]
    c_frames = [r['recovery']['frames'] for r in cand_runs]
    b_e80 = [r['recovery']['error_80_frames_after_cut'] for r in base_runs]
    c_e80 = [r['recovery']['error_80_frames_after_cut'] for r in cand_runs]
    for v in b_e80 + c_e80:
        if not np.isfinite(v):
            raise SystemExit('an error-at-80-frames value is not finite; the capture did not reach '
                             '80 frames after the cut, so G6 cannot be judged and is not reported')
    ok_rec = max(c_frames) <= min(b_frames) + G6_RECOVERY_SLACK
    ok_err = max(c_e80) <= G6_ERROR_RATIO * min(b_e80)
    # A recovery that ran out of frames, or a tolerance below the region's own converged noise, makes
    # G6 unable to discriminate: the comparison would pass vacuously. Report that, not a verdict.
    # Found on shadow_wall 2026-09-15, where both conditions recovered in 82 of 82 frames.
    censored = [r['recovery']['frames'] for r in base_runs + cand_runs
                if crit.recovery_censored(r['recovery']['frames'], r['recovery']['captured_after_cut'])]
    unusable = [r['recovery']['tolerance'] for r in base_runs + cand_runs
                if not crit.recovery_tolerance_usable(r['recovery']['tolerance'],
                                                      r['phases']['hold1']['std_mean'])]
    detail = {
        'recovery_frames': {'baseline': b_frames, 'candidate': c_frames, 'pass': ok_rec},
        'error_80': {'baseline': b_e80, 'candidate': c_e80, 'pass': ok_err},
        'censored_runs': len(censored), 'tolerance_below_noise_runs': len(unusable),
    }
    # Censoring alone decides this. The tolerance-versus-oscillation count is kept as
    # information only: it compared G6's tolerance with a per-pixel temporal std, not
    # the quantity the tolerance bounds, so it cannot show the criterion is unusable.
    if censored:
        return 'NOT JUDGEABLE', detail
    return ('PASSES' if (ok_rec and ok_err) else 'FAILS'), detail


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '31-wall-motion-gate'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    frames = rcv.MOTION_LAST - rcv.SCENE_START + 8
    print(f'VarianceClipGamma=2.0 against followup-14\'s G6 motion gate — frames '
          f'{rcv.MOTION_FIRST}..{rcv.MOTION_LAST}, cut at relative 300')
    print(f'  G6: recovery <= baseline + {G6_RECOVERY_SLACK} frames AND error at 80 frames '
          f'<= {G6_ERROR_RATIO:g}x baseline; worst candidate repeat vs best baseline repeat')
    if args.reuse:
        print(f'  --reuse: only repeat {runner.RETAINED_REPEAT} is retained, so this covers ONE repeat')

    runs = {v: {'window': [], 'shadow_wall': []} for v in VARIANTS}
    fidelity_checked = False
    for vname, cvars in VARIANTS.items():
        for k in runner.reuse_repeat_range(args.repeats, args.reuse):
            work = os.path.join(args.out, f'{vname}_r{k}')
            if not args.reuse:
                print(f'  ... {vname} repeat {k}', flush=True)
                res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', rcv.MOTION_PROJECT),
                                         frames, regions=[rcv.MOTION_ROI, WALL_ROI],
                                         region_fields='taa', region_first=rcv.MOTION_FIRST,
                                         region_last=rcv.MOTION_LAST, cvars={**BASE, **cvars},
                                         timing_first=rcv.MOTION_FIRST, clean=True)
                if res['exit_code'] != 0:
                    raise RuntimeError(f'{vname} r{k}: exit {res["exit_code"]}')
            region = runner.require_region_dir(os.path.join(work, 'region'), f'{vname} repeat {k}')

            official = rcv.motion_stats(region)                    # G6's own code, on `window`
            if not fidelity_checked:
                local = motion_stats_for(region, 'window')
                if json.dumps(local, sort_keys=True) != json.dumps(official, sort_keys=True):
                    raise SystemExit('the generalised motion_stats does not reproduce the official one '
                                     'on `window`; its shadow_wall numbers would not be G6, so nothing '
                                     'is reported')
                print('  fidelity — the generalised algorithm reproduces motion_stats exactly on window')
                fidelity_checked = True
            runs[vname]['window'].append(official)
            runs[vname]['shadow_wall'].append(motion_stats_for(region, 'shadow_wall'))
            runner.release_repeat_dump(work, k)

    report = {'capture_identity': runner.capture_identity(os.path.join(args.out, 'baseline_r0')),
              'variants': VARIANTS, 'phases': rcv.MOTION_PHASES,
              'gate': {'recovery_slack_frames': G6_RECOVERY_SLACK, 'error_ratio': G6_ERROR_RATIO},
              'regions': {}}
    print()
    for region_name in ('window', 'shadow_wall'):
        b, c = runs['baseline'][region_name], runs['clip_wider'][region_name]
        verdict, detail = g6(b, c)
        hold_b = max(r['phases']['hold1']['std_mean'] for r in b)
        hold_c = max(r['phases']['hold1']['std_mean'] for r in c)
        lag_b = max(r['phases']['strafe']['lag'] for r in b)
        lag_c = max(r['phases']['strafe']['lag'] for r in c)
        mean_b = float(np.mean([r['phases']['hold1']['mean'] for r in b]))
        mean_c = float(np.mean([r['phases']['hold1']['mean'] for r in c]))
        print(f'  {region_name}:')
        if verdict == 'NOT JUDGEABLE':
            print(f'    G6 NOT JUDGEABLE — {detail["censored_runs"]} recoveries did not complete within the observed window')
        print(f'    G6 {verdict} — recovery '
              f'{detail["recovery_frames"]["baseline"]} -> {detail["recovery_frames"]["candidate"]} frames '
              f'({"ok" if detail["recovery_frames"]["pass"] else "over"}); error@80 '
              f'{min(detail["error_80"]["baseline"]):.4e} -> {max(detail["error_80"]["candidate"]):.4e} '
              f'({"ok" if detail["error_80"]["pass"] else "over"})')
        print(f'    still-camera oscillation (hold1) {hold_b:.4e} -> {hold_c:.4e} ({hold_c / hold_b - 1:+.1%})')
        print(f'    lag during the strafe            {lag_b:.4e} -> {lag_c:.4e} ({lag_c / lag_b - 1:+.1%})')
        print(f'    energy (hold1 mean)              {mean_b:.4e} -> {mean_c:.4e} ({mean_c / mean_b - 1:+.2%})')
        report['regions'][region_name] = {
            'g6_verdict': verdict, 'g6': detail,
            'hold1_oscillation': {'baseline': hold_b, 'candidate': hold_c},
            'strafe_lag': {'baseline': lag_b, 'candidate': lag_c},
            'hold1_mean': {'baseline': mean_b, 'candidate': mean_c},
            'runs': {'baseline': b, 'candidate': c},
        }

    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, 'wall_motion_gate.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n  -> {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
