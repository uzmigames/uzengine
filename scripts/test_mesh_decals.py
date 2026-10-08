"""Mesh decals (Sponza's dirt quads): a material graph in the Decal domain on a mesh blends its colour into
the G-buffer by its opacity, after the opaque geometry, writing no depth, normal or position and casting no
shadow. Before, the quads were masked at a third of their opacity and drawn opaque: grey blotches.

References: `scripts/decal_reference.py` (a grey wall, a decal quad 3 cm in front with an opacity ramp along
world x, a sun from the camera's side, the wall alone as the control) and Sponza Camera A / C.

Gates (pre-registered in the Rulebook task):
  D1  blend identity: along the ramp the lit colour is lerp(wall, decal, alpha), the wall from the control
      capture at the same pixel and the decal from the ramp's opaque end: median relative deviation <= 0.03,
      R^2 of the luminance against alpha >= 0.99 (before: a step at alpha 0.333).
  D2  no geometry: over the footprint the G-buffer positions lie on the wall plane (|y| <= 0.5 cm; before
      3 cm in front) and the normals are the wall's (dot >= 0.999).
  D3  no shadow: the direct light outside the footprint equals the control's (max |diff| <= 1e-4; before the
      quad's shadow band beside it).
  D4  Sponza Camera A and C: positions change only inside the decal meshes' footprint (the pixels whose world
      position lies in the meshes' world bounds, dilated by 5 cm); the G-buffer pass within 10 % of the build
      before (the direct-light changes reported).
  D5  Sponza's decal footprint is soft: the boundary contrast of the lit colour (the mean |luminance| step
      between the footprint's inner and outer rings) <= 0.5 x the build before (reported with the montage).

Usage: python scripts/test_mesh_decals.py [--out DIR] [--reuse] [--before]
  --before: the capture set of the binary before the item (D1-D3 measured on it; the Sponza captures become
  the references of the after run).
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
import decal_reference as ref  # noqa: E402
from test_layered_reflections import Camera, capture, colour, frames, FOG_PROJECT  # noqa: E402
from test_env_mis import lum, chan, capture_sponza, identity, montage, SPONZA_FRAME, FIRST, REF_FRAME  # noqa: E402
from test_mesh_winding import world_footprint  # noqa: E402

REPO = runner.REPO
W, H = 1920, 1080
DECAL_MESHES = ['decals_1st_floor1_dirt_decal_0', 'Materials_dirt_decal_0']
D1_MAX_DEV, D1_MIN_R2, D2_MAX_CM, D2_MIN_DOT, D3_MAX, D4_COST, D5_RATIO = 0.03, 0.99, 0.5, 0.999, 1e-4, 1.10, 0.5
REC709 = np.array([0.2126, 0.7152, 0.0722])


def footprint(pos, eye):
    """Pixels whose position lies in the decal's x / z range on (or just in front of) the wall."""
    (x0, _, z0), (x1, _, z1) = ref.DECAL
    ok = np.isfinite(pos).all(-1) & (np.linalg.norm(pos - np.asarray(eye), axis=-1) > 1.0)
    return ok & (pos[..., 0] > x0) & (pos[..., 0] < x1) & (pos[..., 2] > z0) & (pos[..., 2] < z1) & (pos[..., 1] > -5.0) & (pos[..., 1] < 1.0)


def erode(m, r):
    out = m.copy()
    for k in range(1, r + 1):
        for ax in (0, 1):
            out &= np.roll(m, k, ax) & np.roll(m, -k, ax)
    return out


def dilate(m, r):
    out = m.copy()
    for k in range(1, r + 1):
        for ax in (0, 1):
            out |= np.roll(m, k, ax) | np.roll(m, -k, ax)
    return out


