"""followup-01, review step 3: border continuity under a DIRECTIONAL known radiance.

Both writers return L * (0.5 + 0.5 dot(dir, d0)) (r.GI.Diagnostic.KnownRadianceMode = 1). The consumer
must then read the two paths equally per pixel (K1, as in the consumer stage), and the filled atlas
border must not show: pixels whose bilinear tap reaches a border texel must read like the others (K3).
Pre-registered in the followup-01 notes, 2026-09-18, before this ran.

  K0  identity lines echo knownRadiance = 1 and knownMode = 1; cache sky fraction 1 on every probe
      outside geometry, per-probe sky luminance in [0, 1]; DDGI per-probe mean in [0, 1] and the same
      number on every probe (max - min <= 1e-3): the field does not depend on position.
  K1  per pixel with geometry: diffuseIndirect DDGI / RC, median/p10/p90 per ROI; band 1 +- 0.05.
  K3  |median(border-touching) - median(interior)| <= 0.02 per ROI, and >= 90 % of border-touching
      pixels within +-0.05 of the interior median.
  C   repeats agree on the K1 and K3 medians to 1e-3.
  Reading: CONTINUOUS (K1 and K3) / DISCONTINUOUS (K1, not K3) / SCALED (not K1) / MIXED.

Usage: python scripts/rendering/run_consumer_known_directional.py [--repeats 2] [--out DIR] [--reuse]
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

KNOWN, MODE = 1.0, 1
BASE = {**uniform.BASE, 'r.GI.Diagnostic.KnownRadianceMode': MODE}
CONDITIONS = {'RC': uniform.CONDITIONS['RC'], 'DDGI': uniform.CONDITIONS['DDGI']}
ROIS = uniform.ROIS
BAND = 0.05
CONT_BAND = 0.02
CONT_FRACTION = 0.90
K0_TOL = 1e-3
REPEAT_TOL = 1e-3
NUM = stage1.NUM
RE_RC_ID = re.compile(r'RC transport: identity [^\n]* knownRadiance=' + NUM + r' knownMode=(\d)')
RE_DDGI_ID = re.compile(r'DDGI transport: identity [^\n]* knownRadiance=' + NUM + r' keepDepth=(\d) knownMode=(\d)')
RE_DDGI_SEQ = re.compile(r'DDGI transport: identity [^\n]* sequence=(\S+)')
STRATIFIED = 'stratified-frame-probe-rotation'
K0B_LOW, K0B_HIGH = 0.48, 0.52   # pre-registered 2026-09-18 with the ray-set correction (H-57)


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def parse_writers(log_path, want_ddgi):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    rc, dd = RE_RC_ID.search(text), RE_DDGI_ID.search(text)
    sq = RE_DDGI_SEQ.search(text)
    out = {'rc_known': float(rc.group(1)) if rc else None, 'rc_mode': int(rc.group(2)) if rc else None,
           'ddgi_known': float(dd.group(1)) if dd else None, 'ddgi_mode': int(dd.group(3)) if dd else None,
           'ddgi_sequence': sq.group(1) if sq else None}
    r = np.full((stage1.TOTAL, 10), np.nan)
    for m in stage3.RE_RC.finditer(text):
        g = m.groups()
        r[int(g[0])] = [float(g[k]) for k in (4, 5, 8, 9, 10, 11, 12, 13, 14, 15)]
    ok = np.isfinite(r).all(1) & (r[:, 0] >= 5.0)
    out['rc_probes'] = int(ok.sum())
    out['rc_sky_fraction_min'] = float(r[ok, 1].min()) if ok.any() else None
    out['rc_sky_lum_min'] = float(r[ok, 3].min()) if ok.any() else None
    out['rc_sky_lum_max'] = float(r[ok, 3].max()) if ok.any() else None
    if want_ddgi:
        d = np.full((stage1.TOTAL, 7), np.nan)
        for m in stage3.RE_DDGI.finditer(text):
            g = m.groups()
            d[int(g[0])] = [float(g[k]) for k in (7, 8, 9, 10, 11, 12, 13)]
        okd = np.isfinite(d).all(1) & ok
        out['ddgi_probes'] = int(okd.sum())
        out['ddgi_mean_min'] = float(d[okd, 1].min()) if okd.any() else None
        out['ddgi_mean_max'] = float(d[okd, 1].max()) if okd.any() else None
    return out


def split_ratio(dd, rc):
    """Per-pixel ratio DDGI/RC with the border-touching / interior split from the consumer's encode."""
    mask = dd['geo'] & rc['geo'] & (rc['lum'] > uniform.MIN_DENOM)
    n = dd['n'][mask]
    unit = np.mean(np.abs(np.linalg.norm(n, axis=-1) - 1.0) <= border.UNIT_TOL)
    oct = border.oct_encode(n)
    touching = (border.border_weight(oct[:, 0]) > 0) | (border.border_weight(oct[:, 1]) > 0)
    q = dd['lum'][mask] / rc['lum'][mask]
    return q, touching, unit


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
    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w)} for w in ws]
                          for c, ws in works.items()}

    # ---- K0 --------------------------------------------------------------------------------
    for cond, ws in works.items():
        for k, w in enumerate(ws):
            p = parse_writers(os.path.join(w, 'run.log'), want_ddgi=(cond == 'DDGI'))
            report.setdefault('K0', {}).setdefault(cond, []).append(p)
            if p['rc_known'] != KNOWN or p['rc_mode'] != MODE:
                raise Stop('K0', f'{cond} r{k}: cache identity knownRadiance={p["rc_known"]} knownMode={p["rc_mode"]}')
            if p['rc_probes'] == 0 or p['rc_sky_fraction_min'] < 1.0 - K0_TOL \
                    or p['rc_sky_lum_min'] < -K0_TOL or p['rc_sky_lum_max'] > KNOWN + K0_TOL:
                raise Stop('K0', f'{cond} r{k}: cache probes outside the field range (sky min {p["rc_sky_fraction_min"]}, '
                                 f'lum {p["rc_sky_lum_min"]}..{p["rc_sky_lum_max"]})')
            if cond == 'DDGI':
                if p['ddgi_known'] != KNOWN or p['ddgi_mode'] != MODE:
                    raise Stop('K0', f'{cond} r{k}: DDGI identity knownRadiance={p["ddgi_known"]} knownMode={p["ddgi_mode"]}')
                if p['ddgi_sequence'] == STRATIFIED:
                    # K0b (H-57): a stratified set rotated per probe gives means near the field's 0.5
                    # that DIFFER between probes; the old "same on every probe" clause no longer applies.
                    if p['ddgi_probes'] == 0 or p['ddgi_mean_min'] < K0B_LOW or p['ddgi_mean_max'] > K0B_HIGH \
                            or (p['ddgi_mean_max'] - p['ddgi_mean_min']) <= K0_TOL:
                        raise Stop('K0b', f'{cond} r{k}: DDGI per-probe means {p["ddgi_mean_min"]}..{p["ddgi_mean_max"]} '
                                          f'not in [{K0B_LOW}, {K0B_HIGH}] or not varying per probe')
                elif p['ddgi_probes'] == 0 or p['ddgi_mean_min'] < -K0_TOL or p['ddgi_mean_max'] > KNOWN + K0_TOL \
                        or (p['ddgi_mean_max'] - p['ddgi_mean_min']) > K0_TOL:
                    raise Stop('K0', f'{cond} r{k}: DDGI per-probe means not position-independent '
                                     f'({p["ddgi_mean_min"]}..{p["ddgi_mean_max"]})')
            print(f'  K0 {cond} r{k}: cache sky lum {p["rc_sky_lum_min"]:.5f}..{p["rc_sky_lum_max"]:.5f} on {p["rc_probes"]} probes'
                  + (f'; DDGI mean {p["ddgi_mean_min"]:.5f}..{p["ddgi_mean_max"]:.5f} on {p["ddgi_probes"]} probes' if cond == 'DDGI' else ''))

    # ---- K1 / K3 ---------------------------------------------------------------------------
    rows = []
    for k in range(args.repeats):
        rc = border.load(runner.require_region_dir(os.path.join(works['RC'][k], 'region'), f'RC r{k}'))
        dd = border.load(runner.require_region_dir(os.path.join(works['DDGI'][k], 'region'), f'DDGI r{k}'))
        for name, *_ in ROIS:
            q, touching, unit = split_ratio(dd[name], rc[name])
            if unit < 0.99:
                raise Stop('G', f'{name} r{k}: recorded normal unit length on only {unit:.1%} of pixels')
            if (~touching).sum() == 0 or touching.sum() == 0:
                raise Stop('K3', f'{name} r{k}: no pixels on one side of the split (touching {touching.sum()}, interior {(~touching).sum()})')
            med_all, med_t, med_i = float(np.median(q)), float(np.median(q[touching])), float(np.median(q[~touching]))
            within = float(np.mean(np.abs(q[touching] - med_i) <= BAND))
            row = {'roi': name, 'repeat': k, 'n': int(q.size), 'touching_fraction': float(touching.mean()),
                   'K1': {'median': med_all, 'p10': float(np.percentile(q, 10)), 'p90': float(np.percentile(q, 90)),
                          'pass': bool(abs(med_all - 1.0) <= BAND)},
                   'K3': {'median_touching': med_t, 'median_interior': med_i, 'gap': abs(med_t - med_i),
                          'touching_within_band_of_interior': within,
                          'pass': bool(abs(med_t - med_i) <= CONT_BAND and within >= CONT_FRACTION)}}
            rows.append(row)
            print(f'  {name:<12} r{k}: K1 DDGI/RC median {med_all:.4f} (p10 {row["K1"]["p10"]:.4f}, p90 {row["K1"]["p90"]:.4f})  '
                  f'K3 touching {med_t:.4f} vs interior {med_i:.4f} (gap {row["K3"]["gap"]:.4f}, '
                  f'{within:.1%} within band, touching {row["touching_fraction"]:.1%})  K1 {row["K1"]["pass"]} K3 {row["K3"]["pass"]}')
    report['rows'] = rows

    # ---- C ---------------------------------------------------------------------------------
    for name, *_ in ROIS:
        for key, sub in (('K1', 'median'), ('K3', 'gap')):
            vals = [r[key][sub] for r in rows if r['roi'] == name]
            if len(vals) >= 2 and max(abs(a - b) for a in vals for b in vals) > REPEAT_TOL:
                raise Stop('C', f'{key} {name}: repeats disagree: {vals}')

    k1_ok = all(r['K1']['pass'] for r in rows)
    k3_ok = all(r['K3']['pass'] for r in rows)
    verdict = 'CONTINUOUS' if (k1_ok and k3_ok) else ('DISCONTINUOUS' if k1_ok else ('SCALED' if not k1_ok and k3_ok else 'MIXED'))
    if not k1_ok:
        verdict = 'SCALED'
    report['verdict'] = {'verdict': verdict, 'K1_all': k1_ok, 'K3_all': k3_ok}
    print(f'\n  K1 all: {k1_ok}; K3 all: {k3_ok} -> {verdict}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '54-consumer-known-directional'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'consumer under a directional known radiance (mode {MODE}) — Camera C, {list(CONDITIONS)} x {args.repeats}')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'base': BASE, 'conditions': CONDITIONS,
              'rois': ROIS, 'harness': runner.harness_stamp(),
              'criteria': {'known': KNOWN, 'mode': MODE, 'band': BAND, 'continuity_band': CONT_BAND,
                           'continuity_fraction': CONT_FRACTION, 'k0_tol': K0_TOL, 'repeat_tol': REPEAT_TOL}}
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
        path = runner.report_path(args.out, 'consumer_known_directional.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
