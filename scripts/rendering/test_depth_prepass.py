"""Depth prepass — gates of lighting block B, item 4c (the side finding of B.5: the GBuffer discarded the
depth prepass).

Before this item the prepass drew nothing: it rendered with the shadow vertex shader (whose view-projection
lives in a constant buffer the pass never bound) and a LessEqual depth test against a reversed-Z clear
of 0.0, then the GBuffer cleared the depth anyway. It cost 0.17 ms on Sponza (QA configuration, 1080p)
and changed no pixel. Now the prepass is the GBuffer pass in a depth-only mode (the GBuffer's own vertex
shader and constants, so the clip-space depths are the same bits), the GBuffer keeps that depth and
draws standard opaque geometry with an Equal test and no depth write (masked and material-graph draws,
which the prepass skips, keep Greater with writes), and `r.DepthPrepass` defaults to the configuration
the measurements favour.

Scenes: scripts/rendering/lights_reference.py variants `lights-reference-overdraw` (48 walls facing the camera, drawn
far to near: every pixel of the stack is shaded once per wall without a prepass) and
`lights-reference-masked` (the mid wall as a Masked material with opacity 1: it must come from the
GBuffer's masked pipeline, never from the prepass); Sponza Camera C (QA configuration).

  Z1  identity (Sponza): with r.DepthPrepass 1 the G-buffer equals r.DepthPrepass 0 bit for bit: world
      positions max |diff| 0 and direct_light max |diff| 0 at frame 120 (the Equal test rejected no fragment).
  Z1b identity (overdraw and masked scenes): the same, at frame 60.
  Z2  the prepass depth is used: on the overdraw scene, GBuffer(prepass on) <= 0.7 x GBuffer(prepass off)
      (the committed build read 0.174 vs 0.180 ms: no effect).
  Z3  the default is the cheaper configuration on Sponza: DepthPrepass + GBuffer with the prepass on, against
      GBuffer alone, and the shipped default (`r.DepthPrepass` unset) matches the smaller sum (ties go to off).
  Z4  Sponza unchanged with the default: positions and direct_light identical to the committed capture
      (test-depth-prepass/before/sponza-noprepass, 794b632f); colour within block B.6's envelope.
  Reported: the prepass cost itself on both scenes, the GBuffer cost with and without it.
Usage: python scripts/rendering/test_depth_prepass.py [--out DIR] [--reuse]
"""
from __future__ import annotations
import argparse
import json
import os
import sys
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regiondump  # noqa: E402
import runner  # noqa: E402
import lights_reference as ref  # noqa: E402
import run_probe_side_sponza as sponza  # noqa: E402

REPO = runner.REPO
BEFORE = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-depth-prepass', 'before')
W, H = 1920, 1080
REC709 = np.array([0.2126, 0.7152, 0.0722])
Z2_RATIO = 0.7
ENVELOPE_PX, ENVELOPE_MED = 90656, 0.034
BASE = {'r.TechPanel.Show': 0, 'r.Toolbar.Show': 0}


def capture(work, project, reuse, cvars, frames, rf, tf):
    if reuse and os.path.isfile(os.path.join(work, 'manifest.json')):
        return json.load(open(os.path.join(work, 'manifest.json')))
    res = runner.run_capture(work, project, frames, regions=[('FRAME', 0, 0, W, H)], region_fields='components', region_first=rf, region_last=rf,
                             cvars={**BASE, **cvars}, timing_first=tf, clean=True, timeout=900, screenshot_frames=str(rf))
    if res['exit_code'] != 0:
        raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    return json.load(open(os.path.join(work, 'manifest.json')))


def frame(work, rf):
    return regiondump.load_sequence(os.path.join(work, 'region'), rf, rf)[0]


def gpu(man, key):
    return (man.get('gpu_ms') or {}).get(key)


