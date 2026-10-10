"""followup-06: re-measure the firefly artefacts with the validated instrumentation, before any filter.

The original counts came from the display path — isolated pixels in a tonemapped screenshot. This
programme has since learned that display-side counts are not evidence about linear radiance
(followup-09 rule 4: no rejection on a post-tonemap threshold count alone), and it now has region
records that carry linear HDR. So the artefacts are re-counted where they live, before anything is
built to suppress them.

  masks     the full followup-09 rectangles, unchanged between conditions
  where     `lighting_reflection` — the SDF reflection radiance before the SSR blend, which is the
            term 4.1 is about — and `taa_output`, what a viewer actually sees
  isolation a pixel is isolated when its luminance exceeds ISOLATION_FACTOR times the median of its
            3x3 neighbourhood AND exceeds an absolute floor, so the median of a dark neighbourhood
            cannot manufacture a ratio. Both conditions, pre-registered below.

Reported per ROI: how many isolated pixels per frame, in the reflection term and in the output, and
what fraction of the region's energy they carry. A filter is worth building only for an artefact that
is still there and still carries something.

This measures; it does not filter. `r.GI.Reflections.HdrWeight` is already a quantified mitigation
(followup-06 4.1) and is left at its default here.

Usage: python scripts/rendering/run_firefly_reassessment.py [--frames 48] [--repeats 3]
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
    'A': ('sponza-flicker-a.uzproject', [('rear_door', 960, 592, 224, 224),
                                         ('right_shadow', 1472, 464, 256, 256)]),
    'W': ('sponza-flicker-w.uzproject', [('shadow_wall', 150, 260, 192, 384)]),
}

#: pre-registered isolation rule
ISOLATION_FACTOR = 8.0     # times the 3x3 neighbourhood median
ABSOLUTE_FLOOR = 1e-3      # linear luminance; below this a "bright" pixel is not visible anyway


def luma(rgb):
    return rgb @ REC709


def neighbourhood_median(a):
    """3x3 median, edges replicated."""
    p = np.pad(a, 1, mode='edge')
    stack = np.stack([p[i:i + a.shape[0], j:j + a.shape[1]]
                      for i in range(3) for j in range(3)])
    return np.median(stack, axis=0)


def isolated_mask(frame_luma):
    med = neighbourhood_median(frame_luma)
    return (frame_luma > ISOLATION_FACTOR * np.maximum(med, 1e-12)) & (frame_luma > ABSOLUTE_FLOOR)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=48)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '30-firefly-reassessment'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    print(f'followup-06 — fireflies re-counted in linear radiance, frames {FIRST}..{last}')
    print(f'  isolation: > {ISOLATION_FACTOR:g}x the 3x3 median AND > {ABSOLUTE_FLOOR:g} luminance')
    if args.reuse:
        print(f'  --reuse: only repeat {runner.RETAINED_REPEAT} is retained')

    res_rows = {}
    for pose_key, (project, rois) in POSES.items():
        for k in runner.reuse_repeat_range(args.repeats, args.reuse):
            work = os.path.join(args.out, pose_key, f'r{k}')
            if not args.reuse:
                print(f'  ... {pose_key} repeat {k}', flush=True)
                r = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', project),
                                       last - SCENE_START + 8, regions=rois,
                                       region_fields='components', region_first=FIRST,
                                       region_last=last, cvars=BASE, timing_first=FIRST, clean=True)
                if r['exit_code'] != 0:
                    raise RuntimeError(f'{pose_key} r{k}: exit {r["exit_code"]}')
            runner.require_region_dir(os.path.join(work, 'region'), f'{pose_key} repeat {k}')
            seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, last)
            for name, *_ in rois:
                for channel in ('lighting_reflection', 'taa_output'):
                    counts, shares = [], []
                    for f in seq:
                        fl = luma(f.rect(name).channel(channel)[..., :3]).astype(np.float64)
                        m = isolated_mask(fl)
                        counts.append(int(m.sum()))
                        total = float(fl.sum())
                        shares.append(float(fl[m].sum() / total) if total > 0 else 0.0)
                    res_rows.setdefault((name, channel), []).append(
                        {'mean_count': float(np.mean(counts)), 'max_count': int(np.max(counts)),
                         'mean_energy_share': float(np.mean(shares)),
                         'pixels': int(np.prod(luma(seq[0].rect(name).channel(channel)[..., :3]).shape))})
            runner.release_repeat_dump(work, k)

    print(f'\n  {"ROI":>14}{"channel":>22}{"isolated/frame":>16}{"worst frame":>13}'
          f'{"energy share":>14}{"of pixels":>11}')
    summary = {}
    for (name, channel), reps in res_rows.items():
        w = max(reps, key=lambda r: r['mean_count'])
        summary[f'{name}|{channel}'] = w
        print(f'  {name:>14}{channel:>22}{w["mean_count"]:>16.1f}{w["max_count"]:>13}'
              f'{w["mean_energy_share"]:>13.2%}{w["mean_count"] / w["pixels"]:>11.4%}')

    print('\n  reading this before deciding whether to build a filter:')
    for key, w in summary.items():
        name, channel = key.split('|')
        if w['mean_count'] == 0:
            verdict = 'nothing to filter by this rule'
        elif w['mean_energy_share'] < 0.01:
            verdict = f'present but carrying {w["mean_energy_share"]:.2%} of the energy'
        else:
            verdict = f'present and carrying {w["mean_energy_share"]:.1%} of the energy'
        print(f'    {name:>14} {channel:>22}: {verdict}')

    report = {'first_frame': FIRST, 'frames': args.frames, 'repeats': args.repeats,
              'capture_identity': runner.capture_identity(os.path.join(args.out, 'W', 'r0')),
              'isolation': {'factor': ISOLATION_FACTOR, 'absolute_floor': ABSOLUTE_FLOOR},
              'rois': {k: v for k, v in POSES.items()}, 'summary': summary,
              'per_repeat': {f'{n}|{c}': r for (n, c), r in res_rows.items()}}
    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, 'fireflies.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2, default=str)
    print(f'\n  -> {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
