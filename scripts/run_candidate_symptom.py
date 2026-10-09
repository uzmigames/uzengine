"""C9: does removing the parity structure move the flicker anyone can see?

The candidate is already rejected, so this is not about certifying it. It answers a question about
H-31's reach: the parity structure is real and the candidate removes it, so if the TAA output's
oscillation does not fall, then the mechanism H-31 found is not what drives the residual flicker —
which is a finding about the mechanism, not a reason to discount the criterion.

Measured at the **full** followup-09 ROIs, not the 64x64 analysis windows, because the thresholds
this programme decided on are defined there. The cheap `output` preset is used (one record per
pixel), so the full rectangles fit; each capture's dump is released once measured.

  metric     per-pixel temporal standard deviation of the TAA output's linear luminance, averaged
             over the rectangle — the oscillation metric the rest of the programme uses
  C9a        candidate <= baseline, per ROI
  C9b        the followup-09 per-ROI thresholds are floors for believing a DIFFERENCE, defined there
             as `max(3 x 95 % half-width, 5 % of the mean)` with the note that a later task "may only
             call a difference an effect above these". So they decide which observed changes are real,
             not whether an oscillation is acceptable. A first version of this script compared the
             oscillation magnitude against them, which is a category error; corrected 2026-09-11
  mode 2     captured as well. It is the independent rotation, which maximises temporal noise by
             design, so it brackets the question: if even the noisiest scheme leaves the TAA
             oscillation where it was, the gather's frame sequence is not what the symptom responds to

Usage: python scripts/run_candidate_symptom.py [--frames 128] [--repeats 3]
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
WARMUP = 200
FIRST = SCENE_START + WARMUP
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)

#: the full followup-09 rectangles and that task's per-ROI decision thresholds
POSES = {
    'W': ('sponza-flicker-w.uzproject', [('shadow_wall', 150, 260, 192, 384)]),
    'A': ('sponza-flicker-a.uzproject', [('rear_door', 960, 592, 224, 224),
                                         ('right_shadow', 1472, 464, 256, 256)]),
}
THRESHOLDS = {'shadow_wall': 4.7e-7, 'rear_door': 8.9e-6, 'right_shadow': 2.1e-5}
MODES = {0: 'shipping', 1: 'candidate', 2: 'independent'}

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def luma(rgb):
    return rgb @ REC709


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '26-candidate-symptom'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    if args.reuse:
        print(f'  --reuse: only repeat {runner.RETAINED_REPEAT} is retained, so this covers ONE '
              'repeat, not the worst of several')
    print(f'C9 — the TAA-output symptom at the full followup-09 ROIs, frames {FIRST}..{last}')
    print(f'  thresholds: ' + ', '.join(f'{k} {v:.1e}' for k, v in THRESHOLDS.items()))

    osc = {}
    for pose_key, (project, rois) in POSES.items():
        for mode in MODES:
            for k in runner.reuse_repeat_range(args.repeats, args.reuse):
                work = os.path.join(args.out, pose_key, f'mode{mode}_r{k}')
                if not args.reuse:
                    print(f'  ... {pose_key} {MODES[mode]} repeat {k}', flush=True)
                    res = runner.run_capture(
                        work, os.path.join(runner.REPO, 'scripts', project),
                        last - SCENE_START + 8, regions=rois, region_fields='output',
                        region_first=FIRST, region_last=last,
                        cvars={**BASE, 'r.GI.FinalGather.SampleRotation': mode},
                        timing_first=FIRST, clean=True)
                    if res['exit_code'] != 0:
                        raise RuntimeError(f'{pose_key} mode {mode} r{k}: exit {res["exit_code"]}')
                if args.reuse:
                    runner.require_region_dir(os.path.join(work, 'region'),
                                              f'{pose_key} {MODES[mode]} repeat {k}')
                seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, last)
                for name, *_ in rois:
                    out = np.stack([luma(f.rect(name).channel('output')[..., :3]) for f in seq])
                    osc.setdefault((name, mode), []).append(float(out.std(axis=0).mean()))
                runner.release_repeat_dump(work, k)

    names = [n for _, rois in POSES.values() for n, *_ in rois]
    print(f'\n  {"ROI":>14}' + ''.join(f'{MODES[m]:>14}' for m in MODES) + f'{"threshold":>12}')
    for name in names:
        row = ''.join(f'{max(osc[(name, m)]):>14.4e}' for m in MODES)
        print(f'  {name:>14}{row}{THRESHOLDS[name]:>12.1e}')

    print()
    worse = []
    for name in names:
        b = max(osc[(name, 0)])
        c = max(osc[(name, 1)])
        if c > b:
            worse.append(f'{name} {b:.3e} -> {c:.3e} ({c / b - 1:+.1%})')
    check(not worse, 'C9a the candidate does not raise the TAA-output oscillation, per ROI',
          '; '.join(worse) if worse
          else '; '.join(f'{n} {max(osc[(n, 0)]):.3e} -> {max(osc[(n, 1)]):.3e} '
                         f'({max(osc[(n, 1)]) / max(osc[(n, 0)]) - 1:+.1%})' for n in names))

    # C9b: which of the observed changes clear followup-09's floor for being believable at all
    worsened, improved, below = [], [], []
    for n in names:
        d = max(osc[(n, 1)]) - max(osc[(n, 0)])
        if d > THRESHOLDS[n]:
            worsened.append(f'{n} {d:+.2e} vs floor {THRESHOLDS[n]:.1e}')
        elif -d > THRESHOLDS[n]:
            improved.append(f'{n} {d:+.2e} vs floor {THRESHOLDS[n]:.1e}')
        else:
            below.append(f'{n} {d:+.2e} vs floor {THRESHOLDS[n]:.1e}')
    print('\n  C9b which changes clear the followup-09 floor for being an effect at all:')
    for line in worsened:
        print(f'    worse     {line}')
    for line in improved:
        print(f'    better    {line}')
    for line in below:
        print(f'    below     {line}')
    # Only a believable WORSENING fails this. An earlier version tested |d|, which would have failed
    # a believable improvement too — wrong, though it did not change the verdict here.
    check(not worsened, 'C9b no believable worsening of the TAA output',
          f'believable worsening: {worsened}' if worsened
          else f'no worsening above floor; believable improvements: {improved or "none"}')

    # what the mechanism is worth at all: does ANY change to the gather's frame sequence move it?
    print('\n  reach of the mechanism — change in TAA-output oscillation against the shipping path:')
    for name in names:
        b = max(osc[(name, 0)])
        for mode in (1, 2):
            c = max(osc[(name, mode)])
            print(f'    {name:>14} {MODES[mode]:>12}: {c / b - 1:+7.1%}')

    report = {'first_frame': FIRST, 'frames': args.frames, 'repeats': args.repeats,
              'capture_identity': runner.capture_identity(os.path.join(args.out, 'W', 'mode0_r0')),
              'rotation_modes': MODES,
              'thresholds': THRESHOLDS, 'modes': MODES,
              'oscillation': {f'{n}_mode{m}': v for (n, m), v in osc.items()},
              'passed': PASS, 'failed': FAIL}
    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, 'symptom.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n{len(PASS)} passed, {len(FAIL)} failed  ->  {path}')
    for f in FAIL:
        print(f'  FAILED: {f}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
