"""Sponza's ambient after the sky capture fix (block B.5c): the capture's sun was sqrt(3) x the sky pass's for a
white sun, so every image-based term (the prefiltered specular, the sky SH, the environment-MIS estimate, the
GI probes' sky) carried sqrt(3) more sky than the sky the frame renders. The fix put Sponza's sky-lit indirect
at 0.894 x (Camera C) / 0.928 x (Camera A) of its calibrated level. The user chose to restore the previous
ambient level: the scene's SkyLight intensity and indirect-lighting intensity carry the sqrt(3) explicitly,
where the capture used to hide it.

References: the captures of the build before the sky capture fix (Camera C: test-env-mis/sponza-default,
build ea13ab21, frame 120; Camera A: test-layered-reflections/sponza-ssr-off, build 9f1be63f, frame 60).

Gates (pre-registered in the Rulebook task):
  Q1  Sponza Camera A and C: the direct light is identical to the build before (the sky light does not touch
      it); positions identical.
  Q2  the sky-lit indirect is back: diffuse_indirect median ratio against the pre-fix capture within
      [0.95, 1.05] at both cameras (the fix alone read 0.894 / 0.928).
  Q3  the colour: median luminance ratio against the pre-fix capture within [0.95, 1.05] at both cameras.
  Reported: specular_indirect ratios, the montage (pre-fix, the fix alone, recalibrated).

Usage: python scripts/test_sponza_recalibration.py [--out DIR] [--reuse]
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regiondump  # noqa: E402
import runner  # noqa: E402
import run_probe_side_sponza as sponza  # noqa: E402
from test_layered_reflections import capture, colour, FOG_PROJECT  # noqa: E402
from test_env_mis import lum, chan, capture_sponza, identity, montage, SPONZA_FRAME  # noqa: E402

REPO = runner.REPO
PRE_FIX = {'camera_a': (os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-layered-reflections', 'sponza-ssr-off'), 60),
           'camera_c': (os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-env-mis', 'sponza-default'), SPONZA_FRAME)}
FIX_ALONE = {'camera_a': (os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-sky-capture', 'sponza-camera-a'), 60),
             'camera_c': (os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-sky-capture', 'sponza-default'), SPONZA_FRAME)}
# This binary with the scene before the edit (the split-sum item's Sponza captures): the identity reference.
SAME_BINARY = {'camera_a': (os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-split-sum', 'sponza-camera-a'), 60),
               'camera_c': (os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-split-sum', 'sponza-default'), SPONZA_FRAME)}
# The captures with sqrt 3 in `intensity` only (the first committed state, 4501fa74), kept before the scenes took
# sqrt 3 in `indirect_lighting_intensity` too: the reference of Q4.
INTENSITY_ONLY = {'camera_a': (os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-sponza-recalibration', 'intensity-only', 'sponza-camera-a'), 60),
                  'camera_c': (os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-sponza-recalibration', 'intensity-only', 'sponza-default'), SPONZA_FRAME)}
Q_LO, Q_HI = 0.95, 1.05
Q4_TOL = 0.005


def compare(now, before, frame_n):
    fr_n = regiondump.load_sequence(os.path.join(now, 'region'), frame_n, frame_n)[0]
    fr_b = regiondump.load_sequence(os.path.join(before, 'region'), frame_n, frame_n)[0]
    s = identity(fr_n, fr_b)
    di_n, di_b = chan(fr_n, 'diffuse_indirect')[..., :3], chan(fr_b, 'diffuse_indirect')[..., :3]
    si_n, si_b = chan(fr_n, 'specular_indirect')[..., :3], chan(fr_b, 'specular_indirect')[..., :3]
    c_n, c_b = colour(fr_n), colour(fr_b)
    m = lum(di_b) > 1e-3
    ms = lum(si_b) > 1e-4
    lc = lum(c_b) > 1e-3
    r = lum(di_n)[m] / lum(di_b)[m]
    s.update({'diffuse_indirect_ratio_median': float(np.median(r)), 'diffuse_indirect_ratio_p10': float(np.percentile(r, 10)), 'diffuse_indirect_ratio_p90': float(np.percentile(r, 90)),
              'specular_indirect_ratio_median': float(np.median(lum(si_n)[ms] / lum(si_b)[ms])) if ms.any() else None,
              'colour_ratio_median': float(np.median(lum(c_n)[lc] / lum(c_b)[lc])), 'colour_median_lum_before': float(np.median(lum(c_b)[lc])), 'colour_median_lum_after': float(np.median(lum(c_n)[lc]))})
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-sponza-recalibration'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    now = {'camera_a': (os.path.join(args.out, 'sponza-camera-a'), 60), 'camera_c': (os.path.join(args.out, 'sponza-default'), SPONZA_FRAME)}
    capture(now['camera_a'][0], FOG_PROJECT, args.reuse, {'r.SSR': 0}, frames=66, first=60, last=60, shot=60)
    capture_sponza(now['camera_c'][0], args.reuse, sponza.PROJECT_CVARS)
    rows = {}
    for key in ('camera_a', 'camera_c'):
        pre, frame_n = PRE_FIX[key]
        if not os.path.isdir(os.path.join(pre, 'region')):
            rows[key] = None
            continue
        rows[key] = compare(now[key][0], pre, frame_n)
        fa = FIX_ALONE[key][0]
        if os.path.isdir(os.path.join(fa, 'region')):
            rows[key]['fix_alone'] = compare(fa, pre, frame_n)
        io_ref = INTENSITY_ONLY[key][0]
        if os.path.isdir(os.path.join(io_ref, 'region')):
            rows[key]['intensity_only'] = compare(now[key][0], io_ref, frame_n)
            s4 = rows[key]['intensity_only']
            print(f'  {key} against the intensity-only captures: positions {s4["pos_max_diff"]:.3g}, direct {s4["direct_max_diff"]:.3g}, '
                  f'diffuse_indirect ratio {s4["diffuse_indirect_ratio_median"]:.4f}, specular ratio {s4["specular_indirect_ratio_median"]}, colour ratio {s4["colour_ratio_median"]:.4f}', flush=True)
        sb = SAME_BINARY[key][0]
        if os.path.isdir(os.path.join(sb, 'region')):
            rows[key]['same_binary'] = compare(now[key][0], sb, frame_n)
            print(f'  {key} against this binary with the scene before the edit: positions {rows[key]["same_binary"]["pos_max_diff"]:.3g}, direct {rows[key]["same_binary"]["direct_max_diff"]:.3g}, '
                  f'diffuse_indirect ratio {rows[key]["same_binary"]["diffuse_indirect_ratio_median"]:.3f}, specular ratio {rows[key]["same_binary"]["specular_indirect_ratio_median"]}, '
                  f'colour ratio {rows[key]["same_binary"]["colour_ratio_median"]:.3f}', flush=True)
        s = rows[key]
        print(f'  {key}: direct {s["direct_max_diff"]:.3g}; diffuse_indirect ratio {s["diffuse_indirect_ratio_median"]:.3f} (p10 {s["diffuse_indirect_ratio_p10"]:.3f}, p90 {s["diffuse_indirect_ratio_p90"]:.3f}; '
              f'the fix alone {s.get("fix_alone", {}).get("diffuse_indirect_ratio_median")}); specular ratio {s["specular_indirect_ratio_median"]}; colour ratio {s["colour_ratio_median"]:.3f} '
              f'(the fix alone {s.get("fix_alone", {}).get("colour_ratio_median")})', flush=True)
    report['cameras'] = rows
    ok = {k: r is not None for k, r in rows.items()}
    # The identity against this binary with the scene before the edit (the pre-fix captures predate the mesh winding
    # repair, the mesh decals and the split-sum item, which move positions and direct light where they apply).
    sbr = {k: (r or {}).get('same_binary') for k, r in rows.items()}
    check('Q1 Sponza Camera A and C: positions and direct light identical to this binary with the scene before the edit',
          all(sbr.values()) and all(r['pos_max_diff'] <= 1e-6 and r['direct_max_diff'] <= 1e-6 for r in sbr.values()),
          '; '.join(f'{k}: positions {r["pos_max_diff"]:.3g}, direct {r["direct_max_diff"]:.3g}' if r else f'{k}: no same-binary reference' for k, r in sbr.items()))
    ior = {k: (r or {}).get('intensity_only') for k, r in rows.items()}

    def _inert(r):
        vals = [r['diffuse_indirect_ratio_median'], r['colour_ratio_median']] + ([r['specular_indirect_ratio_median']] if r.get('specular_indirect_ratio_median') is not None else [])
        return r['pos_max_diff'] <= 1e-6 and r['direct_max_diff'] <= 1e-6 and all(abs(v - 1.0) <= Q4_TOL for v in vals)
    check('Q4 with GI on, indirect_lighting_intensity is inert: against the intensity-only captures, positions and direct identical and the diffuse / specular / colour medians within 0.5 %',
          all(ior.values()) and all(_inert(r) for r in ior.values()),
          '; '.join(f'{k}: diffuse {r["diffuse_indirect_ratio_median"]:.4f}, specular {r.get("specular_indirect_ratio_median")}, colour {r["colour_ratio_median"]:.4f}' if r else f'{k}: no intensity-only reference' for k, r in ior.items()))
    check('Q2 the sky-lit indirect is back: diffuse_indirect median ratio against the pre-fix capture within [0.95, 1.05] at both cameras',
          all(ok.values()) and all(Q_LO <= r['diffuse_indirect_ratio_median'] <= Q_HI for r in rows.values()),
          '; '.join(f'{k}: {r["diffuse_indirect_ratio_median"]:.3f} (the fix alone {r.get("fix_alone", {}).get("diffuse_indirect_ratio_median")})' if r else f'{k}: no reference' for k, r in rows.items()))
    check('Q3 the colour: median luminance ratio against the pre-fix capture within [0.95, 1.05] at both cameras',
          all(ok.values()) and all(Q_LO <= r['colour_ratio_median'] <= Q_HI for r in rows.values()),
          '; '.join(f'{k}: {r["colour_ratio_median"]:.3f}' if r else f'{k}: no reference' for k, r in rows.items()))
    report['montage'] = montage(args.out, [('Camera A, before the sky fix (9f1be63f)', PRE_FIX['camera_a'][0]), ('Camera A, the sky fix alone (b8ff5077)', FIX_ALONE['camera_a'][0]),
                                           ('Camera A, recalibrated', now['camera_a'][0])], 'sponza_recalibration_montage')
    passed = sum(1 for _, ok_, _ in results if ok_)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'sponza_recalibration.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': o, 'detail': str(d)} for c, o, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
