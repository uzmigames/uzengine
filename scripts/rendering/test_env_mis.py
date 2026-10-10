"""Environment-MIS specular (lighting block B.5b): the temporally accumulated environment estimate that
replaces the split-sum specular on smooth metals, measured against references.

References: `scripts/rendering/reflection_reference.py` (the chrome floor seen from the camera and from the camera
mirrored through the floor plane; block B.5's planar-reflection identity), its rough metal floors
(`reflection-reference-rough30` / `-rough50`), the chrome sphere of `sample/projects/chrome_test.uzproject`
(a PMREM environment), and the environment the engine samples, dumped by the sampler as an
equirectangular radiance map (`<work>/region/env_equirect.json` + `.f32`, written on the sampler's rebuild
when a region dump is active) which a CPU integral of the GGX BRDF turns into a ground truth.

Gates (pre-registered in the Rulebook task):
  E1  the chrome floor reflects the sky: floor pixels whose reflected ray sees the sky match F x the mirrored
      view (median relative error <= 0.15; before: 0.996, the estimate was black). SSR off throughout.
  E2  energy on rough metals: on the roughness 0.3 and 0.5 floors the estimate's specular (dump channel
      specular_indirect) against the CPU integral of the dumped environment: median ratio in [0.85, 1.15].
  E2m the chrome floor's specular against F x the dumped environment along the reflected ray: median
      relative error <= 0.15 (the pass alone, without the TAA and the mirrored view).
  E3  coverage: the dielectric boxes keep the split-sum specular: their specular_indirect with the estimate on
      equals the one with `r.EnvMIS 0` (max |diff| <= 1e-4 over the boxes' interior, eroded by 2 px) and is
      not zero (before: replaced by zero on every lit pixel because the pass wrote alpha 1 everywhere). The
      silhouettes are reported: the lighting reads the previous frame's estimate, jittered by the TAA, so a
      box pixel on a floor edge can read the floor's coverage (0.3 % of the boxes' pixels).
  E4  a PMREM environment: the chrome floor under the ballroom PMREM (`reflection-reference-hdri`),
      specular_indirect against F x the dumped environment along the reflected ray, median relative error
      <= 0.2 (before: black). The chrome sphere of `chrome_test` is captured and reported only: its mesh
      renders inside-out (the visible positions are the far hemisphere's, the normals face away from the
      camera), so it cannot measure the estimate.
  E5  cost: the EnvMIS_Temporal pass <= 1.5 ms at 1920x1080 on the reference scene (full-screen floor).
  E6  Sponza unchanged (r.GI.Reflections 1 keeps the estimate out): positions and direct_light identical to
      the capture of the build before the item (test-depth-prepass/sponza-default, build 5470d37f, frame 120),
      colour within the loading-state envelope.
  E7  temporal stability: on the roughness 0.5 floor the mean per-frame |delta luminance| over the floor
      <= 2 % of the floor's median luminance.
  Reported, not gated: the split-sum path (`r.EnvMIS 0`) against the same references, the pass cost before,
  the montage.

Usage: python scripts/rendering/test_env_mis.py [--out DIR] [--reuse] [--before]
  --before: the capture set of the binary before the item (no `r.EnvMIS` CVar, no environment dump): the
  gates that need them are reported as unavailable.
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
from test_layered_reflections import Camera, capture, colour, frames, gpu_sum  # noqa: E402

REPO = runner.REPO
W, H = ref.W, ref.H
FIRST, LAST, REF_FRAME = 90, 99, 95
NEAR = 5.0
REC709 = np.array([0.2126, 0.7152, 0.0722])
GLINT_DEG = 5.0
CHROME_PROJECT = os.path.join(REPO, 'sample', 'projects', 'chrome_test.uzproject')
CHROME_CVARS = {'r.AutoExposure': 0, 'r.Exposure': 1, 'r.GTAO': 0, 'r.RadianceCache': 0, 'r.GI.FinalGather': 0,
                'r.GI.Reflections': 0, 'r.SSGI': 0, 'r.TAA': 1, 'r.Fog': 0, 'r.SSR': 0}
SPHERE_CENTRE, SPHERE_RADIUS, SPHERE_F0 = np.array([0.0, 0.0, 100.0]), 90.0, 1.0
SPONZA_BEFORE = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-depth-prepass', 'sponza-default')   # build 5470d37f, frame 120
SPONZA_FRAME = 120
E1_MAX, E2_LO, E2_HI, E2M_MAX, E3_MAX, E4_MAX, E5_MAX_MS, E7_FRAC = 0.15, 0.85, 1.15, 0.15, 1e-4, 0.2, 1.5, 0.02
ENVELOPE_PX, ENVELOPE_MED = 90656, 0.034
MIN_ROUGHNESS = 0.04   # sqrt(r.Material.MinGGXAlpha = 0.0016): the clamp both the pass and the lighting apply


def lum(c):
    return c @ REC709


def chan(fr, name):
    return fr.rect('FRAME').channel(name).astype(np.float64)


# ── the dumped environment ──────────────────────────────────────────────────────────────────────

def load_env(work):
    meta_path = os.path.join(work, 'region', 'env_equirect.json')
    if not os.path.isfile(meta_path):
        return None
    meta = json.load(open(meta_path))
    data = np.fromfile(os.path.join(work, 'region', meta['file']), dtype=np.float32)
    rgb = data.reshape(meta['height'], meta['width'], 4)[..., :3].astype(np.float64)
    return {'rgb': rgb, 'meta': meta}


CUBE_SWAP = [0, 2, 1]   # world (Z-up) <-> the engine's DX cubemap frame (+Y up): swap y and z (self-inverse)


def env_lookup(env, d_world):
    """Bilinear radiance along the unit world directions d_world (..., 3). The dump is the sampler's
    equirect in cubemap space (the pole is the cube's +Y), so the directions are swapped into it first."""
    rgb = env['rgb']
    h, w = rgb.shape[:2]
    d = d_world[..., CUBE_SWAP]
    theta = np.arccos(np.clip(d[..., 2], -1.0, 1.0))
    phi = np.arctan2(d[..., 1], d[..., 0])
    phi = np.where(phi < 0.0, phi + 2.0 * np.pi, phi)
    u = phi / (2.0 * np.pi) * w - 0.5
    v = theta / np.pi * h - 0.5
    x0 = np.floor(u).astype(int)
    y0 = np.clip(np.floor(v).astype(int), 0, h - 2)
    fx = u - x0
    fy = np.clip(v - y0, 0.0, 1.0)
    x0 = x0 % w
    x1 = (x0 + 1) % w
    y1 = y0 + 1
    c = (rgb[y0, x0] * ((1 - fx) * (1 - fy))[..., None] + rgb[y0, x1] * (fx * (1 - fy))[..., None]
         + rgb[y1, x0] * ((1 - fx) * fy)[..., None] + rgb[y1, x1] * (fx * fy)[..., None])
    return c


def env_texels(env, downsample):
    """Radiance, direction and solid angle of every equirect texel (optionally 2x2-block averaged)."""
    rgb = env['rgb']
    if downsample > 1:
        h, w = rgb.shape[0] // downsample, rgb.shape[1] // downsample
        rgb = rgb[:h * downsample, :w * downsample].reshape(h, downsample, w, downsample, 3).mean((1, 3))
    h, w = rgb.shape[:2]
    v = (np.arange(h) + 0.5) / h
    u = (np.arange(w) + 0.5) / w
    theta = v[:, None] * np.pi
    phi = u[None, :] * 2.0 * np.pi
    st = np.sin(theta)
    d = np.stack([st * np.cos(phi), st * np.sin(phi), np.cos(theta) * np.ones_like(phi)], -1)
    domega = (2.0 * np.pi / w) * (np.pi / h) * st * np.ones_like(phi)
    return rgb.reshape(-1, 3), d.reshape(-1, 3)[:, CUBE_SWAP], domega.reshape(-1)   # directions in world space


def d_ggx(alpha, noh):
    a2 = alpha * alpha
    d = (noh * a2 - noh) * noh + 1.0
    return a2 / (d * d * np.pi)


def vis_smith(alpha, nov, nol):
    a2 = alpha * alpha
    ggxv = nol * np.sqrt(nov * nov * (1.0 - a2) + a2)
    ggxl = nov * np.sqrt(nol * nol * (1.0 - a2) + a2)
    return 0.5 / np.maximum(ggxv + ggxl, 1e-7)


def f_schlick(f0, voh):
    return f0 + (1.0 - f0) * (1.0 - voh)[..., None] ** 5


def ggx_env_integral(env, N, V, roughness, f0, downsample=2, chunk=8):
    """Ground truth of the specular environment term per pixel: sum over the equirect texels of
    D * Vis * F * NoL * L * domega with the GGX functions of brdf.hlsli (alpha = roughness^2)."""
    L, d, dw = env_texels(env, downsample)
    out = np.zeros((len(N), 3))
    alpha = np.maximum(roughness, MIN_ROUGHNESS) ** 2
    for i in range(0, len(N), chunk):
        n, v, a = N[i:i + chunk], V[i:i + chunk], alpha[i:i + chunk]
        nol = n @ d.T                                   # (c, T)
        nov = np.maximum(np.sum(n * v, -1), 1e-4)[:, None]
        h = v[:, None, :] + d[None, :, :]
        h /= np.maximum(np.linalg.norm(h, axis=-1, keepdims=True), 1e-9)
        noh = np.clip(np.sum(n[:, None, :] * h, -1), 0.0, 1.0)
        voh = np.clip(np.sum(v[:, None, :] * h, -1), 0.0, 1.0)
        w = d_ggx(a[:, None], noh) * vis_smith(a[:, None], nov, np.maximum(nol, 1e-4)) * np.maximum(nol, 0.0) * dw[None, :]
        w = np.where(nol > 0.0, w, 0.0)
        F = f_schlick(f0, voh)                          # (c, T, 3)
        out[i:i + chunk] = np.einsum('ct,ctk,tk->ck', w, F, L)
    return out


def mirror_reference(env, N, V, f0):
    """The mirror limit: F(NoV) x the environment along the reflected ray."""
    nov = np.clip(np.sum(N * V, -1), 0.0, 1.0)
    R = 2.0 * nov[:, None] * N - V
    R /= np.maximum(np.linalg.norm(R, axis=-1, keepdims=True), 1e-9)
    F = f0 + (1.0 - f0) * (1.0 - nov) ** 5
    return env_lookup(env, R) * F[:, None], R


# ── pixel sets ─────────────────────────────────────────────────────────────────────────────────

def floor_mask(pos, cam):
    valid = np.isfinite(pos).all(-1) & (np.linalg.norm(pos - cam.eye, axis=-1) > 1.0)
    (fx0, fy0, _), (fx1, fy1, _) = ref.FLOOR
    return valid & (np.abs(pos[..., 2]) < 2.0) & (pos[..., 0] > fx0) & (pos[..., 0] < fx1) & (pos[..., 1] > fy0) & (pos[..., 1] < fy1)


def box_mask(pos):
    m = np.zeros(pos.shape[:2], dtype=bool)
    ok = np.isfinite(pos).all(-1)
    for (lo, hi), _ in ref.BOXES.values():
        inside = ok
        for k in range(3):
            inside = inside & (pos[..., k] > lo[k] - 0.5) & (pos[..., k] < hi[k] + 0.5)
        m |= inside & (pos[..., 2] > 0.5)
    return m


def sky_class(fr_main, fr_mirror, man_main, man_mirror):
    """Floor pixels whose reflected ray sees the sky in the mirrored view (block B.5's classification), the
    Fresnel factor and the mirrored view's colour at the mirrored pixel."""
    cam = Camera(man_main.get('camera', {}).get('position', ref.CAMERA_EYE), man_main.get('camera', {}).get('target', ref.CAMERA_TARGET))
    pos = fr_main.world_positions('FRAME')
    pos_mirror = fr_mirror.world_positions('FRAME')
    floor = floor_mask(pos, cam)
    d = pos - cam.eye
    d = d / np.maximum(np.linalg.norm(d, axis=-1, keepdims=True), 1e-9)
    d_ref = d.copy()
    d_ref[..., 2] = -d_ref[..., 2]
    cos_theta = np.clip(-d[..., 2], 0.0, 1.0)
    F = ref.FLOOR_F0 + (1.0 - ref.FLOOR_F0) * (1.0 - cos_theta) ** 5
    glint = (d_ref @ np.asarray(ref.SUN_DIR)) > np.cos(np.radians(GLINT_DEG))
    ys, xs = np.mgrid[0:H, 0:W]
    mx, my = ref.mirror_pixel(xs, ys)
    mirror_eye = np.asarray(man_mirror.get('camera', {}).get('position', ref.mirror_point(ref.CAMERA_EYE)), dtype=np.float64)
    Q = pos_mirror[my, mx]
    q_dist = np.linalg.norm(Q - mirror_eye, axis=-1)
    geom_m = np.isfinite(Q).all(-1) & (q_dist > 1.0) & (q_dist < 8000.0) & (Q[..., 2] > 1.0)
    sky = floor & ~geom_m & ~glint
    return {'floor': floor, 'sky': sky, 'F': F, 'c_ref': colour(fr_mirror)[my, mx], 'cam': cam, 'pos': pos, 'd_ref': d_ref}


def pixel_shading_inputs(fr, cam, mask, stride=1):
    """World position, normal, view vector, roughness and metallic of the masked pixels (subsampled)."""
    sel = np.zeros_like(mask)
    sel[::stride, ::stride] = True
    m = mask & sel
    pos = fr.world_positions('FRAME')[m]
    N = chan(fr, 'lighting_normal')[..., :3][m]
    N /= np.maximum(np.linalg.norm(N, axis=-1, keepdims=True), 1e-9)
    V = cam.eye - pos
    V /= np.maximum(np.linalg.norm(V, axis=-1, keepdims=True), 1e-9)
    rough = chan(fr, 'lighting_reflection')[..., 3][m]
    spec = chan(fr, 'specular_indirect')
    return {'mask': m, 'pos': pos, 'N': N, 'V': V, 'rough': rough, 'metallic': spec[..., 3][m], 'spec': spec[..., :3][m]}


def rel_err(x, y):
    yl = np.maximum(lum(y), 1e-6)
    return np.abs(lum(x) - lum(y)) / yl


def ratio(x, y):
    return lum(x) / np.maximum(lum(y), 1e-6)


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


def capture_sponza(work, reuse, cvars, frames_n=126, rf=SPONZA_FRAME, tf=60):
    if reuse and os.path.isfile(os.path.join(work, 'manifest.json')):
        return json.load(open(os.path.join(work, 'manifest.json')))
    res = runner.run_capture(work, sponza.QA_PROJECT, frames_n, regions=[('FRAME', 0, 0, W, H)], region_fields='components',
                             region_first=rf, region_last=rf, cvars={'r.TechPanel.Show': 0, 'r.Toolbar.Show': 0, **cvars},
                             timing_first=tf, clean=True, timeout=900, screenshot_frames=str(rf))
    if res['exit_code'] != 0:
        raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    return json.load(open(os.path.join(work, 'manifest.json')))


def identity(fa, fb):
    pa, pb = fa.world_positions('FRAME'), fb.world_positions('FRAME')
    da, db = chan(fa, 'direct_light')[..., :3], chan(fb, 'direct_light')[..., :3]
    ca, cb = colour(fa), colour(fb)
    lb = lum(cb)
    lit = lb > 1e-3
    return {'pos_max_diff': float(np.nanmax(np.abs(pa - pb))), 'direct_max_diff': float(np.abs(da - db).max()),
            'colour_changed_px': int((np.abs(ca - cb).max(-1) > 1e-3).sum()),
            'colour_median_rel': float(np.median((np.abs(lum(ca) - lb))[lit] / lb[lit])) if lit.any() else 0.0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-env-mis'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--before', action='store_true', help='the capture set of the binary before the item')
    args = ap.parse_args()
    results, report = [], {'before_mode': args.before}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    def work(name):
        return os.path.join(args.out, name)

    off = {'r.EnvMIS': 0}
    print('captures: mirror, chrome floor, rough floors, chrome sphere' + ('' if args.before else ', the split-sum controls, Sponza'), flush=True)
    man_mirror = capture(work('mirror'), ref.PROJECT_MIRROR, args.reuse, {'r.SSR': 0})
    man_ref = capture(work('ref-on'), ref.PROJECT, args.reuse, {'r.SSR': 0})
    man_r30 = capture(work('rough30-on'), ref.variant_project('rough30'), args.reuse, {'r.SSR': 0})
    man_r50 = capture(work('rough50-on'), ref.variant_project('rough50'), args.reuse, {'r.SSR': 0})
    man_chrome = capture(work('chrome-on'), CHROME_PROJECT, args.reuse, CHROME_CVARS)
    man_hdri = capture(work('hdri-on'), ref.variant_project(ref.HDRI_VARIANT), args.reuse, {'r.SSR': 0}) if not args.before else None
    if not args.before:
        capture(work('ref-off'), ref.PROJECT, args.reuse, {'r.SSR': 0, **off})
        capture(work('rough30-off'), ref.variant_project('rough30'), args.reuse, {'r.SSR': 0, **off})
        capture(work('rough50-off'), ref.variant_project('rough50'), args.reuse, {'r.SSR': 0, **off})
        capture(work('hdri-off'), ref.variant_project(ref.HDRI_VARIANT), args.reuse, {'r.SSR': 0, **off})
        capture(work('chrome-off'), CHROME_PROJECT, args.reuse, {**CHROME_CVARS, **off})

    k = REF_FRAME - FIRST
    fr_mirror = frames(work('mirror'))
    fr_ref = frames(work('ref-on'))
    fr_ref_off = frames(work('ref-off')) if not args.before else None

    # ── E1: the chrome floor reflects the sky (end to end, against the mirrored view) ──
    cls = sky_class(fr_ref[k], fr_mirror[k], man_ref, man_mirror)
    sky, F, c_ref = cls['sky'], cls['F'], cls['c_ref']
    ref_lum = lum(c_ref) * F
    def sky_err(fr):
        c = colour(fr)
        e = np.abs(lum(c) - ref_lum) / np.maximum(ref_lum, 1e-6)
        r = lum(c) / np.maximum(ref_lum, 1e-6)
        rs = r[sky]
        # The sky cubemap capture scales the sun by |illuminance| (sqrt(3) x the per-channel value of a white
        # sun) while the sky pass uses the per-channel value: the environment is brighter than the sky it
        # renders. Reported beside the gate: the ratio's spread (the shape) and the error with that factor out.
        return {'median_rel_err': float(np.median(e[sky])), 'median_ratio': float(np.median(rs)), 'px': int(sky.sum()),
                'p10_ratio': float(np.percentile(rs, 10)), 'p90_ratio': float(np.percentile(rs, 90)),
                'median_rel_err_over_sqrt3': float(np.median(np.abs(rs / np.sqrt(3.0) - 1.0)))}
    e1 = sky_err(fr_ref[k])
    report['e1'] = {'env_mis': e1, 'split_sum': sky_err(fr_ref_off[k]) if fr_ref_off else None, 'floor_px': int(cls['floor'].sum())}
    print(f'  chrome floor, sky class {e1["px"]} px: env MIS err {e1["median_rel_err"]:.3f} ratio {e1["median_ratio"]:.3f} (p10 {e1["p10_ratio"]:.3f}, p90 {e1["p90_ratio"]:.3f}; err over sqrt3 {e1["median_rel_err_over_sqrt3"]:.3f})'
          + (f'; split-sum err {report["e1"]["split_sum"]["median_rel_err"]:.3f} ratio {report["e1"]["split_sum"]["median_ratio"]:.3f}' if fr_ref_off else ''), flush=True)
    check('E1 the chrome floor reflects the sky: floor pixels whose reflected ray sees the sky match F x the mirrored view (median relative error <= 0.15)',
          sky.sum() >= 10000 and e1['median_rel_err'] <= E1_MAX, f'median error {e1["median_rel_err"]:.3f} (ratio {e1["median_ratio"]:.3f}) over {e1["px"]} px')

    # ── E2 / E2m: energy against the CPU integral of the dumped environment ──
    cam = cls['cam']
    env_ref = load_env(work('ref-on'))
    for key, man, label in (('rough30', man_r30, 'roughness 0.3'), ('rough50', man_r50, 'roughness 0.5')):
        fr_on = frames(work(f'{key}-on'))[k]
        env = load_env(work(f'{key}-on'))
        cam_k = Camera(man.get('camera', {}).get('position', ref.CAMERA_EYE), man.get('camera', {}).get('target', ref.CAMERA_TARGET))
        fl = floor_mask(fr_on.world_positions('FRAME'), cam_k)
        px = pixel_shading_inputs(fr_on, cam_k, fl, stride=24)
        entry = {'px': int(px['mask'].sum()), 'roughness_median': float(np.median(px['rough'])), 'metallic_median': float(np.median(px['metallic']))}
        if env is None:
            entry['ground_truth'] = None
            check(f'E2 energy on the {label} metal floor: specular_indirect / CPU integral of the dumped environment, median ratio in [0.85, 1.15]', False, 'no environment dump')
        else:
            gt = ggx_env_integral(env, px['N'], px['V'], px['rough'], ref.FLOOR_F0)
            r = ratio(px['spec'], gt)
            entry.update({'median_ratio': float(np.median(r)), 'p10_ratio': float(np.percentile(r, 10)), 'p90_ratio': float(np.percentile(r, 90)),
                          'gt_median_lum': float(np.median(lum(gt))), 'spec_median_lum': float(np.median(lum(px['spec'])))})
            if not args.before:
                fr_off = frames(work(f'{key}-off'))[k]
                px_off = pixel_shading_inputs(fr_off, cam_k, fl, stride=24)
                r_off = ratio(px_off['spec'], gt)
                entry['split_sum_median_ratio'] = float(np.median(r_off))
            print(f'  {label} floor ({entry["px"]} px): env MIS / ground truth median {entry["median_ratio"]:.3f} (p10 {entry["p10_ratio"]:.3f}, p90 {entry["p90_ratio"]:.3f})'
                  + (f'; split-sum / ground truth {entry["split_sum_median_ratio"]:.3f}' if 'split_sum_median_ratio' in entry else ''), flush=True)
            check(f'E2 energy on the {label} metal floor: specular_indirect / CPU integral of the dumped environment, median ratio in [0.85, 1.15]',
                  E2_LO <= entry['median_ratio'] <= E2_HI, f'median ratio {entry["median_ratio"]:.3f} over {entry["px"]} px (p10 {entry["p10_ratio"]:.3f}, p90 {entry["p90_ratio"]:.3f})')
        report[f'e2_{key}'] = entry
    if env_ref is None:
        report['e2m'] = None
        check('E2m the chrome floor against F x the dumped environment along the reflected ray (median relative error <= 0.15)', False, 'no environment dump')
    else:
        px = pixel_shading_inputs(fr_ref[k], cam, sky, stride=4)
        gt, R = mirror_reference(env_ref, px['N'], px['V'], ref.FLOOR_F0)
        e = rel_err(px['spec'], gt)
        report['e2m'] = {'median_rel_err': float(np.median(e)), 'median_ratio': float(np.median(ratio(px['spec'], gt))), 'px': int(len(e))}
        check('E2m the chrome floor against F x the dumped environment along the reflected ray (median relative error <= 0.15)',
              report['e2m']['median_rel_err'] <= E2M_MAX, f'median error {report["e2m"]["median_rel_err"]:.3f} (ratio {report["e2m"]["median_ratio"]:.3f}) over {report["e2m"]["px"]} px')

    # ── E3: the dielectric boxes keep the split-sum specular ──
    boxes = box_mask(fr_ref[k].world_positions('FRAME'))
    interior = boxes.copy()
    for kk in (1, 2):
        for ax in (0, 1):
            interior &= np.roll(boxes, kk, ax) & np.roll(boxes, -kk, ax)
    spec_on = chan(fr_ref[k], 'specular_indirect')[..., :3]
    med_on = float(np.median(lum(spec_on)[interior]))
    report['e3'] = {'px': int(boxes.sum()), 'interior_px': int(interior.sum()), 'boxes_specular_median_lum_on': med_on}
    if fr_ref_off:
        spec_off = chan(fr_ref_off[k], 'specular_indirect')[..., :3]
        d3 = np.abs(spec_on - spec_off).max(-1)
        diff = float(d3[interior].max())
        report['e3'].update({'boxes_specular_median_lum_off': float(np.median(lum(spec_off)[interior])), 'max_diff_on_off_interior': diff,
                             'max_diff_on_off_all': float(d3[boxes].max()), 'silhouette_px_over_1e-4': int((d3[boxes] > 1e-4).sum()),
                             'silhouette_fraction': float((d3[boxes] > 1e-4).mean())})
        check('E3 coverage: the dielectric boxes keep the split-sum specular (max |diff| to r.EnvMIS 0 <= 1e-4 over the interior) and it is not zero',
              diff <= E3_MAX and med_on > 1e-4,
              f'interior max |diff| {diff:.2e} over {int(interior.sum())} px; median specular luminance {med_on:.4f}; '
              f'silhouettes: {report["e3"]["silhouette_px_over_1e-4"]} of {int(boxes.sum())} px differ (max {report["e3"]["max_diff_on_off_all"]:.2e}; the previous frame\'s jittered estimate)')
    else:
        check('E3 coverage: the dielectric boxes keep the split-sum specular (max |diff| to r.EnvMIS 0 <= 1e-4 over the interior) and it is not zero',
              False, f'no r.EnvMIS control in this binary; median specular luminance on the boxes {med_on:.4f}')

    # ── E4: a PMREM environment (the chrome floor under the ballroom PMREM); the chrome sphere reported ──
    fr_chrome = frames(work('chrome-on'))[k]
    cam_c = Camera(man_chrome.get('camera', {}).get('position', [400, 0, 100]), man_chrome.get('camera', {}).get('target', [0, 0, 100]), fov_deg=40)
    pos_c = fr_chrome.world_positions('FRAME')
    sphere = np.isfinite(pos_c).all(-1) & (np.abs(np.linalg.norm(pos_c - SPHERE_CENTRE, axis=-1) - SPHERE_RADIUS) < 3.0)
    spec_c = chan(fr_chrome, 'specular_indirect')[..., :3]
    n_c = chan(fr_chrome, 'lighting_normal')[..., :3]
    v_c = cam_c.eye - pos_c
    v_c /= np.maximum(np.linalg.norm(v_c, axis=-1, keepdims=True), 1e-9)
    report['sphere'] = {'px': int(sphere.sum()), 'specular_median_lum': float(np.median(lum(spec_c)[sphere])) if sphere.any() else None,
                        'colour_median_lum': float(np.median(lum(colour(fr_chrome))[sphere])) if sphere.any() else None,
                        'position_x_median': float(np.median(pos_c[sphere][:, 0])) if sphere.any() else None,
                        'NoV_median': float(np.median(np.sum(n_c * v_c, -1)[sphere])) if sphere.any() else None}
    print(f'  chrome sphere ({report["sphere"]["px"]} px): positions x median {report["sphere"]["position_x_median"]} (camera at x = 400), N.V median {report["sphere"]["NoV_median"]}: '
          f'the mesh renders inside-out; specular median luminance {report["sphere"]["specular_median_lum"]}', flush=True)
    if args.before:
        check('E4 a PMREM environment: the chrome floor under the ballroom PMREM, specular_indirect against F x the dumped environment along the reflected ray (median relative error <= 0.2)',
              False, 'no environment dump in this binary')
    else:
        fr_hdri = frames(work('hdri-on'))[k]
        env_hdri = load_env(work('hdri-on'))
        cam_h = Camera(man_hdri.get('camera', {}).get('position', ref.CAMERA_EYE), man_hdri.get('camera', {}).get('target', ref.CAMERA_TARGET))
        fl_h = floor_mask(fr_hdri.world_positions('FRAME'), cam_h)
        report['e4'] = {'floor_px': int(fl_h.sum())}
        if env_hdri is None:
            check('E4 a PMREM environment: the chrome floor under the ballroom PMREM, specular_indirect against F x the dumped environment along the reflected ray (median relative error <= 0.2)',
                  False, 'no environment dump')
        else:
            px = pixel_shading_inputs(fr_hdri, cam_h, fl_h, stride=4)
            gt, R = mirror_reference(env_hdri, px['N'], px['V'], ref.FLOOR_F0)
            e = rel_err(px['spec'], gt)
            fr_hdri_off = frames(work('hdri-off'))[k]
            px_off = pixel_shading_inputs(fr_hdri_off, cam_h, fl_h, stride=4)
            report['e4'].update({'median_rel_err': float(np.median(e)), 'median_ratio': float(np.median(ratio(px['spec'], gt))), 'eval_px': int(len(e)),
                                 'gt_median_lum': float(np.median(lum(gt))), 'split_sum_median_ratio': float(np.median(ratio(px_off['spec'], gt))),
                                 'env_total_luminance': env_hdri['meta'].get('total_luminance')})
            print(f'  chrome floor under the PMREM ({len(e)} px): env MIS err {report["e4"]["median_rel_err"]:.3f}, ratio {report["e4"]["median_ratio"]:.3f}; split-sum ratio {report["e4"]["split_sum_median_ratio"]:.3f}', flush=True)
            check('E4 a PMREM environment: the chrome floor under the ballroom PMREM, specular_indirect against F x the dumped environment along the reflected ray (median relative error <= 0.2)',
                  report['e4']['median_rel_err'] <= E4_MAX, f'median error {report["e4"]["median_rel_err"]:.3f} (ratio {report["e4"]["median_ratio"]:.3f}) over {len(e)} px')

    # ── E5: cost ──
    ms = (man_ref.get('gpu_ms') or {}).get('EnvMIS_Temporal')
    report['e5'] = {'env_mis_ms_reference': ms, 'env_mis_ms_rough50': (man_r50.get('gpu_ms') or {}).get('EnvMIS_Temporal'),
                    'env_mis_ms_chrome': (man_chrome.get('gpu_ms') or {}).get('EnvMIS_Temporal'),
                    'deferred_ms_reference': (man_ref.get('gpu_ms') or {}).get('DeferredLighting')}
    check('E5 the EnvMIS_Temporal pass <= 1.5 ms at 1920x1080 on the reference scene', ms is not None and 0 < ms <= E5_MAX_MS, f'{ms} ms')

    # ── E7: temporal stability on the roughness 0.5 floor ──
    fr_r50 = frames(work('rough50-on'))
    cam50 = Camera(man_r50.get('camera', {}).get('position', ref.CAMERA_EYE), man_r50.get('camera', {}).get('target', ref.CAMERA_TARGET))
    fl50 = floor_mask(fr_r50[k].world_positions('FRAME'), cam50)
    lums = np.stack([lum(colour(f)) for f in fr_r50])
    t50 = float(np.mean(np.abs(np.diff(lums, axis=0)).mean(0)[fl50]))
    med50 = float(np.median(lums[k][fl50]))
    lums_ref = np.stack([lum(colour(f)) for f in fr_ref])
    report['e7'] = {'rough50_mean_abs_delta': t50, 'rough50_median_lum': med50, 'chrome_floor_mean_abs_delta': float(np.mean(np.abs(np.diff(lums_ref, axis=0)).mean(0)[cls['floor']]))}
    check('E7 temporal stability: mean per-frame |delta luminance| over the roughness 0.5 floor <= 2 % of its median luminance',
          t50 <= E7_FRAC * med50, f'{t50:.5f} against {E7_FRAC * med50:.5f} (median luminance {med50:.4f})')

    # ── E6: Sponza unchanged ──
    if not args.before:
        capture_sponza(work('sponza-default'), args.reuse, sponza.PROJECT_CVARS)
        capture_sponza(work('sponza-default-repeat'), args.reuse, sponza.PROJECT_CVARS)
        if os.path.isdir(os.path.join(SPONZA_BEFORE, 'region')):
            fr_now = regiondump.load_sequence(os.path.join(work('sponza-default'), 'region'), SPONZA_FRAME, SPONZA_FRAME)[0]
            fr_rep = regiondump.load_sequence(os.path.join(work('sponza-default-repeat'), 'region'), SPONZA_FRAME, SPONZA_FRAME)[0]
            fr_before = regiondump.load_sequence(os.path.join(SPONZA_BEFORE, 'region'), SPONZA_FRAME, SPONZA_FRAME)[0]
            s6 = identity(fr_now, fr_before)
            s6r = identity(fr_now, fr_rep)
            env_px, env_med = max(s6r['colour_changed_px'], ENVELOPE_PX), max(s6r['colour_median_rel'], ENVELOPE_MED)
            report['e6'] = {'vs_committed': s6, 'repeat_pair': s6r, 'envelope_px': env_px, 'envelope_med': env_med}
            check('E6 Sponza unchanged: positions and direct_light identical to the capture of the build before the item; colour within the loading-state envelope',
                  s6['pos_max_diff'] <= 1e-6 and s6['direct_max_diff'] <= 1e-6 and s6['colour_changed_px'] <= 1.2 * env_px and s6['colour_median_rel'] <= 1.2 * env_med,
                  f'positions {s6["pos_max_diff"]:.3g}, direct {s6["direct_max_diff"]:.3g}, colour changed {s6["colour_changed_px"]} px (median rel {s6["colour_median_rel"]:.3f}); '
                  f'repeat pair {s6r["colour_changed_px"]} px / {s6r["colour_median_rel"]:.3f}; envelope {env_px} px / {env_med:.3f}')
        else:
            check('E6 committed Sponza capture available', False, SPONZA_BEFORE)

    panels = [('mirrored view (reference)', work('mirror'))]
    if not args.before:
        panels.append(('split-sum (r.EnvMIS 0)', work('ref-off')))
    panels += [('env MIS', work('ref-on')), ('roughness 0.5 metal, env MIS', work('rough50-on'))]
    if not args.before:
        panels.append(('chrome floor under the PMREM', work('hdri-on')))
    panels.append(('chrome sphere (PMREM; the mesh renders inside-out)', work('chrome-on')))
    report['montage'] = montage(args.out, panels, 'env_mis_montage' + ('_before' if args.before else ''))

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'env_mis.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
