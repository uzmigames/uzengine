"""Behavioural harness for the GDF diagnostic controls and the motion tracking (followup-13).

  freeze          `r.GI.GDF.FreezeCenter=1` produces no recentre events at all, while the same
                  trajectory without it does (negative control)
  phase shift     `r.GI.GDF.CenterPhaseOffset` moves the event frames without changing the camera
                  path — the events must land on *different* frames, which is what separates a grid
                  effect from a feature of the trajectory
  world tracking  while the camera moves, the world-space tracking keeps most of the rectangle valid
                  and reports a smaller residual than comparing the same screen pixels (negative
                  control: no motion compensation)

Correction (2026-10-08): the world-tracking checks ran on frames 41..100 after FIRST (198..257), but
the trajectory counts its segments from its on_start frame (logged `base_frame=37`), so 198..217 were
still frames. They now run on the moving frames the trajectory logs (218..267 of this dump at
base_frame=37; the block-B sweep of 2026-10-07 logged base_frame=39, which gives 220..267). The
figures recorded with the old window (95 % valid, 4.3x) predate the fix.

Usage: python scripts/rendering/test_gdf_controls.py [--keep]
"""
from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
from run_gdf_motion_study import logged_windows  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 120
LAST = FIRST + 110          # 157..267: segment 2 (-X), moving from base_frame + 181 (218 at base 37)
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-gdf-boundary.uzproject')
ROI = ('lit_wall', 1100, 300, 96, 96)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def capture(work, label, cvars):
    print(f'  … {label}', flush=True)
    res = runner.run_capture(os.path.join(work, label), PROJECT, LAST - SCENE_START + 8,
                             regions=[ROI], region_fields='motion',
                             region_first=FIRST, region_last=LAST,
                             cvars={**BASE, **cvars}, timing_first=FIRST)
    if res['exit_code'] != 0:
        check(False, f'{label}: engine exited {res["exit_code"]}', res['log'])
    return res


def events(res):
    with open(res['log'], 'rb') as f:
        text = f.read().decode('utf-8', 'replace')
    return sorted(int(m.group(1)) for m in
                  (re.search(r'frame=(\d+).*gdfClipmap=', line) for line in text.splitlines()
                   if 'GI recentre' in line and 'gdfClipmap=' in line) if m)


def main():
    runner.require_exe()
    keep = '--keep' in sys.argv
    work = tempfile.mkdtemp(prefix='uz_gdf_controls_')
    print(f'work dir: {work}')
    try:
        base = capture(work, 'baseline', {})
        base_events = events(base)
        check(len(base_events) > 0, 'the trajectory produces clipmap recentres',
              f'{len(base_events)} events, first at frame {base_events[0] if base_events else "-"}')

        frozen = capture(work, 'frozen', {'r.GI.GDF.FreezeCenter': 1})
        check(len(events(frozen)) == 0, 'FreezeCenter=1 produces no recentre events',
              f'{len(events(frozen))} events')

        shifted = capture(work, 'phase_shifted', {'r.GI.GDF.CenterPhaseOffset': 9.765625})
        sh = events(shifted)
        moved = sorted(set(sh) - set(base_events))
        check(len(sh) > 0 and len(moved) >= max(1, len(sh) // 2),
              'a shifted grid phase moves the event frames while the path is unchanged',
              f'{len(sh)} events, {len(moved)} of them on frames the baseline had no event on')

        # world tracking against no compensation, on the frames where the camera moves, as the
        # trajectory logged them (corrected 2026-10-08: 41..100 after FIRST included 20 still frames)
        frames = regiondump.load_sequence(base['region_dir'], FIRST, LAST)
        windows = logged_windows(base['log'])
        moving = [f for f in frames if any(lo <= f.frame <= hi for lo, hi in windows)]
        if len(moving) < 10:
            check(False, 'the dump holds frames where the camera moves',
                  f'{len(moving)} frames of {FIRST}..{LAST} inside the logged windows {windows}')
            return 1
        name, rx, ry, rw, rh = ROI
        tracked, plain, coverage = [], [], []
        for a, b in zip(moving, moving[1:]):
            la = a.rect(name).luminance('output').astype(np.float64)
            lb = b.rect(name).luminance('output').astype(np.float64)
            world = b.world_positions(name)
            here, there = b.project(world), a.project(world)
            gx = (np.arange(rw) + rx)[None, :].astype(np.float64)
            gy = (np.arange(rh) + ry)[:, None].astype(np.float64)
            sx = gx + (there[..., 0] - here[..., 0]) - rx
            sy = gy + (there[..., 1] - here[..., 1]) - ry
            inside = (sx >= 0) & (sx <= rw - 1.001) & (sy >= 0) & (sy <= rh - 1.001)
            xi = np.clip(sx, 0, rw - 1.001)
            yi = np.clip(sy, 0, rh - 1.001)
            x0, y0 = np.floor(xi).astype(int), np.floor(yi).astype(int)
            fx, fy = xi - x0, yi - y0
            samp = (la[y0, x0] * (1 - fx) * (1 - fy) + la[y0, x0 + 1] * fx * (1 - fy)
                    + la[y0 + 1, x0] * (1 - fx) * fy + la[y0 + 1, x0 + 1] * fx * fy)
            tracked.append(float(np.abs(lb - samp)[inside].mean()))
            plain.append(float(np.abs(lb - la)[inside].mean()))
            coverage.append(float(inside.mean()))
        check(np.mean(coverage) > 0.8, 'the tracking keeps most of the rectangle valid while moving',
              f'{np.mean(coverage) * 100:.1f} % of pixels')
        check(np.mean(tracked) < 0.5 * np.mean(plain),
              'negative control: without motion compensation the residual is much larger',
              f'{np.mean(tracked):.3e} tracked vs {np.mean(plain):.3e} same-pixel '
              f'({np.mean(tracked) / max(1e-12, np.mean(plain)):.2f}x)')
    finally:
        print(f'\n{len(PASS)} passed, {len(FAIL)} failed')
        for f in FAIL:
            print(f'  FAILED: {f}')
        if keep:
            print('kept:', work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
