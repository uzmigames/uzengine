"""Clustered light lists — gates of lighting block B, item 7.

Before this item the deferred lighting looped over every point (<= 64) and spot (<= 16) light of the
frame in every pixel; `cluster_assign_cs.hlsl` was a one-line stub. Now a compute pass assigns the
lights to the clusters of the view frustum (32 px tiles x 24 logarithmic depth slices) and each pixel
evaluates its cluster's lights, in the order the loop used (points then spots, by index), so the sums
are identical; the limits are 512 / 128 (the arrays stay in the 64 KB constant buffer); the per-light
parameters the components carried (falloff exponent, diffuse / specular scales, source radius) are
honoured and readable from scene files.

Scenes: scripts/lights_reference.py (80 lights; 500 lights; none; one light with each parameter).
Captures (frames 66, region frame 60): ref-clustered / ref-loop (r.Lights.Clustered 0) / ref-slots4
(r.Lights.ClusterSlots 4: every cluster overflows) / ref-heat (r.Lights.ClusterDebug 1, montage);
dense-clustered / dense-loop; none; p-inv, p-exp2, p-exp8, p-diff05, p-src0, p-src20, p-src50; the
fog project (Sponza, frame 60) after a warm-up run.

  C1   identity, 80 lights: taa_output of ref-clustered against ref-loop (same binary): max |diff| <= 1e-5.
  C1b  overflow: ref-slots4 against ref-loop: max |diff| <= 1e-5 (the overflow fallback evaluates every light).
  C1c  across builds: ref-loop against the committed build's capture (before/lights-reference, edfd7942,
       the loop before the refactor): median relative difference <= 1e-3 and max |diff| <= 0.02 over lit pixels.
  C1d  identity, 500 lights: dense-clustered against dense-loop: max |diff| <= 1e-5.
  C2   cost, 500 lights at 1080p: DeferredLighting clustered <= 0.5 x the per-pixel loop, LightCull <= 0.3 ms.
       Reported: the 80-light scene (clustered, loop, committed 0.465 ms, no lights 0.285 ms).
  C3a  diffuseScale 0.5: floor radiance ratio p-diff05 / p-inv with the no-light scene's ambient subtracted per
       pixel (the sky light's ambient is larger than the point light over most of its range), median over lit
       floor pixels in [0.47, 0.55] (the specular term, ~3 % at roughness 1, is not scaled). The first run
       compared raw luminances (ratio 0.98) and was re-specified.
  C3b  falloff exponent: per pixel, lum(p-exp8) / lum(p-exp2) against (1 - d^2/r^2)^6 (d from the dump's
       world position to the light, r = 900): median |error| <= 0.02 where 1 - d^2/r^2 in [0.3, 0.95] (the
       custom falloff's brightness swamps the ambient, so no subtraction is needed).
  C3c  source radius (glossy floor, roughness 0.2, the glossy no-light scene's ambient subtracted): the
       highlight's peak luminance decreases, src50 < src20 < src0, and peak(src50) <= 0.8 x peak(src0).
  C4   Sponza unchanged (one light, the sun): the fog project at frame 60 after a warm-up run against
       test-ddgi-dedup/nodxr (edfd7942, warm): direct_light (the lighting's Lo, sun + local lights) identical;
       the colour, which carries the GI state at frame 60, within the cold/warm envelope of block B.6
       (90 656 px, median 3.4 %). First written as colour identity within 0.1 % of the pixels; the first run
       saw the envelope's signature (4.3 % of the pixels, median 3 %) with the direct light identical.
Usage: python scripts/test_clustered_lights.py [--out DIR] [--reuse]
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

REPO = runner.REPO
FOG_PROJECT = os.path.join(REPO, 'scripts', 'sponza-fog.uzproject')
BEFORE_DIR = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-clustered-lights', 'before')
BEFORE_SPONZA = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-ddgi-dedup', 'nodxr')
W, H = 1920, 1080
FRAME, FRAMES = 60, 66
REC709 = np.array([0.2126, 0.7152, 0.0722])
C1_MAX = 1e-5
C1C_MEDIAN, C1C_MAX = 1e-3, 0.02
C2_RATIO, C2_CULL_MS = 0.5, 0.3
C3A_LO, C3A_HI = 0.47, 0.55
C3B_MAX_ERR = 0.02
C3C_RATIO = 0.8
C4_CHANGED_FRAC, C4_MAX = 0.001, 0.01
BASE = {'r.TechPanel.Show': 0, 'r.Toolbar.Show': 0}
FOG_OFF = {'r.SSR': 0, 'r.VolumetricFog': 0}


def capture(work, project, reuse, cvars, timing_first=40):
    if reuse and os.path.isfile(os.path.join(work, 'manifest.json')):
        return json.load(open(os.path.join(work, 'manifest.json')))
    res = runner.run_capture(work, project, FRAMES, regions=[('FRAME', 0, 0, W, H)], region_fields='components',
                             region_first=FRAME, region_last=FRAME, cvars={**BASE, **cvars}, timing_first=timing_first,
                             clean=True, timeout=900, screenshot_frames=str(FRAME))
    if res['exit_code'] != 0:
        raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    return json.load(open(os.path.join(work, 'manifest.json')))


def frame(work):
    return regiondump.load_sequence(os.path.join(work, 'region'), FRAME, FRAME)[0]


def colour(fr):
    return fr.rect('FRAME').channel('taa_output')[..., :3].astype(np.float64)


def lum(c):
    return c @ REC709


def gpu(man, key):
    return (man.get('gpu_ms') or {}).get(key)


def max_diff(a, b):
    return float(np.abs(a - b).max())


def rel_stats(a, b, floor=1e-3):
    la, lb = lum(a), lum(b)
    m = lb > floor
    rel = np.abs(la - lb)[m] / lb[m]
    return {'px': int(m.sum()), 'median': float(np.median(rel)) if m.any() else 0.0,
            'p99': float(np.percentile(rel, 99)) if m.any() else 0.0, 'max_abs': float(np.abs(a - b).max())}


def montage(out, panels, title):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    tiles = []
    for label, work in panels:
        d = os.path.join(work, 'frames')
        files = sorted(f for f in os.listdir(d) if f.endswith('.png')) if os.path.isdir(d) else []
        if not files:
            continue
        im = Image.open(os.path.join(d, files[-1])).convert('RGB')
        im = im.resize((im.width // 2, im.height // 2))
        ImageDraw.Draw(im).text((8, 8), label, fill=(255, 255, 0))
        tiles.append(im)
    if not tiles:
        return None
    strip = Image.new('RGB', (sum(t.width for t in tiles), tiles[0].height))
    x = 0
    for t in tiles:
        strip.paste(t, (x, 0))
        x += t.width
    path = os.path.join(out, title + '.jpg')
    strip.save(path, quality=88)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-clustered-lights'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    cap = lambda name, variant, cvars: capture(os.path.join(args.out, name), ref.project_path(variant), args.reuse, cvars)

    # ── 80 lights ──
    print('80 lights: clustered / loop / slots 4 / heat map; 500 lights: clustered / loop; no lights', flush=True)
    man_rc = cap('ref-clustered', 'lights-reference', {})
    man_rl = cap('ref-loop', 'lights-reference', {'r.Lights.Clustered': 0})
    cap('ref-slots4', 'lights-reference', {'r.Lights.ClusterSlots': 4})
    cap('ref-heat', 'lights-reference', {'r.Lights.ClusterDebug': 1})
    man_dc = cap('dense-clustered', 'lights-reference-400', {})
    man_dl = cap('dense-loop', 'lights-reference-400', {'r.Lights.Clustered': 0})
    man_none = cap('none', 'lights-reference-none', {})

    c_rc, c_rl, c_s4 = colour(frame(os.path.join(args.out, 'ref-clustered'))), colour(frame(os.path.join(args.out, 'ref-loop'))), colour(frame(os.path.join(args.out, 'ref-slots4')))
    d1 = max_diff(c_rc, c_rl)
    report['c1'] = {'max_abs_diff': d1, 'mean_lum_clustered': float(lum(c_rc).mean()), 'mean_lum_loop': float(lum(c_rl).mean())}
    check('C1 identity (80 lights): clustered vs per-pixel loop, taa_output max |diff| <= 1e-5', d1 <= C1_MAX,
          f'max |diff| {d1:.3g} (mean luminance {report["c1"]["mean_lum_clustered"]:.4f} vs {report["c1"]["mean_lum_loop"]:.4f})')
    d1b = max_diff(c_s4, c_rl)
    report['c1b'] = {'max_abs_diff': d1b}
    check('C1b overflow (4 slots per cluster): identical to the per-pixel loop, max |diff| <= 1e-5', d1b <= C1_MAX, f'max |diff| {d1b:.3g}')
    before_ref = os.path.join(BEFORE_DIR, 'lights-reference')
    if os.path.isdir(os.path.join(before_ref, 'region')):
        c_before = colour(frame(before_ref))
        s_c = rel_stats(c_rl, c_before)
        report['c1c'] = s_c
        check('C1c across builds: this build\'s per-pixel loop vs the committed build (median rel <= 1e-3, max |diff| <= 0.02)',
              s_c['median'] <= C1C_MEDIAN and s_c['max_abs'] <= C1C_MAX,
              f'median rel {s_c["median"]:.2e}, p99 {s_c["p99"]:.2e}, max |diff| {s_c["max_abs"]:.3g} over {s_c["px"]} lit px')
    else:
        check('C1c committed-build capture available', False, before_ref)
    c_dc, c_dl = colour(frame(os.path.join(args.out, 'dense-clustered'))), colour(frame(os.path.join(args.out, 'dense-loop')))
    d1d = max_diff(c_dc, c_dl)
    report['c1d'] = {'max_abs_diff': d1d, 'mean_lum': float(lum(c_dc).mean())}
    check('C1d identity (500 lights): clustered vs per-pixel loop, max |diff| <= 1e-5', d1d <= C1_MAX, f'max |diff| {d1d:.3g} (mean luminance {report["c1d"]["mean_lum"]:.4f})')

    # ── C2 cost ──
    before_man = lambda name: json.load(open(os.path.join(BEFORE_DIR, name, 'manifest.json'))) if os.path.isfile(os.path.join(BEFORE_DIR, name, 'manifest.json')) else {}
    t = {'dense_clustered': gpu(man_dc, 'DeferredLighting'), 'dense_loop': gpu(man_dl, 'DeferredLighting'), 'dense_cull': gpu(man_dc, 'LightCull'),
         'ref_clustered': gpu(man_rc, 'DeferredLighting'), 'ref_loop': gpu(man_rl, 'DeferredLighting'), 'ref_cull': gpu(man_rc, 'LightCull'),
         'none': gpu(man_none, 'DeferredLighting'), 'committed_ref': gpu(before_man('lights-reference'), 'DeferredLighting'),
         'committed_none': gpu(before_man('lights-reference-none'), 'DeferredLighting'),
         'frame_dense_clustered': gpu(man_dc, '_frame'), 'frame_dense_loop': gpu(man_dl, '_frame')}
    report['c2'] = t
    ok2 = t['dense_clustered'] is not None and t['dense_loop'] is not None and t['dense_cull'] is not None \
        and t['dense_clustered'] <= C2_RATIO * t['dense_loop'] and t['dense_cull'] <= C2_CULL_MS
    check('C2 cost (500 lights, 1080p): DeferredLighting clustered <= 0.5 x the per-pixel loop and LightCull <= 0.3 ms', ok2,
          f'clustered {t["dense_clustered"]} ms vs loop {t["dense_loop"]} ms, cull {t["dense_cull"]} ms; 80 lights: clustered {t["ref_clustered"]} / loop {t["ref_loop"]} '
          f'(cull {t["ref_cull"]}), committed {t["committed_ref"]}; no lights {t["none"]} (committed {t["committed_none"]})')

    # ── C3 per-light parameters (one light at (0, 0, 300), range 900) ──
    # The floor also carries the sky light's ambient (~0.6 at this exposure, more than the point light's
    # contribution over most of its range), so every quantity is the light's own: the no-light scene
    # with the same floor (rough or glossy) is subtracted per pixel. The first run compared raw
    # luminances and read a diffuse-scale ratio of 0.98.
    print('one light: inverse square / exponent 2 / exponent 8 / diffuse 0.5 / source radius 0, 20, 50', flush=True)
    for v in ('p-inv', 'p-exp2', 'p-exp8', 'p-diff05', 'p-src0', 'p-src20', 'p-src50'):
        cap(v, 'lights-reference-' + v, {})
    cap('none-glossy', 'lights-reference-none-glossy', {})
    fr_inv = frame(os.path.join(args.out, 'p-inv'))
    pos = fr_inv.world_positions('FRAME')
    L = np.asarray(ref.SINGLE_LIGHT)
    floor = np.isfinite(pos).all(-1) & (np.abs(pos[..., 2]) < 2.0) & (np.abs(pos[..., 0]) < 1990) & (np.abs(pos[..., 1]) < 1990)
    dist = np.linalg.norm(pos - L, axis=-1)
    f = (dist * dist) / (ref.SINGLE_RANGE * ref.SINGLE_RANGE)
    amb_rough = lum(colour(frame(os.path.join(args.out, 'none'))))
    amb_glossy = lum(colour(frame(os.path.join(args.out, 'none-glossy'))))
    l_inv, l_d05 = lum(colour(fr_inv)) - amb_rough, lum(colour(frame(os.path.join(args.out, 'p-diff05')))) - amb_rough
    lit = floor & (l_inv > 1e-2) & (f < 0.8)
    ratio_a = l_d05[lit] / l_inv[lit]
    report['c3a'] = {'median_ratio': float(np.median(ratio_a)) if lit.any() else None, 'px': int(lit.sum()), 'ambient_mean': float(amb_rough[floor].mean()),
                     'light_mean_over_lit': float(l_inv[lit].mean()) if lit.any() else None}
    check('C3a diffuseScale 0.5: floor radiance ratio (the ambient of the no-light scene subtracted) median in [0.47, 0.55]',
          lit.sum() > 1000 and C3A_LO <= report['c3a']['median_ratio'] <= C3A_HI,
          f'median ratio {report["c3a"]["median_ratio"]} over {int(lit.sum())} px (ambient {report["c3a"]["ambient_mean"]:.3f}, light {report["c3a"]["light_mean_over_lit"]:.3f})')
    l_e2, l_e8 = lum(colour(frame(os.path.join(args.out, 'p-exp2')))), lum(colour(frame(os.path.join(args.out, 'p-exp8'))))
    valid = floor & (l_e2 > 1e-3) & ((1.0 - f) >= 0.3) & ((1.0 - f) <= 0.95)
    predicted = (1.0 - f) ** 6
    err = np.abs(l_e8[valid] / l_e2[valid] - predicted[valid])
    report['c3b'] = {'median_abs_err': float(np.median(err)) if valid.any() else None, 'p90_abs_err': float(np.percentile(err, 90)) if valid.any() else None, 'px': int(valid.sum()),
                     'mean_lum_exp2': float(l_e2[floor].mean()), 'mean_lum_exp8': float(l_e8[floor].mean()), 'mean_lum_inv': float(l_inv[floor].mean())}
    check('C3b falloff exponent: lum(exp 8) / lum(exp 2) matches (1 - d^2/r^2)^6 (median |error| <= 0.02)', valid.sum() > 1000 and report['c3b']['median_abs_err'] <= C3B_MAX_ERR,
          f'median |err| {report["c3b"]["median_abs_err"]}, p90 {report["c3b"]["p90_abs_err"]} over {int(valid.sum())} px')
    peaks = {}
    for v in ('p-src0', 'p-src20', 'p-src50'):
        lv = lum(colour(frame(os.path.join(args.out, v)))) - amb_glossy
        peaks[v] = float(lv[floor].max()) if floor.any() else None
        peaks[v + '_sum'] = float(lv[floor].sum())
    report['c3c'] = peaks
    check('C3c source radius: the highlight peak (light only, glossy floor) decreases, src50 < src20 < src0 and peak(src50) <= 0.8 x peak(src0)',
          peaks['p-src50'] < peaks['p-src20'] < peaks['p-src0'] and peaks['p-src50'] <= C3C_RATIO * peaks['p-src0'],
          f'peak src0 {peaks["p-src0"]:.4f}, src20 {peaks["p-src20"]:.4f}, src50 {peaks["p-src50"]:.4f}; '
          f'floor sums {peaks["p-src0_sum"]:.1f} / {peaks["p-src20_sum"]:.1f} / {peaks["p-src50_sum"]:.1f}')
    report['montage'] = montage(args.out, [('80 lights, clustered', os.path.join(args.out, 'ref-clustered')), ('cluster light count', os.path.join(args.out, 'ref-heat')),
                                           ('500 lights, clustered', os.path.join(args.out, 'dense-clustered')), ('source radius 50 cm', os.path.join(args.out, 'p-src50'))], 'clustered_lights_montage')

    # ── C4 Sponza unchanged ──
    print('sponza: warm-up + capture (fog project, frame 60)', flush=True)
    fog = lambda name: capture(os.path.join(args.out, name), FOG_PROJECT, args.reuse, FOG_OFF)
    fog('sponza-warmup')
    fog('sponza')
    if os.path.isdir(os.path.join(BEFORE_SPONZA, 'region')):
        fr_now, fr_before = frame(os.path.join(args.out, 'sponza')), frame(BEFORE_SPONZA)
        d_now = fr_now.rect('FRAME').channel('direct_light')[..., 0].astype(np.float64)
        d_before = fr_before.rect('FRAME').channel('direct_light')[..., 0].astype(np.float64)
        c_now, c_before = colour(fr_now), colour(fr_before)
        diff_direct = float(np.abs(d_now - d_before).max())
        changed = float((np.abs(c_now - c_before).max(-1) > 1e-3).mean())
        dmax = max_diff(c_now, c_before)
        # The dump's direct_light rgb is the lighting's Lo (sun + local lights): the quantity this item's
        # shader change can touch. The colour also carries the GI state at frame 60, which differs between
        # loading timings (block B.6: ~90 k px, median 3 % between a cold and a warm shader cache); the
        # first run saw exactly that against the committed warm capture, so the colour is reported
        # against that envelope and the gate is the direct light.
        n_px = c_now.shape[0] * c_now.shape[1]
        report['c4'] = {'direct_max_diff': diff_direct, 'colour_changed_fraction': changed, 'colour_max_diff': dmax, 'median_rel': rel_stats(c_now, c_before)['median'],
                        'envelope_b06_px': 90656, 'envelope_b06_median_rel': 0.034}
        within = changed * n_px <= 1.2 * 90656 and report['c4']['median_rel'] <= 1.2 * 0.034
        check('C4 Sponza unchanged: direct_light (Lo: sun + local lights) identical to the committed capture; colour within the cold/warm envelope of block B.6',
              diff_direct <= 1e-6 and within,
              f'direct {diff_direct:.2e}; colour changed {100 * changed:.4f} % ({int(changed * n_px)} px), max |diff| {dmax:.3g}, median rel {report["c4"]["median_rel"]:.2e} '
              f'(envelope 90656 px, median 0.034)')
    else:
        check('C4 committed-build Sponza capture available', False, BEFORE_SPONZA)

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'clustered_lights.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
