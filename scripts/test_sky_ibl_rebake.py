"""Behavioural check that the sky IBL follows the sun (block A.5, 2026-10-05).

Scene `scenes/sky-rebake-reference.uzscene`: a ground, no GI, a directional light of intensity 0 whose
rotation still drives the sky (`atmosphere_sun_light`), and a script on it that turns the sun to the
horizon at frame 100 (`sun_rotate_test.lua`). With no direct light the ground is lit by the sky IBL
alone, so the image changes only if the sky cubemap / SH are re-baked after the move.

  S1  frame 90 vs frame 200 (fixed exposure): mean luminance of the lower half of the frame (ground)
      differs by MORE than 5 % — the IBL was re-baked. Before the fix the two were identical
      (`m_skyIBLDirty` was set once at construction).
  S2  the log carries one "Sky IBL: re-bake queued" line after frame 100 and a second "Sky IBL baked".

Usage: python scripts/test_sky_ibl_rebake.py [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

PROJECT = os.path.join(runner.REPO, 'scripts', 'sky-rebake-reference.uzproject')
F0, F1 = 90, 200
CHANGE_MIN = 0.05


def mean_luma(path):
    img = np.asarray(Image.open(path).convert('RGB'), dtype=np.float64) / 255.0
    half = img[img.shape[0] // 2:]
    return float((half @ np.array([0.2126, 0.7152, 0.0722])).mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', 'test-sky-ibl-rebake'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    work = os.path.join(args.out, 'capture')
    if not args.reuse:
        runner.require_exe()
        res = runner.run_capture(work, PROJECT, F1 + 8, screenshot_frames=f'{F0}-{F1}:{F1 - F0}', timing_first=F1, clean=True)
        if res['exit_code'] != 0:
            raise SystemExit(f'capture failed: exit {res["exit_code"]}')
    frames = os.path.join(work, 'frames')
    pngs = {f: next((os.path.join(frames, n) for n in os.listdir(frames) if n.endswith(f'_{f}.png')), None) for f in (F0, F1)}
    assert all(pngs.values()), pngs
    l0, l1 = mean_luma(pngs[F0]), mean_luma(pngs[F1])
    change = abs(l1 - l0) / max(l0, 1e-9)
    log = open(os.path.join(work, 'run.log'), encoding='utf-8', errors='replace').read()
    rebake_lines = len(re.findall(r'Sky IBL: re-bake queued', log))
    baked_lines = len(re.findall(r'Sky IBL baked', log))
    rotated = 'sun_rotate_test: sun rotated' in log
    results = []

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}')

    print(f'  ground luma frame {F0}: {l0:.5f}   frame {F1}: {l1:.5f}   change {change:.2%}   rotated: {rotated}')
    check('S0 the script rotated the sun', rotated, '')
    check('S1 ground brightness follows the sun (> 5 %)', change > CHANGE_MIN, f'{change:.2%}')
    check('S2 one re-bake queued and baked after the move', rebake_lines >= 1 and baked_lines >= 2,
          f'queued {rebake_lines}, baked {baked_lines}')
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'sky_ibl_rebake.json'), 'w', encoding='utf-8') as fh:
        json.dump({'luma': {str(F0): l0, str(F1): l1}, 'change': change, 'rebake_lines': rebake_lines,
                   'baked_lines': baked_lines, 'checks': [{'id': c, 'pass': ok, 'detail': d} for c, ok, d in results],
                   'identity': runner.capture_identity(work)}, fh, indent=1)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
