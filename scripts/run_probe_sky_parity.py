"""followup-01: the two writers' SKY, per probe, made comparable.

The per-probe comparison (43-probe-parity) left a bound: on probes the DDGI sees as pure sky, the SDF
writer's mean radiance over its sky fraction was at most 0.66x the DDGI's per-ray sky radiance. A bound
conditioned on a decomposition is not a measurement of the sky. Each writer now records the radiance
carried by its sky rays alone, logs the identity of its sky sample (cubemap SRV, mip, swizzle, ray
count, sequence, multiplier), and the SDF writer can trace the DDGI's ray sequence, so the two sample
the SAME directions from the SAME positions.

Pre-registered before the captures (followup-01 notes, 2026-09-16):

  capture   Camera C, still, both diagnostics on under the DDGI configuration; 2 conditions x 2 repeats:
              own_sequences   the cache's per-frame + per-probe rotation (the shipping sequence)
              same_sequence   r.GI.RadianceCache.Diagnostic.DdgiSequence=1: the cache traces the DDGI's
                              index/count-perturbed Hammersley set, identical for every probe
  pairing   by probe index (same layout, same grid: Q0 as before).

  S0  the identity lines: both writers report the same cubemap SRV, mip 0, swizzle (x,z,y), the same
      ray count and the same sky multiplier; and the cache reports the sequence the condition asked
      for. A difference here is a finding in itself and stops the comparison of the sky term.
  S1  per probe, on probes where BOTH writers have a sky fraction >= 0.25 (at least 8 of 32 sky rays):
      the per-sky-ray radiance of each writer, skyLum / skyFraction, and their ratio DDGI / SDF.
      Reported for both conditions: median, p10, p90, and the count.
  S2  the SDF writer's lost fraction (rays that exhausted the 96-step budget) per probe: median, p90,
      and the fraction of probes with any lost ray — the direct measurement of the budget loss the
      record suspected; and the hit/sky split of each writer's mean radiance.
  C   the repeats of each condition must give S1 medians within 5 %.

  The reading, fixed now (on the same_sequence condition, S1 ratio median):
    FETCH DIFFERS      the ratio is outside 1 +- 0.05 with identical directions: the sky sample itself
                       (SRV, mip, swizzle, multiplier, or something after the sample) differs.
    DISTRIBUTION       same_sequence ratio within 1 +- 0.05 while own_sequences is not: the direction
                       distribution (per-probe rotation over a non-uniform sky) was the difference.
    NEITHER            both within 1 +- 0.05: the sky term is equal and the 0.66 bound was driven by
                       hits, not sky.
    UNRESOLVED         anything else.

Nothing here is a share of the consumer-level 0.51x; it is the writers' sky term only.

Usage: python scripts/run_probe_sky_parity.py [--repeats 2] [--out DIR] [--reuse]
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

import runner  # noqa: E402

PROJECT = 'sponza-lighting-camera-c.uzproject'
FRAMES = 240
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.GI.FinalGather': 0, 'r.RadianceCache': 1, 'r.TAA': 1,
        'r.RayTracing': 1, 'r.RayTracing.GI': 1, 'r.GI.DDGI.Diagnostic': 1, 'r.GI.RadianceCache.Diagnostic': 1}
CONDITIONS = {'own_sequences': {}, 'same_sequence': {'r.GI.RadianceCache.Diagnostic.DdgiSequence': 1}}
TOTAL = 16 ** 3
MIN_SKY = 0.25
BAND = 0.05
REPEAT_TOL = 0.05
NUM = r'([-+0-9.eE]+|nan|inf|-nan|-inf)'
RE_DDGI = re.compile(r'DDGI transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) world=\(' + NUM + ',' + NUM + ',' + NUM
                     + r'\) skyFraction=' + NUM + r' meanRadiance=' + NUM + r' skyLum=' + NUM)
RE_RC = re.compile(r'RC transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) fieldDistance=' + NUM + r' skyFraction='
                   + NUM + r' frontHit=' + NUM + r' sunLum=' + NUM + r' meanRadiance=' + NUM + r' skyLum=' + NUM
                   + r' lost=' + NUM + r' hitLum=' + NUM)
RE_ID_DDGI = re.compile(r'DDGI transport: identity skySrv=(\d+) mip=(\d+) swizzle=\(([^)]*)\) rays=(\d+) sequence=(\S+) '
                        r'skyMultiplier=\(' + NUM + ',' + NUM + ',' + NUM + r'\)')
RE_ID_RC = re.compile(r'RC transport: identity skySrv=(\d+) mip=(\d+) swizzle=\(([^)]*)\) rays=(\d+) sequence=(\S+) '
                      r'skyMultiplier=\(' + NUM + ',' + NUM + ',' + NUM + r'\)')
RE_QUEUE_D = re.compile(r'DDGI transport: queued diagnostic snapshot frame=(\d+) origin=\(' + NUM + ',' + NUM + ',' + NUM
                        + r'\) spacing=' + NUM)
RE_QUEUE_R = re.compile(r'RC diagnostic: queued SH snapshot frame=(\d+) .*origin=\(' + NUM + ',' + NUM + ',' + NUM
                        + r'\) spacing=' + NUM)


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def parse(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    d = np.full((TOTAL, 3), np.nan)   # sky, mean, skyLum
    r = np.full((TOTAL, 6), np.nan)   # field, sky, mean, skyLum, lost, hitLum
    for m in RE_DDGI.finditer(text):
        d[int(m.group(1))] = [float(m.group(8)), float(m.group(9)), float(m.group(10))]
    for m in RE_RC.finditer(text):
        g = m.groups()
        r[int(g[0])] = [float(g[4]), float(g[5]), float(g[8]), float(g[9]), float(g[10]), float(g[11])]
    idd, idr = RE_ID_DDGI.search(text), RE_ID_RC.search(text)
    ident = {}
    for name, m in (('ddgi', idd), ('rc', idr)):
        ident[name] = None if not m else {'skySrv': int(m.group(1)), 'mip': int(m.group(2)), 'swizzle': m.group(3),
                                          'rays': int(m.group(4)), 'sequence': m.group(5),
                                          'multiplier': [float(m.group(6)), float(m.group(7)), float(m.group(8))]}
    qd, qr = RE_QUEUE_D.search(text), RE_QUEUE_R.search(text)
    grid = {'ddgi': ([float(qd.group(k)) for k in (2, 3, 4)] + [float(qd.group(5))]) if qd else None,
            'rc': ([float(qr.group(k)) for k in (2, 3, 4)] + [float(qr.group(5))]) if qr else None}
    return {'d': d, 'r': r, 'identity': ident, 'grid': grid,
            'd_lines': int(np.count_nonzero(np.isfinite(d[:, 1]))), 'r_lines': int(np.count_nonzero(np.isfinite(r[:, 2])))}


def pct(a):
    return {'median': float(np.median(a)), 'p10': float(np.percentile(a, 10)), 'p90': float(np.percentile(a, 90)),
            'n': int(a.size)}


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
                                     cvars={**BASE, **extra}, timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{cond} repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w)} for w in ws]
                          for c, ws in works.items()}

    results = {}
    for cond, ws in works.items():
        results[cond] = []
        for k, w in enumerate(ws):
            p = parse(os.path.join(w, 'run.log'))
            # ---- Q0 ------------------------------------------------------------------------
            if p['grid']['ddgi'] is None or p['grid']['rc'] is None or p['grid']['ddgi'] != p['grid']['rc']:
                raise Stop('Q0', f'{cond} r{k}: grids differ or missing: {p["grid"]}')
            if p['d_lines'] != TOTAL or p['r_lines'] != TOTAL:
                raise Stop('Q0', f'{cond} r{k}: probe lines ddgi {p["d_lines"]}, rc {p["r_lines"]}')
            # ---- S0 ------------------------------------------------------------------------
            idd, idr = p['identity']['ddgi'], p['identity']['rc']
            if idd is None or idr is None:
                raise Stop('S0', f'{cond} r{k}: an identity line is missing (ddgi {idd is not None}, rc {idr is not None})')
            same = (idd['skySrv'] == idr['skySrv'] and idd['mip'] == idr['mip'] and idd['swizzle'] == idr['swizzle']
                    and idd['rays'] == idr['rays'] and idd['multiplier'] == idr['multiplier'])
            want_seq = 'ddgi-index-count' if cond == 'same_sequence' else 'cache-frame-probe-rotation'
            print(f'  S0 {cond} r{k}: ddgi {idd}  rc {idr}  same sample identity: {same}; '
                  f'rc sequence as asked: {idr["sequence"] == want_seq}')
            if not same:
                raise Stop('S0', f'{cond} r{k}: the writers do not sample the same sky (identity differs) — a '
                                 'finding in itself; the sky-term ratio is not computed on unequal samples')
            if idr['sequence'] != want_seq:
                raise Stop('S0', f'{cond} r{k}: the cache reports sequence {idr["sequence"]}, condition asked {want_seq}')
            # ---- S1 / S2 -------------------------------------------------------------------
            d, r = p['d'], p['r']
            ok = np.isfinite(d).all(1) & np.isfinite(r).all(1) & (r[:, 0] >= 5.0)
            both_sky = ok & (d[:, 0] >= MIN_SKY) & (r[:, 1] >= MIN_SKY)
            per_ray_d = d[both_sky, 2] / d[both_sky, 0]
            per_ray_r = r[both_sky, 3] / r[both_sky, 1]
            ratio = per_ray_d / np.maximum(per_ray_r, 1e-12)
            lost = r[ok, 4]
            rep = {'probes_outside_geometry': int(ok.sum()), 'probes_both_sky': int(both_sky.sum()),
                   'sky_per_ray_ddgi': pct(per_ray_d) if per_ray_d.size else None,
                   'sky_per_ray_sdf': pct(per_ray_r) if per_ray_r.size else None,
                   'sky_per_ray_ratio_ddgi_over_sdf': pct(ratio) if ratio.size else None,
                   'sdf_lost_fraction': pct(lost) if lost.size else None,
                   'sdf_probes_with_any_lost_ray': float(np.mean(lost > 0)) if lost.size else None,
                   'sdf_mean_split': {'sky_share': float(np.nanmedian(r[ok, 3] / np.maximum(r[ok, 2], 1e-12))),
                                      'hit_share': float(np.nanmedian(r[ok, 5] / np.maximum(r[ok, 2], 1e-12)))},
                   'ddgi_mean_split': {'sky_share': float(np.nanmedian(d[ok, 2] / np.maximum(d[ok, 1], 1e-12)))},
                   'identity': p['identity']}
            results[cond].append(rep)
            q = rep['sky_per_ray_ratio_ddgi_over_sdf']
            print(f'  S1 {cond} r{k}: n={rep["probes_both_sky"]}  per-sky-ray DDGI median '
                  f'{rep["sky_per_ray_ddgi"]["median"]:.4f}  SDF median {rep["sky_per_ray_sdf"]["median"]:.4f}  '
                  f'ratio median {q["median"]:.4f} (p10 {q["p10"]:.4f}, p90 {q["p90"]:.4f})')
            print(f'  S2 {cond} r{k}: SDF lost fraction median {rep["sdf_lost_fraction"]["median"]:.4f}, p90 '
                  f'{rep["sdf_lost_fraction"]["p90"]:.4f}; probes with any lost ray {rep["sdf_probes_with_any_lost_ray"]:.1%}; '
                  f'SDF mean = sky {rep["sdf_mean_split"]["sky_share"]:.3f} + hit {rep["sdf_mean_split"]["hit_share"]:.3f}')
    report['results'] = results

    # ---- C -----------------------------------------------------------------------------------
    for cond, reps in results.items():
        meds = [x['sky_per_ray_ratio_ddgi_over_sdf']['median'] for x in reps if x['sky_per_ray_ratio_ddgi_over_sdf']]
        if len(meds) >= 2:
            spread = max(abs(a - b) / max(abs(a), abs(b)) for i, a in enumerate(meds) for b in meds[i + 1:])
            report.setdefault('C', {})[cond] = {'medians': meds, 'spread': spread, 'pass': bool(spread <= REPEAT_TOL)}
            if spread > REPEAT_TOL:
                raise Stop('C', f'{cond}: repeats disagree on the ratio median by {spread:.1%}')

    # ---- reading -----------------------------------------------------------------------------
    own = float(np.mean([x['sky_per_ray_ratio_ddgi_over_sdf']['median'] for x in results['own_sequences']]))
    same = float(np.mean([x['sky_per_ray_ratio_ddgi_over_sdf']['median'] for x in results['same_sequence']]))
    own_in, same_in = abs(own - 1.0) <= BAND, abs(same - 1.0) <= BAND
    if not same_in:
        verdict = 'FETCH DIFFERS'
    elif same_in and not own_in:
        verdict = 'DISTRIBUTION'
    elif same_in and own_in:
        verdict = 'NEITHER'
    else:
        verdict = 'UNRESOLVED'
    report['verdict'] = {'verdict': verdict, 'own_sequences_ratio_median': own, 'same_sequence_ratio_median': same,
                         'band': BAND}
    print(f'\n  per-sky-ray ratio DDGI/SDF: own sequences {own:.4f}, same sequence {same:.4f} (band +-{BAND:.0%}) '
          f'-> {verdict}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '45-probe-sky-parity'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'sky-only radiance per probe, both writers — Camera C, {args.repeats} repeats x {list(CONDITIONS)}')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'base': BASE, 'conditions': CONDITIONS,
              'harness': runner.harness_stamp(),
              'criteria': {'min_sky_fraction': MIN_SKY, 'band': BAND, 'repeat_tolerance': REPEAT_TOL}}
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
        path = os.path.join(args.out, 'probe_sky_parity.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
