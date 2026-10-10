"""Behavioural harness for the radiance cache probe classification (followup-01,
r.GI.RadianceCache.ProbeClassification). Runs the engine on the probe-side reference
(scripts/probe_side_reference.py, QA configuration) and checks what the GPU wrote, against geometry
whose sides are known by construction.

  T1  rule 0 is the current rule, exactly: class codes 0/1 only, class 1 <=> field distance < 5 cm,
      and the side classifier pipeline is not even created
  T2  rule 1 leaves the field alone: field distance identical to rule 0 on every probe; codes 2..5 only
  T3  rule 1: every probe inside solid is invalid
  T4  rule 1: NEAR_OUT's lit face is supported (<= 1 % unsupported) and within 10 % of CORE/NEAR_IN
  T5  rule 1: NEAR_OUT's sealed room is no brighter than under rule 0
  T6  rule 1: the sealed shell interior stays below 1e-3 of the lit faces
  N1  negative control: under rule 0 NEAR_OUT's lit face is unsupported (>= 99 %), so T4 measures the
      defect the candidate addresses rather than a scene that never had it

Usage: python scripts/test_probe_classification.py [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import runner  # noqa: E402
import probe_side_reference as ref  # noqa: E402
import run_probe_side_reference as study  # noqa: E402

CAMERAS = (('SOLID', 'EXT'), ('SOLID', 'NEAR_OUT'), ('SHELL', 'INT_E'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', 'test-probe-classification'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, labels, tables, logs = [], {}, {}, {}

    def check(name, ok, detail):
        results.append((name, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {name}: {detail}')

    cfg = {**study.BASE, **ref.CONFIGS['QA']}
    for rule in study.RULES:
        for scene, camera in CAMERAS:
            work = os.path.join(args.out, f'rule{rule}', f'{scene}_{camera}')
            if not args.reuse:
                res = runner.run_capture(work, ref.project_path(scene, camera), study.FRAMES,
                                         regions=ref.regions((scene, camera)), region_fields='components',
                                         region_first=study.FIRST, region_last=study.LAST,
                                         cvars={**cfg, 'r.GI.RadianceCache.ProbeClassification': rule},
                                         timing_first=200, clean=True)
                if res['exit_code'] != 0:
                    print(f'capture failed: {work}')
                    return 2
            labels[(rule, scene, camera)], _ = study.label_summary(work, scene, camera)
            tables[(rule, scene, camera)] = study.probe_table(os.path.join(work, 'run.log'))
            logs[(rule, scene, camera)] = open(os.path.join(work, 'run.log'), encoding='utf-8', errors='replace').read()

    t0, t1 = tables[(0, 'SOLID', 'EXT')], tables[(1, 'SOLID', 'EXT')]
    codes0 = set(np.unique(np.concatenate([tables[(0,) + k]['cls'] for k in CAMERAS])).tolist())
    rule_ok = all(np.array_equal(tables[(0,) + k]['cls'] == 1, tables[(0,) + k]['field'] < 5.0) for k in CAMERAS)
    created = any('created RadianceCache_ProbeSide' in logs[(0,) + k] for k in CAMERAS)
    check('T1 rule 0 is the current rule', codes0 <= {0, 1} and rule_ok and not created,
          f'codes {sorted(codes0)}, class 1 <=> field < 5: {rule_ok}, classifier created: {created}')

    same = all(np.array_equal(tables[(0,) + k]['field'], tables[(1,) + k]['field']) for k in CAMERAS)
    codes1 = set(np.unique(np.concatenate([tables[(1,) + k]['cls'] for k in CAMERAS])).tolist())
    check('T2 rule 1 leaves the field alone', same and codes1 <= {2, 3, 4, 5},
          f'field identical: {same}, codes {sorted(codes1)}')

    inside_ok, n_inside = True, 0
    for k in CAMERAS:
        t = tables[(1,) + k]
        cls, _, _ = ref.truth(k[0], t['world'])
        m = cls == 'inside'
        n_inside += int(m.sum())
        inside_ok &= bool(study.invalid_of(t['cls'][m]).all())
    check('T3 rule 1: inside solid is invalid', inside_ok, f'{n_inside} probe records inside solid')

    ext1 = labels[(1, 'SOLID', 'EXT')]
    lref = 0.5 * (ext1['CORE:lit_face']['mean'] + ext1['NEAR_IN:lit_face']['mean'])
    lit = ext1['NEAR_OUT:lit_face']
    check('T4 rule 1: the lit face is recovered', lit['unsupported'] <= 0.01 and abs(lit['mean'] / lref - 1.0) <= 0.10,
          f'unsupported {lit["unsupported"]:.3f}, mean / Lref {lit["mean"] / lref:.3f}')

    d0 = labels[(0, 'SOLID', 'NEAR_OUT')]['NEAR_OUT:dark_all']['mean']
    d1 = labels[(1, 'SOLID', 'NEAR_OUT')]['NEAR_OUT:dark_all']['mean']
    check('T5 rule 1: the sealed room is no brighter', d1 <= d0 + 1e-3 * lref, f'rule 0 {d0:.3g}, rule 1 {d1:.3g}')

    s1 = labels[(1, 'SHELL', 'INT_E')]['SHELL:all_e']['mean']
    check('T6 rule 1: the shell interior stays dark', s1 <= 1e-3 * lref, f'{s1:.3g} (Lref {lref:.3g})')

    n0 = labels[(0, 'SOLID', 'EXT')]['NEAR_OUT:lit_face']['unsupported']
    check('N1 negative control: rule 0 leaves the lit face unsupported', n0 >= 0.99, f'unsupported {n0:.3f}')

    failed = [r for r in results if not r[1]]
    print(f'{len(results) - len(failed)}/{len(results)} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
