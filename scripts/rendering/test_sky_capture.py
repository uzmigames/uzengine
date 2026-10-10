"""The sky cubemap capture's sun intensity (lighting block B.5c): the capture scaled the sun by the length of
the illuminance vector (sqrt(3) x the per-channel value of a white sun) while the sky pass scales it per
channel, so every image-based term (the prefiltered specular, the sky SH, the environment-MIS estimate, the
GI probes' sky) saw a sky brighter than the one the frame renders. The capture now takes the per-channel
illuminance.

References: the environment the sampler builds from, dumped by block B.5b's diagnostics
(`<work>/region/env_equirect.*`, cubemap space), against the sky the frame renders along the same
directions (the sky pixels of the mirrored view and of the warm-sun variant); the chrome floor against the
mirrored view (block B.5b's E1); the chrome floor under the PMREM and Sponza as controls.

Gates (pre-registered in the Rulebook task):
  S1  the capture is the sky pass: the dumped environment against the mirrored view's sky pixels along the
      same directions, the ratio sky pass / capture has a median within [0.95, 1.05] and a p10-p90 within
      [0.90, 1.10] (before: 0.578 at every elevation).
  S2  the chrome floor reflects the sky (block B.5b's E1): floor pixels whose reflected ray sees the sky match
      F x the mirrored view, median relative error <= 0.15 (before: 0.714).
  S3  a coloured sun, per channel: on the warm-sun variant (sun colour (1.0, 0.7, 0.4)) the ratio sky pass /
      capture per channel (r, g, b, medians over the sky pixels) lies within [0.95, 1.05] for each channel
      (before: a scalar capture, the ratios differ per channel).
  S4  Sponza: the direct light is identical to the capture of the build before the item; the indirect light
      darkens, never brightens (median ratio of diffuse_indirect over lit pixels within [0.5, 1.0]); the colour
      change is reported with a montage for the look decision.
  S5  control: the chrome floor under the PMREM (no atmosphere capture) is bit-identical to the build before
      (specular_indirect max |diff| = 0).
  Reported: the Sponza colour statistics, the environment's total luminance before / after, the montage.

Usage: python scripts/rendering/test_sky_capture.py [--out DIR] [--reuse] [--before]
  --before: measures S1-S3 on the binary before the item (the warm scene is captured; the mirror, chrome floor,
  PMREM floor and Sponza captures of block B.5b are reused).
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
import reflection_reference as ref  # noqa: E402
import run_probe_side_sponza as sponza  # noqa: E402
from test_layered_reflections import Camera, capture, colour, frames  # noqa: E402
from test_env_mis import (load_env, env_lookup, lum, chan, sky_class, floor_mask, pixel_shading_inputs,  # noqa: E402
                          capture_sponza, identity, montage, SPONZA_FRAME, FIRST, REF_FRAME)

REPO = runner.REPO
W, H = ref.W, ref.H
B05B = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-env-mis')
S1_LO, S1_HI, S1_P_LO, S1_P_HI = 0.95, 1.05, 0.90, 1.10
S2_MAX = 0.15
S3_LO, S3_HI = 0.95, 1.05
S4_LO, S4_HI = 0.5, 1.0
REC709 = np.array([0.2126, 0.7152, 0.0722])


def view_rays(cam):
    ys, xs = np.mgrid[0:H, 0:W]
    d = (cam.fwd[None, None] + cam.right[None, None] * (((xs + 0.5) / W * 2 - 1) / cam.w)[..., None]
         + cam.up[None, None] * ((1 - (ys + 0.5) / H * 2) / cam.h)[..., None])
    return d / np.linalg.norm(d, axis=-1, keepdims=True)


def sky_vs_env(work, man, fov=ref.FOV, min_elevation=0.02):
    """The frame's sky pixels (no geometry, above the horizon) against the dumped environment along the view
    rays: per-channel and luminance ratios sky pass / capture, by elevation band."""
    env = load_env(work)
    if env is None:
        return None
    fr = frames(work)[REF_FRAME - FIRST]
    cam = Camera(man.get('camera', {}).get('position', ref.CAMERA_EYE), man.get('camera', {}).get('target', ref.CAMERA_TARGET), fov_deg=fov)
    pos = fr.world_positions('FRAME')
    c = colour(fr)
    d = view_rays(cam)
    sky = ~(np.isfinite(pos).all(-1) & (np.linalg.norm(pos - cam.eye, axis=-1) > 1.0)) & (d[..., 2] > min_elevation)
    e = env_lookup(env, d[sky])
    cs = c[sky]
    r = lum(cs) / np.maximum(lum(e), 1e-6)
    out = {'px': int(sky.sum()), 'median_ratio': float(np.median(r)), 'p10_ratio': float(np.percentile(r, 10)), 'p90_ratio': float(np.percentile(r, 90)),
           'channel_median_ratio': [float(np.median(cs[:, k] / np.maximum(e[:, k], 1e-6))) for k in range(3)],
           'env_total_luminance': env['meta'].get('total_luminance'), 'bands': {}}
    el = np.degrees(np.arcsin(np.clip(d[sky][:, 2], -1, 1)))
    for lo, hi in ((0, 5), (5, 10), (10, 20), (20, 30), (30, 45), (45, 90)):
        m = (el >= lo) & (el < hi)
        if m.sum() > 100:
            out['bands'][f'{lo}-{hi}'] = {'px': int(m.sum()), 'median_ratio': float(np.median(r[m]))}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-sky-capture'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--before', action='store_true')
    args = ap.parse_args()
    results, report = [], {'before_mode': args.before}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    def work(name):
        return os.path.join(args.out, name)

    if args.before:
        # the block B.5b captures of the same binary stand in for everything but the warm scene
        dirs = {'mirror': os.path.join(B05B, 'mirror'), 'ref-on': os.path.join(B05B, 'ref-on'), 'hdri-on': os.path.join(B05B, 'hdri-on'),
                'sponza-default': os.path.join(B05B, 'sponza-default'), 'warm': work('warm')}
        mans = {k: json.load(open(os.path.join(v, 'manifest.json'))) for k, v in dirs.items() if k != 'warm'}
        mans['warm'] = capture(work('warm'), ref.variant_project(ref.WARM_VARIANT), args.reuse, {'r.SSR': 0})
    else:
        dirs = {k: work(k) for k in ('mirror', 'ref-on', 'hdri-on', 'sponza-default', 'warm')}
        mans = {'mirror': capture(work('mirror'), ref.PROJECT_MIRROR, args.reuse, {'r.SSR': 0}),
                'ref-on': capture(work('ref-on'), ref.PROJECT, args.reuse, {'r.SSR': 0}),
                'warm': capture(work('warm'), ref.variant_project(ref.WARM_VARIANT), args.reuse, {'r.SSR': 0}),
                'hdri-on': capture(work('hdri-on'), ref.variant_project(ref.HDRI_VARIANT), args.reuse, {'r.SSR': 0}),
                'sponza-default': capture_sponza(work('sponza-default'), args.reuse, sponza.PROJECT_CVARS)}

    # ── S1: the capture against the sky pass (mirrored view) ──
    s1 = sky_vs_env(dirs['mirror'], mans['mirror'])
    report['s1'] = s1
    if s1 is None:
        check('S1 the capture is the sky pass: dumped environment vs the mirrored view sky along the same rays, ratio median in [0.95, 1.05], p10-p90 in [0.90, 1.10]', False, 'no environment dump')
    else:
        print('  mirrored view sky vs capture: ' + ', '.join(f'{k} {v["median_ratio"]:.3f}' for k, v in s1['bands'].items()), flush=True)
        check('S1 the capture is the sky pass: dumped environment vs the mirrored view sky along the same rays, ratio median in [0.95, 1.05], p10-p90 in [0.90, 1.10]',
              S1_LO <= s1['median_ratio'] <= S1_HI and S1_P_LO <= s1['p10_ratio'] and s1['p90_ratio'] <= S1_P_HI,
              f'median {s1["median_ratio"]:.3f} (p10 {s1["p10_ratio"]:.3f}, p90 {s1["p90_ratio"]:.3f}) over {s1["px"]} px; environment total luminance {s1["env_total_luminance"]}')

    # ── S2: the chrome floor reflects the sky (block B.5b's E1) ──
    k = REF_FRAME - FIRST
    fr_mirror, fr_ref = frames(dirs['mirror'])[k], frames(dirs['ref-on'])[k]
    cls = sky_class(fr_ref, fr_mirror, mans['ref-on'], mans['mirror'])
    ref_lum = lum(cls['c_ref']) * cls['F']
    c = colour(fr_ref)
    e2 = np.abs(lum(c) - ref_lum) / np.maximum(ref_lum, 1e-6)
    r2 = lum(c) / np.maximum(ref_lum, 1e-6)
    sky = cls['sky']
    report['s2'] = {'median_rel_err': float(np.median(e2[sky])), 'median_ratio': float(np.median(r2[sky])), 'px': int(sky.sum()),
                    'p10_ratio': float(np.percentile(r2[sky], 10)), 'p90_ratio': float(np.percentile(r2[sky], 90))}
    check('S2 the chrome floor reflects the sky: floor pixels whose reflected ray sees the sky match F x the mirrored view (median relative error <= 0.15)',
          sky.sum() >= 10000 and report['s2']['median_rel_err'] <= S2_MAX,
          f'median error {report["s2"]["median_rel_err"]:.3f} (ratio {report["s2"]["median_ratio"]:.3f}, p10 {report["s2"]["p10_ratio"]:.3f}, p90 {report["s2"]["p90_ratio"]:.3f}) over {sky.sum()} px')

    # ── S3: a coloured sun, per channel ──
    s3 = sky_vs_env(dirs['warm'], mans['warm'])
    report['s3'] = s3
    if s3 is None:
        check('S3 a coloured sun: the ratio sky pass / capture per channel within [0.95, 1.05] on the warm-sun variant', False, 'no environment dump')
    else:
        ch = s3['channel_median_ratio']
        check('S3 a coloured sun: the ratio sky pass / capture per channel within [0.95, 1.05] on the warm-sun variant',
              all(S3_LO <= v <= S3_HI for v in ch), f'r {ch[0]:.3f}, g {ch[1]:.3f}, b {ch[2]:.3f} over {s3["px"]} px (luminance median {s3["median_ratio"]:.3f})')

    if not args.before:
        # ── S4: Sponza ──
        before_sp = os.path.join(B05B, 'sponza-default')
        if os.path.isdir(os.path.join(before_sp, 'region')):
            fr_now = regiondump.load_sequence(os.path.join(dirs['sponza-default'], 'region'), SPONZA_FRAME, SPONZA_FRAME)[0]
            fr_before = regiondump.load_sequence(os.path.join(before_sp, 'region'), SPONZA_FRAME, SPONZA_FRAME)[0]
            s4 = identity(fr_now, fr_before)
            di_now, di_before = chan(fr_now, 'diffuse_indirect')[..., :3], chan(fr_before, 'diffuse_indirect')[..., :3]
            si_now, si_before = chan(fr_now, 'specular_indirect')[..., :3], chan(fr_before, 'specular_indirect')[..., :3]
            lit = lum(di_before) > 1e-3
            r_di = lum(di_now)[lit] / lum(di_before)[lit]
            lit_s = lum(si_before) > 1e-4
            r_si = lum(si_now)[lit_s] / lum(si_before)[lit_s]
            c_now, c_before = colour(fr_now), colour(fr_before)
            lc = lum(c_before) > 1e-3
            s4.update({'diffuse_indirect_ratio_median': float(np.median(r_di)), 'diffuse_indirect_ratio_p10': float(np.percentile(r_di, 10)),
                       'diffuse_indirect_ratio_p90': float(np.percentile(r_di, 90)), 'specular_indirect_ratio_median': float(np.median(r_si)),
                       'colour_ratio_median': float(np.median(lum(c_now)[lc] / lum(c_before)[lc])), 'colour_mean_signed_delta': float(np.mean((lum(c_now) - lum(c_before))[lc])),
                       'colour_median_lum_before': float(np.median(lum(c_before)[lc])), 'colour_median_lum_after': float(np.median(lum(c_now)[lc]))})
            report['s4'] = s4
            print(f'  Sponza: direct max |diff| {s4["direct_max_diff"]:.3g}; diffuse_indirect ratio median {s4["diffuse_indirect_ratio_median"]:.3f} '
                  f'(p10 {s4["diffuse_indirect_ratio_p10"]:.3f}, p90 {s4["diffuse_indirect_ratio_p90"]:.3f}); specular_indirect ratio {s4["specular_indirect_ratio_median"]:.3f}; '
                  f'colour ratio median {s4["colour_ratio_median"]:.3f}, {s4["colour_changed_px"]} px changed', flush=True)
            check('S4 Sponza: direct light identical to the build before the item; the indirect light darkens, never brightens (diffuse_indirect ratio median within [0.5, 1.0])',
                  s4['pos_max_diff'] <= 1e-6 and s4['direct_max_diff'] <= 1e-6 and S4_LO <= s4['diffuse_indirect_ratio_median'] <= S4_HI,
                  f'positions {s4["pos_max_diff"]:.3g}, direct {s4["direct_max_diff"]:.3g}; diffuse_indirect ratio {s4["diffuse_indirect_ratio_median"]:.3f}; colour ratio {s4["colour_ratio_median"]:.3f} over {int(lc.sum())} lit px')
        else:
            check('S4 Sponza capture of the build before the item available', False, before_sp)

        # ── S5: the PMREM floor is untouched ──
        before_h = os.path.join(B05B, 'hdri-on')
        if os.path.isdir(os.path.join(before_h, 'region')):
            fr_h_now, fr_h_before = frames(dirs['hdri-on'])[k], frames(before_h)[k]
            dh = float(np.abs(chan(fr_h_now, 'specular_indirect')[..., :3] - chan(fr_h_before, 'specular_indirect')[..., :3]).max())
            report['s5'] = {'specular_max_diff': dh, 'colour_max_diff': float(np.abs(colour(fr_h_now) - colour(fr_h_before)).max())}
            check('S5 control: the chrome floor under the PMREM is bit-identical to the build before the item (specular_indirect max |diff| = 0)', dh == 0.0,
                  f'specular max |diff| {dh:.3g}, colour max |diff| {report["s5"]["colour_max_diff"]:.3g}')
        else:
            check('S5 PMREM floor capture of the build before the item available', False, before_h)

        panels = [('Sponza Camera C, before (B.5b build)', before_sp), ('Sponza Camera C, after', dirs['sponza-default']),
                  ('mirrored view (the sky pass)', dirs['mirror']), ('chrome floor, before', os.path.join(B05B, 'ref-on')), ('chrome floor, after', dirs['ref-on']),
                  ('warm sun, after', dirs['warm'])]
        report['montage'] = montage(args.out, panels, 'sky_capture_montage')

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'sky_capture.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
