"""The split-sum specular on rough metals (lighting side item): block B.5b measured the prefiltered-cubemap
path at 3.3 x / 4.9 x the CPU integral on the roughness 0.3 / 0.5 metal floors while the environment-MIS
estimate read 1.0 x. The lighting picked the prefiltered mip with a PMREM-style curve (roughness x (2 -
roughness) x maxMip x 1.2) while the engine's own bake distributes roughness linearly over the mips
(roughness 0.5 read a mip baked for 0.9). The engine's bake is now read linearly; the PMREM path keeps its
curve behind a per-frame flag.

References: the dumped environment (block B.5b's sampler dump) integrated with the GGX BRDF on the CPU
(`test_env_mis.ggx_env_integral`), on the sky bake (reflection-reference-rough30 / -rough50) and on the PMREM
(reflection-reference-hdri-rough30 / -hdri-rough50); the chrome floors as the mip-0 controls.

Gates (pre-registered in the Rulebook task):
  P1  sky bake, roughness 0.3 and 0.5 metal floors (`r.EnvMIS 0`): split-sum specular_indirect / CPU integral,
      median ratio within [0.75, 1.3] (before 3.26 / 4.85; the split-sum's own approximations, N = V = R and
      the analytic DFG, are worth a few tens of a percent at most).
  P1b the engine against a CPU model of its own approximations (the prefiltered radiance at the lerped R with the
      bake's N = V = R lobe, the Karis DFG, the multi-scatter term with the dump's L2 irradiance): median ratio within
      [0.9, 1.1] on both sky-bake floors, and the P1 ratio binned by NoV. Added after the first after-run
      (2026-10-08): P1 still failed on the 0.5 floor while the engine matched the model within 2 %, so the remainder
      is the split-sum's N = V = R assumption at grazing incidence (the floors sit at NoV 0.1-0.7), not the
      implementation.
  P2r (reported) the chrome floor against the mirror reference of the dumped environment (F(NoV) x L(R)), before
      and after: the old curve put roughness 0.02 at mip 0.33 (a third of the roughness-0.14 mip), which at the
      horizon mixes the ground into the sky's reflection, so P2's premise "chrome unchanged" was wrong.
  P2  the chrome floor (roughness 0.02, sky bake, `r.EnvMIS 0`): specular_indirect against the build before,
      median ratio within [0.95, 1.05] (mip 0 under either mapping).
  P3  PMREM path, roughness 0.3 and 0.5 metal floors: split-sum / CPU integral reported (the PMREM's own
      roughness-per-LOD convention is kept); the chrome floor under the PMREM unchanged (ratio within
      [0.95, 1.05] against the build before).
  P4  the dielectric boxes (roughness 0.7, F0 0.04): specular_indirect against the build before, reported.
  P5  Sponza Camera A and C: direct light identical to the build before; specular_indirect and colour ratios
      reported (Sponza's stone is dielectric; its metal door changes).
  P6  the deferred lighting pass cost within 10 % of before.
  P7  the sky irradiance frame: the dielectric box tops' diffuse_indirect / albedo against E / pi of the dumped
      environment for the upward normal, per channel within [0.85, 1.15] (before 10.3 / 5.5 / 3.0: the SH was
      projected in the cubemap's frame and evaluated with the world normal).
  P8  the bake: each dumped prefiltered mip (prefiltered_mip<N>, the sampler's diagnostics after the sky bake)
      against the CPU GGX prefilter of the environment at the mip's roughness (N = V = R), median ratio within
      [0.9, 1.1] over 400 directions of the upper hemisphere; mips 0-5 gated, 6-7 reported.

Usage: python scripts/rendering/test_split_sum.py [--out DIR] [--reuse] [--before]
  --before: the before state on the current binary (build 513e2983): the reflection-reference captures (the chrome
  floor, the rough floors, the PMREM floors) go to test-split-sum/before; the Sponza captures of the mesh-decal
  item (the same build) stand in for Sponza. The first before-run reused block B.5b's sky-bake captures, taken
  before the sky capture fix (B.5c: 1 / sqrt 3 on the whole sky), which made P2 read 0.60 for the wrong reason;
  the references are now captured on the build before itself.
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
from test_layered_reflections import Camera, capture, colour, frames, FOG_PROJECT  # noqa: E402
from test_env_mis import (CUBE_SWAP, load_env, env_lookup, env_texels, lum, chan, ggx_env_integral, pixel_shading_inputs, floor_mask, box_mask, ratio,  # noqa: E402
                          capture_sponza, identity, montage, mirror_reference, SPONZA_FRAME, FIRST, REF_FRAME)

REPO = runner.REPO
B05B = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-env-mis')
DECALS = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-mesh-decals')
P1_LO, P1_HI, P2_LO, P2_HI, P6_COST = 0.75, 1.3, 0.95, 1.05, 1.10
P1B_LO, P1B_HI = 0.9, 1.1
NOV_BINS = ((0.0, 0.2), (0.2, 0.35), (0.35, 0.5), (0.5, 0.7), (0.7, 1.01))
SH_C0, SH_C1, SH_C2, SH_C3, SH_C4 = 0.282094791773878, 0.32573500793528, 0.27313710764802, 0.07884789131313, 0.13656855382401
P7_LO, P7_HI, P8_LO, P8_HI, P8_GATED_MIPS, PREFILTERED_MIPS = 0.85, 1.15, 0.9, 1.1, 6, 8
OFF = {'r.SSR': 0, 'r.EnvMIS': 0}
REC709 = np.array([0.2126, 0.7152, 0.0722])


def irradiance_over_pi(env, N):
    """E / pi of the dumped environment for the world normal N (the cosine-weighted mean radiance)."""
    L, d, dw = env_texels(env, 1)
    nol = d @ np.asarray(N, dtype=np.float64)
    return (L * (np.maximum(nol, 0.0) * dw)[:, None]).sum(0) / np.pi


def box_top_irradiance(work, env):
    """diffuse_indirect / albedo on the dielectric box tops (metallic 0, roughness 0.7: (1 - scatter) ~ 0.96),
    per channel, against E / pi for the upward normal."""
    fr = frames(work)[REF_FRAME - FIRST]
    pos = fr.world_positions('FRAME')
    n = chan(fr, 'lighting_normal')[..., :3]
    di = chan(fr, 'diffuse_indirect')[..., :3]
    e = irradiance_over_pi(env, (0.0, 0.0, 1.0))
    rows = {}
    for key, ((lo, hi), col) in ref.BOXES.items():
        top = (np.isfinite(pos).all(-1) & (np.abs(pos[..., 2] - hi[2]) < 1.0) & (pos[..., 0] > lo[0] + 2) & (pos[..., 0] < hi[0] - 2)
               & (pos[..., 1] > lo[1] + 2) & (pos[..., 1] < hi[1] - 2) & (n[..., 2] > 0.99))
        if top.sum() < 50:
            continue
        med = np.median(di[top], axis=0) / np.asarray(col)
        rows[key] = {'px': int(top.sum()), 'irradiance_over_albedo': med.tolist(), 'ratio_to_env': (med / np.maximum(e, 1e-6)).tolist()}
    allr = np.array([r['ratio_to_env'] for r in rows.values()])
    return {'env_e_over_pi': e.tolist(), 'boxes': rows, 'ratio_median_rgb': np.median(allr, axis=0).tolist() if len(allr) else None}


def load_mip_dump(work, mip):
    meta_path = os.path.join(work, 'region', f'prefiltered_mip{mip}.json')
    if not os.path.isfile(meta_path):
        return None
    meta = json.load(open(meta_path))
    data = np.fromfile(os.path.join(work, 'region', meta['file']), dtype=np.float32)
    return {'rgb': data.reshape(meta['height'], meta['width'], 4)[..., :3].astype(np.float64), 'meta': meta}


def cpu_prefilter(env, dirs_world, roughness, downsample=2):
    """The split-sum's first sum as the bake defines it: sum L(l) * NoL over the GGX lobe with N = V = R,
    which for a lobe sampled by D(h) is the D * NoL weighted mean of the radiance (the weight includes the
    half-vector Jacobian 1 / (4 VoH) through the NDF sampling: w = D(h) * NoH / (4 VoH) * NoL / pdf = NoL)."""
    L, d, dw = env_texels(env, downsample)
    alpha = max(roughness, 1e-3) ** 2
    out = np.zeros((len(dirs_world), 3))
    for i, N in enumerate(dirs_world):
        nol = d @ N
        ok = nol > 0
        h = d[ok] + N
        h /= np.linalg.norm(h, axis=-1, keepdims=True)
        noh = np.clip(h @ N, 0, 1)
        voh = noh                                           # V = N
        D = d_ggx_np(alpha, noh)
        # samples drawn with pdf_l = D * NoH / (4 VoH): the lobe mean over L is sum L * NoL * pdf_l * dw / sum NoL * pdf_l * dw
        w = D * noh / (4.0 * np.maximum(voh, 1e-4)) * nol[ok] * dw[ok]
        out[i] = (L[ok] * w[:, None]).sum(0) / max(w.sum(), 1e-12)
    return out


def d_ggx_np(alpha, noh):
    a2 = alpha * alpha
    den = (noh * a2 - noh) * noh + 1.0
    return a2 / (den * den * np.pi)


def packed_sh_from_env(env):
    """The engine's sky SH from the dumped environment: sh_projection.cpp's basis (Ramamoorthi's order and signs)
    on the cube-space texel directions, packed as PackSHToCompactFormat does after the split-sum item (the
    cosine convolution folded into the constants; EvalSH_L2's dot products evaluate it)."""
    L, d_world, dw = env_texels(env, 1)
    d = d_world[:, CUBE_SWAP]
    x, y, z = d[:, 0], d[:, 1], d[:, 2]
    basis = np.stack([0.282094791773878 + 0 * x, 0.488602511902920 * y, 0.488602511902920 * z, 0.488602511902920 * x,
                      1.092548430592079 * x * y, 1.092548430592079 * y * z, 0.315391565252520 * (3 * z * z - 1),
                      1.092548430592079 * x * z, 0.546274215296040 * (x * x - y * y)], 1)
    sh = (basis * dw[:, None]).T @ L
    out = np.zeros((7, 4))
    for c in range(3):
        out[c] = [SH_C1 * sh[3, c], SH_C1 * sh[1, c], SH_C1 * sh[2, c], SH_C0 * sh[0, c] - SH_C3 * sh[6, c]]
        out[3 + c] = [SH_C2 * sh[4, c], SH_C2 * sh[5, c], 3 * SH_C3 * sh[6, c], SH_C2 * sh[7, c]]
    out[6] = [SH_C4 * sh[8, 0], SH_C4 * sh[8, 1], SH_C4 * sh[8, 2], 1.0]
    return out


def eval_packed_sh(out, n_cube):
    """ibl.hlsli EvalSH_L2 (E / pi for the cube-space normal)."""
    n = np.asarray(n_cube, dtype=np.float64)
    nv = np.concatenate([n, np.ones(n.shape[:-1] + (1,))], -1)
    vb = np.stack([n[..., 0] * n[..., 1], n[..., 1] * n[..., 2], n[..., 2] * n[..., 2], n[..., 2] * n[..., 0]], -1)
    vc = n[..., 0] ** 2 - n[..., 1] ** 2
    return np.maximum(np.stack([nv @ out[c] + vb @ out[3 + c] + out[6][c] * vc for c in range(3)], -1), 0.0)


def karis_dfg(rough, nov):
    """ibl.hlsli EnvironmentBRDFApprox (Karis 2014): (A, B) with F0 * A + B."""
    r = rough[:, None] * np.array([-1.0, -0.0275, -0.572, 0.022]) + np.array([1.0, 0.0425, 1.04, -0.04])
    a004 = np.minimum(r[:, 0] * r[:, 0], np.exp2(-9.28 * nov)) * r[:, 0] + r[:, 1]
    return np.stack([-1.04 * a004 + r[:, 2], 1.04 * a004 + r[:, 3]], 1)


def split_sum_model(env, px, f0):
    """deferred_lighting_ps.hlsl's image-based specular for a metal (metallic 1, F0 = base colour) on the CPU:
    the prefiltered radiance at R = normalize(lerp(reflect(-V, N), N, roughness^4)) with the bake's N = V = R lobe
    at the surface's roughness (the continuous-roughness ideal of the linear mip read), times F0 * A + B, plus the
    multi-scatter term Fms * Ems times the L2 sky irradiance (E / pi) of the dump for the normal."""
    N, V, rough = px['N'], px['V'], px['rough']
    nov = np.clip((N * V).sum(-1), 1e-4, 1.0)
    R = -V + 2.0 * nov[:, None] * N
    R = R + (N - R) * (rough ** 4)[:, None]
    R /= np.linalg.norm(R, axis=-1, keepdims=True)
    rad = np.zeros((len(R), 3))
    rk = np.round(rough, 3)
    for rv in np.unique(rk):
        sel = rk == rv
        rad[sel] = cpu_prefilter(env, R[sel], float(rv), downsample=2)
    ab = karis_dfg(rough, nov)
    F0 = np.full((len(R), 3), f0, dtype=np.float64)
    single = F0 * ab[:, :1] + ab[:, 1:]
    ems = 1.0 - ab.sum(1)
    favg = F0 + (1.0 - F0) * 0.047619
    fms = single * favg / np.maximum(1.0 - ems[:, None] * favg, 0.001)
    multi = np.maximum(fms * ems[:, None], 0.0)
    irr = eval_packed_sh(packed_sh_from_env(env), N[:, CUBE_SWAP])
    return {'spec': rad * single + irr * multi, 'single': rad * single, 'multi': irr * multi, 'nov': nov}


def gt_ratio(work, man, stride=24, f0=ref.FLOOR_F0):
    """Split-sum specular_indirect / the CPU GGX integral of the dumped environment over the floor (P1), the engine
    against the CPU model of its own approximations (P1b) and both ratios binned by NoV."""
    env = load_env(work)
    if env is None:
        return None
    fr = frames(work)[REF_FRAME - FIRST]
    cam = Camera(man.get('camera', {}).get('position', ref.CAMERA_EYE), man.get('camera', {}).get('target', ref.CAMERA_TARGET))
    fl = floor_mask(fr.world_positions('FRAME'), cam)
    px = pixel_shading_inputs(fr, cam, fl, stride=stride)
    gt = ggx_env_integral(env, px['N'], px['V'], px['rough'], f0, downsample=2)
    r = ratio(px['spec'], gt)
    mdl = split_sum_model(env, px, f0)
    rm = ratio(px['spec'], mdl['spec'])
    bins = []
    for lo, hi in NOV_BINS:
        sel = (mdl['nov'] >= lo) & (mdl['nov'] < hi)
        if sel.sum() < 10:
            continue
        bins.append({'nov': [lo, hi], 'px': int(sel.sum()), 'engine_over_integral': float(np.median(r[sel])), 'model_over_integral': float(np.median(ratio(mdl['spec'][sel], gt[sel]))),
                     'single_over_integral': float(np.median(ratio(mdl['single'][sel], gt[sel]))), 'engine_over_model': float(np.median(rm[sel]))})
    return {'px': int(len(r)), 'median_ratio': float(np.median(r)), 'p10_ratio': float(np.percentile(r, 10)), 'p90_ratio': float(np.percentile(r, 90)),
            'roughness_median': float(np.median(px['rough'])), 'gt_median_lum': float(np.median(lum(gt))), 'spec_median_lum': float(np.median(lum(px['spec']))),
            'model': {'median_ratio': float(np.median(rm)), 'p10_ratio': float(np.percentile(rm, 10)), 'p90_ratio': float(np.percentile(rm, 90)),
                      'single_median_lum': float(np.median(lum(mdl['single']))), 'multi_median_lum': float(np.median(lum(mdl['multi']))), 'model_median_lum': float(np.median(lum(mdl['spec'])))},
            'nov_bins': bins}


def chrome_vs_mirror(work, man, stride=8, f0=ref.FLOOR_F0):
    """The chrome floor's specular_indirect against the mirror reference (F(NoV) x the dumped environment along the
    reflected ray), median ratio over the floor; the split-sum and the reference both ignore the scene's geometry."""
    env = load_env(work)
    if env is None:
        return None
    fr = frames(work)[REF_FRAME - FIRST]
    cam = Camera(man.get('camera', {}).get('position', ref.CAMERA_EYE), man.get('camera', {}).get('target', ref.CAMERA_TARGET))
    px = pixel_shading_inputs(fr, cam, floor_mask(fr.world_positions('FRAME'), cam), stride=stride)
    mr, _ = mirror_reference(env, px['N'], px['V'], f0)
    r = ratio(px['spec'], mr)
    return {'px': int(len(r)), 'median_ratio': float(np.median(r)), 'p10_ratio': float(np.percentile(r, 10)), 'p90_ratio': float(np.percentile(r, 90))}


def floor_spec_ratio(work_now, work_before, man):
    """specular_indirect now / before over the floor (median)."""
    fr_n, fr_b = frames(work_now)[REF_FRAME - FIRST], frames(work_before)[REF_FRAME - FIRST]
    cam = Camera(man.get('camera', {}).get('position', ref.CAMERA_EYE), man.get('camera', {}).get('target', ref.CAMERA_TARGET))
    fl = floor_mask(fr_n.world_positions('FRAME'), cam)
    sn, sb = chan(fr_n, 'specular_indirect')[..., :3], chan(fr_b, 'specular_indirect')[..., :3]
    m = fl & (lum(sb) > 1e-4)
    r = lum(sn)[m] / lum(sb)[m]
    return {'px': int(m.sum()), 'median_ratio': float(np.median(r)), 'p10_ratio': float(np.percentile(r, 10)), 'p90_ratio': float(np.percentile(r, 90))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-split-sum'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--before', action='store_true')
    args = ap.parse_args()
    results, report = [], {'before_mode': args.before}
    before_dir = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-split-sum', 'before')
    if args.before and os.path.normcase(os.path.normpath(args.out)) == os.path.normcase(os.path.normpath(os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-split-sum'))):
        args.out = before_dir

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    def work(name):
        return os.path.join(args.out, name)

    # captures (sky bake: the chrome floor and the rough floors with the estimate off; PMREM: the same); the before
    # mode takes the same captures on the build before, into test-split-sum/before
    dirs = {k: work(k) for k in ('ref', 'rough30', 'rough50', 'hdri')}
    mans = {'ref': capture(work('ref'), ref.PROJECT, args.reuse, OFF),
            'rough30': capture(work('rough30'), ref.variant_project('rough30'), args.reuse, OFF),
            'rough50': capture(work('rough50'), ref.variant_project('rough50'), args.reuse, OFF),
            'hdri': capture(work('hdri'), ref.variant_project(ref.HDRI_VARIANT), args.reuse, OFF)}
    for key in ref.HDRI_ROUGH_VARIANTS:
        dirs[key] = work(key)
        mans[key] = capture(work(key), ref.variant_project(key), args.reuse, OFF)

    # ── P1: the sky bake's rough floors against the integral ──
    for key, label in (('rough30', 'roughness 0.3'), ('rough50', 'roughness 0.5')):
        r = gt_ratio(dirs[key], mans[key])
        report[f'p1_{key}'] = r
        if r is None:
            check(f'P1 sky bake, {label} metal floor: split-sum / CPU integral median ratio within [0.75, 1.3]', False, 'no environment dump')
        else:
            print(f'  {label} floor (sky bake, {r["px"]} px): split-sum / integral median {r["median_ratio"]:.3f} (p10 {r["p10_ratio"]:.3f}, p90 {r["p90_ratio"]:.3f})', flush=True)
            check(f'P1 sky bake, {label} metal floor: split-sum / CPU integral median ratio within [0.75, 1.3]', P1_LO <= r['median_ratio'] <= P1_HI,
                  f'median ratio {r["median_ratio"]:.3f} (p10 {r["p10_ratio"]:.3f}, p90 {r["p90_ratio"]:.3f}) over {r["px"]} px')
            m = r['model']
            print(f'  {label} floor: engine / CPU split-sum model {m["median_ratio"]:.3f} (p10 {m["p10_ratio"]:.3f}, p90 {m["p90_ratio"]:.3f}); model parts: single {m["single_median_lum"]:.4f}, '
                  f'multi {m["multi_median_lum"]:.4f}, integral {r["gt_median_lum"]:.4f}; by NoV: '
                  + '; '.join(f'[{b["nov"][0]:.2f},{b["nov"][1]:.2f}) n={b["px"]} engine/integral {b["engine_over_integral"]:.2f}, single/integral {b["single_over_integral"]:.2f}, engine/model {b["engine_over_model"]:.3f}' for b in r['nov_bins']), flush=True)
            check(f'P1b sky bake, {label} metal floor: the engine against the CPU model of its own approximations, median ratio within [0.9, 1.1]', P1B_LO <= m['median_ratio'] <= P1B_HI,
                  f'median ratio {m["median_ratio"]:.3f} (p10 {m["p10_ratio"]:.3f}, p90 {m["p90_ratio"]:.3f}); engine / integral by NoV '
                  + ', '.join(f'[{b["nov"][0]:.2f},{b["nov"][1]:.2f}) {b["engine_over_integral"]:.2f}' for b in r['nov_bins']))

    # ── P7: the sky irradiance frame (the dielectric box tops of the chrome-floor scene) ──
    env_ref = load_env(dirs['ref'])
    if env_ref is None:
        check('P7 the sky irradiance frame: the box tops\' diffuse_indirect / albedo against E / pi of the environment for the upward normal, per channel within [0.85, 1.15]', False, 'no environment dump')
    else:
        p7 = box_top_irradiance(dirs['ref'], env_ref)
        report['p7'] = p7
        rr = p7['ratio_median_rgb']
        print(f'  box tops: E/pi of the dump {np.round(p7["env_e_over_pi"], 4).tolist()}, ratio per channel {np.round(rr, 3).tolist() if rr else None}', flush=True)
        check('P7 the sky irradiance frame: the box tops\' diffuse_indirect / albedo against E / pi of the environment for the upward normal, per channel within [0.85, 1.15]',
              rr is not None and all(P7_LO <= v <= P7_HI for v in rr), f'ratio r/g/b {np.round(rr, 3).tolist() if rr else None} over {len(p7["boxes"])} boxes')

    # ── P8: the bake (the dumped prefiltered mips against a CPU prefilter) ──
    if env_ref is not None and load_mip_dump(dirs['ref'], 0) is not None:
        rng = np.random.default_rng(5)
        v = rng.random(400) * 0.95 + 0.05                       # upper hemisphere (z > 0.05)
        phi = rng.random(400) * 2 * np.pi
        st = np.sqrt(1 - v * v)
        dirs_w = np.stack([st * np.cos(phi), st * np.sin(phi), v], -1)
        p8, ok8, det8 = {}, True, []
        for mip in range(PREFILTERED_MIPS):
            dump = load_mip_dump(dirs['ref'], mip)
            if dump is None:
                p8[mip] = None
                continue
            rough = mip / (PREFILTERED_MIPS - 1)
            engine = env_lookup(dump, dirs_w)
            cpu = cpu_prefilter(env_ref, dirs_w, rough, downsample=1 if mip <= 2 else 2)
            r = lum(engine) / np.maximum(lum(cpu), 1e-6)
            p8[mip] = {'roughness': rough, 'median_ratio': float(np.median(r)), 'p10_ratio': float(np.percentile(r, 10)), 'p90_ratio': float(np.percentile(r, 90))}
            det8.append(f'mip {mip} (r {rough:.2f}): {p8[mip]["median_ratio"]:.3f}')
            if mip < P8_GATED_MIPS:
                ok8 &= P8_LO <= p8[mip]['median_ratio'] <= P8_HI
        report['p8'] = p8
        print('  prefiltered mips / CPU prefilter: ' + ', '.join(det8), flush=True)
        check('P8 the bake: each dumped prefiltered mip against the CPU GGX prefilter at the mip\'s roughness, median ratio within [0.9, 1.1] (mips 0-5 gated)', ok8, '; '.join(det8))
    else:
        check('P8 the bake: each dumped prefiltered mip against the CPU GGX prefilter at the mip\'s roughness, median ratio within [0.9, 1.1] (mips 0-5 gated)', False, 'no prefiltered mip dump')

    # ── P3: the PMREM path ──
    for key, label in (('hdri-rough30', 'roughness 0.3'), ('hdri-rough50', 'roughness 0.5')):
        r = gt_ratio(dirs[key], mans[key])
        report[f'p3_{key}'] = r
        if r:
            print(f'  {label} floor (PMREM, {r["px"]} px): split-sum / integral median {r["median_ratio"]:.3f} (p10 {r["p10_ratio"]:.3f}, p90 {r["p90_ratio"]:.3f}) [reported]', flush=True)

    if not args.before:
        # ── P2 / P3 control / P4: against the build before ──
        bd = {'ref': os.path.join(before_dir, 'ref'), 'hdri': os.path.join(before_dir, 'hdri')}
        if os.path.isdir(os.path.join(bd['ref'], 'region')):
            r2 = floor_spec_ratio(dirs['ref'], bd['ref'], mans['ref'])
            report['p2'] = r2
            check('P2 the chrome floor (sky bake): specular_indirect against the build before, median ratio within [0.95, 1.05]', P2_LO <= r2['median_ratio'] <= P2_HI,
                  f'median ratio {r2["median_ratio"]:.3f} (p10 {r2["p10_ratio"]:.3f}, p90 {r2["p90_ratio"]:.3f}) over {r2["px"]} px')
            report['p2_mirror'] = {'before': chrome_vs_mirror(bd['ref'], json.load(open(os.path.join(bd['ref'], 'manifest.json')))), 'after': chrome_vs_mirror(dirs['ref'], mans['ref'])}
            print('  chrome floor against the mirror reference of the dumped environment: before '
                  + (f'{report["p2_mirror"]["before"]["median_ratio"]:.3f}' if report['p2_mirror']['before'] else 'n/a') + ', after '
                  + (f'{report["p2_mirror"]["after"]["median_ratio"]:.3f}' if report['p2_mirror']['after'] else 'n/a') + ' [reported]', flush=True)
            fr_n, fr_b = frames(dirs['ref'])[REF_FRAME - FIRST], frames(bd['ref'])[REF_FRAME - FIRST]
            boxes = box_mask(fr_n.world_positions('FRAME'))
            sn, sb = chan(fr_n, 'specular_indirect')[..., :3], chan(fr_b, 'specular_indirect')[..., :3]
            m = boxes & (lum(sb) > 1e-5)
            report['p4'] = {'px': int(m.sum()), 'median_ratio': float(np.median(lum(sn)[m] / lum(sb)[m])) if m.any() else None,
                            'median_lum_before': float(np.median(lum(sb)[m])) if m.any() else None, 'median_lum_after': float(np.median(lum(sn)[m])) if m.any() else None}
            print(f'  dielectric boxes: specular_indirect now / before median {report["p4"]["median_ratio"]} over {int(m.sum())} px [reported]', flush=True)
        else:
            check('P2 chrome-floor capture of the build before available', False, bd['ref'])
        if os.path.isdir(os.path.join(bd['hdri'], 'region')):
            r3 = floor_spec_ratio(dirs['hdri'], bd['hdri'], mans['hdri'])
            report['p3_control'] = r3
            check('P3 the chrome floor under the PMREM: specular_indirect against the build before, median ratio within [0.95, 1.05]', P2_LO <= r3['median_ratio'] <= P2_HI,
                  f'median ratio {r3["median_ratio"]:.3f} (p10 {r3["p10_ratio"]:.3f}, p90 {r3["p90_ratio"]:.3f}) over {r3["px"]} px')
        else:
            check('P3 PMREM floor capture of the build before available', False, bd['hdri'])

        # ── P5 / P6: Sponza ──
        man_a = capture(work('sponza-camera-a'), FOG_PROJECT, args.reuse, {'r.SSR': 0}, frames=66, first=60, last=60, shot=60)
        man_c = capture_sponza(work('sponza-default'), args.reuse, sponza.PROJECT_CVARS)
        rows = {}
        for key, now, bef, frame_n, man in (('camera_a', work('sponza-camera-a'), os.path.join(DECALS, 'sponza-camera-a'), 60, man_a),
                                           ('camera_c', work('sponza-default'), os.path.join(DECALS, 'sponza-default'), SPONZA_FRAME, man_c)):
            if not os.path.isdir(os.path.join(bef, 'region')):
                rows[key] = None
                continue
            fr_n = regiondump.load_sequence(os.path.join(now, 'region'), frame_n, frame_n)[0]
            fr_b = regiondump.load_sequence(os.path.join(bef, 'region'), frame_n, frame_n)[0]
            s = identity(fr_n, fr_b)
            sn, sb = chan(fr_n, 'specular_indirect')[..., :3], chan(fr_b, 'specular_indirect')[..., :3]
            m = lum(sb) > 1e-4
            cn, cb = colour(fr_n), colour(fr_b)
            lc = lum(cb) > 1e-3
            s.update({'specular_ratio_median': float(np.median(lum(sn)[m] / lum(sb)[m])) if m.any() else None, 'specular_ratio_p10': float(np.percentile(lum(sn)[m] / lum(sb)[m], 10)) if m.any() else None,
                      'specular_ratio_p90': float(np.percentile(lum(sn)[m] / lum(sb)[m], 90)) if m.any() else None,
                      'colour_ratio_median': float(np.median(lum(cn)[lc] / lum(cb)[lc])), 'metallic_px': int((chan(fr_n, 'specular_indirect')[..., 3] > 0.5).sum()),
                      'deferred_ms_before': (json.load(open(os.path.join(bef, 'manifest.json'))).get('gpu_ms') or {}).get('DeferredLighting'),
                      'deferred_ms_after': (man.get('gpu_ms') or {}).get('DeferredLighting')})
            rows[key] = s
            print(f'  Sponza {key}: direct max |diff| {s["direct_max_diff"]:.3g}; specular_indirect ratio median {s["specular_ratio_median"]} (p10 {s["specular_ratio_p10"]}, p90 {s["specular_ratio_p90"]}); '
                  f'colour ratio {s["colour_ratio_median"]:.3f}; metallic px {s["metallic_px"]}; deferred {s["deferred_ms_before"]} -> {s["deferred_ms_after"]} ms', flush=True)
        report['p5'] = rows
        ok5 = all(r is not None and r['pos_max_diff'] <= 1e-6 and r['direct_max_diff'] <= 1e-6 for r in rows.values())
        check('P5 Sponza Camera A and C: positions and direct light identical to the build before (specular and colour ratios reported)', ok5,
              '; '.join(f'{k}: direct {r["direct_max_diff"]:.3g}, specular ratio {r["specular_ratio_median"]}, colour ratio {r["colour_ratio_median"]:.3f}' if r else f'{k}: no reference' for k, r in rows.items()))
        costs = [(r['deferred_ms_before'], r['deferred_ms_after']) for r in rows.values() if r and r['deferred_ms_before'] and r['deferred_ms_after']]
        check('P6 the deferred lighting pass within 10 % of the build before', bool(costs) and all(a <= P6_COST * b for b, a in costs),
              '; '.join(f'{b:.3f} -> {a:.3f} ms' for b, a in costs) if costs else 'no timing')
        report['montage'] = montage(args.out, [('roughness 0.5 metal, split-sum, before', os.path.join(before_dir, 'rough50')), ('roughness 0.5 metal, split-sum, after', dirs['rough50']),
                                               ('roughness 0.5 metal, env MIS (reference)', os.path.join(B05B, 'rough50-on')),
                                               ('Sponza Camera A, before', os.path.join(DECALS, 'sponza-camera-a')), ('Sponza Camera A, after', work('sponza-camera-a'))], 'split_sum_montage')

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'split_sum.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
