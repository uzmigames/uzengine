"""Do the reflections carry the same frame-parity structure? Measured, not inferred.

`GatherSdfReflections` builds its rotation from the same hash and the same base-2 radical inverse as
the diffuse gather, so the structure H-31 found is *expected* there. Expected is not measured, and
similar code justifies a test rather than extending a diagnosis. This is that test, on the
reflections' own recorded channel.

  What it reads. `lighting_reflection` is the SDF reflection radiance before the SSR blend, written
  by the deferred lighting pass at render resolution. Only pixels whose recorded path bitmask has
  the SDF-reflections bit set are counted, and only where the pass reported geometry — a pixel that
  never took the path says nothing about it.

  The statistic is the one H-31 used, so the two are comparable: does the region-mean series separate
  into disjoint odd-frame and even-frame groups, and by what ratio.

  A control that matters. The diffuse candidate (`r.GI.FinalGather.SampleRotation`) does not touch
  the reflections path. So the reflection measurement must come out the SAME in both modes. If it
  does not, the candidate is not confined to what it claims and the comparison is void.

This reuses the candidate evaluation's captures; it adds no captures of its own.

Usage: python scripts/run_reflection_parity_study.py
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
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
ROIS = {'W': ('shadow_wall', 64, 64), 'A': ('right_shadow', 64, 64)}
PATH_SDF_REFLECTIONS = 1


def luma(rgb):
    return rgb @ REC709


# period_share lives in flicker/criteria.py: this local copy summed the one-sided spectrum
# without weighting conjugate pairs, which inflated the period-2 share.
period_share = crit.period_share


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--captures', default=os.path.join(runner.REPO, '.screenshots',
                                                       'flicker-elimination', '22-candidate',
                                                       'static'))
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '23-reflection-parity'))
    args = ap.parse_args()
    last = FIRST + args.frames - 1

    print('reflections: frame parity, on their own channel')
    results = {}
    for pose_key, (name, _, _) in ROIS.items():
        for mode in (0, 1):
            region = os.path.join(args.captures, pose_key, f'mode{mode}_r0', 'region')
            if not os.path.isdir(region):
                print(f'  skip {pose_key} mode {mode}: no capture at {region}')
                continue
            seq = regiondump.load_sequence(region, FIRST, last)
            frames = np.array([f.frame for f in seq])

            refl, mask = [], []
            for f in seq:
                r = f.rect(name)
                rec = r.channel('lighting_reflection')
                bits = r.path_mask()
                geometry = r.geometry_mask()
                refl.append(luma(rec[..., :3]))
                mask.append(((bits & PATH_SDF_REFLECTIONS) != 0) & geometry)
            refl = np.stack(refl)
            mask = np.stack(mask)

            # a pixel counts only where it took the path in every frame, so the series is not a
            # changing population of pixels
            always = mask.all(axis=0)
            share = float(mask.mean())
            if not always.any():
                print(f'  {pose_key} mode {mode}: the SDF reflections path is taken on '
                      f'{share:.1%} of pixel-frames and by no pixel in every frame — '
                      'nothing measurable here')
                results[f'{pose_key}_mode{mode}'] = {'pixels': 0, 'path_share': share}
                continue

            series = refl[:, always].mean(axis=1)
            odd, even = frames % 2 == 1, frames % 2 == 0
            o, e = series[odd], series[even]
            separates = bool(o.min() > e.max() or e.min() > o.max())
            ratio = (max(o.mean(), e.mean()) / min(o.mean(), e.mean())
                     if min(o.mean(), e.mean()) > 0 else float('inf'))
            entry = {
                'pixels': int(always.sum()), 'path_share': share,
                'separates_by_parity': separates, 'ratio': float(ratio),
                'period2_share': period_share(series, 2),
                'region_mean': float(series.mean()),
                'odd_mean': float(o.mean()), 'even_mean': float(e.mean()),
            }
            results[f'{pose_key}_mode{mode}'] = entry
            print(f'  {pose_key} mode {mode}: {entry["pixels"]} pixels took the path in every frame '
                  f'({share:.1%} of pixel-frames)')
            print(f'      odd {o.mean():.6e}  even {e.mean():.6e}  ratio {ratio:.3f}  '
                  f'separates {separates}  period-2 share {entry["period2_share"]:.1%}')

    # ---- the control: the diffuse candidate must not have touched reflections
    print()
    ok, detail = True, []
    for pose_key in ROIS:
        a = results.get(f'{pose_key}_mode0')
        b = results.get(f'{pose_key}_mode1')
        if not a or not b or not a.get('pixels') or not b.get('pixels'):
            continue
        rel = abs(b['region_mean'] / a['region_mean'] - 1.0) if a['region_mean'] else float('inf')
        detail.append(f'{pose_key} {rel:.2e}')
        if rel > 1e-6:
            ok = False
    if detail:
        print(f'  {"ok  " if ok else "FAIL"} control: the diffuse candidate leaves the reflections '
              f'unchanged — ' + '; '.join(detail))
    else:
        print('  control not evaluated: no pose had measurable reflections in both modes')

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, 'reflection_parity.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump({'first_frame': FIRST, 'frames': args.frames, 'results': results,
                   'candidate_confined': ok if detail else None}, fh, indent=2)
    print(f'\n  -> {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
