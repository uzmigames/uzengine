"""Mesh winding (the sample meshes exported counter-clockwise): the engine renders clockwise-front, so a
counter-clockwise mesh shows the inside of its far side (block B.5b's chrome sphere). The whole-mesh
inversions were repaired in place with `scripts/repair_mesh_winding.py`; this harness measures it.

Gates (pre-registered in the Rulebook task):
  W1  the audit reads no inverted mesh (LOD0 >= 99 % inconsistent with the clockwise-front rule) among the
      sample meshes; the mixed class (simplifier residue) is reported.
  W2  chrome_test: the sphere's visible positions are the near hemisphere's (x median > 0 with the camera at
      x = 400) and N.V > 0 at the median (before: x median -51.6, N.V -0.70).
  W3  the chrome sphere against F x the dumped PMREM environment along the reflected ray (block B.5b's E4 as
      first written): median relative error <= 0.2 (before 0.99).
  W4  Sponza Camera C against the capture of the build before (test-sky-capture/sponza-default, frame 120):
      positions identical outside the repaired meshes' screen bounds; the changed pixels reported with their
      share inside those bounds.

Usage: python scripts/test_mesh_winding.py [--out DIR] [--reuse]
"""
import argparse
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regiondump  # noqa: E402
import runner  # noqa: E402
import run_probe_side_sponza as sponza  # noqa: E402
from test_layered_reflections import Camera, capture, colour, frames  # noqa: E402
from test_env_mis import (load_env, lum, chan, mirror_reference, pixel_shading_inputs, rel_err, ratio,  # noqa: E402
                          capture_sponza, identity, montage, CHROME_PROJECT, CHROME_CVARS, SPHERE_CENTRE, SPHERE_RADIUS, SPHERE_F0,
                          SPONZA_FRAME, FIRST, REF_FRAME)
import repair_mesh_winding as winding  # noqa: E402

REPO = runner.REPO
W, H = 1920, 1080
SPONZA_BEFORE = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-sky-capture', 'sponza-default')   # build b8ff5077, old meshes, frame 120
REPAIRED_SPONZA = ['arch_stones_01_stone_trims_01_0', 'column_2ndfloor_19_column_head_2ndfloor_02_0', 'column_2ndfloor_19_column_head_2ndfloor_03_0',
                   'lamps_2ndflloor_01_metal_door_0', 'ornament_door_2ndfloor_15_metal_door_0', 'ornament_doors_09_metal_door_0',
                   'roof_floor_03_01_roof_tiles_01_0']
W3_MAX = 0.2


def mesh_bounds(path):
    import struct
    b = open(path, 'rb').read()
    return np.array(struct.unpack_from('<6f', b, 32)).reshape(2, 3)


