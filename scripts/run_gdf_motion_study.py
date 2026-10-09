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

Correction (2026-10-08). The moving-frame mask was FIRST + seg * SEGMENT + 41..100 with FIRST = 157,
but `sponza-gdf-boundary.lua` counts its segments from on_start (logged `base_frame=37`, no warm-up),
so the mask ran 120 frames late: 160 of its 360 "moving" frames were still, while every recentre (the
camera only recentres the clipmaps while it moves) fell in the near-event set. The 1.35x / 1.31x
excess (ledger H-07b) and the phase sweep (H-40) were that artefact, and the fixed-centre run was
never evaluated at the event frames. Now the windows come from the trajectory's own log line and the
study stops if a recentre falls outside them; the fixed-centre run is evaluated at every variant's
event frames, with the per-frame ratio against it; `--reanalyse` recomputes a finished package from
its csv/ and JSON without the engine. Task notes, "Correction (2026-10-08)".

Usage: python scripts/run_gdf_motion_study.py [--out DIR] [--repeats 3]
       python scripts/run_gdf_motion_study.py --reanalyse PACKAGE_DIR [--log RUN_LOG]
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37       # the trajectory's on_start frame, logged as base_frame (37 until 2026-10-05, 39 on 2026-10-07)
WARMUP = 120
SEGMENT = 140          # 40 still + 60 moving + 40 still, per scripts/sponza-gdf-boundary.lua
HOLD, MOVE = 40, 60
SEGMENTS = 6
FIRST = SCENE_START + WARMUP   # first DUMPED frame; segment 1 (+X, moving 78..137) runs before it
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


MOVE_WINDOW = re.compile(r'move=(\d+)\.\.(\d+)')
FROZEN = 'clipmap centres frozen'


def trajectory_windows(base_frame=SCENE_START):
    """Moving frames of sponza-gdf-boundary.lua, inclusive: base + seg * SEGMENT + HOLD + 1 .. + HOLD + MOVE.

    The Lua counts its segments from its on_start frame (logged as base_frame), not from the end of
    the dump's warm-up: 78..137, 218..277, ..., 778..837 at base_frame=37, which every retained log of
    2026-09-16..2026-10-05 reads. The block-B sweep of 2026-10-07 logged 39, so the base depends on the
    build: when a run log exists, take the windows from it (logged_windows).
    """
    return [(base_frame + s * SEGMENT + HOLD + 1, base_frame + s * SEGMENT + HOLD + MOVE)
            for s in range(SEGMENTS)]


def logged_windows(log_path):
    """The moving windows as the trajectory logged them (`GdfBoundaryQA base_frame=... move=a..b`)."""
    with open(log_path, 'rb') as f:
        text = f.read().decode('utf-8', 'replace')
    for line in text.splitlines():
        if 'GdfBoundaryQA base_frame=' in line:
            windows = [(int(a), int(b)) for a, b in MOVE_WINDOW.findall(line)]
            if len(windows) != SEGMENTS:
                raise RuntimeError(f'{log_path}: {len(windows)} move windows logged, expected {SEGMENTS}')
            return windows
    raise RuntimeError(f'{log_path}: no "GdfBoundaryQA base_frame=" line, so the moving frames are unknown')


def moving_mask(numbers, windows):
    m = np.zeros(len(numbers), dtype=bool)
    for lo, hi in windows:
        m |= (numbers >= lo) & (numbers <= hi)
    return m


def events_outside(events, windows, first, last):
    """Recentre frames inside [first, last] that fall in no moving window. The camera only recentres the
    clipmaps while it moves, so any such frame means the windows do not describe this run."""
    return sorted({e['frame'] for e in events if first <= e['frame'] <= last
                   and not any(lo <= e['frame'] <= hi for lo, hi in windows)})


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


