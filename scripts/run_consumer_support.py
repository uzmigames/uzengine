"""followup-01, review item 2 (2026-09-18): do the cache path's zeros on Sponza's walls come from lack of
support — every one of the eight probes rejected — and by which rule?

Pre-registered in the followup-01 notes before this ran ("Support in the black regions"). Reads the
diagnostic records LIGHTING_EXTRA +11 (rc_support) and +12 (ddgi_support); no rule was changed.
  S0  the binary writes the declared layout (scripts/test_binary_layout.py, run first) and both
      channels are finite on every geometry pixel
  S1  black SDF pixels (diffuse_indirect exactly 0 in all three components): the fraction with
      rc_support.w == 0 (no probe survived). Reading fixed before capture: SUPPORT if >= 99 % on every
      ROI with >= 100 black pixels; OTHER if not — zeros with support, reported by count.
  S2  among the unsupported black pixels: facing emptied the support (y == 0, z > 0), validity did
      (z == 0, y > 0), both (y == z == 0), or the survivors of each rule are different probes
      (y > 0, z > 0, w == 0). Reported, no verdict.
  S3  context, reported: w and x on non-black SDF pixels; on the DDGI condition the rejected count z,
      the share of pixels with no probe above 1e-3 visibility (y == 0) and, of those, the share the
      query still answered (x > 1e-6, i.e. a fully occluded set renormalised to full strength).
  C   repeats agree on S1's fractions to 1e-3.

Usage: python scripts/run_consumer_support.py [--repeats 2] [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
import run_consumer_known_radiance as uniform  # noqa: E402
import run_consumer_visibility_split as split  # noqa: E402

BASE = split.BASE                       # real lighting, known radiance off, final gather OFF (as 38/57/59)
CONDITIONS = {'SDF': split.CONDITIONS['SDF'], 'DDGI': split.CONDITIONS['DDGI']}
ROIS = uniform.ROIS
MIN_BLACK = 100
SUPPORT_FRACTION = 0.99
REPEAT_TOL = 1e-3
VIS_EPS = 1e-3                          # the shader's own threshold for the y/z counts
ANSWER_EPS = 1e-6                       # the shader's own threshold for "the DDGI answered"


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def load(region_dir):
    seq = regiondump.load_sequence(region_dir, uniform.FIRST, uniform.LAST)
    out = {'layouts': sorted({tuple(fr.layout) for fr in seq}), 'fields': seq[0].fields}
    for name, *_ in ROIS:
        rgb, geo, rc, dd = [], [], [], []
        for fr in seq:
            r = fr.rect(name)
            rgb.append(r.channel('diffuse_indirect')[..., :3].astype(np.float64))
            geo.append(r.channel('lighting_normal')[..., 3].astype(np.float64) >= 0.0)
            rc.append(r.channel('rc_support').astype(np.float64))
            dd.append(r.channel('ddgi_support').astype(np.float64))
        out[name] = {'rgb': np.stack(rgb), 'geo': np.stack(geo), 'rc': np.stack(rc), 'dd': np.stack(dd)}
    return out


def q(a):
    if a.size == 0:
        return None
    return {'n': int(a.size), 'min': float(a.min()), 'p10': float(np.percentile(a, 10)), 'median': float(np.median(a)),
            'p90': float(np.percentile(a, 90)), 'max': float(a.max())}


def hist8(a):
    return [int(v) for v in np.bincount(np.clip(a.astype(int), 0, 8), minlength=9)[:9]]


def fmt(v):
    return 'n/a' if v is None else f'{v:.4f}'


def analyse_sdf(d):
    g, black = d['geo'], d['geo'] & (d['rgb'] == 0.0).all(-1)
    x, y, z, w = (d['rc'][..., i] for i in range(4))
    if not np.isfinite(d['rc'][g]).all():
        raise Stop('S0', 'rc_support is not finite on a geometry pixel')
    unsup = black & (w == 0)
    n_black, n_unsup = int(black.sum()), int(unsup.sum())
    row = {'geometry': int(g.sum()), 'black': n_black,
           'unsupported_black': n_unsup, 'frac_unsupported': (n_unsup / n_black) if n_black else None,
           # the converse and the ambiguity, so a reader/writer defect cannot hide behind the fractions
           'unsupported_not_black': int((g & (w == 0) & ~black).sum()),
           'black_all_zero_record': int((black & (d['rc'] == 0.0).all(-1)).sum()),
           'S2': {'facing_only': int((unsup & (y == 0) & (z > 0)).sum()),
                  'validity_only': int((unsup & (z == 0) & (y > 0)).sum()),
                  'both': int((unsup & (y == 0) & (z == 0)).sum()),
                  'disjoint_survivors': int((unsup & (y > 0) & (z > 0)).sum())},
           'S3_nonblack': {'w_hist_0_8': hist8(w[g & ~black]), 'x': q(x[g & ~black]),
                           'y_hist_0_8': hist8(y[g & ~black]), 'z_hist_0_8': hist8(z[g & ~black])},
           'S3_black': {'y_hist_0_8': hist8(y[black]), 'z_hist_0_8': hist8(z[black])}}
    return row


def analyse_ddgi(d):
    g, black = d['geo'], d['geo'] & (d['rgb'] == 0.0).all(-1)
    x, y, z, w = (d['dd'][..., i] for i in range(4))
    if not np.isfinite(d['dd'][g]).all():
        raise Stop('S0', 'ddgi_support is not finite on a geometry pixel')
    none_vis = g & (y == 0)
    n_none = int(none_vis.sum())
    return {'geometry': int(g.sum()), 'black': int(black.sum()),
            'z_hist_0_8': hist8(z[g]), 'w_hist_0_8': hist8(w[g]), 'x': q(x[g]),
            'share_x_below_1e-3': float((x[g] < 1e-3).mean()) if g.any() else None,
            'none_visible': n_none, 'share_none_visible': (n_none / int(g.sum())) if g.any() else None,
            'none_visible_but_answered': int((none_vis & (x > ANSWER_EPS)).sum()),
            'none_visible_x': q(x[none_vis])}


def run(args, report):
    if not args.reuse:
        print('  S0: the binary writes the declared layout', flush=True)
        gate = subprocess.run([sys.executable, os.path.join(runner.REPO, 'scripts', 'test_binary_layout.py')],
                              capture_output=True, text=True)
        report['S0_binary_layout'] = {'exit': gate.returncode, 'tail': gate.stdout[-1500:]}
        print('\n'.join('     ' + l for l in gate.stdout.strip().splitlines()[-4:]))
        if gate.returncode != 0:
            raise Stop('S0', f'test_binary_layout.py exit {gate.returncode}')

    works = {c: [] for c in CONDITIONS}
    # This study releases no repeat, so --reuse re-reads every repeat it captured (as 38/57/59 do).
    for k in range(args.repeats):
        for cond, extra in CONDITIONS.items():
            work = os.path.join(args.out, f'{cond}_r{k}')
            works[cond].append(work)
            if args.reuse:
                continue
            print(f'  ... {cond} repeat {k}', flush=True)
            res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', uniform.PROJECT), uniform.FRAMES,
                                     regions=ROIS, region_fields='components', region_first=uniform.FIRST,
                                     region_last=uniform.LAST, cvars={**BASE, **extra}, timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{cond} repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w)} for w in ws] for c, ws in works.items()}

    # ---- identity (as 59): the DDGI capture's writer identity and the shared sky ------------------
    for k, (ws, wd) in enumerate(zip(works['SDF'], works['DDGI'])):
        s, a = split.identity(os.path.join(ws, 'run.log')), split.identity(os.path.join(wd, 'run.log'))
        if a['ddgi'] is None or a['farDepth'] != 0:
            raise Stop('K0', f'r{k}: DDGI identity missing or farDepth={a["farDepth"]}')
        if a['rc_skySrv'] is None or a['rc_skySrv'] != a['ddgi']['skySrv']:
            raise Stop('K0', f'r{k}: cache and DDGI do not share the sky within the DDGI process')
        if s['rc_identity'] is None or s['rc_identity'] != a['rc_identity']:
            raise Stop('K0', f'r{k}: cache identity differs across conditions: {s["rc_identity"]} vs {a["rc_identity"]}')
        print(f'  K0 r{k}: DDGI sequence {a["sequence"]}, farDepth 0, sky shared; cache identity equal across conditions')

    # ---- S0 (layout on the dumps) / S1 / S2 / S3 ------------------------------------------------------
    rows = {'SDF': [], 'DDGI': []}
    for cond in CONDITIONS:
        for k, w in enumerate(works[cond]):
            d = load(runner.require_region_dir(os.path.join(w, 'region'), f'{cond} r{k}'))
            # The header packs 0 for a group absent from the capture's field mask (regiondump.unpack_layout),
            # so the declared layout is compared masked. The first run of this study compared it unmasked
            # and stopped on its own gate (2026-09-18); the binary gate above had passed.
            want = tuple(n if d['fields'] & (1 << i) else 0 for i, n in enumerate(regiondump.CURRENT_LAYOUT))
            if d['layouts'] != [want]:
                raise Stop('S0', f'{cond} r{k}: dumps carry layout(s) {d["layouts"]}, reader expects {want} for fields={d["fields"]}')
            for name, *_ in ROIS:
                row = {'roi': name, 'repeat': k}
                if cond == 'SDF':
                    row.update(analyse_sdf(d[name]))
                    f = fmt(row['frac_unsupported'])
                    s2 = row['S2']
                    print(f'  S1 {name:<12} r{k}: geometry {row["geometry"]:6d}  black {row["black"]:6d}  unsupported '
                          f'{row["unsupported_black"]:6d} ({f})  unsupported-not-black {row["unsupported_not_black"]}  '
                          f'all-zero records {row["black_all_zero_record"]}')
                    print(f'  S2 {name:<12} r{k}: facing-only {s2["facing_only"]}  validity-only {s2["validity_only"]}  both {s2["both"]}  '
                          f'disjoint survivors {s2["disjoint_survivors"]}')
                    print(f'  S3 {name:<12} r{k}: non-black w hist {row["S3_nonblack"]["w_hist_0_8"]}  x {row["S3_nonblack"]["x"]}')
                    print(f'     {"":<12}      black y hist {row["S3_black"]["y_hist_0_8"]}  black z hist {row["S3_black"]["z_hist_0_8"]}')
                else:
                    row.update(analyse_ddgi(d[name]))
                    share = fmt(row['share_none_visible'])
                    print(f'  S3 {name:<12} r{k} DDGI: geometry {row["geometry"]:6d}  black {row["black"]}  rejected hist {row["z_hist_0_8"]}  '
                          f'trilinear hist {row["w_hist_0_8"]}  none visible {row["none_visible"]} '
                          f'({share}), of which answered '
                          f'{row["none_visible_but_answered"]}  x {row["x"]}  share x<1e-3 {row["share_x_below_1e-3"]}')
                rows[cond].append(row)
    report['rows'] = rows

    # ---- C ----------------------------------------------------------------------------------------
    for name, *_ in ROIS:
        fr = [r['frac_unsupported'] for r in rows['SDF'] if r['roi'] == name and r['black'] >= MIN_BLACK]
        if len(fr) >= 2 and max(abs(a - b) for a in fr for b in fr) > REPEAT_TOL:
            raise Stop('C', f'{name}: repeats disagree on the unsupported fraction: {fr}')

    # ---- reading ----------------------------------------------------------------------------------
    eligible = [r for r in rows['SDF'] if r['black'] >= MIN_BLACK]
    if not eligible:
        reading = 'NO BLACK'
    elif all(r['frac_unsupported'] >= SUPPORT_FRACTION for r in eligible):
        reading = 'SUPPORT'
    else:
        reading = 'OTHER'
    zeros_with_support = sum(r['black'] - r['unsupported_black'] for r in rows['SDF'])
    report['reading'] = reading
    report['zeros_with_support_total'] = int(zeros_with_support)
    print(f'\n  -> {reading}   (ROIs eligible: {sorted({r["roi"] for r in eligible})}; black pixels with support, all ROIs/repeats: {zeros_with_support})')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '60-consumer-support'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'support in the black regions — Camera C, real lighting, {list(CONDITIONS)} x {args.repeats}')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'base': BASE, 'conditions': CONDITIONS,
              'rois': ROIS, 'harness': runner.harness_stamp(),
              'criteria': {'min_black': MIN_BLACK, 'support_fraction': SUPPORT_FRACTION, 'repeat_tol': REPEAT_TOL,
                           'vis_eps': VIS_EPS, 'answer_eps': ANSWER_EPS}}
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
        path = runner.report_path(args.out, 'consumer_support.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
