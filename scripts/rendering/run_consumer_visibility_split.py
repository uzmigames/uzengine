"""followup-01: the remaining DXR/SDF gap at the consumer under REAL lighting, split into the visibility
test (Chebyshev under real depths) and the writers.

Pre-registered in the followup-01 notes, 2026-09-18, before this ran.
  conditions  SDF (RT off) / DDGI (RT on, real depths) / DDGI_FAR (RT on, r.GI.Diagnostic.DdgiFarDepth=1:
              the depth pass stores the far distance, the radiance stays real -> visibility 1)
  V = DDGI / DDGI_FAR    the energy the visibility test removes under real depths
  W = DDGI_FAR / SDF     what separates the paths with visibility out of the way
  P1' = DDGI / SDF       = V x W on the same captures (consistency line)
  K0  the DDGI identity echoes farDepth as asked; the two DDGI captures share sky, sun and sequence.
  expectation (reported, not judged): W median within +-0.10 of the R1 writer-side prediction.
  C   repeats agree on the V and W medians to 1e-3.

Usage: python scripts/rendering/run_consumer_visibility_split.py [--repeats 2] [--out DIR] [--reuse]
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
import run_probe_sky_parity as stage1  # noqa: E402
import run_probe_hit_identity as stage3  # noqa: E402
import run_consumer_known_radiance as uniform  # noqa: E402
import run_consumer_border_test as border  # noqa: E402

BASE = {k: v for k, v in uniform.BASE.items() if not k.startswith('r.GI.Diagnostic.')}
BASE['r.GI.Diagnostic.KnownRadiance'] = 0
CONDITIONS = {
    'SDF':      {'r.RayTracing': 0, 'r.RayTracing.GI': 0, 'r.GI.Diagnostic.DdgiFarDepth': 0},
    'DDGI':     {'r.RayTracing': 1, 'r.RayTracing.GI': 1, 'r.GI.Diagnostic.DdgiFarDepth': 0},
    'DDGI_FAR': {'r.RayTracing': 1, 'r.RayTracing.GI': 1, 'r.GI.Diagnostic.DdgiFarDepth': 1},
}
ROIS = uniform.ROIS
EXPECT_BAND = 0.10
REPEAT_TOL = 1e-3
RE_FAR = re.compile(r'DDGI transport: identity [^\n]* farDepth=(\d)')
RE_SEQ = re.compile(r'DDGI transport: identity [^\n]* sequence=(\S+)')


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def identity(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    dd = stage3.RE_ID_DDGI.search(text)
    far, seq = RE_FAR.search(text), RE_SEQ.search(text)
    rc = stage1.RE_ID_RC.search(text)
    out = {'ddgi': None, 'farDepth': int(far.group(1)) if far else None, 'sequence': seq.group(1) if seq else None,
           'rc_skySrv': int(rc.group(1)) if rc else None,
           # Comparable ACROSS processes: mip, swizzle, rays, multiplier. The SRV index is a bindless heap
           # slot and differs between a process with DXR on and one with it off (37225 vs 37223 on the
           # first run of this study, same cubemap); it is compared only WITHIN a process.
           'rc_identity': None if not rc else (int(rc.group(2)), rc.group(3), int(rc.group(4)),
                                               [float(rc.group(k)) for k in (6, 7, 8)])}
    if dd:
        out['ddgi'] = {'skySrv': int(dd.group(1)), 'multiplier': [float(dd.group(k)) for k in (6, 7, 8)],
                       'lightColor': [float(dd.group(k)) for k in (9, 10, 11)],
                       'lightDir': [float(dd.group(k)) for k in (12, 13, 14)], 'lightIntensity': float(dd.group(15))}
    return out


def stats(num, den):
    mask = num['geo'] & den['geo'] & (den['lum'] > uniform.MIN_DENOM)
    if not mask.any():
        return None
    q = num['lum'][mask] / den['lum'][mask]
    return {'n': int(mask.sum()), 'median': float(np.median(q)), 'p10': float(np.percentile(q, 10)), 'p90': float(np.percentile(q, 90))}


def run(args, report):
    works = {c: [] for c in CONDITIONS}
    for k in range(args.repeats):
        for cond, extra in CONDITIONS.items():
            work = os.path.join(args.out, f'{cond}_r{k}')
            works[cond].append(work)
            if args.reuse:
                continue
            print(f'  ... {cond} repeat {k}', flush=True)
            res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', uniform.PROJECT), uniform.FRAMES,
                                     regions=ROIS, region_fields='components', region_first=uniform.FIRST,
                                     region_last=uniform.LAST, cvars={**BASE, **extra}, timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{cond} repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w)} for w in ws] for c, ws in works.items()}

    # ---- K0 --------------------------------------------------------------------------------
    for k in range(args.repeats):
        a = identity(os.path.join(works['DDGI'][k], 'run.log'))
        b = identity(os.path.join(works['DDGI_FAR'][k], 'run.log'))
        s = identity(os.path.join(works['SDF'][k], 'run.log'))
        if a['ddgi'] is None or b['ddgi'] is None:
            raise Stop('K0', f'r{k}: DDGI identity line missing (DDGI {a["ddgi"] is not None}, FAR {b["ddgi"] is not None})')
        if a['farDepth'] != 0 or b['farDepth'] != 1:
            raise Stop('K0', f'r{k}: farDepth echoed as DDGI={a["farDepth"]}, FAR={b["farDepth"]}')
        if a['ddgi'] != b['ddgi'] or a['sequence'] != b['sequence']:
            raise Stop('K0', f'r{k}: the two DDGI captures differ in writer identity: {a} vs {b}')
        # Within each DXR-on process the cache and the DDGI must sample the same cubemap (same SRV);
        # across processes only the identity tuple (mip, swizzle, rays, multiplier) is comparable.
        for label, cap in (('DDGI', a), ('DDGI_FAR', b)):
            if cap['rc_skySrv'] is None or cap['rc_skySrv'] != cap['ddgi']['skySrv']:
                raise Stop('K0', f'r{k}: {label}: the cache (SRV {cap["rc_skySrv"]}) and the DDGI (SRV {cap["ddgi"]["skySrv"]}) '
                                 f'do not share the sky within the process')
        if s['rc_identity'] is None or s['rc_identity'] != a['rc_identity'] or a['rc_identity'] != b['rc_identity']:
            raise Stop('K0', f'r{k}: cache identity differs across conditions: SDF {s["rc_identity"]}, DDGI {a["rc_identity"]}, '
                             f'FAR {b["rc_identity"]}')
        print(f'  K0 r{k}: DDGI/FAR identical writer identity (sequence {a["sequence"]}, sun {a["ddgi"]["lightColor"]}), farDepth 0/1, sky SRV shared with SDF')

    # ---- V / W / P1' -----------------------------------------------------------------------
    rows = []
    for k in range(args.repeats):
        sdf = border.load(runner.require_region_dir(os.path.join(works['SDF'][k], 'region'), f'SDF r{k}'))
        dd = border.load(runner.require_region_dir(os.path.join(works['DDGI'][k], 'region'), f'DDGI r{k}'))
        far = border.load(runner.require_region_dir(os.path.join(works['DDGI_FAR'][k], 'region'), f'DDGI_FAR r{k}'))
        for name, *_ in ROIS:
            row = {'roi': name, 'repeat': k, 'V': stats(dd[name], far[name]), 'W': stats(far[name], sdf[name]), 'P1': stats(dd[name], sdf[name])}
            rows.append(row)
            print(f'  {name:<12} r{k}: V = DDGI/DDGI_FAR median {row["V"]["median"]:.4f} (p10 {row["V"]["p10"]:.4f}, p90 {row["V"]["p90"]:.4f})  '
                  f'W = DDGI_FAR/SDF median {row["W"]["median"]:.4f} (p10 {row["W"]["p10"]:.4f}, p90 {row["W"]["p90"]:.4f})  '
                  f'P1\' = DDGI/SDF median {row["P1"]["median"]:.4f}  n={row["V"]["n"]}')
    report['rows'] = rows

    # ---- C ---------------------------------------------------------------------------------
    for name, *_ in ROIS:
        for key in ('V', 'W'):
            meds = [r[key]['median'] for r in rows if r['roi'] == name]
            if len(meds) >= 2 and max(abs(x - y) for x in meds for y in meds) > REPEAT_TOL:
                raise Stop('C', f'{key} {name}: repeats disagree: {meds}')

    # ---- expectation against R1 (reported) ---------------------------------------------------
    r1_path = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '58-probe-indirect-residual', 'probe_indirect_residual.json')
    r1 = None
    if os.path.exists(r1_path):
        j = json.load(open(r1_path, encoding='utf-8'))
        if j.get('status') == 'complete' and j.get('rows'):
            r1 = float(j['rows'][0]['ratio_m_median'])
    w_med = float(np.median([r['W']['median'] for r in rows]))
    v_med = float(np.median([r['V']['median'] for r in rows]))
    report['summary'] = {'V_median_over_rois': v_med, 'W_median_over_rois': w_med, 'R1_writer_ratio': r1,
                         'W_within_expectation': (abs(w_med - r1) <= EXPECT_BAND) if r1 is not None else None}
    print(f'\n  V (visibility) median over ROIs {v_med:.4f};  W (writers) median {w_med:.4f};  R1 writer-side ratio '
          f'{"n/a" if r1 is None else f"{r1:.4f}"} -> W within +-{EXPECT_BAND}: {report["summary"]["W_within_expectation"]}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '59-consumer-visibility-split'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'visibility vs writers under real lighting — Camera C, {list(CONDITIONS)} x {args.repeats}')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'base': BASE, 'conditions': CONDITIONS,
              'rois': ROIS, 'harness': runner.harness_stamp(), 'criteria': {'expect_band': EXPECT_BAND, 'repeat_tol': REPEAT_TOL}}
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
        # A --reuse run re-reads the retained repeat only and must not overwrite a full run's report
        # (runner.report_path writes <stem>.reuse.json). Bypassed until 2026-09-18; fixed on review.
        path = runner.report_path(args.out, 'consumer_visibility_split.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
