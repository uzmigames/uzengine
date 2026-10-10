"""The valid gather control followup-12 left undone: hold the gather's output, keep the path.

followup-12 attributed the shadowed walls to diffuse indirect (97.5 % of `shadow_wall`) but could
not test it: skipping `UpdateProbes` disabled the GI path outright (mean x1770, and the recorded
path bits showed neither the gather nor the reflections running), and freezing the sampling seed
reuses one noise pattern instead of averaging it (H-19, rejected). Its notes end with "a valid
gather freeze has to hold the gather's output, and that control does not exist yet".

`r.GI.FinalGather.FreezeAfterFrame` is that control. The gather already writes a texture the
deferred lighting pass reads, so skipping its dispatch leaves the texture at its last converged
contents while the lighting path stays fully active.

Measured on the two regions followup-12 separated, plus the door and the right shadow it found to be
translucent-dominated:

  W `shadow_wall`   97.5 % diffuse indirect — if the gather's variation drives the residual there,
                    holding its output should remove most of it
  A `rear_door`, A `right_shadow`   dominated by the forward translucent pass (H-06b), so the
                    gather freeze should do comparatively little, which is the discriminating
                    prediction rather than a null result

Controls, checked before anything is interpreted:

  path unchanged    the recorded active-path bitmask must be identical between variants. This is
                    exactly what the earlier attempt failed: it removed the gather from the path
                    instead of holding its output;
  step at the right frame   a third capture freezes in the MIDDLE of the measured window, and the
                    output's frame-to-frame change must drop at exactly that frame. This replaces
                    an earlier control that watched the recorded diffuse-indirect component: that
                    accumulator is only written by the SDF-gather branch, and on these ROIs the
                    recorded path bitmask is 0, so the branch never runs and the channel is
                    identically zero in both variants. A control that reads an empty channel
                    reports "not held" no matter what the intervention did;
  exposure equal    the recorded exposure must match.

**Preregistered decision rule.** The gather's per-frame variation is the dominant cause of a
region's residual only if holding its output removes more than half of that region's oscillation,
beyond the followup-09 threshold for that ROI. A fall smaller than that is reported as a
contribution, not a cause. The prediction being tested is *differential*: a large fall on
`shadow_wall` together with a small one on the translucent-dominated regions.

Usage: python scripts/run_gather_freeze_study.py [--frames 128] [--no-keep-raw]
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

#: followup-09 per-ROI decision thresholds, linear radiance.
THRESHOLDS = {'shadow_wall': 4.7e-7, 'rear_door': 8.9e-6, 'right_shadow': 2.1e-5}
POSES = {
    'W': ('sponza-flicker-w.uzproject', [('shadow_wall', 150, 260, 192, 384)]),
    'A': ('sponza-flicker-a.uzproject', [('rear_door', 960, 592, 224, 224),
                                         ('right_shadow', 1472, 464, 256, 256)]),
}


def luma(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--no-keep-raw', action='store_true')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '16-gather-freeze'))
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_gather_freeze')
    freeze_at = FIRST - 8
    payload = {'first_frame': FIRST, 'frames': args.frames, 'freeze_after': freeze_at,
               'thresholds': THRESHOLDS, 'poses': {}}

    for pose_key, (project, rois) in POSES.items():
        data = {}
        mid = FIRST + args.frames // 2
        for vname, cvars in (('baseline', {}),
                             ('gather_frozen', {'r.GI.FinalGather.FreezeAfterFrame': freeze_at}),
                             ('frozen_midway', {'r.GI.FinalGather.FreezeAfterFrame': mid})):
            print(f'  ... {pose_key} {vname}', flush=True)
            res = runner.run_capture(os.path.join(work, f'{pose_key}_{vname}'),
                                     os.path.join(runner.REPO, 'scripts', project),
                                     last - SCENE_START + 8, regions=rois, region_fields='full',
                                     region_first=FIRST, region_last=last,
                                     cvars={**BASE, **cvars}, timing_first=FIRST)
            if res['exit_code'] != 0:
                raise RuntimeError(f'{pose_key} {vname}: engine exited {res["exit_code"]}')
            seq = regiondump.load_sequence(res['region_dir'], FIRST, last)
            data[vname] = {name: {
                'output': np.stack([f.rect(name).luminance('taa_output') for f in seq]).astype(np.float64),
                'diffuse': np.stack([f.rect(name).channel('diffuse_indirect')[..., :3]
                                     for f in seq]).astype(np.float64),
                'paths': sorted(int(v) for v in np.unique(np.stack(
                    [f.rect(name).channel('emissive_and_mis')[..., 3].view(np.uint32) for f in seq]))),
                'exposure': np.stack([f.rect(name).channel('taa_output')[..., 3]
                                      for f in seq]).astype(np.float64),
            } for name, *_ in rois}
            shutil.copy(res['manifest_path'],
                        os.path.join(args.out, 'manifests', f'{pose_key}-{vname}.json'))
            if not args.no_keep_raw:
                # Kept so the temporal evolution can be re-examined without recapturing — but
                # subsampled: the `full` preset is 15 records per pixel, and six runs of these
                # rectangles came to 17 GB. Every 4th frame, plus every frame within 8 of the
                # midway freeze, is what a re-examination of the step actually needs.
                dst = os.path.join(args.out, 'raw', f'{pose_key}-{vname}')
                if os.path.isdir(dst):
                    shutil.rmtree(dst)
                os.makedirs(dst, exist_ok=True)
                for n in sorted(os.listdir(res['region_dir'])):
                    if not n.startswith('region_'):
                        continue
                    fr = int(n.split('_')[1].split('.')[0])
                    if (fr - FIRST) % 4 == 0 or abs(fr - mid) <= 8:
                        shutil.copy(os.path.join(res['region_dir'], n), os.path.join(dst, n))
            shutil.rmtree(res['region_dir'], ignore_errors=True)

        entry = {'rois': {}}
        for name, *_ in rois:
            b, f = data['baseline'][name], data['gather_frozen'][name]
            m = data['frozen_midway'][name]
            # Frame-to-frame change either side of the midway freeze: it must collapse at that
            # frame and stay collapsed, which no channel-reading control can fake.
            step = np.abs(np.diff(m['output'], axis=0)).mean(axis=(1, 2))
            half = args.frames // 2
            before = float(step[:half - 2].mean())
            after = float(step[half + 2:].mean())
            gather_std_b = before
            gather_std_f = after
            osc_b = float(b['output'].std(axis=0).mean())
            osc_f = float(f['output'].std(axis=0).mean())
            controls = {
                'path_bits_baseline': b['paths'],
                'path_bits_frozen': f['paths'],
                'path_identical': b['paths'] == f['paths'],
                'step_before_midway_freeze': gather_std_b,
                'step_after_midway_freeze': gather_std_f,
                'gather_held': bool(gather_std_b > 0 and gather_std_f < 0.5 * gather_std_b),
                'exposure_max_delta': float(np.abs(b['exposure'] - f['exposure']).max()),
            }
            controls['exposure_identical'] = bool(controls['exposure_max_delta'] < 1e-9)
            valid = (controls['path_identical'] and controls['gather_held']
                     and controls['exposure_identical'])
            drop = (osc_b - osc_f)
            entry['rois'][name] = {
                'controls': controls, 'valid': bool(valid),
                'oscillation_baseline': osc_b, 'oscillation_frozen': osc_f,
                'oscillation_change_pct': (osc_f / osc_b - 1) * 100 if osc_b else None,
                'mean_baseline': float(b['output'].mean()),
                'mean_frozen': float(f['output'].mean()),
                'energy_change_pct': (float(f['output'].mean()) / float(b['output'].mean()) - 1) * 100,
                'verdict': ('invalid — a control failed' if not valid else
                            'dominant cause' if (drop > 0.5 * osc_b
                                                 and drop > THRESHOLDS.get(name, 0)) else
                            'contributes, not dominant'),
            }
            e = entry['rois'][name]
            print(f"      {name:13s} step before/after the midway freeze {gather_std_b:.3e} -> "
                  f"{gather_std_f:.3e} "
                  f"({'held' if controls['gather_held'] else 'NOT HELD'}), path "
                  f"{'identical' if controls['path_identical'] else 'CHANGED ' + str(b['paths']) + '->' + str(f['paths'])}")
            print(f"      {'':13s} oscillation {osc_b:.4e} -> {osc_f:.4e} "
                  f"({e['oscillation_change_pct']:+.1f} %), energy {e['energy_change_pct']:+.2f} % "
                  f"-> {e['verdict']}", flush=True)
        payload['poses'][pose_key] = entry

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'gather-freeze.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(payload, fh, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
