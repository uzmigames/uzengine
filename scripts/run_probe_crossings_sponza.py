"""followup-01, 2026-09-23, X2: does the crossing count (r.GI.RadianceCache.ProbeClassification = 2)
misfire on Sponza's open meshes?

Mode 2 adds one vote to mode 1: a probe is also inside when more than half of the classifier's 64 fixed
rays leave some mesh more often than they enter it (class 6). Over closed solids that is exact; over
open meshes (single-sided partitions) a free probe's ray can pick up an unmatched exit. Sponza is the
scene with those meshes, so it is where the risk shows.

One capture per configuration (QA: the H-63 setup; PROJECT: GDF 256 / mesh SDF 128), Camera C, rule 2,
the radiance cache diagnostic on. Per probe: class code and crossing fraction.

Pre-registered (followup-01 notes, "Crossing count for embedded faces"): more than 1 % of the probes
that mode 1 calls valid INSIDE the building (grid cells x 5..12, y 3..15, z 8..13 of the world-fixed
grid with origin (-2266.67, -2533.33, -2000)) turning class 6 = the open-mesh risk is real and mode 2 is
not proposed. Every class-6 probe is listed with its position, field distance and fractions.

Usage: python scripts/run_probe_crossings_sponza.py [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import runner  # noqa: E402
import run_probe_side_reference as refstudy  # noqa: E402
import run_probe_side_sponza as sponza  # noqa: E402

GRID_ORIGIN = np.array([-2266.6667, -2533.3333, -2000.0])
SPACING = 4000.0 / 15.0
BUILDING = ((5, 12), (3, 15), (8, 13))
X2_MAX = 0.01
FRAMES = 210
CONFIGS = {'QA': dict(sponza.S1_BASE), 'PROJECT': dict(sponza.PROJECT_CVARS)}


def in_building(world):
    cell = np.round((world - GRID_ORIGIN) / SPACING).astype(int)
    m = np.ones(len(world), bool)
    for axis, (lo, hi) in enumerate(BUILDING):
        m &= (cell[:, axis] >= lo) & (cell[:, axis] <= hi)
    return m, cell


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '63-probe-side-crossings', 'sponza'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    report = {'rule': 2, 'x2_max': X2_MAX, 'configs': {}}
    for name, cvars in CONFIGS.items():
        work = os.path.join(args.out, name)
        if not args.reuse:
            print(f'  ... {name}', flush=True)
            runner.require_exe()
            res = runner.run_capture(work, sponza.QA_PROJECT, FRAMES,
                                     cvars={**cvars, 'r.GI.RadianceCache.Diagnostic': 1,
                                            'r.GI.RadianceCache.ProbesPerFrame': 4096,
                                            'r.GI.RadianceCache.ProbeClassification': 2},
                                     timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise RuntimeError(f'{name}: exit {res["exit_code"]}')
        t = refstudy.probe_table(os.path.join(work, 'run.log'))
        if np.isnan(t['crossing']).all():
            raise RuntimeError(f'{name}: no crossingFraction in the RC transport lines (stale binary?)')
        cls = t['cls']
        inside_b, cell = in_building(t['world'])
        codes = {int(c): int((cls == c).sum()) for c in np.unique(cls)}
        mode1_valid_b = inside_b & np.isin(cls, (2, 4, 6))
        c6_b = inside_b & (cls == 6)
        frac = float(c6_b.sum() / mode1_valid_b.sum()) if mode1_valid_b.any() else None
        valid_cross = t['crossing'][inside_b & np.isin(cls, (2, 4))]
        rep = {'codes': codes, 'building_probes': int(inside_b.sum()),
               'building_mode1_valid': int(mode1_valid_b.sum()), 'building_class6': int(c6_b.sum()),
               'x2_fraction': frac, 'x2_pass': bool(frac is not None and frac <= X2_MAX),
               'class6_total': int((cls == 6).sum()),
               'valid_crossing_percentiles': (np.percentile(valid_cross, [50, 90, 99, 100]).tolist()
                                              if len(valid_cross) else None),
               'class6': [{'world': t['world'][i].tolist(), 'cell': cell[i].tolist(), 'building': bool(inside_b[i]),
                           'field': float(t['field'][i]), 'side': float(t['side'][i]),
                           'back': float(t['back'][i]), 'crossing': float(t['crossing'][i])}
                          for i in np.where(cls == 6)[0]]}
        report['configs'][name] = rep
        print(f'  {name}: codes {codes}; building: {rep["building_mode1_valid"]} mode-1-valid, '
              f'{rep["building_class6"]} class 6 -> {frac if frac is None else f"{frac:.2%}"} '
              f'({"PASS" if rep["x2_pass"] else "FAIL"}); class 6 overall {rep["class6_total"]}; '
              f'crossing fraction of valid building probes p50/p90/p99/max {rep["valid_crossing_percentiles"]}')
    report['pass'] = all(r['x2_pass'] for r in report['configs'].values())
    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, 'probe_crossings_sponza.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=1)
    print(f'  X2 {"PASS" if report["pass"] else "FAIL"} -> {path}')
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
