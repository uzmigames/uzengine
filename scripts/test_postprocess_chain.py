"""The post-process chain (Rulebook task phase0_postprocess-chain-restore, re-scoped 2026-10-09): an inventory of the
passes the frame registers and the image identity across the change that documents them.

The first version of this task claimed 7eea6b95 dropped CAS_Sharpen, ColorGradingLUT, DOF, FXAA, LDR_Blit, LensFlare
and MotionBlur. Wrong: in 7eea6b95^ they sat after an unconditional `return; // Skip full pipeline` that bd4db199
(2026-03-31) put in RegisterPostProcessPasses (the SponzaTest reference has the same return), and their shaders are
the rewrite's scaffolds (one-line no-op kernels; CAS and FXAA copy their input). 7eea6b95 deleted dead text. The frame
has been Bloom + Tonemap (+ TAA / SSR composite / region-dump readback when active) since 2026-03-31.

Gates:
  I1  inventory: no CAS, FXAA, DOF, LensFlare, MotionBlur or ColorGrading pass is registered in any configuration
      (defaults, all off, TAA off, DOF on, lens flare on), as before the change; a later task that implements a shader
      and wires its pass flips its row deliberately.
  I2  identity: the default and the all-off screenshots equal the before captures within the cross-process floor of
      two before-runs (measured 0: bit-identical).
  The Lua lens-flare setter's CVar sync is checked by a one-off Lua script (the task's notes), not here.

Usage: python scripts/test_postprocess_chain.py [--before] [--reuse]
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
import runner  # noqa: E402

REPO = runner.REPO
OUT = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-postprocess-chain')
REF = os.path.join(REPO, 'scripts', 'reflection-reference.uzproject')
SUN = os.path.join(REPO, 'scripts', 'cloud-reference-sun.uzproject')
SPONZA = os.path.join(REPO, 'scripts', 'sponza-lighting-camera-c.uzproject')
OFF = {'r.CAS': 0, 'r.MotionBlur': 0, 'r.DOF': 0, 'r.LensFlare': 0}
FRAMES, SHOT = 130, 120
CASES = {
    'ref-default': (REF, {}),
    'ref-alloff': (REF, OFF),
    'ref-alloff-repeat': (REF, OFF),
    'ref-notaa': (REF, {'r.TAA': 0}),
    'ref-dof': (REF, {'r.DOF': 1, 'r.DOF.FocusDistance': 500}),
    'sun-lensflare': (SUN, {'r.LensFlare': 1}),
    'sponza-default': (SPONZA, {}),
}
POST_KEYS = ('CAS', 'MotionBlur', 'FXAA', 'DOF', 'LensFlare', 'LDR', 'ColorGrading', 'Tonemap', 'Bloom')


def capture(work, project, cvars, reuse):
    if reuse and os.path.isfile(os.path.join(work, 'manifest.json')):
        return json.load(open(os.path.join(work, 'manifest.json')))
    res = runner.run_capture(work, project, FRAMES, cvars={'r.TechPanel.Show': 0, 'r.Toolbar.Show': 0, **cvars},
                             timing_first=60, clean=True, timeout=900, screenshot_frames=f'{SHOT}-{SHOT}')
    if res['exit_code'] != 0:
        raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    return json.load(open(os.path.join(work, 'manifest.json')))


def post_passes(man):
    return {k: round(v, 4) for k, v in (man.get('gpu_ms') or {}).items() if any(p in k for p in POST_KEYS)}


def shot(work):
    from PIL import Image
    d = os.path.join(work, 'frames')
    files = sorted(f for f in os.listdir(d) if f.endswith('.png')) if os.path.isdir(d) else []
    if not files:
        return None
    return np.asarray(Image.open(os.path.join(d, files[-1])).convert('RGB'), dtype=np.float64)


def diff(a, b):
    if a is None or b is None or a.shape != b.shape:
        return None
    d = np.abs(a - b)
    return {'mean': float(d.mean()), 'max': float(d.max()), 'p99': float(np.percentile(d, 99))}


def laplacian_energy(img):
    y = img @ np.array([0.2126, 0.7152, 0.0722])
    lap = 4 * y[1:-1, 1:-1] - y[:-2, 1:-1] - y[2:, 1:-1] - y[1:-1, :-2] - y[1:-1, 2:]
    return float((lap ** 2).mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--before', action='store_true')
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, report = [], {'cases': {}}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    side = 'before' if args.before else 'after'
    mans = {}
    for name, (project, cvars) in CASES.items():
        if args.before is False and name == 'ref-alloff-repeat':
            continue
        work = os.path.join(OUT, side, name)
        mans[name] = capture(work, project, cvars, args.reuse)
        report['cases'][name] = {'post_gpu_ms': post_passes(mans[name]), 'exe': mans[name].get('executable', {}).get('hash')}
        print(f'  {side} {name}: post passes {report["cases"][name]["post_gpu_ms"]}', flush=True)
    has = lambda name, key: any(key in k for k in report['cases'][name]['post_gpu_ms'])
    if args.before:
        fl = diff(shot(os.path.join(OUT, 'before', 'ref-alloff')), shot(os.path.join(OUT, 'before', 'ref-alloff-repeat')))
        report['floor'] = fl
        print(f'  floor (two before-runs of the all-off configuration): {fl}', flush=True)
        check('B1 the before captures completed', all(mans.values()), f'{len(mans)} captures')
        check('B2 before: CAS, FXAA, DOF, LensFlare absent from gpu_ms (the regression)',
              not any(has(n, k) for n in mans for k in ('CAS', 'FXAA', 'DOF', 'LensFlare')),
              {n: sorted(report['cases'][n]['post_gpu_ms']) for n in mans})
    else:
        bfloor = json.load(open(os.path.join(OUT, 'before', 'postprocess_chain.json')))['report'].get('floor') or {'mean': 0.0, 'max': 0.0}
        dead = ('CAS', 'FXAA', 'DOF', 'LensFlare', 'MotionBlur', 'ColorGrading', 'LDR')
        reg = {n: sorted(k for k in report['cases'][n]['post_gpu_ms'] if any(x in k for x in dead)) for n in report['cases']}
        check('I1 inventory: no scaffold post-process pass is registered in any configuration', not any(reg.values()), reg)
        ids = {}
        for name in ('ref-default', 'ref-alloff', 'sponza-default'):
            ids[name] = diff(shot(os.path.join(OUT, 'after', name)), shot(os.path.join(OUT, 'before', name)))
        report['identity'] = {'delta': ids, 'floor': bfloor}
        ok2 = all(d is not None and d['mean'] <= max(2 * bfloor['mean'], 0.05) and d['max'] <= bfloor['max'] + 1 for d in ids.values())
        check('I2 identity: the default and all-off frames equal the build before within the floor', ok2, f'{ids}, floor {bfloor}')
        g6 = {}
        for name in ('ref-default', 'sponza-default', 'sun-lensflare'):
            a, b = shot(os.path.join(OUT, 'after', name)), shot(os.path.join(OUT, 'before', name))
            g6[name] = {'delta': diff(a, b), 'laplacian_ratio': (laplacian_energy(a) / max(laplacian_energy(b), 1e-12)) if a is not None and b is not None else None}
        report['g6'] = g6
        print(f'  G6 [reported] {g6}', flush=True)
        report['motion_blur_passes'] = {n: [k for k in report['cases'][n]['post_gpu_ms'] if 'MotionBlur' in k] for n in report['cases']}
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    os.makedirs(os.path.join(OUT, side), exist_ok=True)
    with open(os.path.join(OUT, side, 'postprocess_chain.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