def boundary_contrast(lum_img, mask):
    inner = mask & ~erode(mask, 2)
    outer = dilate(mask, 3) & ~mask
    if not inner.any() or not outer.any():
        return None
    return float(abs(lum_img[inner].mean() - lum_img[outer].mean()) / max(lum_img[outer].mean(), 1e-6))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-mesh-decals'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--before', action='store_true')
    args = ap.parse_args()
    results, report = [], {'before_mode': args.before}
    before_dir = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-mesh-decals', 'before')

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    def work(name):
        return os.path.join(args.out, name)

    k = REF_FRAME - FIRST
    man_on = capture(work('decal'), ref.PROJECT, args.reuse, {})
    man_wall = capture(work('wall'), ref.PROJECT_WALL, args.reuse, {})
    fr_on, fr_wall = frames(work('decal'))[k], frames(work('wall'))[k]
    cam = Camera(man_on.get('camera', {}).get('position', ref.CAMERA_EYE), man_on.get('camera', {}).get('target', ref.CAMERA_TARGET), fov_deg=ref.FOV)
    pos_on, pos_wall = fr_on.world_positions('FRAME'), fr_wall.world_positions('FRAME')
    c_on, c_wall = colour(fr_on), colour(fr_wall)
    fp = erode(footprint(pos_on, cam.eye), 3)

    # ── D1: the blend identity along the ramp ──
    alpha = ref.alpha_of_x(pos_on[..., 0])
    opaque_end = fp & (alpha > 0.97)
    c_decal = np.median(c_on[opaque_end], axis=0) if opaque_end.any() else np.zeros(3)
    pred = c_wall * (1.0 - alpha)[..., None] + c_decal[None, None, :] * alpha[..., None]
    sel = fp & (lum(pred) > 1e-3)
    dev = np.abs(lum(c_on) - lum(pred)) / np.maximum(lum(pred), 1e-6)
    a, l = alpha[sel], lum(c_on)[sel]
    if sel.sum() > 1000:
        A = np.stack([a, np.ones_like(a)], 1)
        coef, res, *_ = np.linalg.lstsq(A, l, rcond=None)
        r2 = 1.0 - float(((l - A @ coef) ** 2).sum() / max(((l - l.mean()) ** 2).sum(), 1e-12))
    else:
        r2 = None
    report['d1'] = {'px': int(sel.sum()), 'median_rel_dev': float(np.median(dev[sel])) if sel.any() else None, 'p90_rel_dev': float(np.percentile(dev[sel], 90)) if sel.any() else None,
                    'r2_lum_vs_alpha': r2, 'decal_colour_lum': float(lum(c_decal)), 'wall_colour_lum_median': float(np.median(lum(c_wall)[fp])) if fp.any() else None}
    print(f'  ramp: {int(sel.sum())} px, median deviation {report["d1"]["median_rel_dev"]}, R2 {r2}, decal end lum {report["d1"]["decal_colour_lum"]:.4f}, wall lum {report["d1"]["wall_colour_lum_median"]}', flush=True)
    check('D1 blend identity: the lit colour along the ramp is lerp(wall, decal, alpha) (median relative deviation <= 0.03, R2 >= 0.99)',
          sel.sum() > 1000 and report['d1']['median_rel_dev'] <= D1_MAX_DEV and r2 is not None and r2 >= D1_MIN_R2,
          f'median deviation {report["d1"]["median_rel_dev"]} (p90 {report["d1"]["p90_rel_dev"]}), R2 {r2} over {int(sel.sum())} px')

    # ── D2: no geometry ──
    n_on = chan(fr_on, 'lighting_normal')[..., :3]
    y_abs = np.abs(pos_on[..., 1])
    dot_n = -n_on[..., 1]
    report['d2'] = {'px': int(fp.sum()), 'y_median_cm': float(np.median(y_abs[fp])), 'y_p90_cm': float(np.percentile(y_abs[fp], 90)), 'normal_dot_median': float(np.median(dot_n[fp])),
                    'normal_dot_p10': float(np.percentile(dot_n[fp], 10))}
    check('D2 no geometry: under the decal the G-buffer positions lie on the wall plane (|y| <= 0.5 cm) and the normals are the wall\'s (dot >= 0.999)',
          report['d2']['y_p90_cm'] <= D2_MAX_CM and report['d2']['normal_dot_p10'] >= D2_MIN_DOT,
          f'|y| median {report["d2"]["y_median_cm"]:.3f} cm (p90 {report["d2"]["y_p90_cm"]:.3f}), normal dot median {report["d2"]["normal_dot_median"]:.4f} (p10 {report["d2"]["normal_dot_p10"]:.4f}) over {int(fp.sum())} px')

    # ── D3: no shadow ──
    d_on, d_wall = chan(fr_on, 'direct_light')[..., :3], chan(fr_wall, 'direct_light')[..., :3]
    outside = ~dilate(footprint(pos_on, cam.eye) | footprint(pos_wall, cam.eye), 4)
    diff = np.abs(d_on - d_wall).max(-1)
    report['d3'] = {'outside_px': int(outside.sum()), 'direct_max_diff_outside': float(diff[outside].max()), 'direct_changed_outside_px': int((diff[outside] > D3_MAX).sum()),
                    'direct_max_diff_inside': float(diff[~outside].max())}
    check('D3 no shadow: the direct light outside the decal footprint equals the control\'s (max |diff| <= 1e-4)', report['d3']['direct_max_diff_outside'] <= D3_MAX,
          f'max |diff| outside {report["d3"]["direct_max_diff_outside"]:.3g} ({report["d3"]["direct_changed_outside_px"]} px above 1e-4); inside {report["d3"]["direct_max_diff_inside"]:.3g}')

    # ── Sponza ──
    man_a = capture(work('sponza-camera-a'), FOG_PROJECT, args.reuse, {'r.SSR': 0}, frames=66, first=60, last=60, shot=60)
    man_c = capture_sponza(work('sponza-default'), args.reuse, sponza.PROJECT_CVARS)
    if not args.before:
        ref_a, ref_c = os.path.join(before_dir, 'sponza-camera-a'), os.path.join(before_dir, 'sponza-default')
        if os.path.isdir(os.path.join(ref_a, 'region')) and os.path.isdir(os.path.join(ref_c, 'region')):
            rows = {}
            for key, now, bef, frame_n in (('camera_a', work('sponza-camera-a'), ref_a, 60), ('camera_c', work('sponza-default'), ref_c, SPONZA_FRAME)):
                fr_n = regiondump.load_sequence(os.path.join(now, 'region'), frame_n, frame_n)[0]
                fr_b = regiondump.load_sequence(os.path.join(bef, 'region'), frame_n, frame_n)[0]
                man = man_a if key == 'camera_a' else man_c
                pa, pb = fr_n.world_positions('FRAME'), fr_b.world_positions('FRAME')
                pos_changed = ~(np.isclose(np.nan_to_num(pa, nan=-1e9), np.nan_to_num(pb, nan=-1e9), atol=0.5).all(-1))
                # the decal meshes' footprint: pixels whose position (before: the quad's; after: the wall's behind it)
                # lies in the meshes' world bounds, dilated by 5 cm
                inside = world_footprint(pb, DECAL_MESHES) | world_footprint(pa, DECAL_MESHES)
                da, db = chan(fr_n, 'direct_light')[..., :3], chan(fr_b, 'direct_light')[..., :3]
                direct_changed = np.abs(da - db).max(-1) > 1e-4
                la, lb = lum(colour(fr_n)), lum(colour(fr_b))
                mask = pos_changed & inside
                rows[key] = {'positions_changed_px': int(pos_changed.sum()), 'positions_changed_outside_bounds_px': int((pos_changed & ~inside).sum()), 'bounds_px': int(inside.sum()),
                             'direct_changed_px': int(direct_changed.sum()), 'direct_changed_inside_fraction': float(direct_changed[inside].sum() / max(direct_changed.sum(), 1)),
                             'footprint_px': int(mask.sum()), 'boundary_contrast_before': boundary_contrast(lb, mask), 'boundary_contrast_after': boundary_contrast(la, mask),
                             'gbuffer_ms_before': None, 'gbuffer_ms_after': (man.get('gpu_ms') or {}).get('GBuffer')}
                try:
                    rows[key]['gbuffer_ms_before'] = (json.load(open(os.path.join(bef, 'manifest.json'))).get('gpu_ms') or {}).get('GBuffer')
                except Exception:
                    pass
                print(f'  Sponza {key}: positions changed {rows[key]["positions_changed_px"]} px ({rows[key]["positions_changed_outside_bounds_px"]} outside the decal bounds of {rows[key]["bounds_px"]} px), '
                      f'direct changed {rows[key]["direct_changed_px"]} px ({100 * rows[key]["direct_changed_inside_fraction"]:.1f} % inside); boundary contrast {rows[key]["boundary_contrast_before"]} -> {rows[key]["boundary_contrast_after"]}; '
                      f'GBuffer {rows[key]["gbuffer_ms_before"]} -> {rows[key]["gbuffer_ms_after"]} ms', flush=True)
            report['d4'] = rows
            ok_pos = all(r['positions_changed_outside_bounds_px'] == 0 for r in rows.values())
            costs = [(r['gbuffer_ms_before'], r['gbuffer_ms_after']) for r in rows.values() if r['gbuffer_ms_before'] and r['gbuffer_ms_after']]
            ok_cost = all(a <= D4_COST * b for b, a in costs) if costs else False
            check('D4 Sponza Camera A and C: positions change only inside the decal meshes\' world-bounds footprint; the GBuffer pass within 10 % of the build before',
                  ok_pos and ok_cost, '; '.join(f'{k}: {r["positions_changed_px"]} px changed, {r["positions_changed_outside_bounds_px"]} outside, GBuffer {r["gbuffer_ms_before"]} -> {r["gbuffer_ms_after"]} ms' for k, r in rows.items()))
            cs = [(r['boundary_contrast_before'], r['boundary_contrast_after']) for r in rows.values() if r['boundary_contrast_before'] and r['boundary_contrast_after'] is not None and r['footprint_px'] > 500]
            check('D5 Sponza\'s decal footprint is soft: the boundary contrast of the lit colour <= 0.5 x the build before',
                  bool(cs) and all(a <= D5_RATIO * b for b, a in cs), '; '.join(f'{b:.3f} -> {a:.3f}' for b, a in cs) if cs else 'no footprint')
        else:
            check('D4 Sponza captures of the build before available', False, before_dir)
        report['montage'] = montage(args.out, [('reference, before (masked)', os.path.join(before_dir, 'decal')), ('reference, after (blended)', work('decal')),
                                               ('Sponza Camera A, before', os.path.join(before_dir, 'sponza-camera-a')), ('Sponza Camera A, after', work('sponza-camera-a'))], 'mesh_decals_montage')

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'mesh_decals.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
