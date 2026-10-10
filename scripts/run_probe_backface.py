"""followup-01, H-63 follow-on (2026-09-18): do the rays of a probe the cache classifies "inside" (GDF
distance < 5 cm) find back faces, as a probe inside a closed volume would, or front faces, as a probe
in free space would?

Pre-registered in the followup-01 notes before this ran ("Back-face fraction at the probes"). The DDGI
per-probe line carries `hitBackFraction=` (share of HIT rays whose committed triangle was back-facing;
the trace culls nothing). Protocol: study 60's DDGI condition, 2 repeats.
  B0  all 4096 lines carry the field; skyFraction and hitCount equal 60/DDGI_r0 exactly, hitDist within
      1e-3 cm (the instrument changed nothing in the trace)
  B1  median back-face fraction over the GDF-invalid probes (fieldDistance < 5 cm, same process) and
      over the valid ones (control). INSIDE if invalid >= 0.5 and valid <= 0.1; FREE SPACE if invalid
      <= 0.1 and valid <= 0.1; MIXED otherwise. Reported: the split >= 0.5 / <= 0.1 / between, and
      the sixteen probes named in H-63.
  C   repeats agree on B1's medians to 1e-3.

Usage: python scripts/run_probe_backface.py [--repeats 2] [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import runner  # noqa: E402
import run_consumer_known_radiance as uniform  # noqa: E402
import run_consumer_support as support  # noqa: E402

BASE = support.BASE
COND = support.CONDITIONS['DDGI']
ROIS = uniform.ROIS
TOTAL = 4096
INVALID_BELOW = 5.0          # kSurfaceEpsilon in radiance_cache_trace_cs.hlsl
INSIDE_MIN = 0.5
FREE_MAX = 0.1
DIST_TOL = 1e-3
REPEAT_TOL = 1e-3
NAMED = (2537, 2793, 2553, 2809, 2536, 2792, 2538, 2794, 2251, 2507, 2252, 2267, 2265, 2266, 2250, 2777)
REFERENCE = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '60-consumer-support', 'DDGI_r0', 'run.log')

RE_DDGI = re.compile(r'DDGI transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) world=\((-?[\d.e+-]+),(-?[\d.e+-]+),(-?[\d.e+-]+)\) '
                     r'skyFraction=([\d.e+-]+) meanRadiance=[\d.e+-]+ skyLum=[\d.e+-]+ hitDist=([\d.e+-]+) hitLitFraction=[\d.e+-]+ '
                     r'hitAlbedo=[\d.e+-]+ hitCount=([\d.e+-]+)(?: hitBackFraction=([\d.e+-]+))?')
RE_RC = re.compile(r'RC transport: probe=(\d+) cell=\([\d,]+\) fieldDistance=(-?[\d.e+-]+)')


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def parse(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    ddgi, rc = {}, {}
    for m in RE_DDGI.finditer(text):
        ddgi[int(m.group(1))] = {'cell': tuple(int(m.group(k)) for k in (2, 3, 4)),
                                 'world': tuple(float(m.group(k)) for k in (5, 6, 7)),
                                 'sky': float(m.group(8)), 'hitDist': float(m.group(9)), 'hitCount': float(m.group(10)),
                                 'back': None if m.group(11) is None else float(m.group(11))}
    for m in RE_RC.finditer(text):
        rc[int(m.group(1))] = float(m.group(2))
    return ddgi, rc


def run(args, report):
    works = []
    for k in range(args.repeats):
        work = os.path.join(args.out, f'DDGI_r{k}')
        works.append(work)
        if args.reuse:
            continue
        print(f'  ... DDGI repeat {k}', flush=True)
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', uniform.PROJECT), uniform.FRAMES,
                                 regions=ROIS, region_fields='components', region_first=uniform.FIRST,
                                 region_last=uniform.LAST, cvars={**BASE, **COND}, timing_first=200, clean=True)
        if res['exit_code'] != 0:
            raise Stop('capture', f'repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = [{'dir': w, 'identity': runner.capture_identity(w)} for w in works]

    ref, _ = parse(REFERENCE)
    if len(ref) != TOTAL:
        raise Stop('B0', f'reference {REFERENCE}: {len(ref)} probe lines')
    rows = []
    for k, w in enumerate(works):
        ddgi, rc = parse(os.path.join(w, 'run.log'))
        if len(ddgi) != TOTAL or len(rc) != TOTAL:
            raise Stop('B0', f'r{k}: probe lines ddgi {len(ddgi)}, rc {len(rc)}')
        missing = [p for p in range(TOTAL) if ddgi[p]['back'] is None]
        if missing:
            raise Stop('B0', f'r{k}: {len(missing)} probe lines without hitBackFraction (first {missing[:5]})')
        # ---- B0: the trace is unchanged --------------------------------------------------------
        sky_diff = sum(1 for p in range(TOTAL) if ddgi[p]['sky'] != ref[p]['sky'])
        cnt_diff = sum(1 for p in range(TOTAL) if ddgi[p]['hitCount'] != ref[p]['hitCount'])
        dist_max = max(abs(ddgi[p]['hitDist'] - ref[p]['hitDist']) for p in range(TOTAL))
        if sky_diff or cnt_diff or dist_max > DIST_TOL:
            raise Stop('B0', f'r{k}: against 60/DDGI_r0: skyFraction differs on {sky_diff} probes, hitCount on {cnt_diff}, '
                             f'max |hitDist diff| {dist_max:.4g} cm')
        print(f'  B0 r{k}: {TOTAL} lines with hitBackFraction; skyFraction/hitCount identical to 60/DDGI_r0, hitDist max diff {dist_max:.2e} cm')
        # ---- B1 ----------------------------------------------------------------------------------
        fd = np.array([rc[p] for p in range(TOTAL)])
        back = np.array([ddgi[p]['back'] for p in range(TOTAL)])
        hits = np.array([ddgi[p]['hitCount'] for p in range(TOTAL)])
        inv = (fd < INVALID_BELOW) & (hits > 0)
        val = (fd >= INVALID_BELOW) & (hits > 0)
        inv_med, val_med = float(np.median(back[inv])), float(np.median(back[val]))
        split = {'ge_0.5': int((back[inv] >= INSIDE_MIN).sum()), 'le_0.1': int((back[inv] <= FREE_MAX).sum()),
                 'between': int(((back[inv] > FREE_MAX) & (back[inv] < INSIDE_MIN)).sum()), 'n': int(inv.sum()),
                 'no_hits': int(((fd < INVALID_BELOW) & (hits == 0)).sum())}
        vsplit = {'ge_0.5': int((back[val] >= INSIDE_MIN).sum()), 'le_0.1': int((back[val] <= FREE_MAX).sum()),
                  'between': int(((back[val] > FREE_MAX) & (back[val] < INSIDE_MIN)).sum()), 'n': int(val.sum())}
        named = [{'probe': p, 'cell': ddgi[p]['cell'], 'world': ddgi[p]['world'], 'fieldDistance': rc[p],
                  'hitDist': ddgi[p]['hitDist'], 'sky': ddgi[p]['sky'], 'back': ddgi[p]['back']} for p in NAMED]
        # a probe more than 5 cm inside and its rays' mean distance: the pairing H-63 reported
        deep = inv & (fd < -20.0)
        row = {'repeat': k, 'invalid_median_back': inv_med, 'valid_median_back': val_med, 'invalid_split': split,
               'valid_split': vsplit, 'named': named,
               'invalid_back_quantiles': [float(v) for v in np.percentile(back[inv], [10, 25, 50, 75, 90])],
               'valid_back_quantiles': [float(v) for v in np.percentile(back[val], [10, 25, 50, 75, 90])],
               'deeper_than_20cm': {'n': int(deep.sum()), 'median_back': float(np.median(back[deep])) if deep.any() else None}}
        rows.append(row)
        print(f'  B1 r{k}: invalid probes n={split["n"]} back-face median {inv_med:.3f} (p10..p90 {np.round(row["invalid_back_quantiles"], 3).tolist()})  '
              f'split >=0.5: {split["ge_0.5"]}  <=0.1: {split["le_0.1"]}  between: {split["between"]}  no hits: {split["no_hits"]}')
        print(f'        valid probes n={vsplit["n"]} back-face median {val_med:.3f} (p10..p90 {np.round(row["valid_back_quantiles"], 3).tolist()})  '
              f'split >=0.5: {vsplit["ge_0.5"]}  <=0.1: {vsplit["le_0.1"]}  between: {vsplit["between"]}')
        print(f'        deeper than 20 cm inside: n={row["deeper_than_20cm"]["n"]} median back {row["deeper_than_20cm"]["median_back"]}')
        print('        probe  cell          fd      hitDist  sky    back')
        for n in named:
            print(f'        {n["probe"]:5d}  {str(n["cell"]):12s} {n["fieldDistance"]:7.1f}  {n["hitDist"]:7.1f}  {n["sky"]:.3f}  {n["back"]:.3f}')
    report['rows'] = rows

    # ---- C ------------------------------------------------------------------------------------
    for key in ('invalid_median_back', 'valid_median_back'):
        meds = [r[key] for r in rows]
        if len(meds) >= 2 and max(abs(a - b) for a in meds for b in meds) > REPEAT_TOL:
            raise Stop('C', f'{key}: repeats disagree: {meds}')

    inv_med, val_med = rows[0]['invalid_median_back'], rows[0]['valid_median_back']
    if val_med > FREE_MAX:
        reading = 'MIXED (control failed)'
    elif inv_med >= INSIDE_MIN:
        reading = 'INSIDE'
    elif inv_med <= FREE_MAX:
        reading = 'FREE SPACE'
    else:
        reading = 'MIXED'
    report['reading'] = reading
    print(f'\n  -> {reading}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '61-probe-backface'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'back-face fraction at the probes — Camera C, real lighting, DDGI condition x {args.repeats}')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'base': BASE, 'condition': COND,
              'harness': runner.harness_stamp(), 'reference': REFERENCE,
              'criteria': {'invalid_below_cm': INVALID_BELOW, 'inside_min': INSIDE_MIN, 'free_max': FREE_MAX,
                           'dist_tol_cm': DIST_TOL, 'repeat_tol': REPEAT_TOL, 'named_probes': list(NAMED)}}
    code = 1
    try:
        run(args, report)
        report['status'] = 'complete'
        code = 0
    except Stop as s:
        report['stopped_at'] = s.gate
        report['reasons'].append(s.reason)
        print(f'\n  STOPPED at {s.gate}: {s.reason}')
        code = 2
    except Exception:  # noqa: BLE001 - every exit writes the report
        report['stopped_at'] = 'error'
        report['reasons'].append(traceback.format_exc())
        traceback.print_exc()
    finally:
        os.makedirs(args.out, exist_ok=True)
        path = runner.report_path(args.out, 'probe_backface.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