def event_stats(frames, residual, events, window=2, moving_windows=None):
    """Residual at event frames (+-window) against the rest of the moving frames.

    `frames` are region frames or plain frame numbers. `moving_windows` defaults to the trajectory's
    windows at base_frame=37; pass logged_windows(run_log) when the run's log is at hand, since the
    base moves with the build. Until 2026-10-08 this used FIRST + seg * SEGMENT + 41..100, 120 frames
    late.
    """
    numbers = np.array([getattr(f, 'frame', f) for f in frames])
    moving = moving_mask(numbers, moving_windows or trajectory_windows())
    ev = np.zeros(len(numbers), dtype=bool)
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


def frozen_control(series, events_by_label, windows, same_phase):
    """The fixed-centre run, which has no recentre at all, at each variant's event frames under the same
    mask, and, for the variants on its lattice phase (`same_phase`), the per-frame residual ratio
    variant / frozen at those frames; before ce2caa5e a phase offset changed the field at every frame,
    so that ratio means nothing for the phase-shifted variants. If the near-event excess were made by
    the recentres, the frozen run would not show it at the same frames. (Item 1.5 claimed this
    control; it was first computed on 2026-10-08.)"""
    out = {}
    if FROZEN not in series:
        return out
    for label, events in events_by_label.items():
        if label == FROZEN or not events:
            continue
        out[label] = {}
        for roi_name, (numbers, resid) in series[label].items():
            fnum, fres = series[FROZEN][roi_name]
            if not np.array_equal(numbers, fnum):
                raise RuntimeError(f'{label} / {roi_name}: frame numbers differ from the frozen run')
            st = event_stats(fnum, fres, events, moving_windows=windows)
            if label in same_phase:
                index = {int(n): i for i, n in enumerate(numbers)}
                per_frame = [float(resid[index[f]] / fres[index[f]])
                             for f in sorted({e['frame'] for e in events})
                             if f in index and np.isfinite(resid[index[f]]) and np.isfinite(fres[index[f]])
                             and fres[index[f]] > 0]
                st['event_frames_compared'] = len(per_frame)
                st['per_frame_ratio_min'] = min(per_frame) if per_frame else float('nan')
                st['per_frame_ratio_max'] = max(per_frame) if per_frame else float('nan')
            out[label][roi_name] = st
    return out


def report(stats_by_label, control, windows):
    print('moving windows: ' + ', '.join(f'{a}..{b}' for a, b in windows))
    for label, rois in stats_by_label.items():
        for roi_name, st in rois.items():
            line = (f'  {label}, {roi_name}: {st["events_in_window"]} events in the windows, residual near '
                    f'{st["residual_near_event"]:.3e} vs elsewhere {st["residual_far"]:.3e} '
                    f'({st["ratio"]:.3f}x), coverage while moving '
                    f'{st.get("coverage_moving", float("nan")) * 100:.1f} %')
            c = control.get(label, {}).get(roi_name)
            if c:
                line += f'; frozen run at the same frames {c["ratio"]:.3f}x'
                if 'per_frame_ratio_min' in c:
                    line += (f', per frame {c["per_frame_ratio_min"]:.5f}..{c["per_frame_ratio_max"]:.5f} '
                             f'of it ({c["event_frames_compared"]} event frames)')
            print(line)


