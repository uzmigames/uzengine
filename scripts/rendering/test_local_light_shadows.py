"""Behavioural check of spot and point light shadows (block A.1, 2026-10-05).

Scene `scenes/local-shadow-reference.uzscene`: a 3x3 ground at z = 0, the 60 cm thick wall
`solid_near_out_east` (x 330..390, y -450..450, top z = 450), a directional light of intensity 0 (without
one LightFrameData's defaults light the whole ground with sun colour (3, 2.85, 2.7)), a sky light at
0.05 so `direct_light` is the local lights alone, a spot light at (300, 0, 900) aimed straight down
with a 15 degree outer angle (cone radius 241 cm on the ground; rotation pitch +90: the spot's direction
is rotation * Forward and the cone tests dot(L, -direction), while the sun entity's rotation points
TOWARDS the sun), and a point light at (280, 380, 200) with a 330 cm range, both casting
shadows. Four ground regions are selected by WORLD position from the frame's region dump:

  spot_shadow   x 410..470, y -60..60   east of the wall, under the wall's shadow (lit only if shadows leak)
  spot_lit      x 150..300, y -60..60   west of the wall, inside the cone, nothing between it and the spot
  point_shadow  x 440..520, y 350..410  east of the wall, the point light is west of it
  point_lit     x 120..200, y 350..410  west of the wall, 233 cm from the point light

Checks (pre-registered): L1 spot_lit mean > 1e-3; L2 spot_lit / spot_shadow >= 10; L3 point_lit
mean > 1e-3; L4 point_lit / point_shadow >= 10; L5 (no acne) the lit regions have no more than 5 % of
pixels below a tenth of their own mean.

Before the fix the atlas was cleared to 0.0 and drawn with Less: spot pixels read fully shadowed
(L1 fails) and point lights never sampled their tiles (L4 fails, ratio ~1.5 from attenuation).

Usage: python scripts/rendering/test_local_light_shadows.py [--out DIR] [--reuse] [--frames N]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'local-shadow-reference.uzproject')
W, H = 640, 360
FRAME = 100
REGIONS = {
    'spot_shadow': ((410, 470), (-60, 60)),
    'spot_lit': ((150, 300), (-60, 60)),
    'point_shadow': ((440, 520), (350, 410)),
    'point_lit': ((120, 200), (350, 410)),
}
REC709 = np.array([0.2126, 0.7152, 0.0722])
LIT_MIN, RATIO_MIN, ACNE_MAX = 1e-3, 10.0, 0.05


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', 'test-local-light-shadows'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--frames', type=int, default=FRAME + 8)
    args = ap.parse_args()
    work = os.path.join(args.out, 'capture')
    if not args.reuse:
        runner.require_exe()
        res = runner.run_capture(work, PROJECT, args.frames, regions=[('FRAME', 0, 0, W, H)], region_fields='components',
                                 region_first=FRAME, region_last=FRAME, screenshot_frames=f'{FRAME}-{FRAME}',
                                 timing_first=FRAME, clean=True)
        if res['exit_code'] != 0:
            raise SystemExit(f'capture failed: exit {res["exit_code"]}')
    fr = regiondump.load_sequence(os.path.join(work, 'region'), FRAME, FRAME)[0]
    pos = fr.world_positions('FRAME')
    direct = fr.rect('FRAME').channel('direct_light')[..., :3].astype(np.float64) @ REC709
    ground = np.isfinite(pos).all(-1) & (np.abs(pos[..., 2]) < 3.0)
    stats = {}
    for name, ((x0, x1), (y0, y1)) in REGIONS.items():
        m = ground & (pos[..., 0] >= x0) & (pos[..., 0] <= x1) & (pos[..., 1] >= y0) & (pos[..., 1] <= y1)
        v = direct[m]
        stats[name] = {'pixels': int(m.sum()), 'mean': float(v.mean()) if m.any() else None,
                       'acne': float((v < 0.1 * v.mean()).mean()) if m.any() and v.mean() > 0 else None}
    hit = ground & (direct > 0.01 * direct[ground].max()) if ground.any() else ground
    if hit.any():
        c = pos[hit].mean(0)
        print(f'  lit ground pixels {int(hit.sum())}, centroid x {c[0]:.0f} y {c[1]:.0f}; x range {pos[hit][:,0].min():.0f}..{pos[hit][:,0].max():.0f}, y range {pos[hit][:,1].min():.0f}..{pos[hit][:,1].max():.0f}')
    results = []

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}')

    for n, st in stats.items():
        mean = 'n/a' if st['mean'] is None else '%.5e' % st['mean']
        print(f'  {n:13} pixels {st["pixels"]:6d}  mean {mean}')
    sl, ss, pl, ps = (stats[k]['mean'] for k in ('spot_lit', 'spot_shadow', 'point_lit', 'point_shadow'))
    ok_px = all(stats[k]['pixels'] >= 50 for k in REGIONS)
    check('L0 every region has >= 50 ground pixels', ok_px, {k: stats[k]['pixels'] for k in REGIONS})
    check('L1 spot lit region receives light', sl is not None and sl > LIT_MIN, f'mean {sl}')
    check('L2 spot shadow >= 10x darker than lit', sl and ss is not None and sl / max(ss, 1e-12) >= RATIO_MIN,
          f'ratio {sl / max(ss, 1e-12) if sl and ss is not None else None}')
    check('L3 point lit region receives light', pl is not None and pl > LIT_MIN, f'mean {pl}')
    check('L4 point shadow >= 10x darker than lit', pl and ps is not None and pl / max(ps, 1e-12) >= RATIO_MIN,
          f'ratio {pl / max(ps, 1e-12) if pl and ps is not None else None}')
    check('L5 no acne in the lit regions', all(stats[k]['acne'] is not None and stats[k]['acne'] <= ACNE_MAX for k in ('spot_lit', 'point_lit')),
          {k: stats[k]['acne'] for k in ('spot_lit', 'point_lit')})
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'local_light_shadows.json'), 'w', encoding='utf-8') as fh:
        json.dump({'stats': stats, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results],
                   'identity': runner.capture_identity(work)}, fh, indent=1)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
