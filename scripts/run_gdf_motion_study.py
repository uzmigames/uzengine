"""Are the lighting steps seen while moving caused by the GDF clipmap recentring? (followup-13)

The 2026-09-09 evidence was a coincidence count on fixed-screen regions, which conflates parallax,
content and exposure with lighting. This replaces it with:

* a **world-space comparison**: every pixel's world position is reconstructed from its recorded view
  distance and the recorded unjittered view-projection, projected into the previous frame, and
  compared there. Pixels that leave the rectangle, land on a different surface (depth or normal
  mismatch) or were disoccluded are rejected, and the valid coverage is reported with every number;
* a **trajectory that crosses one axis at a time** (`scripts/sponza-gdf-boundary.lua`), with a stop
  before and after each crossing, so an event has a quiet baseline on both sides;
* two controls that separate the grid from the path: `r.GI.GDF.FreezeCenter` (no recentring at all)
  and `r.GI.GDF.CenterPhaseOffset` (the same camera path crossing the grid at *different* frames).

Causality is only claimed if the discontinuity follows the shifted grid and shrinks when the centres
are frozen. Otherwise the row stays inconclusive with the next control named.

Usage: python scripts/run_gdf_motion_study.py [--out DIR] [--repeats 3]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
WARMUP = 120
SEGMENT = 140          # 40 still + 60 moving + 40 still, per scripts/sponza-gdf-boundary.lua
SEGMENTS = 6
FIRST = SCENE_START + WARMUP
LAST = FIRST + SEGMENT * SEGMENTS - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-gdf-boundary.uzproject')
ROIS = [('shadow_wall', 150, 300, 128, 128), ('lit_wall', 1100, 300, 128, 128)]
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
HALF_VOXEL_CM = 9.765625   # clipmap 0 voxel is 19.53 cm (extent 2500, resolution 256)

VARIANTS = [
    ('baseline', {}),
    ('clipmap centres frozen', {'r.GI.GDF.FreezeCenter': 1}),
    # Sub-voxel phase offsets move where the snapping lattice falls WITHOUT moving any geometry.
    # That is what separates the two halves of a recentre: if the residual varies systematically
    # with phase, it is the field's quantisation to a new lattice; if it is flat across phase but
    # disappears when the centres are frozen, it is the recentre event — the sampling origin moving
    # — and not the lattice. One point could not tell a trend from a coincidence, so there are three.
    ('grid phase shifted a quarter voxel', {'r.GI.GDF.CenterPhaseOffset': 0.5 * HALF_VOXEL_CM}),
    ('grid phase shifted half a voxel', {'r.GI.GDF.CenterPhaseOffset': HALF_VOXEL_CM}),
    ('grid phase shifted three quarter voxel', {'r.GI.GDF.CenterPhaseOffset': 1.5 * HALF_VOXEL_CM}),
]


def capture(work, label, cvars, keep=False):
    d = os.path.join(work, label.replace(' ', '_'))
    res = runner.run_capture(d, PROJECT, LAST - SCENE_START + 8, regions=ROIS,
                             region_fields='motion', region_first=FIRST, region_last=LAST,
                             cvars={**BASE, **cvars}, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    return res


def recentre_events(log_path):
    """Frames at which a GDF clipmap recentred, from the engine log."""
    out = []
    with open(log_path, 'rb') as f:
        text = f.read().decode('utf-8', 'replace')
    for line in text.splitlines():
        if 'GI recentre' in line and 'gdfClipmap=' in line:
            m = re.search(r'frame=(\d+).*gdfClipmap=(\d+)', line)
            if m:
                out.append({'frame': int(m.group(1)), 'clipmap': int(m.group(2)),
                            'axis': axis_of(line)})
    return out


def axis_of(line):
    m = re.search(r'previousCenter=\(([^)]*)\) newCenter=\(([^)]*)\)', line)
    if not m:
        return '?'
    a = [float(v) for v in m.group(1).split(',')]
    b = [float(v) for v in m.group(2).split(',')]
    moved = [i for i in range(3) if abs(a[i] - b[i]) > 1e-4]
    return ''.join('xyz'[i] for i in moved) or 'none'


def sample_bilinear(image, px, py):
    h, w = image.shape
    x0 = np.floor(px).astype(int)
    y0 = np.floor(py).astype(int)
    fx, fy = px - x0, py - y0
    x0c, y0c = np.clip(x0, 0, w - 2), np.clip(y0, 0, h - 2)
    v00 = image[y0c, x0c]
    v10 = image[y0c, x0c + 1]
    v01 = image[y0c + 1, x0c]
    v11 = image[y0c + 1, x0c + 1]
    return (v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy)
            + v01 * (1 - fx) * fy + v11 * fx * fy)


def motion_compensated_series(frames, roi):
    """Per-frame residual against the previous frame, tracking world points.

    Returns the per-frame residual, the fraction of pixels that stayed valid, and the ROI mean.
    """
    name, rx, ry, rw, rh = roi
    out, coverage, means = [], [], []
    prev = None
    for fr in frames:
        rect = fr.rect(name)
        lum = rect.luminance('output')
        world = fr.world_positions(name)
        normal = rect.channel('lighting_normal')[..., :3]
        dist = rect.channel('lighting_normal')[..., 3]
        geo = dist >= 0.0
        means.append(float(lum[geo].mean()) if geo.any() else float('nan'))
        if prev is None:
            out.append(float('nan'))
            coverage.append(0.0)
            prev = (fr, lum, world, normal, dist, geo)
            continue
        pfr, plum, pworld, pnormal, pdist, pgeo = prev
        # Where did this frame's world points sit in the previous frame? The tracking is done as a
        # *displacement* between the two projections of the same world point, so the small constant
        # offset between the recorded camera and the unjittered matrix (and the jitter itself, up to
        # half a pixel) cancels instead of biasing every sample.
        here = fr.project(world)
        there = pfr.project(world)
        grid_x = (np.arange(rw) + rx)[None, :].astype(np.float64)
        grid_y = (np.arange(rh) + ry)[:, None].astype(np.float64)
        sx = grid_x + (there[..., 0] - here[..., 0]) - rx
        sy = grid_y + (there[..., 1] - here[..., 1]) - ry
        inside = (sx >= 0) & (sx <= rw - 1.001) & (sy >= 0) & (sy <= rh - 1.001)
        valid = inside & geo
        if valid.any():
            prev_lum = sample_bilinear(plum, np.clip(sx, 0, rw - 1.001), np.clip(sy, 0, rh - 1.001))
            prev_dist = sample_bilinear(pdist, np.clip(sx, 0, rw - 1.001), np.clip(sy, 0, rh - 1.001))
            prev_n = np.stack([sample_bilinear(pnormal[..., c], np.clip(sx, 0, rw - 1.001),
                                               np.clip(sy, 0, rh - 1.001)) for c in range(3)], axis=-1)
            # Reject disocclusions and surface changes: the tracked point must be at the same
            # distance from the camera and face the same way.
            same_surface = (np.abs(prev_dist - dist) < np.maximum(2.0, 0.01 * dist)) & \
                           ((prev_n * normal).sum(axis=-1) > 0.95)
            valid = valid & same_surface & (prev_dist >= 0)
            resid = np.abs(lum - prev_lum)
            out.append(float(resid[valid].mean()) if valid.any() else float('nan'))
        else:
            out.append(float('nan'))
        coverage.append(float(valid.mean()))
        prev = (fr, lum, world, normal, dist, geo)
    return np.asarray(out), np.asarray(coverage), np.asarray(means)


def event_stats(frames, residual, events, window=2):
    """Residual at event frames (+-window) against the rest of the moving segments."""
    numbers = np.array([f.frame for f in frames])
    moving = np.zeros(len(frames), dtype=bool)
    for seg in range(SEGMENTS):
        lo = FIRST + seg * SEGMENT + 41
        hi = FIRST + seg * SEGMENT + 100
        moving |= (numbers >= lo) & (numbers <= hi)
    ev = np.zeros(len(frames), dtype=bool)
    for e in events:
        ev |= np.abs(numbers - e['frame']) <= window
    near = moving & ev & np.isfinite(residual)
    far = moving & ~ev & np.isfinite(residual)
    return {
        'events_in_window': int(sum(1 for e in events if moving[np.argmin(np.abs(numbers - e['frame']))])),
        'frames_near_event': int(near.sum()),
        'frames_far': int(far.sum()),
        'residual_near_event': float(residual[near].mean()) if near.any() else float('nan'),
        'residual_far': float(residual[far].mean()) if far.any() else float('nan'),
        'ratio': float(residual[near].mean() / residual[far].mean()) if far.any() and near.any() else float('nan'),
        'residual_moving_max': float(np.nanmax(residual[moving])) if moving.any() else float('nan'),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '13-isolate-gdf-motion'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    os.makedirs(os.path.join(args.out, 'csv'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_gdf_motion')
    payload = {'rois': ROIS, 'first_frame': FIRST, 'last_frame': LAST, 'segment_frames': SEGMENT,
               'base_cvars': BASE, 'half_voxel_cm': HALF_VOXEL_CM, 'variants': {}}

    for label, cvars in VARIANTS:
        # Every variant gets the repeats, not just the baseline. A variant measured once and
        # compared against a baseline measured three times is not a comparison of equals.
        repeats = args.repeats
        runs = []
        for rep in range(repeats):
            print(f'  … {label}{"" if repeats == 1 else f" repeat {rep + 1}"}', flush=True)
            res = capture(work, f'{label}_{rep}', cvars)
            frames = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
            events = recentre_events(res['log'])
            entry = {'events': events, 'rois': {}}
            for roi in ROIS:
                resid, cov, means = motion_compensated_series(frames, roi)
                st = event_stats(frames, resid, events)
                st['coverage_mean'] = float(np.nanmean(cov[1:]))
                st['coverage_min'] = float(np.nanmin(cov[1:]))
                st['roi_mean'] = float(np.nanmean(means))
                entry['rois'][roi[0]] = st
                if rep == 0:
                    with open(os.path.join(args.out, 'csv',
                                           f'{label.replace(" ", "_")}-{roi[0]}.csv'), 'w',
                              encoding='utf-8', newline='') as f:
                        f.write('frame,residual,coverage,roi_mean\n')
                        for fr, r, c, m in zip(frames, resid, cov, means):
                            f.write(f'{fr.frame},{r},{c},{m}\n')
            runs.append(entry)
            if rep == 0:
                shutil.copy(res['manifest_path'],
                            os.path.join(args.out, 'manifests', f'{label.replace(" ", "_")}.json'))
            shutil.rmtree(res['region_dir'], ignore_errors=True)
        payload['variants'][label] = {'cvars': cvars, 'runs': runs}
        ev = runs[0]['events']
        print(f"      {len(ev)} recentre events, axes {sorted({e['axis'] for e in ev})}")
        for roi_name, st in runs[0]['rois'].items():
            print(f"      {roi_name}: residual near events {st['residual_near_event']:.3e} vs "
                  f"elsewhere {st['residual_far']:.3e} ({st['ratio']:.2f}x), "
                  f"coverage {st['coverage_mean'] * 100:.1f} %")

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'gdf-motion-study.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