def reanalyse(package, log_path=None):
    """Recompute a finished package from its csv/ (repeat 0) and gdf-motion-study.json; no engine."""
    with open(os.path.join(package, 'gdf-motion-study.json'), encoding='utf-8') as f:
        study = json.load(f)
    windows = logged_windows(log_path) if log_path else trajectory_windows()
    print(f'windows from {log_path or f"base_frame={SCENE_START} (no --log given)"}')
    series, events_by_label, stats = {}, {}, {}
    for label, v in study['variants'].items():
        events = v['runs'][0]['events']
        events_by_label[label], series[label], stats[label] = events, {}, {}
        for roi in ROIS:
            with open(os.path.join(package, 'csv', f'{label.replace(" ", "_")}-{roi[0]}.csv'),
                      encoding='utf-8') as f:
                rows = list(csv.DictReader(f))
            numbers = np.array([int(r['frame']) for r in rows])
            resid = np.array([float(r['residual']) for r in rows])
            cov = np.array([float(r['coverage']) for r in rows])
            series[label][roi[0]] = (numbers, resid)
            st = event_stats(numbers, resid, events, moving_windows=windows)
            st['coverage_moving'] = float(np.nanmean(cov[moving_mask(numbers, windows)]))
            stats[label][roi[0]] = st
        stray = events_outside(events, windows, int(numbers.min()), int(numbers.max()))
        if stray:
            raise SystemExit(f'{label}: recentres at {stray} fall outside every moving window; '
                             f'the windows do not describe this run')
    same_phase = {label for label, v in study['variants'].items()
                  if not v['cvars'].get('r.GI.GDF.CenterPhaseOffset')}
    report(stats, frozen_control(series, events_by_label, windows, same_phase), windows)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '13-isolate-gdf-motion'))
    ap.add_argument('--reanalyse', metavar='PACKAGE_DIR',
                    help='recompute a finished package from its csv/ and JSON; does not run the engine')
    ap.add_argument('--log', help='with --reanalyse: a run.log of this trajectory to take the windows from')
    args = ap.parse_args()
    if args.reanalyse:
        return reanalyse(args.reanalyse, args.log)
    runner.require_exe()
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    os.makedirs(os.path.join(args.out, 'csv'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_gdf_motion')
    payload = {'rois': ROIS, 'first_frame': FIRST, 'last_frame': LAST, 'segment_frames': SEGMENT,
               'base_cvars': BASE, 'half_voxel_cm': HALF_VOXEL_CM, 'variants': {}}
    series, events_by_label, first_stats, windows = {}, {}, {}, None

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
            run_windows = logged_windows(res['log'])
            if windows is not None and run_windows != windows:
                raise RuntimeError(f'{label}: moving windows {run_windows} differ from the first '
                                   f'run, {windows}')
            windows = run_windows
            stray = events_outside(events, windows, FIRST, LAST)
            if stray:
                raise RuntimeError(f'{label}: recentres at {stray} fall outside the logged moving '
                                   f'windows {windows}')
            numbers = np.array([fr.frame for fr in frames])
            entry = {'events': events, 'moving_windows': windows, 'rois': {}}
            for roi in ROIS:
                resid, cov, means = motion_compensated_series(frames, roi)
                st = event_stats(frames, resid, events, moving_windows=windows)
                st['coverage_mean'] = float(np.nanmean(cov[1:]))
                st['coverage_min'] = float(np.nanmin(cov[1:]))
                st['coverage_moving'] = float(np.nanmean(cov[moving_mask(numbers, windows)]))
                st['roi_mean'] = float(np.nanmean(means))
                entry['rois'][roi[0]] = st
                if rep == 0:
                    series.setdefault(label, {})[roi[0]] = (numbers, resid)
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
        events_by_label[label], first_stats[label] = runs[0]['events'], runs[0]['rois']
        ev = runs[0]['events']
        print(f"      {len(ev)} recentre events, axes {sorted({e['axis'] for e in ev})}")
        for roi_name, st in runs[0]['rois'].items():
            print(f"      {roi_name}: residual near events {st['residual_near_event']:.3e} vs "
                  f"elsewhere {st['residual_far']:.3e} ({st['ratio']:.2f}x), "
                  f"coverage {st['coverage_mean'] * 100:.1f} % over all frames, "
                  f"{st['coverage_moving'] * 100:.1f} % while moving")

    payload['moving_windows'] = windows
    same_phase = {label for label, cvars in VARIANTS if not cvars.get('r.GI.GDF.CenterPhaseOffset')}
    payload['frozen_at_event_frames'] = frozen_control(series, events_by_label, windows, same_phase)
    print()
    report(first_stats, payload['frozen_at_event_frames'], windows)
    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'gdf-motion-study.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
