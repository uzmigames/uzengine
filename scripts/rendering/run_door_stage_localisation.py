"""Where does `rear_door`'s oscillation first appear: deferred, atmosphere, translucency, particles?

One region, one mask, one symptom, held fixed for the whole comparison.

  region    `rear_door`, the FULL followup-09 rectangle (960, 592, 224, 224). Not a crop. Every
            condition uses this same rectangle, and `shadow_wall`'s full rectangle is captured
            alongside as a control.
  symptom   per-pixel temporal standard deviation of the TAA output's linear luminance, averaged
            over the rectangle. One number per condition; nothing else is compared.

The frame order is deferred lighting -> aerial perspective (applied inside the lighting passes) ->
translucency -> particles -> TAA. Each stage after the first is switched off in turn with a CVar
that already exists, and the symptom is measured on the fixed mask:

  baseline        everything on
  no_aerial       r.AerialPerspective=0
  no_translucency r.Show.Translucency=0
  no_particles    r.Show.Particles=0

**What this can and cannot conclude.** Switching a stage off changes the image, so a fall in the
symptom is consistent with two different things: that stage introduced the oscillation, or that stage
was simply covering the pixel and took its own content away with it. The mean is therefore reported
beside the oscillation — a stage that merely stopped covering the pixel moves the mean a great deal —
and `shadow_wall` is carried as a control where the answer is already known to be the opaque gather,
so none of these three stages should matter there. If a stage moves `shadow_wall` as much as it moves
`rear_door`, this method is not localising anything and the result is void.

Usage: python scripts/rendering/run_door_stage_localisation.py [--frames 128] [--repeats 3]
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

#: the fixed masks — full followup-09 rectangles, identical in every condition
POSES = {
    'A': ('sponza-flicker-a.uzproject', [('rear_door', 960, 592, 224, 224),
                                         ('right_shadow', 1472, 464, 256, 256)]),
    'W': ('sponza-flicker-w.uzproject', [('shadow_wall', 150, 260, 192, 384)]),
}
FLOORS = {'shadow_wall': 4.7e-7, 'rear_door': 8.9e-6, 'right_shadow': 2.1e-5}

CONDITIONS = {
    'baseline': {},
    'no_aerial': {'r.AerialPerspective': 0},
    'no_translucency': {'r.Show.Translucency': 0},
    'no_particles': {'r.Show.Particles': 0},
}
#: the control region, where the driver is already known and none of these stages should matter
CONTROL_ROI = 'shadow_wall'


def luma(rgb):
    return rgb @ REC709


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '28-door-stages'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    if args.reuse:
        print(f'  --reuse: only repeat {runner.RETAINED_REPEAT} is retained, so this covers ONE '
              'repeat, not the worst of several')
    print(f'rear_door stage localisation — fixed mask, one symptom, frames {FIRST}..{last}')
    print('  symptom: per-pixel temporal std of the TAA output luminance, averaged over the '
          'FULL followup-09 rectangle')

    osc, mean = {}, {}
    for pose_key, (project, rois) in POSES.items():
        for cond, cvars in CONDITIONS.items():
            for k in runner.reuse_repeat_range(args.repeats, args.reuse):
                work = os.path.join(args.out, pose_key, f'{cond}_r{k}')
                if not args.reuse:
                    print(f'  ... {pose_key} {cond} repeat {k}', flush=True)
                    res = runner.run_capture(
                        work, os.path.join(runner.REPO, 'scripts', 'rendering', project),
                        last - SCENE_START + 8, regions=rois, region_fields='output',
                        region_first=FIRST, region_last=last,
                        cvars={**BASE, **cvars}, timing_first=FIRST, clean=True)
                    if res['exit_code'] != 0:
                        raise RuntimeError(f'{pose_key} {cond} r{k}: exit {res["exit_code"]}')
                if args.reuse:
                    runner.require_region_dir(os.path.join(work, 'region'),
                                              f'{pose_key} {cond} repeat {k}')
                seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, last)
                for name, *_ in rois:
                    out = np.stack([luma(f.rect(name).channel('output')[..., :3]) for f in seq])
                    osc.setdefault((name, cond), []).append(float(out.std(axis=0).mean()))
                    mean.setdefault((name, cond), []).append(float(out.mean()))
                runner.release_repeat_dump(work, k)

    names = [n for _, rois in POSES.values() for n, *_ in rois]
    print(f'\n  {"ROI":>14}{"condition":>18}{"oscillation":>14}{"vs base":>10}'
          f'{"mean":>13}{"vs base":>10}')
    rows = {}
    for name in names:
        ob = max(osc[(name, 'baseline')])
        mb = float(np.mean(mean[(name, 'baseline')]))
        for cond in CONDITIONS:
            o = max(osc[(name, cond)])
            m = float(np.mean(mean[(name, cond)]))
            rows[f'{name}|{cond}'] = {'oscillation': o, 'mean': m,
                                      'osc_rel': o / ob - 1.0 if ob else float('nan'),
                                      'mean_rel': m / mb - 1.0 if mb else float('nan')}
            print(f'  {name:>14}{cond:>18}{o:>14.4e}{o / ob - 1:>+9.1%}'
                  f'{m:>13.4e}{m / mb - 1:>+9.1%}')

    print('\n  where the door\'s oscillation first appears — change against baseline, '
          'judged on followup-09\'s floor:')
    door_base = max(osc[('rear_door', 'baseline')])
    verdicts = {}
    for cond in CONDITIONS:
        if cond == 'baseline':
            continue
        d = max(osc[('rear_door', cond)]) - door_base
        believable = abs(d) > FLOORS['rear_door']
        verdicts[cond] = {'delta': d, 'believable': believable}
        print(f'    {cond:>18}: {d:+.3e} (floor {FLOORS["rear_door"]:.1e}) '
              f'{"REMOVES a believable part" if (believable and d < 0) else ""}'
              f'{"ADDS a believable part" if (believable and d > 0) else ""}'
              f'{"below the floor" if not believable else ""}')

    print(f'\n  control — the same switches at {CONTROL_ROI}, whose driver is the opaque gather:')
    ctrl_base = max(osc[(CONTROL_ROI, 'baseline')])
    control_ok = True
    for cond in CONDITIONS:
        if cond == 'baseline':
            continue
        d = max(osc[(CONTROL_ROI, cond)]) - ctrl_base
        moved = abs(d) > FLOORS[CONTROL_ROI]
        if moved:
            control_ok = False
        print(f'    {cond:>18}: {d:+.3e} (floor {FLOORS[CONTROL_ROI]:.1e}) '
              f'{"MOVED — the method is not localising" if moved else "unmoved, as required"}')
    print(f'  control verdict: {"passes" if control_ok else "FAILS — the localisation is void"}')

    report = {'first_frame': FIRST, 'frames': args.frames, 'repeats': args.repeats,
              'capture_identity': runner.capture_identity(os.path.join(args.out, pose_key, 'baseline_r0')),
              'conditions': CONDITIONS,
              'masks': {n: r for _, rois in POSES.values() for r in [rois] for n, *_ in rois},
              'rois': POSES, 'floors': FLOORS,
              'rows': rows, 'door_verdicts': verdicts, 'control_passes': control_ok}
    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, 'door_stages.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2, default=str)
    print(f'\n  -> {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
