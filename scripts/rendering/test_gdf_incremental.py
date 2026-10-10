"""Incremental global distance field — gates of lighting block B, item 2 (phase 2: toroidal scroll).

The clipmaps are stored toroidally (world voxel k in texel k mod resolution) and a recentre
recomposes only the slabs that entered the window (`r.GI.GDF.Incremental`, default on). Three
gates, pre-registered in `.rulebook/tasks/phase0_lighting-block-b-02-gdf-incremental/tasks.md`:

  G1  field identity. `scripts/rendering/run_gdf_field_compare.py` (boundary trajectory, clipmap 0,
      `r.GI.GDF.Diagnostic.FieldCompare=1`): in toroidal mode the diagnostic compares the field
      written by the slab path with a full recompose of the same window on the next frame, texel by
      texel. Verdict must be IDENTICAL (every event 100 % identical, max |delta| 0 cm).
  G4  Sponza Camera C: the three ROIs' `diffuse_indirect` means (project configuration, gather on)
      with `r.GI.GDF.Incremental` 1 vs 0 agree to 1 % relative. Phase 2 measured 2e-6 (only the
      one-voxel window shrink of the readers differed); since phase 4 the regional path keeps the
      radiance cache's history outside the change boxes while a whole-window recompose discards it,
      and the steady state after loading differs by the residual of that blending (0.1-0.2 %
      measured; `scripts/rendering/run_gdf_regional_ab.py` shows regional + history discarded == whole-window
      exactly, so the field is the same).
  G5  recentre cost: on the long sweep (`sponza-longsweep-camera.uzproject`, 300 frames, timing from
      frame 40) the `GDF_Composite` mean GPU time with Incremental 1 is <= 25 % of the mean with
      Incremental 0; the same path produces the same recentres in both runs.
  G6  (phase 3) the truncation band `r.GI.GDF.BandVoxels` (default 32) leaves the Camera C ROI means
      within 1e-4 relative of the untruncated field (band 0), both with whole-window recomposes so
      only the band differs: the readers only step by the distance (measured bit-identical).

Usage: python scripts/rendering/test_gdf_incremental.py [--out DIR] [--reuse] [--skip-g1]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
import run_probe_side_sponza as sponza  # noqa: E402

SWEEP_PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-longsweep-camera.uzproject')
CLASSIFICATION = {'r.GI.RadianceCache.ProbeClassification': 3, 'r.GI.RadianceCache.ReachRelocate': 1,
                  'r.GI.RadianceCache.ReachVisibleSeeds': 1}        # the project configuration since 6.4 closed
# G4: since phase 4 the regional path keeps the radiance cache's history outside the change boxes
# (a whole-window recompose discards it), so the steady state after Sponza's loading differs by the
# residual of that blending: 0.1-0.2 % measured, gate 1 %. The field itself is bit-identical (field
# compare IDENTICAL; scripts/rendering/run_gdf_regional_ab.py: regional + history discarded == whole-window).
G4_REL_TOL, G6_REL_TOL, G5_RATIO_MAX = 1e-2, 1e-4, 0.25
REC709 = np.array([0.2126, 0.7152, 0.0722])


def capture_rois(work, incremental, reuse, band=None, extra=None):
    cvars = {**sponza.PROJECT_CVARS, **CLASSIFICATION, 'r.GI.GDF.Incremental': incremental}
    if band is not None:
        cvars['r.GI.GDF.BandVoxels'] = band
    if extra:
        cvars.update(extra)
    if not reuse:
        res = runner.run_capture(work, sponza.QA_PROJECT, sponza.uniform.FRAMES, regions=sponza.ROIS,
                                 region_fields='components', region_first=sponza.uniform.FIRST,
                                 region_last=sponza.uniform.LAST, cvars=cvars, timing_first=200, clean=True)
        if res['exit_code'] != 0:
            raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    return sponza.roi_support(work, sponza.uniform.FIRST, sponza.uniform.LAST)


def capture_sweep(work, incremental, reuse):
    if not reuse:
        res = runner.run_capture(work, SWEEP_PROJECT, 300, cvars={'r.GI.GDF.Incremental': incremental},
                                 timing_first=40, clean=True)
        if res['exit_code'] != 0:
            raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    with open(os.path.join(work, 'manifest.json'), encoding='utf-8') as fh:
        m = json.load(fh)
    recentres = 0
    with open(os.path.join(work, 'run.log'), encoding='utf-8', errors='replace') as fh:
        for line in fh:
            if 'GI recentre:' in line and 'gdfClipmap=0' in line and 'reason=recentre' in line:
                recentres += 1
    return {'composite_ms': (m.get('gpu_ms') or {}).get('GDF_Composite'), 'frame_ms': (m.get('gpu_ms') or {}).get('_frame'),
            'samples': (m.get('frames') or {}).get('timing_samples'), 'recentres_clipmap0': recentres}


def g1(out, reuse):
    cmd = [sys.executable, os.path.join(runner.REPO, 'scripts', 'rendering', 'run_gdf_field_compare.py'), '--repeats', '1',
           '--out', os.path.join(out, 'g1-field-compare')]
    if reuse:
        cmd.append('--reuse')
    res = subprocess.run(cmd, cwd=runner.REPO, capture_output=True, text=True, timeout=3600)
    text = res.stdout + res.stderr
    m = re.search(r'verdict over (\d+) events: (\w+)', text)
    return {'exit': res.returncode, 'events': int(m.group(1)) if m else 0, 'verdict': m.group(2) if m else None,
            'tail': text[-600:]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', 'test-gdf-incremental'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--skip-g1', action='store_true')
    args = ap.parse_args()
    if not args.reuse:
        runner.require_exe()
    results = []

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}')

    report = {}
    if not args.skip_g1:
        print('G1 field compare (slab result vs full recompose)', flush=True)
        report['g1'] = g1(args.out, args.reuse)
        check('G1 slab recompose == full recompose on every recentre',
              report['g1']['verdict'] == 'IDENTICAL' and report['g1']['events'] > 0,
              f'{report["g1"]["events"]} events, verdict {report["g1"]["verdict"]}')
    print('G4 Sponza Camera C ROIs, Incremental 1 vs 0', flush=True)
    on = capture_rois(os.path.join(args.out, 'g4-incremental-on'), 1, args.reuse)
    off = capture_rois(os.path.join(args.out, 'g4-incremental-off'), 0, args.reuse)
    rel = {n: abs(on[n]['mean'] - off[n]['mean']) / max(abs(off[n]['mean']), 1e-12) for n in on}
    report['g4'] = {'on': on, 'off': off, 'relative': rel}
    for n in on:
        print(f'  {n:12} off {off[n]["mean"]:.6e}  on {on[n]["mean"]:.6e}  rel {rel[n]:.2e}  unsupported {off[n]["unsupported"]:.4f} -> {on[n]["unsupported"]:.4f}')
    check('G4 Camera C ROI means within 1 % of the whole-window path (history kept outside the change boxes)',
          all(v <= G4_REL_TOL for v in rel.values()), {n: f'{v:.2e}' for n, v in rel.items()})
    # G6 (phase 3): the truncation band (r.GI.GDF.BandVoxels, default 32) must leave the lighting
    # unchanged against the untruncated field: the readers only step by the distance. Measured on
    # whole-window recomposes on both sides (band 0 disables the regional path), so only the band differs.
    print('G6 Sponza Camera C ROIs, band 32 vs untruncated (whole-window recomposes)', flush=True)
    untruncated = capture_rois(os.path.join(args.out, 'g6-band-0'), 0, args.reuse, band=0)
    rel6 = {n: abs(off[n]['mean'] - untruncated[n]['mean']) / max(abs(untruncated[n]['mean']), 1e-12) for n in off}
    report['g6'] = {'untruncated': untruncated, 'relative': rel6}
    for n in off:
        print(f'  {n:12} band 0 {untruncated[n]["mean"]:.6e}  band 32 {off[n]["mean"]:.6e}  rel {rel6[n]:.2e}')
    check('G6 Camera C ROI means unchanged by the band (<= 1e-4 relative)', all(v <= G6_REL_TOL for v in rel6.values()),
          {n: f'{v:.2e}' for n, v in rel6.items()})
    print('G5 long sweep recentre cost, Incremental 1 vs 0', flush=True)
    sweep_on = capture_sweep(os.path.join(args.out, 'g5-incremental-on'), 1, args.reuse)
    sweep_off = capture_sweep(os.path.join(args.out, 'g5-incremental-off'), 0, args.reuse)
    report['g5'] = {'on': sweep_on, 'off': sweep_off}
    print(f'  GDF_Composite mean ms: off {sweep_off["composite_ms"]}  on {sweep_on["composite_ms"]}; '
          f'clipmap-0 recentres off {sweep_off["recentres_clipmap0"]} on {sweep_on["recentres_clipmap0"]}; '
          f'frame ms off {sweep_off["frame_ms"]} on {sweep_on["frame_ms"]}')
    ok5 = (sweep_off['composite_ms'] and sweep_on['composite_ms'] is not None
           and sweep_on['composite_ms'] <= G5_RATIO_MAX * sweep_off['composite_ms']
           and sweep_on['recentres_clipmap0'] == sweep_off['recentres_clipmap0'] and sweep_off['recentres_clipmap0'] > 0)
    check('G5 recentre cost <= 25 % of the full recompose, same recentres', ok5,
          f'ratio {sweep_on["composite_ms"] / sweep_off["composite_ms"] if sweep_off["composite_ms"] else None}')
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'gdf_incremental.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]},
                  fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
