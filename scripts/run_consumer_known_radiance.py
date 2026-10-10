"""followup-01, consumer stage: feed both GI formats a KNOWN radiance and compare what the consumer
makes of it — reconstruction, query, and the Chebyshev visibility term — with the tracing out of the way.

Pre-registered in the followup-01 notes, 2026-09-18, before this ran:

  capture   Camera C, still; final gather, SDF reflections, SSGI and GTAO off; radiance cache on with
            the whole grid traced each frame; r.GI.Diagnostic.KnownRadiance = 1. Three conditions,
            2 repeats each, region dump `components` on three 128x128 ROIs at frames 200-202:
              RC         r.RayTracing = 0 (the cache is the consumer's source)
              DDGI       r.RayTracing = 1, r.RayTracing.GI = 1, KeepDepth = 0 (visibility 1)
              DDGI_CHEB  as DDGI with KeepDepth = 1 (real hit distances -> Chebyshev acts)
  K0  both identity lines echo the known value (and keepDepth as asked); per probe outside geometry
      the cache's skyLum/skyFraction and the DDGI's meanRadiance are 1 +- 1e-3 (KeepDepth = 0).
  K1  per pixel with geometry: diffuseIndirect luminance DDGI / RC, median/p10/p90 per ROI; band 1 +- 0.05.
  K2  DDGI_CHEB / DDGI, same statistic, same band.
  C   repeats agree on the K1 and K2 medians to 1e-3.
  Reading: CONSUMER SCALES (K1 outside) / VISIBILITY (K1 inside, K2 outside) / NEITHER (both inside) /
           MIXED (anything else).

Usage: python scripts/run_consumer_known_radiance.py [--repeats 2] [--out DIR] [--reuse]
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

import regiondump  # noqa: E402
import runner  # noqa: E402
import run_probe_sky_parity as stage1  # noqa: E402
import run_probe_hit_identity as stage3  # noqa: E402

PROJECT = stage1.PROJECT
FRAMES = 210
FIRST, LAST = 200, 202
KNOWN = 1.0
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.GI.FinalGather': 0, 'r.GI.Reflections': 0, 'r.SSGI': 0,
        'r.GTAO': 0, 'r.RadianceCache': 1, 'r.GI.RadianceCache.ProbesPerFrame': 4096, 'r.TAA': 1,
        'r.GI.Diagnostic.KnownRadiance': KNOWN, 'r.GI.DDGI.Diagnostic': 1, 'r.GI.RadianceCache.Diagnostic': 1,
        'r.TechPanel.Show': 0}
CONDITIONS = {
    'RC':        {'r.RayTracing': 0, 'r.RayTracing.GI': 0, 'r.GI.Diagnostic.KnownRadianceKeepDepth': 0},
    'DDGI':      {'r.RayTracing': 1, 'r.RayTracing.GI': 1, 'r.GI.Diagnostic.KnownRadianceKeepDepth': 0},
    'DDGI_CHEB': {'r.RayTracing': 1, 'r.RayTracing.GI': 1, 'r.GI.Diagnostic.KnownRadianceKeepDepth': 1},
}
ROIS = [('shadow_wall', 150, 300, 128, 128), ('lit_wall', 1100, 300, 128, 128), ('centre', 896, 476, 128, 128)]
K0_TOL = 1e-3
BAND = 0.05
REPEAT_TOL = 1e-3
MIN_DENOM = 1e-6
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
NUM = stage1.NUM
RE_RC_KNOWN = re.compile(r'RC transport: identity [^\n]* knownRadiance=' + NUM)
RE_DDGI_KNOWN = re.compile(r'DDGI transport: identity [^\n]* knownRadiance=' + NUM + r' keepDepth=(\d)')


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def parse_writers(log_path, want_ddgi):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    rc = RE_RC_KNOWN.search(text)
    dd = RE_DDGI_KNOWN.search(text)
    out = {'rc_known': float(rc.group(1)) if rc else None,
           'ddgi_known': float(dd.group(1)) if dd else None,
           'ddgi_keep_depth': int(dd.group(2)) if dd else None}
    r = np.full((stage1.TOTAL, 10), np.nan)
    for m in stage3.RE_RC.finditer(text):
        g = m.groups()
        r[int(g[0])] = [float(g[k]) for k in (4, 5, 8, 9, 10, 11, 12, 13, 14, 15)]
    ok = np.isfinite(r).all(1) & (r[:, 0] >= 5.0)
    out['rc_probes'] = int(ok.sum())
    out['rc_sky_fraction_min'] = float(r[ok, 1].min()) if ok.any() else None
    out['rc_per_sky_ray_min'] = float((r[ok, 3] / np.maximum(r[ok, 1], 1e-12)).min()) if ok.any() else None
    out['rc_per_sky_ray_max'] = float((r[ok, 3] / np.maximum(r[ok, 1], 1e-12)).max()) if ok.any() else None
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


def load_rois(region_dir):
    """Returns {roi: {'lum': (frames, h, w), 'geo': (frames, h, w)}}."""
    seq = regiondump.load_sequence(region_dir, FIRST, LAST)
    out = {}
    for name, *_ in ROIS:
        lum, geo = [], []
        for f in seq:
            r = f.rect(name)
            lum.append(r.channel('diffuse_indirect')[..., :3].astype(np.float64) @ REC709)
            geo.append(r.channel('lighting_normal')[..., 3].astype(np.float64) >= 0.0)
        out[name] = {'lum': np.stack(lum), 'geo': np.stack(geo)}
    return out


def ratio_stats(num, den):
    """Per-pixel, per-frame ratio on pixels with geometry in both and a usable denominator."""
    mask = num['geo'] & den['geo'] & (den['lum'] > MIN_DENOM)
    if not mask.any():
        return None
    q = num['lum'][mask] / den['lum'][mask]
    return {'n': int(mask.sum()), 'excluded_fraction': float(1.0 - mask.mean()),
            'median': float(np.median(q)), 'p10': float(np.percentile(q, 10)), 'p90': float(np.percentile(q, 90))}


def run(args, report):
    works = {c: [] for c in CONDITIONS}
    for k in range(args.repeats):
        for cond, extra in CONDITIONS.items():
            work = os.path.join(args.out, f'{cond}_r{k}')
            works[cond].append(work)
            if args.reuse:
                continue
            print(f'  ... {cond} repeat {k}', flush=True)
            res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', PROJECT), FRAMES,
                                     regions=ROIS, region_fields='components', region_first=FIRST,
                                     region_last=LAST, cvars={**BASE, **extra}, timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{cond} repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w)} for w in ws]
                          for c, ws in works.items()}

    # ---- K0 --------------------------------------------------------------------------------
    k0 = {}
    for cond, ws in works.items():
        k0[cond] = []
        for k, w in enumerate(ws):
            p = parse_writers(os.path.join(w, 'run.log'), want_ddgi=(cond != 'RC'))
            k0[cond].append(p)
            if p['rc_known'] != KNOWN:
                raise Stop('K0', f'{cond} r{k}: cache identity reports knownRadiance={p["rc_known"]}, asked {KNOWN}')
            if p['rc_probes'] == 0 or abs(p['rc_per_sky_ray_min'] - KNOWN) > K0_TOL or abs(p['rc_per_sky_ray_max'] - KNOWN) > K0_TOL \
                    or p['rc_sky_fraction_min'] < 1.0 - K0_TOL:
                raise Stop('K0', f'{cond} r{k}: cache probes did not store the known value '
                                 f'(per-sky-ray {p["rc_per_sky_ray_min"]}..{p["rc_per_sky_ray_max"]}, sky min {p["rc_sky_fraction_min"]})')
            if cond != 'RC':
                keep = CONDITIONS[cond]['r.GI.Diagnostic.KnownRadianceKeepDepth']
                if p['ddgi_known'] != KNOWN or p['ddgi_keep_depth'] != keep:
                    raise Stop('K0', f'{cond} r{k}: DDGI identity reports knownRadiance={p["ddgi_known"]} keepDepth={p["ddgi_keep_depth"]}')
                if p['ddgi_probes'] == 0 or abs(p['ddgi_mean_min'] - KNOWN) > K0_TOL or abs(p['ddgi_mean_max'] - KNOWN) > K0_TOL:
                    raise Stop('K0', f'{cond} r{k}: DDGI probes did not store the known value '
                                     f'(mean {p["ddgi_mean_min"]}..{p["ddgi_mean_max"]})')
            print(f'  K0 {cond} r{k}: cache per-sky-ray {p["rc_per_sky_ray_min"]:.5f}..{p["rc_per_sky_ray_max"]:.5f} on '
                  f'{p["rc_probes"]} probes' + (f'; DDGI mean {p["ddgi_mean_min"]:.5f}..{p["ddgi_mean_max"]:.5f} on '
                                               f'{p["ddgi_probes"]} probes, keepDepth={p["ddgi_keep_depth"]}' if cond != 'RC' else ''))
    report['K0'] = k0

    # ---- K1 / K2 ---------------------------------------------------------------------------
    rois = {c: [load_rois(runner.require_region_dir(os.path.join(w, 'region'), f'{c} r{k}'))
                for k, w in enumerate(ws)] for c, ws in works.items()}
    k1, k2 = {}, {}
    for name, *_ in ROIS:
        k1[name] = [ratio_stats(rois['DDGI'][k][name], rois['RC'][k][name]) for k in range(args.repeats)]
        k2[name] = [ratio_stats(rois['DDGI_CHEB'][k][name], rois['DDGI'][k][name]) for k in range(args.repeats)]
        for k in range(args.repeats):
            a, b = k1[name][k], k2[name][k]
            print(f'  K1 {name:<12} r{k}: DDGI/RC median {a["median"]:.4f} (p10 {a["p10"]:.4f}, p90 {a["p90"]:.4f}, n={a["n"]}, '
                  f'excluded {a["excluded_fraction"]:.1%})' if a else f'  K1 {name} r{k}: no usable pixels')
            print(f'  K2 {name:<12} r{k}: CHEB/DDGI median {b["median"]:.4f} (p10 {b["p10"]:.4f}, p90 {b["p90"]:.4f}, n={b["n"]})'
                  if b else f'  K2 {name} r{k}: no usable pixels')
    report['K1'], report['K2'] = k1, k2

    # ---- C ---------------------------------------------------------------------------------
    for label, table in (('K1', k1), ('K2', k2)):
        for name, reps in table.items():
            meds = [r['median'] for r in reps if r]
            if len(meds) >= 2 and max(abs(a - b) for a in meds for b in meds) > REPEAT_TOL:
                raise Stop('C', f'{label} {name}: repeats disagree on the median: {meds}')

    # ---- reading ---------------------------------------------------------------------------
    k1_out = any(abs(np.mean([r['median'] for r in reps if r]) - 1.0) > BAND for reps in k1.values() if any(reps))
    k2_out = any(abs(np.mean([r['median'] for r in reps if r]) - 1.0) > BAND for reps in k2.values() if any(reps))
    verdict = 'CONSUMER SCALES' if k1_out else ('VISIBILITY' if k2_out else 'NEITHER')
    report['verdict'] = {'verdict': verdict, 'band': BAND,
                         'K1_medians': {n: [r['median'] for r in reps if r] for n, reps in k1.items()},
                         'K2_medians': {n: [r['median'] for r in reps if r] for n, reps in k2.items()}}
    print(f'\n  K1 outside band: {k1_out}; K2 outside band: {k2_out} -> {verdict}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '52-consumer-known-radiance'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'consumer under known radiance L={KNOWN} — Camera C, {list(CONDITIONS)} x {args.repeats}')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'base': BASE, 'conditions': CONDITIONS,
              'rois': ROIS, 'frames': [FIRST, LAST], 'harness': runner.harness_stamp(),
              'criteria': {'known': KNOWN, 'k0_tol': K0_TOL, 'band': BAND, 'repeat_tol': REPEAT_TOL}}
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
        path = runner.report_path(args.out, 'consumer_known_radiance.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
