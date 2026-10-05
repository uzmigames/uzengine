"""A scene without a directional-light entity must not be lit by a phantom sun (2026-10-05).

Until 2026-10-05 `LightFrameData` defaulted to sun direction (0.5, 0.3, 0.8) and colour (3, 2.85, 2.7),
so every scene without a DirectionalLightComponent was lit, and shadowed by the CSM, from a light that
exists nowhere in its data. The sun colour now defaults to zero, `hasDirectionalLight` tells the frame
whether a sun exists, the cascade pass is skipped without one (cascadeCount 0: full visibility) and
the sky takes its direction from `SkyAtmosphereComponent::sunDirection` (scene key `sun_direction`).

Scene `scenes/local-shadow-nosun.uzscene` = `local-shadow-reference` without its Sun entity: the ground
at z = 0, the wall `solid_near_out_east` (x 330..390), a spot at (300, 0, 900) pointing down (cone
radius 241 cm on the ground) and a point light at (280, 380, 200) with range 330, both casting
shadows, a sky light at 0.05 and the sky atmosphere with `sun_illuminance` 1. Regions on the ground are
selected by WORLD position from the frame's region dump:

  dark          x -700..-350, y -450..450  outside both local lights: with no sun it must be black
  spot_lit / spot_shadow / point_lit / point_shadow  as in test_local_light_shadows.py

Checks (pre-registered):
  N0 every region has >= 50 ground pixels
  N1 `direct_light` in `dark` <= 1e-5 (the phantom sun gave about 1.0 there)
  N2 sun shadow visibility (`direct_light`.w) is 1 on every ground pixel (no cascades were rendered)
  N3 `diffuse_indirect` in `dark` > 1e-5 (the sky light still lights the scene)
  N4 the sky follows the component's `sun_direction`: a second capture of the same scene with the sky
     sun below the horizon (`local-shadow-nosun-sundown.uzscene`, sun_direction [0.5, 0.3, -0.8]) has
     less than half the `diffuse_indirect` of the default in `dark` (the camera sees no sky pixels, so
     the sky is judged through the sky light it feeds)
  N5 the local lights are untouched: lit regions > 1e-3, lit / shadow >= 10 for both lights

Usage: python scripts/test_no_sun_lighting.py [--out DIR] [--reuse] [--frames N]
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

PROJECT = os.path.join(runner.REPO, 'scripts', 'local-shadow-nosun.uzproject')
W, H = 640, 360
FRAME = 100
REGIONS = {
    'dark': ((-700, -350), (-450, 450)),
    'spot_shadow': ((410, 470), (-60, 60)),
    'spot_lit': ((150, 300), (-60, 60)),
    'point_shadow': ((440, 520), (350, 410)),
    'point_lit': ((120, 200), (350, 410)),
}
REC709 = np.array([0.2126, 0.7152, 0.0722])
PROJECT_SUNDOWN = os.path.join(runner.REPO, 'scripts', 'local-shadow-nosun-sundown.uzproject')
DARK_MAX, INDIRECT_MIN, SUNDOWN_MAX_RATIO, LIT_MIN, RATIO_MIN = 1e-5, 1e-5, 0.5, 1e-3, 10.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', 'test-no-sun-lighting'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--frames', type=int, default=FRAME + 8)
    args = ap.parse_args()
    work = os.path.join(args.out, 'capture')
    work_b = os.path.join(args.out, 'capture-sundown')
    if not args.reuse:
        runner.require_exe()
        for w, project in ((work, PROJECT), (work_b, PROJECT_SUNDOWN)):
            res = runner.run_capture(w, project, args.frames, regions=[('FRAME', 0, 0, W, H)], region_fields='components',
                                     region_first=FRAME, region_last=FRAME, screenshot_frames=f'{FRAME}-{FRAME}',
                                     timing_first=FRAME, clean=True)
            if res['exit_code'] != 0:
                raise SystemExit(f'capture failed: exit {res["exit_code"]}')
    fr = regiondump.load_sequence(os.path.join(work, 'region'), FRAME, FRAME)[0]
    rect = fr.rect('FRAME')
    pos = fr.world_positions('FRAME')
    direct4 = rect.channel('direct_light').astype(np.float64)
    direct = direct4[..., :3] @ REC709
    sunvis = direct4[..., 3]
    indirect = rect.channel('diffuse_indirect')[..., :3].astype(np.float64) @ REC709
    geometry = np.isfinite(pos).all(-1)
    ground = geometry & (np.abs(pos[..., 2]) < 3.0)
    stats = {}
    for name, ((x0, x1), (y0, y1)) in REGIONS.items():
        m = ground & (pos[..., 0] >= x0) & (pos[..., 0] <= x1) & (pos[..., 1] >= y0) & (pos[..., 1] <= y1)
        stats[name] = {'pixels': int(m.sum()),
                       'direct': float(direct[m].mean()) if m.any() else None,
                       'indirect': float(indirect[m].mean()) if m.any() else None}
    stats['ground_sunvis_min'] = float(sunvis[ground].min()) if ground.any() else None
    stats['ground_sunvis_mean'] = float(sunvis[ground].mean()) if ground.any() else None
    # B: the same scene with the sky's sun below the horizon (sun_direction z < 0): the sky IBL must follow it
    fr_b = regiondump.load_sequence(os.path.join(work_b, 'region'), FRAME, FRAME)[0]
    pos_b = fr_b.world_positions('FRAME')
    indirect_b = fr_b.rect('FRAME').channel('diffuse_indirect')[..., :3].astype(np.float64) @ REC709
    (x0, x1), (y0, y1) = REGIONS['dark']
    ground_b = np.isfinite(pos_b).all(-1) & (np.abs(pos_b[..., 2]) < 3.0)
    m_b = ground_b & (pos_b[..., 0] >= x0) & (pos_b[..., 0] <= x1) & (pos_b[..., 1] >= y0) & (pos_b[..., 1] <= y1)
    stats['dark_sundown'] = {'pixels': int(m_b.sum()), 'indirect': float(indirect_b[m_b].mean()) if m_b.any() else None}
    results = []

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}')

    for n in REGIONS:
        st = stats[n]
        print(f'  {n:13} pixels {st["pixels"]:6d}  direct {st["direct"]!s:>12}  indirect {st["indirect"]!s:>12}')
    print(f'  ground sun visibility min {stats["ground_sunvis_min"]} mean {stats["ground_sunvis_mean"]}; '
          f'dark indirect with the sky sun below the horizon {stats["dark_sundown"]["indirect"]} ({stats["dark_sundown"]["pixels"]} px)')
    d = stats['dark']
    sl, ss, pl, ps = (stats[k]['direct'] for k in ('spot_lit', 'spot_shadow', 'point_lit', 'point_shadow'))
    check('N0 every region has >= 50 ground pixels', all(stats[k]['pixels'] >= 50 for k in REGIONS),
          {k: stats[k]['pixels'] for k in REGIONS})
    check('N1 no direct light outside the local lights (no phantom sun)',
          d['direct'] is not None and d['direct'] <= DARK_MAX, f'dark direct mean {d["direct"]}')
    check('N2 sun shadow visibility is 1 on the whole ground (no cascades)',
          stats['ground_sunvis_min'] is not None and stats['ground_sunvis_min'] >= 0.999,
          f'min {stats["ground_sunvis_min"]}')
    check('N3 the sky light still lights the dark region',
          d['indirect'] is not None and d['indirect'] > INDIRECT_MIN, f'dark indirect mean {d["indirect"]}')
    sd = stats['dark_sundown']['indirect']
    check('N4 the sky follows sun_direction (sun below the horizon dims the sky light)',
          d['indirect'] is not None and sd is not None and sd < SUNDOWN_MAX_RATIO * d['indirect'],
          f'dark indirect {d["indirect"]} -> {sd} with the sun below the horizon')
    ok_local = (sl is not None and sl > LIT_MIN and pl is not None and pl > LIT_MIN
                and ss is not None and ps is not None
                and sl / max(ss, 1e-12) >= RATIO_MIN and pl / max(ps, 1e-12) >= RATIO_MIN)
    check('N5 local lights unchanged (lit > 1e-3, lit / shadow >= 10)', ok_local,
          f'spot {sl} / {ss}, point {pl} / {ps}')
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'no_sun_lighting.json'), 'w', encoding='utf-8') as fh:
        json.dump({'stats': stats, 'checks': [{'id': c, 'pass': ok, 'detail': str(dd)} for c, ok, dd in results],
                   'identity': runner.capture_identity(work)}, fh, indent=1)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
