"""Which render pass actually produces each region's colour (followup-12, checklist 1.1/1.7).

The deferred lighting records only describe surfaces shaded by the deferred pass. Before attributing
a region's brightness or its flicker to a lighting component, this measures how much of the region's
scene colour survives with the forward translucent pass disabled (`r.Show.Translucency=0`) and how
much the recorded components explain. A region whose colour comes from the forward pass cannot be
explained by the deferred records at all, and saying so is the point of this script.

Usage: python scripts/run_pass_attribution.py [--out DIR]
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
FIRST = SCENE_START + 120
FRAMES = 8
LAST = FIRST + FRAMES - 1
REC709 = np.array([0.2126, 0.7152, 0.0722])
POSES = {
    'W': (os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject'),
          [('shadow_wall', 150, 260, 192, 384), ('wall_bounce', 620, 300, 128, 384),
           ('window', 770, 150, 192, 512)]),
    'A': (os.path.join(runner.REPO, 'scripts', 'sponza-flicker-a.uzproject'),
          [('rear_door', 960, 592, 224, 224), ('right_shadow', 1472, 464, 256, 256),
           ('lit_floor', 832, 816, 256, 64)]),
}
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}


def measure(work, pose, label, cvars):
    project, rois = POSES[pose]
    res = runner.run_capture(os.path.join(work, f'{pose}_{label}'), project, LAST - SCENE_START + 8,
                             regions=rois, region_fields='full', region_first=FIRST, region_last=LAST,
                             cvars={**BASE, **cvars}, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{pose}/{label}: engine exited {res["exit_code"]}')
    frames = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
    out = {}
    for name, *_ in rois:
        rects = [f.rect(name) for f in frames]
        raw = np.stack([r.luminance('taa_raw') for r in rects]).astype(np.float64)
        comp = np.stack([(r.channel('direct_light')[..., :3] + r.channel('diffuse_indirect')[..., :3]
                          + r.channel('specular_indirect')[..., :3]
                          + r.channel('emissive_and_mis')[..., :3]) @ REC709 for r in rects])
        geo = rects[0].geometry_mask()
        out[name] = {
            'scene_colour_mean': float(raw[:, geo].mean()),
            'components_mean': float(comp[:, geo].mean()),
            'scene_colour_std': float(raw[:, geo].std(axis=0).mean()),
            'geometry_fraction': float(geo.mean()),
        }
    shutil.rmtree(res['region_dir'], ignore_errors=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '12-isolate-shadow-lighting'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_pass_attribution')

    payload = {'first_frame': FIRST, 'frames': FRAMES, 'poses': {}}
    for pose in POSES:
        print(f'pose {pose}', flush=True)
        base = measure(work, pose, 'baseline', {})
        notl = measure(work, pose, 'no_translucent', {'r.Show.Translucency': 0})
        rows = {}
        for name in base:
            b, n = base[name], notl[name]
            deferred_share = n['scene_colour_mean'] / b['scene_colour_mean'] if b['scene_colour_mean'] else float('nan')
            rows[name] = {
                'scene_colour_mean': b['scene_colour_mean'],
                'scene_colour_without_translucent': n['scene_colour_mean'],
                'recorded_components_mean': b['components_mean'],
                'deferred_share_of_colour': deferred_share,
                'translucent_share_of_colour': 1.0 - deferred_share if deferred_share == deferred_share else float('nan'),
                'components_explain_fraction': (b['components_mean'] / b['scene_colour_mean']
                                                if b['scene_colour_mean'] else float('nan')),
                'classification': ('deferred' if deferred_share > 0.9 else
                                   'translucent' if deferred_share < 0.1 else 'mixed'),
            }
            r = rows[name]
            print(f"  {name:14s} colour {r['scene_colour_mean']:.5f}  without translucent "
                  f"{r['scene_colour_without_translucent']:.5f}  components explain "
                  f"{r['components_explain_fraction'] * 100:5.1f} %  -> {r['classification']}")
        payload['poses'][pose] = rows

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'pass-attribution.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