def identity(fa, fb):
    pa, pb = fa.world_positions('FRAME'), fb.world_positions('FRAME')
    da = fa.rect('FRAME').channel('direct_light')[..., :3].astype(np.float64)
    db = fb.rect('FRAME').channel('direct_light')[..., :3].astype(np.float64)
    ca = fa.rect('FRAME').channel('taa_output')[..., :3].astype(np.float64)
    cb = fb.rect('FRAME').channel('taa_output')[..., :3].astype(np.float64)
    geom_a = np.isfinite(pa).all(-1).sum()
    return {'pos_max_diff': float(np.nanmax(np.abs(pa - pb))), 'direct_max_diff': float(np.abs(da - db).max()),
            'colour_max_diff': float(np.abs(ca - cb).max()), 'colour_changed_px': int((np.abs(ca - cb).max(-1) > 1e-3).sum()),
            'colour_median_rel': float(np.median((np.abs(ca - cb) @ REC709)[(cb @ REC709) > 1e-3] / (cb @ REC709)[(cb @ REC709) > 1e-3])) if ((cb @ REC709) > 1e-3).any() else 0.0,
            'geometry_px': int(geom_a)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-depth-prepass'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    qa = {**sponza.PROJECT_CVARS}
    print('sponza: prepass on / off / default; overdraw: on / off; masked: on / off', flush=True)
    man_on = capture(os.path.join(args.out, 'sponza-on'), sponza.QA_PROJECT, args.reuse, {**qa, 'r.DepthPrepass': 1}, 126, 120, 60)
    man_off = capture(os.path.join(args.out, 'sponza-off'), sponza.QA_PROJECT, args.reuse, {**qa, 'r.DepthPrepass': 0}, 126, 120, 60)
    man_def = capture(os.path.join(args.out, 'sponza-default'), sponza.QA_PROJECT, args.reuse, qa, 126, 120, 60)
    man_ov_on = capture(os.path.join(args.out, 'overdraw-on'), ref.project_path('lights-reference-overdraw'), args.reuse, {'r.DepthPrepass': 1}, 66, 60, 40)
    man_ov_off = capture(os.path.join(args.out, 'overdraw-off'), ref.project_path('lights-reference-overdraw'), args.reuse, {'r.DepthPrepass': 0}, 66, 60, 40)
    capture(os.path.join(args.out, 'masked-on'), ref.project_path('lights-reference-masked'), args.reuse, {'r.DepthPrepass': 1}, 66, 60, 40)
    capture(os.path.join(args.out, 'masked-off'), ref.project_path('lights-reference-masked'), args.reuse, {'r.DepthPrepass': 0}, 66, 60, 40)

    # Z1 identity on Sponza
    s = identity(frame(os.path.join(args.out, 'sponza-on'), 120), frame(os.path.join(args.out, 'sponza-off'), 120))
    report['z1'] = s
    check('Z1 identity (Sponza): with the prepass the G-buffer positions and the direct light are bit-identical to no prepass',
          s['pos_max_diff'] == 0.0 and s['direct_max_diff'] == 0.0,
          f'positions max |diff| {s["pos_max_diff"]:.3g}, direct {s["direct_max_diff"]:.3g}, colour max |diff| {s["colour_max_diff"]:.3g} ({s["colour_changed_px"]} px > 1e-3), geometry {s["geometry_px"]} px')
    # Z1b identity on the reference scenes
    z1b = {}
    for scene in ('overdraw', 'masked'):
        z1b[scene] = identity(frame(os.path.join(args.out, scene + '-on'), 60), frame(os.path.join(args.out, scene + '-off'), 60))
    report['z1b'] = z1b
    check('Z1b identity (overdraw stack, masked wall): positions and direct light bit-identical with and without the prepass',
          all(v['pos_max_diff'] == 0.0 and v['direct_max_diff'] == 0.0 for v in z1b.values()),
          '; '.join(f'{k}: positions {v["pos_max_diff"]:.3g}, direct {v["direct_max_diff"]:.3g}, colour {v["colour_max_diff"]:.3g}' for k, v in z1b.items()))

    # Z2 the prepass depth is used (overdraw)
    t = {'overdraw_gbuffer_on': gpu(man_ov_on, 'GBuffer'), 'overdraw_gbuffer_off': gpu(man_ov_off, 'GBuffer'), 'overdraw_prepass': gpu(man_ov_on, 'DepthPrepass'),
         'sponza_gbuffer_on': gpu(man_on, 'GBuffer'), 'sponza_gbuffer_off': gpu(man_off, 'GBuffer'), 'sponza_prepass': gpu(man_on, 'DepthPrepass'),
         'sponza_frame_on': gpu(man_on, '_frame'), 'sponza_frame_off': gpu(man_off, '_frame'), 'overdraw_frame_on': gpu(man_ov_on, '_frame'), 'overdraw_frame_off': gpu(man_ov_off, '_frame')}
    report['timings'] = t
    check('Z2 the prepass depth is used: on the overdraw stack GBuffer(prepass on) <= 0.7 x GBuffer(prepass off)',
          t['overdraw_gbuffer_on'] is not None and t['overdraw_gbuffer_off'] is not None and t['overdraw_gbuffer_on'] <= Z2_RATIO * t['overdraw_gbuffer_off'],
          f'GBuffer {t["overdraw_gbuffer_on"]:.3f} ms with the prepass ({t["overdraw_prepass"]:.3f} ms) vs {t["overdraw_gbuffer_off"]:.3f} ms without; frame {t["overdraw_frame_on"]:.3f} vs {t["overdraw_frame_off"]:.3f} ms')

    # Z3 the default is the cheaper configuration on Sponza
    sum_on = (t['sponza_prepass'] or 0.0) + (t['sponza_gbuffer_on'] or 0.0)
    sum_off = t['sponza_gbuffer_off'] or 0.0
    cheaper = 'on' if sum_on < sum_off else 'off'
    default_prepass = int((man_def.get('cvars') or {}).get('r.DepthPrepass', -1))
    report['z3'] = {'sum_on': sum_on, 'sum_off': sum_off, 'cheaper': cheaper, 'default_r_DepthPrepass': default_prepass}
    check('Z3 the shipped default is the cheaper configuration on Sponza (prepass + GBuffer vs GBuffer alone; ties go to off)',
          (default_prepass == 1) == (cheaper == 'on'),
          f'prepass on {sum_on:.3f} ms (prepass {t["sponza_prepass"]:.3f} + GBuffer {t["sponza_gbuffer_on"]:.3f}) vs off {sum_off:.3f} ms; cheaper {cheaper}; default r.DepthPrepass {default_prepass}')

    # Z4 Sponza unchanged with the default
    before = os.path.join(BEFORE, 'sponza-noprepass')
    if os.path.isdir(os.path.join(before, 'region')):
        s4 = identity(frame(os.path.join(args.out, 'sponza-default'), 120), frame(before, 120))
        report['z4'] = s4
        check('Z4 Sponza unchanged with the default: positions and direct_light identical to the committed capture; colour within the loading-state envelope',
              s4['pos_max_diff'] == 0.0 and s4['direct_max_diff'] == 0.0 and s4['colour_changed_px'] <= 1.2 * ENVELOPE_PX and s4['colour_median_rel'] <= 1.2 * ENVELOPE_MED,
              f'positions {s4["pos_max_diff"]:.3g}, direct {s4["direct_max_diff"]:.3g}, colour changed {s4["colour_changed_px"]} px (median rel {s4["colour_median_rel"]:.3f}; envelope {ENVELOPE_PX} px / {ENVELOPE_MED})')
    else:
        check('Z4 committed-build capture available', False, before)

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'depth_prepass.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
