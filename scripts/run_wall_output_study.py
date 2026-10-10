"""followup-12, the wall: what the FINAL OUTPUT stage can do about the residual.

The gather candidate is abandoned — it made the symptom worse (H-36) and the parity structure it
removed turned out to be load-bearing. This looks instead at the stage the flicker is actually
measured in, the TAA resolve, and asks whether any knob it already exposes reduces `shadow_wall`'s
residual without paying the price that sank the previous candidates.

Region and symptom, fixed and identical in every condition:
  region    `shadow_wall`, the FULL followup-09 rectangle (150, 260, 192, 384)
  symptom   per-pixel temporal std of the TAA output's linear luminance, averaged over the rectangle

Conditions — only knobs that already exist, one changed at a time:
  baseline              as shipped
  antiflicker_max       r.TAA.AntiFlickerMax 0.3 -> 1.0   (an anti-flicker control not at maximum)
  current_weight_low    r.TAA.CurrentFrameWeight 0.04 -> 0.02   (more history)
  clip_wider            r.TAA.VarianceClipGamma 1.25 -> 2.0   (its own help text: wider = ghosting,
                        tighter = flickering)

Pre-registered before the captures:
  W1  the oscillation falls by more than followup-09's floor for this ROI (4.7e-07)
  W2  energy holds: region mean within 1 % of baseline
  W3  it is not achieved by blurring: mean spatial gradient magnitude within 5 % of baseline. This
      is the check followup-14's candidate had to answer, and it is applied here from the start

**What this cannot decide.** Every condition here trades temporal stability for responsiveness, and
responsiveness only shows under motion. A static camera cannot see ghosting or lag, so W1-W3 passing
means "reduces the residual on a still frame without blurring it", never "is a good default". The
motion case belongs to followup-13 and is not measured here; no recommendation is made without it.

Usage: python scripts/run_wall_output_study.py [--frames 128] [--repeats 3]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import criteria as crit  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)

ROI = ('shadow_wall', 150, 260, 192, 384)
PROJECT = 'sponza-flicker-w.uzproject'
FLOOR = 4.7e-7
W2_ENERGY = 0.01
W3_SHARPNESS = 0.05

CONDITIONS = {
    'baseline': {},
    'antiflicker_max': {'r.TAA.AntiFlickerMax': 1.0},
    'current_weight_low': {'r.TAA.CurrentFrameWeight': 0.02},
    'clip_wider': {'r.TAA.VarianceClipGamma': 2.0},
}

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def luma(rgb):
    return rgb @ REC709


def gradient(frame_luma):
    """Mean spatial gradient magnitude — falls when an image is blurred."""
    gy = np.abs(np.diff(frame_luma, axis=0)).mean()
    gx = np.abs(np.diff(frame_luma, axis=1)).mean()
    return float(0.5 * (gx + gy))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '29-wall-output'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    name = ROI[0]
    print(f'shadow_wall at the final output — fixed mask, frames {FIRST}..{last}')
    print(f'  pre-registered: W1 fall > {FLOOR:.1e}; W2 energy within {W2_ENERGY:.0%}; '
          f'W3 sharpness within {W3_SHARPNESS:.0%}')
    if args.reuse:
        print(f'  --reuse: only repeat {runner.RETAINED_REPEAT} is retained, so this covers ONE '
              'repeat, not the worst of several')

    osc, mean, sharp = {}, {}, {}
    for cond, cvars in CONDITIONS.items():
        for k in runner.reuse_repeat_range(args.repeats, args.reuse):
            work = os.path.join(args.out, f'{cond}_r{k}')
            if not args.reuse:
                print(f'  ... {cond} repeat {k}', flush=True)
                res = runner.run_capture(
                    work, os.path.join(runner.REPO, 'scripts', PROJECT),
                    last - SCENE_START + 8, regions=[ROI], region_fields='output',
                    region_first=FIRST, region_last=last,
                    cvars={**BASE, **cvars}, timing_first=FIRST, clean=True)
                if res['exit_code'] != 0:
                    raise RuntimeError(f'{cond} r{k}: exit {res["exit_code"]}')
            runner.require_region_dir(os.path.join(work, 'region'), f'{cond} repeat {k}')
            seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, last)
            out = np.stack([luma(f.rect(name).channel('output')[..., :3]) for f in seq])
            osc.setdefault(cond, []).append(crit.oscillation(out))
            mean.setdefault(cond, []).append(float(out.mean()))
            sharp.setdefault(cond, []).append(float(np.mean([gradient(fr) for fr in out])))
            runner.release_repeat_dump(work, k)

    ob = max(osc['baseline'])
    mb = float(np.mean(mean['baseline']))
    sb = float(np.mean(sharp['baseline']))
    print(f'\n  {"condition":>20}{"oscillation":>14}{"vs base":>10}{"energy":>10}{"sharpness":>12}')
    rows = {}
    for cond in CONDITIONS:
        o, m, s = max(osc[cond]), float(np.mean(mean[cond])), float(np.mean(sharp[cond]))
        rows[cond] = {'oscillation': o, 'mean': m, 'sharpness': s,
                      'osc_delta': o - ob, 'energy_rel': m / mb - 1.0, 'sharp_rel': s / sb - 1.0}
        print(f'  {cond:>20}{o:>14.4e}{o / ob - 1:>+9.1%}{m / mb - 1:>+9.2%}{s / sb - 1:>+11.2%}')

    print()
    winners = []
    for cond in CONDITIONS:
        if cond == 'baseline':
            continue
        r = rows[cond]
        w1 = crit.believable_change(r['osc_delta'], FLOOR) == 'better'
        w2 = abs(r['energy_rel']) <= W2_ENERGY
        w3 = abs(r['sharp_rel']) <= W3_SHARPNESS
        verdict = 'PASSES all three' if (w1 and w2 and w3) else 'fails'
        detail = (f"W1 {'ok' if w1 else 'no'} ({r['osc_delta']:+.2e}), "
                  f"W2 {'ok' if w2 else 'no'} ({r['energy_rel']:+.2%}), "
                  f"W3 {'ok' if w3 else 'no'} ({r['sharp_rel']:+.2%})")
        print(f'  {cond:>20}: {verdict} — {detail}')
        if w1 and w2 and w3:
            winners.append(cond)
        rows[cond].update({'W1': w1, 'W2': w2, 'W3': w3})

    check(bool(winners), 'at least one existing output knob reduces the wall residual without '
                         'costing energy or sharpness',
          f'{winners}' if winners else 'none of the three did')
    print('\n  Not decided here: every one of these trades responsiveness for stability, and that '
          'only shows under motion. No recommendation without the motion case (followup-13).')

    report = {'first_frame': FIRST, 'frames': args.frames, 'repeats': args.repeats,
              'capture_identity': runner.capture_identity(os.path.join(args.out, 'baseline_r0')), 'roi': ROI, 'conditions': CONDITIONS,
              'criteria': {'W1_floor': FLOOR, 'W2_energy': W2_ENERGY, 'W3_sharpness': W3_SHARPNESS},
              'rows': rows, 'passed': PASS, 'failed': FAIL}
    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, 'wall_output.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n  -> {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
