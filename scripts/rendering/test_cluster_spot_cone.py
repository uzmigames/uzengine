"""The cluster spot cone test (block B.7's open item): a spot light is assigned to a cluster only when its cone
reaches the cluster's bounding sphere. The lighting's cone factor is exactly 0 outside the outer angle, so the
sum is unchanged; only the slots and the per-pixel loop's work change.

Gates (pre-registered in the Rulebook task):
  K1  identity: on the lights reference (16 spots) and its dense variant (100 spots), taa_output and direct_light
      with the cone test (r.Lights.ClusterSpotCone 1) equal the sphere-only assignment (0): max |diff| <= 1e-5.
  K2  fewer slots: the mean cluster light count over lit pixels (r.Lights.ClusterDebug 1's rc_support record)
      drops by >= 20 % on the dense scene.
  K3  LightCull costs at most 0.05 ms more than the sphere-only assignment.
  K4  DeferredLighting on the dense scene is not slower (<= 1.02 x the sphere-only assignment).

Usage: python scripts/rendering/test_cluster_spot_cone.py [--out DIR] [--reuse]
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
import lights_reference as ref  # noqa: E402
from test_env_mis import lum, chan  # noqa: E402

REPO = runner.REPO
W, H = 1920, 1080
FRAMES, RF = 66, 60
K1_MAX, K2_DROP, K3_MS, K4_RATIO = 1e-5, 0.20, 0.05, 1.02
BASE = {'r.TechPanel.Show': 0, 'r.Toolbar.Show': 0, 'r.VolumetricFog': 0}


def capture(work, project, reuse, cvars):
    if reuse and os.path.isfile(os.path.join(work, 'manifest.json')):
        return json.load(open(os.path.join(work, 'manifest.json')))
    res = runner.run_capture(work, project, FRAMES, regions=[('FRAME', 0, 0, W, H)], region_fields='components', region_first=RF, region_last=RF,
                             cvars={**BASE, **cvars}, timing_first=40, clean=True, timeout=900, screenshot_frames=str(RF))
    if res['exit_code'] != 0:
        raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    return json.load(open(os.path.join(work, 'manifest.json')))


def frame(work):
    return regiondump.load_sequence(os.path.join(work, 'region'), RF, RF)[0]


def gpu(man, key):
    return (man.get('gpu_ms') or {}).get(key)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-cluster-spot-cone'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    scenes = {'ref': 'lights-reference', 'dense': 'lights-reference-400'}
    mans = {}
    for key, variant in scenes.items():
        for cone in (0, 1):
            name = f'{key}-cone{cone}'
            mans[name] = capture(os.path.join(args.out, name), ref.project_path(variant), args.reuse, {'r.Lights.ClusterSpotCone': cone})
        # the cluster light count: ClusterDebug 1 writes (slice, count, first slot, tile) into rc_support
        for cone in (0, 1):
            name = f'{key}-cone{cone}-debug'
            mans[name] = capture(os.path.join(args.out, name), ref.project_path(variant), args.reuse, {'r.Lights.ClusterSpotCone': cone, 'r.Lights.ClusterDebug': 1})

    # ── K1: identity ──
    ok1, det1 = True, []
    for key in scenes:
        f0, f1 = frame(os.path.join(args.out, f'{key}-cone0')), frame(os.path.join(args.out, f'{key}-cone1'))
        c0, c1 = chan(f0, 'taa_output')[..., :3], chan(f1, 'taa_output')[..., :3]
        d0, d1 = chan(f0, 'direct_light')[..., :3], chan(f1, 'direct_light')[..., :3]
        dc, dd = float(np.abs(c1 - c0).max()), float(np.abs(d1 - d0).max())
        report[f'k1_{key}'] = {'taa_max_diff': dc, 'direct_max_diff': dd}
        ok1 &= dc <= K1_MAX and dd <= K1_MAX
        det1.append(f'{key}: taa {dc:.3g}, direct {dd:.3g}')
    check('K1 identity: taa_output and direct_light with the cone test equal the sphere-only assignment (max |diff| <= 1e-5)', ok1, '; '.join(det1))

    # ── K2: fewer slots ──
    counts = {}
    for key in scenes:
        for cone in (0, 1):
            fr = frame(os.path.join(args.out, f'{key}-cone{cone}-debug'))
            rc = chan(fr, 'rc_support')
            pos = fr.world_positions('FRAME')
            lit = np.isfinite(pos).all(-1) & (lum(chan(fr, 'taa_output')[..., :3]) > 1e-3)
            cnt = rc[..., 1][lit]
            counts[(key, cone)] = {'mean': float(cnt.mean()), 'p90': float(np.percentile(cnt, 90)), 'max': float(cnt.max()), 'px': int(lit.sum())}
    report['k2'] = {f'{k}-cone{c}': v for (k, c), v in counts.items()}
    drop = 1.0 - counts[('dense', 1)]['mean'] / max(counts[('dense', 0)]['mean'], 1e-6)
    drop_ref = 1.0 - counts[('ref', 1)]['mean'] / max(counts[('ref', 0)]['mean'], 1e-6)
    report['k2']['dense_drop'] = drop
    report['k2']['ref_drop'] = drop_ref
    print(f'  cluster light count (mean over lit px): ref {counts[("ref", 0)]["mean"]:.2f} -> {counts[("ref", 1)]["mean"]:.2f} ({100 * drop_ref:.1f} % fewer); '
          f'dense {counts[("dense", 0)]["mean"]:.2f} -> {counts[("dense", 1)]["mean"]:.2f} ({100 * drop:.1f} % fewer)', flush=True)
    check('K2 fewer slots: the mean cluster light count over lit pixels drops by >= 20 % on the dense scene with the cone test', drop >= K2_DROP,
          f'dense {counts[("dense", 0)]["mean"]:.2f} -> {counts[("dense", 1)]["mean"]:.2f} ({100 * drop:.1f} % fewer); ref {100 * drop_ref:.1f} % fewer')

    # ── K3 / K4: cost ──
    t = {name: {'LightCull': gpu(m, 'LightCull'), 'DeferredLighting': gpu(m, 'DeferredLighting')} for name, m in mans.items() if not name.endswith('debug')}
    report['timings'] = t
    lc0, lc1 = t['dense-cone0']['LightCull'], t['dense-cone1']['LightCull']
    dl0, dl1 = t['dense-cone0']['DeferredLighting'], t['dense-cone1']['DeferredLighting']
    check('K3 LightCull costs at most 0.05 ms more than the sphere-only assignment (dense scene)', lc0 is not None and lc1 is not None and lc1 <= lc0 + K3_MS,
          f'{lc0} -> {lc1} ms')
    check('K4 DeferredLighting on the dense scene is not slower (<= 1.02 x the sphere-only assignment)', dl0 is not None and dl1 is not None and dl1 <= K4_RATIO * dl0,
          f'{dl0} -> {dl1} ms (ref {t["ref-cone0"]["DeferredLighting"]} -> {t["ref-cone1"]["DeferredLighting"]} ms)')

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'cluster_spot_cone.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
