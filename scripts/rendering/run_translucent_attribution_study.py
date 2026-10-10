"""followup-12, outstanding item: where does the door's and the right shadow's oscillation enter?

The wall is accounted for. Its gather input alternates with frame parity, and C9 showed that removing
that alternation makes the wall's visible flicker *worse*, so the parity lock is load-bearing there.
Neither of pose A's regions behaved that way: every rotation change at `rear_door` and
`right_shadow` was below followup-09's floor for being an effect at all. So whatever drives those two
is not the gather's sample sequence, and this asks a prior question before any translucent
instrumentation is written: does their oscillation enter *before* or *after* deferred lighting?

It can be asked from records that already exist, in one frame, with no filtering in between:

  composite   `direct_light + diffuse_indirect + specular_indirect + emissive_and_mis`, the deferred
              lighting pass's own output for that pixel — everything the opaque surface contributes
  taa_raw     the TAA resolve's raw centre sample, which is the scene colour it was handed, i.e.
              after every pass between deferred lighting and the resolve
  added       taa_raw - composite, same frame, same pixel: everything added after deferred lighting

`added` is NOT the translucent pass alone. Aerial perspective and any other stage between the two
also live in it (`APVolume` is 0.15 ms of every frame here). Naming it "translucency" would be the
kind of attribution this programme keeps having to retract, so it is named for what it is, and
isolating the translucent pass within it needs that pass instrumented — which is the decision this
study informs rather than pre-empts.

Pre-registered before the captures:

  Q1  the share of the region's post-lighting oscillation that `added` carries. If `added` oscillates
      at or below the composite's, then the oscillation is already present in the opaque lighting and
      instrumenting the translucent pass would be looking in the wrong place.
  Q2  whether the pixels in question even take the glass/transmission path, from the recorded path
      bitmask. A region called translucent-dominated that never sets that bit was mis-described.
  Q3  the comparison against `shadow_wall`, where the answer is known to be the opaque gather, so the
      statistic is calibrated on a case with a known answer rather than read in isolation.

Differences are judged against followup-09's per-ROI floors, which is what they are for.

Usage: python scripts/rendering/run_translucent_attribution_study.py [--frames 128] [--repeats 3]
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

#: 64x64 windows centred in the followup-09 rectangles, so 12 records per pixel stay affordable
POSES = {
    'A': ('sponza-flicker-a.uzproject', [('rear_door', 1040, 672, 64, 64),
                                         ('right_shadow', 1568, 560, 64, 64)]),
    'W': ('sponza-flicker-w.uzproject', [('shadow_wall', 214, 420, 64, 64)]),
}
FLOORS = {'shadow_wall': 4.7e-7, 'rear_door': 8.9e-6, 'right_shadow': 2.1e-5}
PATH_GLASS = 64


def luma(rgb):
    return rgb @ REC709


def measure(region_dir, first, last, name):
    seq = regiondump.load_sequence(region_dir, first, last)
    direct, diffuse, specular, emissive, raw, out = [], [], [], [], [], []
    glass, geometry = [], []
    for f in seq:
        r = f.rect(name)
        direct.append(r.channel('direct_light')[..., :3])
        diffuse.append(r.channel('diffuse_indirect')[..., :3])
        specular.append(r.channel('specular_indirect')[..., :3])
        emissive.append(r.channel('emissive_and_mis')[..., :3])
        raw.append(r.channel('taa_raw')[..., :3])
        out.append(r.channel('taa_output')[..., :3])
        glass.append((r.path_mask() & PATH_GLASS) != 0)
        geometry.append(r.geometry_mask())
    composite = np.stack(direct) + np.stack(diffuse) + np.stack(specular) + np.stack(emissive)
    raw = np.stack(raw)
    added = raw - composite
    return {
        'composite': float(luma(composite).std(axis=0).mean()),
        'taa_raw': float(luma(raw).std(axis=0).mean()),
        'added': float(luma(added).std(axis=0).mean()),
        'taa_output': float(luma(np.stack(out)).std(axis=0).mean()),
        'added_mean': float(luma(added).mean()),
        'composite_mean': float(luma(composite).mean()),
        'raw_mean': float(luma(raw).mean()),
        'glass_share': float(np.stack(glass).mean()),
        'geometry_share': float(np.stack(geometry).mean()),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '27-translucent-attribution'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    print(f'followup-12 — where the oscillation enters, frames {FIRST}..{last}')
    print('  composite = deferred lighting\'s own output; added = taa_raw - composite, i.e. every '
          'pass between them')

    data = {}
    for pose_key, (project, rois) in POSES.items():
        for k in range(args.repeats):
            work = os.path.join(args.out, pose_key, f'r{k}')
            if not args.reuse:
                print(f'  ... {pose_key} repeat {k}', flush=True)
                res = runner.run_capture(
                    work, os.path.join(runner.REPO, 'scripts', 'rendering', project),
                    last - SCENE_START + 8, regions=rois, region_fields='components',
                    region_first=FIRST, region_last=last, cvars=BASE,
                    timing_first=FIRST, clean=True)
                if res['exit_code'] != 0:
                    raise RuntimeError(f'{pose_key} r{k}: exit {res["exit_code"]}')
            for name, *_ in rois:
                data.setdefault(name, []).append(measure(os.path.join(work, 'region'),
                                                         FIRST, last, name))
            if k > 0:
                shutil.rmtree(os.path.join(work, 'region'), ignore_errors=True)

    print(f'\n  {"ROI":>14}{"composite":>12}{"added":>12}{"taa_raw":>12}{"taa_out":>12}'
          f'{"added/raw":>11}{"glass":>8}{"geom":>8}')
    rows = {}
    for name, reps in data.items():
        w = max(reps, key=lambda m: m['taa_raw'])      # worst repeat by the input's oscillation
        share = w['added'] / w['taa_raw'] if w['taa_raw'] else float('nan')
        rows[name] = {**w, 'added_over_raw': share}
        print(f'  {name:>14}{w["composite"]:>12.3e}{w["added"]:>12.3e}{w["taa_raw"]:>12.3e}'
              f'{w["taa_output"]:>12.3e}{share:>11.1%}{w["glass_share"]:>8.1%}'
              f'{w["geometry_share"]:>8.1%}')

    print('\n  Q1 — does the oscillation enter after deferred lighting?')
    for name, v in rows.items():
        verdict = ('after: `added` oscillates more than the composite'
                   if v['added'] > v['composite']
                   else 'before: the composite already oscillates at least as much')
        print(f'    {name:>14}: {verdict} '
              f'(composite {v["composite"]:.3e} vs added {v["added"]:.3e})')

    print('\n  Q2 — do these pixels take the glass/transmission path at all?')
    for name, v in rows.items():
        print(f'    {name:>14}: glass bit set on {v["glass_share"]:.1%} of pixel-frames, '
              f'deferred geometry on {v["geometry_share"]:.1%}')

    print('\n  Q3 — calibration against the region whose answer is known')
    wall = rows.get('shadow_wall')
    if wall:
        print(f'    shadow_wall is driven by the opaque gather, and its added/raw share is '
              f'{wall["added_over_raw"]:.1%}. A pose-A region materially above that is carrying '
              f'something the wall does not.')
        for name, v in rows.items():
            if name == 'shadow_wall':
                continue
            print(f'    {name:>14}: added/raw {v["added_over_raw"]:.1%} '
                  f'({"above" if v["added_over_raw"] > wall["added_over_raw"] else "at or below"} '
                  f'the wall)')

    print('\n  magnitudes, to check nothing here is a ratio of near-zeros:')
    for name, v in rows.items():
        print(f'    {name:>14}: composite mean {v["composite_mean"]:.4e}, '
              f'added mean {v["added_mean"]:+.4e}, taa_raw mean {v["raw_mean"]:.4e}')

    report = {'first_frame': FIRST, 'frames': args.frames, 'repeats': args.repeats,
              'rois': {n: [r for r in reps] for n, reps in data.items()},
              'worst_repeat': rows, 'floors': FLOORS}
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, 'attribution.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n  -> {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
