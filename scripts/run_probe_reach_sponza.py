"""followup-01, 2026-09-24: mode 3 (reachability from the camera) on Sponza's probes — X2m and C3.

  X2m  among the building's probes (cells x 5..12, y 3..15, z 8..13) whose PROJECT field distance is
       >= 50 cm — open space by the field, the population mode 2 wrongly discarded (H-69) — at least
       95 % valid under mode 3 (Camera C, PROJECT configuration). Mode 0 on the same capture set is
       reported beside it.
  C3   convergence: `r.GI.RadianceCache.ReachPasses` 64 and 256 give identical class codes.

Every invalid probe of the X2m population is listed. Pre-registered in the followup-01 notes
("Reachability from the camera — pre-registration").

Usage: python scripts/run_probe_reach_sponza.py [--out DIR] [--reuse]
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
from run_probe_crossings_sponza import in_building  # noqa: E402

X2M_FIELD, X2M_MIN = 50.0, 0.95
FRAMES = 210
RUNS = {'rule0': {'r.GI.RadianceCache.ProbeClassification': 0},
        'rule3_p64': {'r.GI.RadianceCache.ProbeClassification': 3, 'r.GI.RadianceCache.ReachPasses': 64},
        'rule3_p256': {'r.GI.RadianceCache.ProbeClassification': 3, 'r.GI.RadianceCache.ReachPasses': 256}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '64-probe-reach', 'sponza-probes'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    tables = {}
    for name, cvars in RUNS.items():
        work = os.path.join(args.out, name)
        if not args.reuse:
            print(f'  ... {name}', flush=True)
            runner.require_exe()
            res = runner.run_capture(work, sponza.QA_PROJECT, FRAMES,
                                     cvars={**sponza.PROJECT_CVARS, 'r.GI.RadianceCache.Diagnostic': 1,
                                            'r.GI.RadianceCache.ProbesPerFrame': 4096, **cvars},
                                     timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise RuntimeError(f'{name}: exit {res["exit_code"]}')
        tables[name] = refstudy.probe_table(os.path.join(work, 'run.log'))

    t0, t3, t3b = tables['rule0'], tables['rule3_p64'], tables['rule3_p256']
    assert (t0['world'] == t3['world']).all() and (t3['world'] == t3b['world']).all()
    b, cell = in_building(t3['world'])
    pop = b & (t3['field'] >= X2M_FIELD)
    inv0, inv3 = refstudy.invalid_of(t0['cls']), refstudy.invalid_of(t3['cls'])
    x2 = float((~inv3[pop]).mean()) if pop.any() else None
    c3_diff = int((t3['cls'] != t3b['cls']).sum())
    report = {
        'X2m': {'population': int(pop.sum()), 'valid_mode3': x2, 'valid_mode0': float((~inv0[pop]).mean()),
                'pass': bool(x2 is not None and x2 >= X2M_MIN),
                'invalid': [{'world': t3['world'][i].tolist(), 'cell': cell[i].tolist(), 'field': float(t3['field'][i]),
                             'class': int(t3['cls'][i]), 'climb': float(t3['reloc'][i])}
                            for i in np.where(pop & inv3)[0]]},
        'C3': {'differing_probes': c3_diff, 'pass': c3_diff == 0},
        'building': {'probes': int(b.sum()),
                     'valid_mode0': int((~inv0[b]).sum()), 'valid_mode3': int((~inv3[b]).sum()),
                     'codes_mode3': {int(c): int(((t3['cls'] == c) & b).sum()) for c in np.unique(t3['cls'])}},
        'whole_grid': {'valid_mode0': int((~inv0).sum()), 'valid_mode3': int((~inv3).sum()),
                       'valid_both': int((~inv0 & ~inv3).sum()),
                       'mode0_only': int((~inv0 & inv3).sum()), 'mode3_only': int((inv0 & ~inv3).sum())},
    }
    report['pass'] = report['X2m']['pass'] and report['C3']['pass']
    print(f'  X2m: {report["X2m"]["population"]} building probes with field >= {X2M_FIELD:g} cm; valid mode 3 '
          f'{x2:.1%} (mode 0 {report["X2m"]["valid_mode0"]:.1%}) -> {"PASS" if report["X2m"]["pass"] else "FAIL"}')
    print(f'  C3: {c3_diff} probes differ between 64 and 256 passes -> {"PASS" if c3_diff == 0 else "FAIL"}')
    print(f'  building: {report["building"]}')
    print(f'  whole grid: {report["whole_grid"]}')
    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, 'probe_reach_sponza.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=1)
    print(f'  -> {path}')
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