def screen_rect(bounds, cam, scale=1.0):
    corners = np.array([[bounds[i, 0], bounds[j, 1], bounds[k, 2]] for i in (0, 1) for j in (0, 1) for k in (0, 1)]) * scale
    px, py, z = cam.project(corners)
    if (z <= 1.0).all():
        return None
    ok = z > 1.0
    return (float(px[ok].min()), float(py[ok].min()), float(px[ok].max()), float(py[ok].max()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-mesh-winding'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    def work(name):
        return os.path.join(args.out, name)

    # ── W1: the audit ──
    inverted, mixed, total = [], [], 0
    for p in sorted(winding.glob.glob(os.path.join(REPO, 'sample', 'content', 'meshes', '**', '*.uzmesh'), recursive=True)):
        rows = winding.audit(p)
        if rows is None:
            continue
        total += 1
        rel = os.path.relpath(p, REPO)
        if rows[0][1] >= 0.99:
            inverted.append(rel)
        elif rows[0][1] > 0.01:
            mixed.append((rel, rows[0][1]))
    report['w1'] = {'meshes': total, 'inverted': inverted, 'mixed': mixed}
    check('W1 no sample mesh is a whole-mesh inversion (LOD0 >= 99 % inconsistent with clockwise-front)', len(inverted) == 0,
          f'{total} meshes, inverted {inverted}, mixed {[(m, round(f, 3)) for m, f in mixed]}')

    # ── W2 / W3: the chrome sphere ──
    k = REF_FRAME - FIRST
    man_c = capture(work('chrome'), CHROME_PROJECT, args.reuse, CHROME_CVARS)
    fr_c = frames(work('chrome'))[k]
    cam_c = Camera(man_c.get('camera', {}).get('position', [400, 0, 100]), man_c.get('camera', {}).get('target', [0, 0, 100]), fov_deg=40)
    pos_c = fr_c.world_positions('FRAME')
    sphere = np.isfinite(pos_c).all(-1) & (np.abs(np.linalg.norm(pos_c - SPHERE_CENTRE, axis=-1) - SPHERE_RADIUS) < 3.0)
    n_c = chan(fr_c, 'lighting_normal')[..., :3]
    v_c = cam_c.eye - pos_c
    v_c /= np.maximum(np.linalg.norm(v_c, axis=-1, keepdims=True), 1e-9)
    nov = np.sum(n_c * v_c, -1)
    report['w2'] = {'px': int(sphere.sum()), 'position_x_median': float(np.median(pos_c[sphere][:, 0])) if sphere.any() else None,
                    'NoV_median': float(np.median(nov[sphere])) if sphere.any() else None, 'NoV_p10': float(np.percentile(nov[sphere], 10)) if sphere.any() else None}
    check('W2 chrome_test: the sphere shows its near hemisphere (positions x median > 0 with the camera at x = 400) and N.V > 0 at the median',
          sphere.sum() > 10000 and report['w2']['position_x_median'] > 0 and report['w2']['NoV_median'] > 0,
          f'{int(sphere.sum())} px, x median {report["w2"]["position_x_median"]}, N.V median {report["w2"]["NoV_median"]} (p10 {report["w2"]["NoV_p10"]})')
    env_c = load_env(work('chrome'))
    if env_c is None or not sphere.any():
        check('W3 the chrome sphere against F x the dumped PMREM along the reflected ray (median relative error <= 0.2)', False, 'no environment dump')
    else:
        px = pixel_shading_inputs(fr_c, cam_c, sphere, stride=2)
        gt, R = mirror_reference(env_c, px['N'], px['V'], SPHERE_F0)
        e = rel_err(px['spec'], gt)
        up = R[:, 2] > 0.05
        report['w3'] = {'median_rel_err': float(np.median(e)), 'median_ratio': float(np.median(ratio(px['spec'], gt))), 'eval_px': int(len(e)),
                        'median_rel_err_upward': float(np.median(e[up])) if up.any() else None, 'specular_median_lum': float(np.median(lum(px['spec'])))}
        check('W3 the chrome sphere against F x the dumped PMREM along the reflected ray (median relative error <= 0.2)',
              report['w3']['median_rel_err'] <= W3_MAX, f'median error {report["w3"]["median_rel_err"]:.3f} (ratio {report["w3"]["median_ratio"]:.3f}) over {len(e)} px; upward rays {report["w3"]["median_rel_err_upward"]}')

    # ── W4: Sponza Camera C ──
    man_s = capture_sponza(work('sponza-default'), args.reuse, sponza.PROJECT_CVARS)
    if os.path.isdir(os.path.join(SPONZA_BEFORE, 'region')):
        fr_now = regiondump.load_sequence(os.path.join(work('sponza-default'), 'region'), SPONZA_FRAME, SPONZA_FRAME)[0]
        fr_before = regiondump.load_sequence(os.path.join(SPONZA_BEFORE, 'region'), SPONZA_FRAME, SPONZA_FRAME)[0]
        s4 = identity(fr_now, fr_before)
        pa, pb = fr_now.world_positions('FRAME'), fr_before.world_positions('FRAME')
        pos_changed = ~(np.isclose(np.nan_to_num(pa, nan=-1e9), np.nan_to_num(pb, nan=-1e9), atol=1e-3).all(-1))
        da, db = chan(fr_now, 'direct_light')[..., :3], chan(fr_before, 'direct_light')[..., :3]
        direct_changed = np.abs(da - db).max(-1) > 1e-4
        cam_s = Camera(man_s.get('camera', {}).get('position', [0, 0, 0]), man_s.get('camera', {}).get('target', [0, 1, 0]))
        inside = np.zeros((H, W), dtype=bool)
        rects = {}
        ys, xs = np.mgrid[0:H, 0:W]
        for m in REPAIRED_SPONZA:
            r = screen_rect(mesh_bounds(os.path.join(REPO, 'sample', 'content', 'meshes', 'sponza', m + '.uzmesh')), cam_s)
            rects[m] = r
            if r:
                x0, y0, x1, y1 = r
                inside |= (xs >= x0 - 2) & (xs <= x1 + 2) & (ys >= y0 - 2) & (ys <= y1 + 2)
        pos_outside = int((pos_changed & ~inside).sum())
        s4.update({'positions_changed_px': int(pos_changed.sum()), 'positions_changed_outside_bounds_px': pos_outside,
                   'direct_changed_px': int(direct_changed.sum()), 'direct_changed_inside_bounds_fraction': float(direct_changed[inside].sum() / max(direct_changed.sum(), 1)),
                   'bounds_px': int(inside.sum()), 'screen_rects': rects})
        report['w4'] = s4
        print(f'  Sponza Camera C: positions changed {s4["positions_changed_px"]} px ({pos_outside} outside the repaired meshes\' bounds), direct changed {s4["direct_changed_px"]} px '
              f'({100 * s4["direct_changed_inside_bounds_fraction"]:.1f} % inside the bounds, which cover {s4["bounds_px"]} px); colour changed {s4["colour_changed_px"]} px', flush=True)
        check('W4 Sponza Camera C: positions identical outside the repaired meshes\' screen bounds; the direct-light changes reported with their share inside those bounds',
              pos_outside == 0, f'positions changed {s4["positions_changed_px"]} px, {pos_outside} outside the bounds; direct changed {s4["direct_changed_px"]} px, {100 * s4["direct_changed_inside_bounds_fraction"]:.1f} % inside')
    else:
        check('W4 Sponza capture of the build before available', False, SPONZA_BEFORE)

    report['montage'] = montage(args.out, [('chrome sphere, before (B.5b capture)', os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-env-mis', 'chrome-on')),
                                           ('chrome sphere, after the winding repair', work('chrome')),
                                           ('Sponza Camera C, before', SPONZA_BEFORE), ('Sponza Camera C, after', work('sponza-default'))], 'mesh_winding_montage')
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'mesh_winding.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
