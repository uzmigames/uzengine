"""Direct readback of the material AO the deferred lighting consumes (RT0.a), with and without
`r.Material.AOMap`. GTAO is off so the recorded `ao` is the G-Buffer alpha alone.

Records LIGHTING_EXTRA +1.w ('diffuse_indirect' channel, w = ambient occlusion) on two rectangles
over three frames, for each condition, and prints the distribution. No verdict — a reading.

Usage: python scripts/rendering/run_material_ao_check.py [--project sample/projects/sponza.uzproject] [--reuse]
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

ROIS = [('shadow_wall', 150, 300, 128, 128), ('lit_wall', 1100, 300, 128, 128), ('centre', 896, 476, 128, 128)]
FIRST, LAST, FRAMES = 58, 60, 70
BASE = {'r.TechPanel.Show': 0, 'r.GTAO': 0, 'r.TAA': 1}
CONDITIONS = {'maps_on': {}, 'ao_map_off': {'r.Material.AOMap': 0}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default='sample/projects/sponza.uzproject')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '47-material-ao-check'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    project = os.path.join(runner.REPO, args.project)
    tag = os.path.splitext(os.path.basename(args.project))[0]
    for cond, extra in CONDITIONS.items():
        work = os.path.join(args.out, f'{tag}_{cond}')
        if not args.reuse:
            res = runner.run_capture(work, project, FRAMES, regions=ROIS, region_fields='components',
                                     region_first=FIRST, region_last=LAST, cvars={**BASE, **extra},
                                     timing_first=200, clean=True)
            if res['exit_code'] != 0:
                print(f'{cond}: capture exit {res["exit_code"]}'); return 2
        region = runner.require_region_dir(os.path.join(work, 'region'), cond)
        seq = regiondump.load_sequence(region, FIRST, LAST)
        for roi_name, *_ in ROIS:
            vals, dists = [], []
            for f in seq:
                r = f.rect(roi_name)
                vals.append(r.channel('diffuse_indirect')[..., 3].astype(np.float64).ravel())
                dists.append(r.channel('lighting_normal')[..., 3].astype(np.float64).ravel())
            ao = np.concatenate(vals); dist = np.concatenate(dists)
            geo = dist >= 0.0
            a = ao[geo]
            if a.size == 0:
                print(f'{tag} {cond:<11} {roi_name:<12} geometry 0.0% — no reading'); continue
            print(f'{tag} {cond:<11} {roi_name:<12} geometry {geo.mean():.1%}  ao: mean {a.mean():.4f} min {a.min():.4f} '
                  f'max {a.max():.4f}  ==1.0: {(a >= 0.999).mean():.1%}  in[0.24,0.26]: {((a > 0.24) & (a < 0.26)).mean():.1%}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
