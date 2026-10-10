"""H-28 re-run: does holding the gather's output dominate the static wall's residual? (followup-12)

The first run of this test reported -81.9 % but its controls were read through a broken record
layout, its freeze suspends a pass that serves two modes, and its "drop at exactly that frame" claim
compared two intervals with the transition excluded. The layout is fixed and validated against a
fixture (`scripts/test_region_format.py`); this re-runs the experiment against criteria fixed before
capturing:

  independent control  the consumed texture must *vary* before the freeze and be *stable* after,
                       with the SDF mode active throughout. Whether the flicker falls is the
                       result; it is never evidence that the control worked.
  isolation            direct light, reflections, AO, shadow visibility, exposure and geometry must
                       stay within the repeatability floor measured from the baseline repeats.
                       Diffuse indirect and the TAA output are expected to move — they are what the
                       intervention acts through.
  time                 consecutive frames, real frame indices. The last dispatch, the first frame
                       that reuses the texture and the TAA's later response are reported separately.
                       Only the *interruption of texture updates* has to land on the predicted
                       frame: the TAA history makes the flicker response gradual by construction.
  repeats              three runs per condition — baseline, frozen after stabilisation, frozen
                       midway — with the spread, the mean energy and the oscillation reported.

**Preregistered decision rule.** H-28 is supported only if (a) every control passes, (b) the
oscillation falls by more than half and beyond this ROI's threshold (4.7e-7 linear radiance) in all
three repeats, and (c) the interruption of texture updates coincides with the predicted frame in the
midway condition. A fall without the controls is not evidence, and a control failure voids the run
rather than weakening it.

Even if all three hold, this shows the *gather output's variation* dominates the residual. Which
part of the gather — its sampling, its reconstruction or its own history — is a separate question.

Usage: python scripts/run_h28_revalidation.py [--repeats 3] [--frames 128]
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
FIRST = SCENE_START + 200
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = ('shadow_wall', 150, 260, 192, 384)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
THRESHOLD = 4.7e-7          # followup-09 decision threshold for this ROI
#: lighting|taa|lighting_extra — everything the controls need, without the TAA_EXTRA bulk.
FIELDS = 'components'


def luma(a):
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def read(region_dir, first, last):
    seq = regiondump.load_sequence(region_dir, first, last)
    r = [f.rect(ROI[0]) for f in seq]
    return {
        'frames': np.array([f.frame for f in seq]),
        'output': np.stack([x.luminance('taa_output') for x in r]).astype(np.float64),
        'consumed': np.stack([x.channel('ssgi_consumed')[..., :3] for x in r]).astype(np.float64),
        'mode': np.stack([x.channel('ssgi_consumed')[..., 3].view(np.uint32) for x in r]),
        'direct': np.stack([x.channel('direct_light')[..., :3] for x in r]).astype(np.float64),
        'shadow': np.stack([x.channel('direct_light')[..., 3] for x in r]).astype(np.float64),
        'diffuse': np.stack([x.channel('diffuse_indirect')[..., :3] for x in r]).astype(np.float64),
        'ao': np.stack([x.channel('diffuse_indirect')[..., 3] for x in r]).astype(np.float64),
        'specular': np.stack([x.channel('specular_indirect')[..., :3] for x in r]).astype(np.float64),
        'roughness': np.stack([x.channel('lighting_reflection')[..., 3] for x in r]).astype(np.float64),
        'dist': np.stack([x.channel('lighting_normal')[..., 3] for x in r]).astype(np.float64),
        'exposure': np.stack([x.channel('taa_output')[..., 3] for x in r]).astype(np.float64),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '16-gather-freeze'))
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    mid = FIRST + args.frames // 2
    early = FIRST - 8
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_h28')

    conditions = (('baseline', {}),
                  ('frozen_early', {'r.GI.FinalGather.FreezeAfterFrame': early}),
                  ('frozen_midway', {'r.GI.FinalGather.FreezeAfterFrame': mid}))
    payload = {'roi': ROI, 'first_frame': FIRST, 'frames': args.frames, 'freeze_early': early,
               'freeze_midway': mid, 'repeats': args.repeats, 'threshold': THRESHOLD,
               'conditions': {}}
    runs = {}

    for cname, cvars in conditions:
        runs[cname] = []
        for rep in range(args.repeats):
            print(f'  ... {cname} repeat {rep + 1}/{args.repeats}', flush=True)
            res = runner.run_capture(os.path.join(work, f'{cname}_{rep}'), PROJECT,
                                     last - SCENE_START + 8, regions=[ROI], region_fields=FIELDS,
                                     region_first=FIRST, region_last=last,
                                     cvars={**BASE, **cvars}, timing_first=FIRST)
            if res['exit_code'] != 0:
                raise RuntimeError(f'{cname}/{rep}: engine exited {res["exit_code"]}')
            runs[cname].append(read(res['region_dir'], FIRST, last))
            if rep == 0:
                shutil.copy(res['manifest_path'],
                            os.path.join(args.out, 'manifests', f'h28-{cname}.json'))
                dst = os.path.join(args.out, 'raw', f'h28-{cname}')
                if os.path.isdir(dst):
                    shutil.rmtree(dst)
                shutil.copytree(res['region_dir'], dst)   # continuous, real frame indices
            shutil.rmtree(res['region_dir'], ignore_errors=True)

    # ---- repeatability floor, from the baseline repeats -----------------------
    def floor(key):
        b = runs['baseline']
        return max(float(np.abs(b[0][key] - b[i][key]).mean()) for i in range(1, len(b)))

    keys = ('direct', 'shadow', 'specular', 'ao', 'roughness', 'dist', 'exposure')
    floors = {k: floor(k) for k in keys}
    payload['repeatability_floor'] = floors

    # ---- controls -------------------------------------------------------------
    def per_frame_change(a):
        return np.abs(np.diff(a, axis=0)).mean(axis=tuple(range(1, a.ndim)))

    controls = {}
    for cname in ('frozen_early', 'frozen_midway'):
        c = runs[cname][0]
        b = runs['baseline'][0]
        iso = {k: float(np.abs(c[k] - b[k]).mean()) for k in keys}
        iso_ok = all(iso[k] <= max(2.0 * floors[k], 1e-12) for k in keys)
        mode_ok = bool(np.all(c['mode'] == 1))
        ch = per_frame_change(c['consumed'])
        frames = c['frames'][1:]
        if cname == 'frozen_early':
            stable = float(ch.mean())
            varying = float(per_frame_change(b['consumed']).mean())
            frozen_from = int(frames[0]) if stable < 0.01 * varying else None
        else:
            pre = ch[frames <= mid]
            post = ch[frames > mid]
            stable, varying = float(post.mean()), float(pre.mean())
            nz = np.flatnonzero(ch > 0.01 * max(varying, 1e-30))
            frozen_from = int(frames[nz[-1]] + 1) if len(nz) else int(frames[0])
        controls[cname] = {
            'mode_is_sdf_gather': mode_ok,
            'consumed_change_before': varying,
            'consumed_change_after': stable,
            'texture_updates_stop_at_frame': frozen_from,
            'predicted_frame': (early + 1) if cname == 'frozen_early' else (mid + 1),
            'consumed_control_ok': bool(varying > 0 and stable < 0.01 * varying),
            'isolation': iso, 'isolation_ok': bool(iso_ok),
        }
        k = controls[cname]
        print(f"      {cname}: mode SDF {mode_ok}; consumed change {varying:.3e} -> {stable:.3e} "
              f"({'stable' if k['consumed_control_ok'] else 'NOT STABLE'}); updates stop at frame "
              f"{frozen_from} (predicted {k['predicted_frame']}); isolation "
              f"{'within floor' if iso_ok else 'BREACHED'}")
        if not iso_ok:
            for kk in keys:
                if iso[kk] > max(2.0 * floors[kk], 1e-12):
                    print(f"          {kk}: {iso[kk]:.3e} against a floor of {floors[kk]:.3e}")
    payload['controls'] = controls

    # ---- result ---------------------------------------------------------------
    for cname, _ in conditions:
        osc = [float(r['output'].std(axis=0).mean()) for r in runs[cname]]
        eng = [float(r['output'].mean()) for r in runs[cname]]
        payload['conditions'][cname] = {
            'oscillation': osc, 'oscillation_mean': float(np.mean(osc)),
            'oscillation_spread': float(max(osc) - min(osc)),
            'energy_mean': float(np.mean(eng)), 'energy_spread': float(max(eng) - min(eng)),
        }
        e = payload['conditions'][cname]
        print(f"      {cname:14s} oscillation {e['oscillation_mean']:.4e} "
              f"(spread {e['oscillation_spread']:.1e}), energy {e['energy_mean']:.6f}")

    b = payload['conditions']['baseline']
    f = payload['conditions']['frozen_early']
    drops = [b['oscillation'][i] - f['oscillation'][i] for i in range(args.repeats)]
    all_drop = all(d > 0.5 * b['oscillation'][i] and d > THRESHOLD for i, d in enumerate(drops))
    ctrl_ok = all(c['consumed_control_ok'] and c['isolation_ok'] and c['mode_is_sdf_gather']
                  for c in controls.values())
    timing_ok = (controls['frozen_midway']['texture_updates_stop_at_frame']
                 == controls['frozen_midway']['predicted_frame'])
    payload['controls_pass'] = bool(ctrl_ok)
    payload['drop_in_every_repeat'] = bool(all_drop)
    payload['timing_matches'] = bool(timing_ok)
    payload['verdict'] = ('supported' if (ctrl_ok and all_drop and timing_ok)
                          else 'invalid — a control failed' if not ctrl_ok
                          else 'rejected')
    print(f"\n  controls {ctrl_ok}, drop in every repeat {all_drop}, updates stop on the predicted "
          f"frame {timing_ok}")
    print(f"  verdict by the preregistered rule: {payload['verdict']}")

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'h28-revalidation.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(payload, fh, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
