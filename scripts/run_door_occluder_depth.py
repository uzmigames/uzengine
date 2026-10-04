"""followup-12: which opaque surface rejects the glass, and how its depth moves against the fragment's
along the jitter.

The review asked for this before any intervention is chosen.

**A first attempt failed its own control, and the failure is the reason this version exists.** It read
the glass fragment's clip-space depth and the opaque surface's RADIAL DISTANCE in centimetres
(`lighting_normal.w`, which really is `length(worldPos - cameraPos)`), and converted the first into
the second by unprojecting with the frame's view-projection matrix. V1 — at pixel-frames where the
depth test PASSED, the glass must be nearer — failed on 1564 of 1564 pixel-frames, with the glass
apparently up to 2871 cm BEHIND the surface it was drawn in front of. A matrix convention, a near
plane and a depth encoding all sit between those two quantities, and the control could not say which
was wrong. Nothing was reported from it.

**A second attempt failed the same control, and it is why the engine now records one more number.**
That version compared the fragment depth with the TAA's `centre depth` (`taa_clip_min.w`), which is in
the right space but is NOT the pixel's own depth: the TAA dilates it to the nearest of a five-sample
cross for silhouette-correct reprojection, so at a glass pixel it can belong to a neighbour. V1 failed
on all 1564 pixel-frames by a deficit of only 1.32e-05, which is what a neighbouring surface at nearly
the same depth looks like. Nothing was reported from it either.

The deferred pass owns the depth texture and already clears the translucent record every frame, so it
now writes the pixel's OWN opaque depth into that record's free z component. The comparison is then
between two numbers written for the same pixel in the same reversed-Z clip space: no matrix, no near
plane, no unprojection, no dilation. Nearer is LARGER, and the translucent pipeline tests `Greater`.

Pre-registered before the numbers were read (followup-12 notes, 2026-09-16):

  captures  pose A, still camera, frames 237..268 (four jitter cycles), `--region-fields components`,
            one process per condition, dumps retained:
              depth_on   the shipping behaviour
              depth_off  r.Translucency.DepthTest=0, which records every rasterised glass fragment,
                         including the ones the depth test rejects. Its IMAGE is never read.
  target    the pixels drawn in SOME frames only under `depth_on` (the 125 of H-45 and H-47).

  V1  the depths are comparable only if they reproduce the depth test: at every pixel-frame where the
      glass passed under `depth_on`, the glass depth must be GREATER than the opaque depth at the SAME
      pixel (reversed-Z nearer), within 1e-7. This is the depth test itself, so any mismatch of space,
      pixel or sign breaks it — and twice already has. If it fails, nothing below is reported.
  V2  the opaque depth is identical in both conditions at the target pixels. The translucent pass
      writes no depth, so turning its depth test off cannot move the depth buffer. Tolerance 1e-7.

  R1  per target pixel, over the frames where the glass is REJECTED versus those where it PASSES: the
      mean opaque depth in each and the difference, and the same for the glass depth. This is the
      question — whether the opaque surface moves, the glass moves, or both.
  R2  the opaque surface's identity as far as the dump carries it: the normal and the radial distance
      in centimetres (`lighting_normal`), in the rejecting frames and in the passing frames. A pixel
      whose normal changes is looking at two different surfaces; one whose normal holds is looking at
      one surface whose depth moved.
  R3  the margin between the two depths in each set of frames — how close the surfaces are where the
      outcome flips.

What this can and cannot say, fixed in advance: it describes which surface is in front, by how much,
per pixel and per frame. It says nothing about what any intervention would do, and a surface being
nearer is the mechanism by which coverage changes, not a cause of the flicker.

Usage: python scripts/run_door_occluder_depth.py [--frames 32] [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
FIRST = 237
NAME = 'rear_door'
ROI = (NAME, 960, 592, 224, 224)
PROJECT = 'sponza-flicker-a.uzproject'
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
CONDITIONS = {'depth_on': {}, 'depth_off': {'r.Translucency.DepthTest': 0}}
DEPTH_EPS = 1e-7


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def load(region_dir, last):
    seq = regiondump.load_sequence(region_dir, FIRST, last)
    first = seq[0].rect(NAME)
    h, w, T = first.h, first.w, len(seq)
    cap = {'T': T, 'h': h, 'w': w, 'frames': [int(f.frame) for f in seq],
           'jitter': np.array([int(f.jitter_index) for f in seq], dtype=np.int64),
           'cov': np.zeros((T, h, w), dtype=bool), 'layers': np.zeros((T, h, w)),
           'glass_z': np.zeros((T, h, w)), 'opaque_z': np.zeros((T, h, w)),
           'opaque_n': np.zeros((T, h, w, 3)), 'opaque_cm': np.zeros((T, h, w))}
    for t, f in enumerate(seq):
        r = f.rect(NAME)
        src = r.channel('translucent_source').astype(np.float64)
        lay = r.channel('translucent_layers').astype(np.float64)
        ln = r.channel('lighting_normal').astype(np.float64)
        for name, arr in (('translucent_source', src), ('translucent_layers', lay),
                          ('lighting_normal', ln)):
            if not np.all(np.isfinite(arr)):
                raise Stop('finite', f'{name} holds non-finite values at frame {f.frame}')
        cap['cov'][t] = src[..., 3] >= 0.0
        cap['layers'][t] = lay[..., 0]
        cap['glass_z'][t] = lay[..., 1]
        cap['opaque_z'][t] = lay[..., 2]
        cap['opaque_n'][t] = ln[..., :3]
        cap['opaque_cm'][t] = ln[..., 3]
    return cap


def run(args, report):
    last = FIRST + args.frames - 1
    works = {}
    for cond, cvars in CONDITIONS.items():
        work = os.path.join(args.out, cond)
        works[cond] = work
        if args.reuse:
            continue
        print(f'  ... {cond}', flush=True)
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', PROJECT),
                                 last - SCENE_START + 8, regions=[ROI], region_fields='components',
                                 region_first=FIRST, region_last=last, cvars={**BASE, **cvars},
                                 timing_first=FIRST, clean=True)
        if res['exit_code'] != 0:
            raise Stop('capture', f'{cond}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = {c: {'dir': w, 'identity': runner.capture_identity(w)} for c, w in works.items()}

    on = load(runner.require_region_dir(os.path.join(works['depth_on'], 'region'), 'depth_on'), last)
    off = load(runner.require_region_dir(os.path.join(works['depth_off'], 'region'), 'depth_off'), last)
    if on['frames'] != off['frames'] or not np.array_equal(on['jitter'], off['jitter']):
        raise Stop('alignment', 'the two conditions differ in frames or jitter sequence')
    report['frames'] = [on['frames'][0], on['frames'][-1]]

    target = on['cov'].any(axis=0) & ~on['cov'].all(axis=0)
    report['target_pixels'] = int(target.sum())
    print(f'  target pixels (drawn in some frames only): {int(target.sum())}')
    if not target.any():
        raise Stop('target', 'no pixel is drawn in some frames only in this window')

    # ---- V0: the opaque depth record agrees with the geometry record ---------------------------
    # Reversed-Z far is 0, so a stale binary that never writes this record leaves zeros and V1 would
    # pass on them. The test is not "mostly non-zero": at these pixels the glass often has NO opaque
    # geometry behind it (the pixel sees sky), and zero is then the correct reading. The record must
    # agree with the independent geometry record: non-zero exactly where `lighting_normal.w` >= 0.
    geo = target[None] & (on['opaque_cm'] >= 0.0)
    sky = target[None] & (on['opaque_cm'] < 0.0)
    with_geo = float(np.mean(off['opaque_z'][geo] > 0.0)) if geo.any() else 0.0
    with_sky = float(np.mean(off['opaque_z'][sky] == 0.0)) if sky.any() else 1.0
    report['V0'] = {'non_zero_where_geometry': with_geo, 'zero_where_sky': with_sky,
                    'pixel_frames_with_geometry': int(geo.sum()), 'pixel_frames_sky': int(sky.sum()),
                    'pass': bool(with_geo > 0.999 and with_sky > 0.999)}
    print(f'  V0 opaque depth vs the geometry record: non-zero where geometry {with_geo:.2%}, '
          f'zero where sky {with_sky:.2%}')
    if not report['V0']['pass']:
        raise Stop('V0', 'the opaque depth record disagrees with the geometry record, so it is not the '
                         'depth of this pixel and nothing below is reported')

    # ---- V1: the two depths reproduce the depth test --------------------------------------------
    passed = on['cov'] & target[None] & (off['layers'] >= 1.0)
    nearer = off['glass_z'] > (off['opaque_z'] - DEPTH_EPS)
    ok, total = int(np.count_nonzero(passed & nearer)), int(np.count_nonzero(passed))
    report['V1'] = {'pixel_frames': total, 'glass_nearer': ok, 'epsilon': DEPTH_EPS,
                    'pass': bool(total > 0 and ok == total)}
    print(f'  V1 where the glass passed, its depth is nearer in reversed-Z: {ok}/{total}')
    if total == 0:
        raise Stop('V1', 'no pixel-frame has a passing glass fragment with a recorded depth')
    if ok != total:
        worst = float(np.max((off['opaque_z'] - off['glass_z'])[passed & ~nearer]))
        report['V1']['worst_deficit'] = worst
        raise Stop('V1', f'the depths do not reproduce the depth test ({total - ok} pixel-frames, worst '
                         f'deficit {worst:.3e}); they are not comparable and nothing below is reported')

    # ---- V1b: the converse — where the glass was rejected it must NOT be nearer -----------------
    rejected_pf = (~on['cov']) & target[None] & (off['layers'] >= 1.0)
    not_nearer = off['glass_z'] <= (off['opaque_z'] + DEPTH_EPS)
    okb, totb = int(np.count_nonzero(rejected_pf & not_nearer)), int(np.count_nonzero(rejected_pf))
    report['V1b'] = {'pixel_frames': totb, 'glass_not_nearer': okb, 'pass': bool(totb > 0 and okb == totb)}
    print(f'  V1b where the glass was rejected, it is not nearer: {okb}/{totb}')
    if not report['V1b']['pass']:
        raise Stop('V1b', 'a rejected fragment reads as nearer than the surface that rejected it; the '
                          'two depths are not the ones the test compared')

    # ---- V2: the depth buffer is untouched by the translucent depth test -------------------------
    delta = np.abs(on['opaque_z'] - off['opaque_z'])[target[None] & np.ones_like(on['cov'])]
    report['V2'] = {'worst': float(delta.max()), 'pass': bool(delta.max() <= DEPTH_EPS)}
    print(f'  V2 opaque depth identical across conditions: worst {report["V2"]["worst"]:.3e}')
    if not report['V2']['pass']:
        raise Stop('V2', 'the depth buffer differs between conditions, so the comparison is not like '
                         'for like')

    # ---- R1 / R2 / R3 ----------------------------------------------------------------------------
    rows = []
    for p in np.flatnonzero(target):
        y, x = divmod(int(p), on['w'])
        drew = on['cov'][:, y, x]
        rej = ~drew & (off['layers'][:, y, x] >= 1.0)
        if not drew.any() or not rej.any():
            continue
        row = {'x': int(ROI[1] + x), 'y': int(ROI[2] + y),
               'frames_passing': int(drew.sum()), 'frames_rejected': int(rej.sum()),
               'opaque_z_passing': float(on['opaque_z'][drew, y, x].mean()),
               'opaque_z_rejected': float(on['opaque_z'][rej, y, x].mean()),
               'glass_z_passing': float(off['glass_z'][drew, y, x].mean()),
               'glass_z_rejected': float(off['glass_z'][rej, y, x].mean()),
               'opaque_cm_passing': float(on['opaque_cm'][drew, y, x].mean()),
               'opaque_cm_rejected': float(on['opaque_cm'][rej, y, x].mean()),
               'normal_passing': [round(float(v), 3) for v in on['opaque_n'][drew, y, x].mean(axis=0)],
               'normal_rejected': [round(float(v), 3) for v in on['opaque_n'][rej, y, x].mean(axis=0)]}
        row['opaque_z_shift'] = row['opaque_z_rejected'] - row['opaque_z_passing']
        row['glass_z_shift'] = row['glass_z_rejected'] - row['glass_z_passing']
        row['opaque_cm_shift'] = row['opaque_cm_rejected'] - row['opaque_cm_passing']
        row['margin_passing'] = row['glass_z_passing'] - row['opaque_z_passing']
        row['margin_rejected'] = row['glass_z_rejected'] - row['opaque_z_rejected']
        a, b = np.asarray(row['normal_passing']), np.asarray(row['normal_rejected'])
        na, nb = np.linalg.norm(a), np.linalg.norm(b)
        # Undefined when one state has no surface at all: comparing a normal with nothing is not an
        # angle of zero, and must not be averaged as if it were.
        row['normal_angle_deg'] = float(np.degrees(np.arccos(np.clip(a @ b / (na * nb), -1, 1)))) \
            if na > 1e-6 and nb > 1e-6 else float('nan')
        rows.append(row)
    report['pixels'] = rows
    if not rows:
        raise Stop('R1', 'no target pixel has both passing and rejected frames')

    cz = np.array([r['opaque_z_shift'] for r in rows])
    gz = np.array([r['glass_z_shift'] for r in rows])
    cm = np.array([r['opaque_cm_shift'] for r in rows])
    ang = np.array([r['normal_angle_deg'] for r in rows])
    mp = np.array([r['margin_passing'] for r in rows])
    mr = np.array([r['margin_rejected'] for r in rows])
    report['summary'] = {
        'pixels_compared': len(rows),
        'opaque_depth_shift': {'median': float(np.median(cz)), 'max_abs': float(np.abs(cz).max())},
        'glass_depth_shift': {'median': float(np.median(gz)), 'max_abs': float(np.abs(gz).max())},
        'opaque_distance_shift_cm': {'median': float(np.median(cm)), 'max_abs': float(np.abs(cm).max())},
        'normal_angle_deg': {'median': float(np.nanmedian(ang)), 'max': float(np.nanmax(ang)),
                             'pixels_over_5deg': int(np.count_nonzero(ang > 5.0))},
        'margin': {'median_passing': float(np.median(mp)), 'median_rejected': float(np.median(mr))},
        'which_moves_more': ('the opaque surface' if np.median(np.abs(cz)) > np.median(np.abs(gz))
                             else 'the glass fragment'),
    }
    s = report['summary']
    print(f'\n  R1 rejecting frames minus passing frames, per pixel (reversed-Z depth):')
    print(f'     opaque depth:          median {s["opaque_depth_shift"]["median"]:+.3e}, '
          f'max |shift| {s["opaque_depth_shift"]["max_abs"]:.3e}')
    print(f'     glass fragment depth:  median {s["glass_depth_shift"]["median"]:+.3e}, '
          f'max |shift| {s["glass_depth_shift"]["max_abs"]:.3e}')
    print(f'     opaque radial distance: median {s["opaque_distance_shift_cm"]["median"]:+.3f} cm, '
          f'max {s["opaque_distance_shift_cm"]["max_abs"]:.3f} cm')
    print(f'     larger median movement: {s["which_moves_more"]}')
    undefined = int(np.count_nonzero(np.isnan(ang)))
    report['summary']['normal_angle_deg']['undefined_pixels'] = undefined
    if undefined == len(rows):
        print(f'  R2 opaque normal: NOT APPLICABLE at all {len(rows)} pixels — one of the two states has '
              f'no surface, so there is no normal to compare')
    else:
        print(f'  R2 opaque normal between the two sets: median {s["normal_angle_deg"]["median"]:.2f} deg, '
              f'max {s["normal_angle_deg"]["max"]:.2f} deg, {s["normal_angle_deg"]["pixels_over_5deg"]} of '
              f'{len(rows)} pixels over 5 deg, {undefined} undefined')
    print(f'  R3 margin (glass minus opaque, positive = glass nearer): median '
          f'{s["margin"]["median_passing"]:+.3e} passing, {s["margin"]["median_rejected"]:+.3e} rejected')

    def cluster(which):
        buckets = {}
        for r in rows:
            k = (tuple(np.round(r['normal_' + which], 1)), round(r['opaque_cm_' + which] / 10.0) * 10)
            buckets[k] = buckets.get(k, 0) + 1
        return sorted(({'normal': list(k[0]), 'distance_cm_bucket': k[1], 'pixels': v}
                       for k, v in buckets.items()), key=lambda d: -d['pixels'])[:6]

    report['surfaces'] = {'when_passing': cluster('passing'), 'when_rejected': cluster('rejected')}
    print('\n  the opaque surface, by normal and radial distance bucket:')
    for which in ('when_passing', 'when_rejected'):
        for surf in report['surfaces'][which]:
            print(f'    {which:<14} normal {surf["normal"]}  ~{surf["distance_cm_bucket"]} cm  '
                  f'{surf["pixels"]} px')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=32)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '41-door-occluder-depth'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'rear_door: the opaque surface that rejects the glass — frames {FIRST}..'
          f'{FIRST + args.frames - 1}, depths compared in reversed-Z clip space')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'roi': ROI,
              'conditions': CONDITIONS, 'harness': runner.harness_stamp(),
              'criteria': {'depth_epsilon': DEPTH_EPS}}
    code = 1
    try:
        run(args, report)
        report['status'] = 'complete'
        code = 0
    except Stop as s:
        report['stopped_at'] = s.gate
        report['reasons'].append(s.reason)
        print(f'\n  STOPPED at {s.gate}: {s.reason}')
        code = 2
    except Exception:  # noqa: BLE001 - every exit writes the report
        report['stopped_at'] = 'error'
        report['reasons'].append(traceback.format_exc())
        traceback.print_exc()
    finally:
        os.makedirs(args.out, exist_ok=True)
        path = os.path.join(args.out, 'occluder_depth.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
