"""followup-01, sky-only stage 2: the same directions on EVERY probe, and an exact per-probe test.

Stage 1 (`run_probe_sky_parity.py`, 45-probe-sky-parity) compared the writers' per-sky-ray radiance
under a `same_sequence` condition and read FETCH DIFFERS from the aggregate median (1.3824). Its logs
showed the precondition had held on one quarter of the grid only: the SDF writer traces
`r.GI.RadianceCache.ProbesPerFrame` = 1024 probes per frame, so at the frame-120 snapshot its four
slices carried four different frame rotations of the Hammersley set, while the DDGI writer traces the
full grid every frame. On the slice traced at frame 120 (probes 1024-2047) the ratio was 1.000000 on
every all-sky probe; on the slice traced at frame 119 it was 1.382441 on every one.

Stage 2 enforces the precondition (`ProbesPerFrame=4096`: the whole grid every frame) and replaces the
median with a per-probe exact test that can fail on a single probe.

Pre-registered before the captures (followup-01 notes, 2026-09-16):

  capture   Camera C, still, stage 1's base CVars + DdgiSequence=1 + ProbesPerFrame=4096; 2 repeats.
  Q0, S0    as in stage 1 (grids equal; sample identity equal; sequence as asked).
  P1        both snapshots queued at the same frame; the cache's snapshot-frame line reports
            probeOffset=0 probeBudget=4096. Otherwise the comparison stops.
  S1x       on every probe where BOTH writers report all rays as sky: |DDGI/SDF per-sky-ray - 1| <= 1e-5.
            One probe outside -> FETCH DIFFERS (listed); none outside -> FETCH EQUAL.
  S1p       probes where both have sky fraction >= 0.25 but not all rays: ratio distribution, reported.
  S3        probes the DDGI sees as all-sky where the SDF field still hits: count, reported.
  C         repeats agree on S1x's count outside and on the DDGI per-sky-ray median to 1e-6.

Usage: python scripts/rendering/run_probe_sky_parity_full.py [--repeats 2] [--out DIR] [--reuse]
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

FULL = 16 ** 3
COND = {'r.GI.RadianceCache.Diagnostic.DdgiSequence': 1, 'r.GI.RadianceCache.ProbesPerFrame': FULL}
ALL_SKY = 0.999
EXACT_TOL = 1e-5
RE_RC_FRAME = re.compile(r'RC diagnostic: frame=(\d+) .*probeOffset=(\d+) probeBudget=(\d+) firstWrite=(\d+)')


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def snapshot_frames(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    qd, qr = stage1.RE_QUEUE_D.search(text), stage1.RE_QUEUE_R.search(text)
    fd = int(qd.group(1)) if qd else None
    fr = int(qr.group(1)) if qr else None
    slice_line = None
    if fr is not None:
        for m in RE_RC_FRAME.finditer(text):
            if int(m.group(1)) == fr:
                slice_line = {'frame': fr, 'probeOffset': int(m.group(2)), 'probeBudget': int(m.group(3)),
                              'firstWrite': int(m.group(4))}
                break
    return fd, fr, slice_line


def run(args, report):
    works = []
    for k in range(args.repeats):
        work = os.path.join(args.out, f'same_sequence_full_r{k}')
        works.append(work)
        if args.reuse:
            continue
        print(f'  ... same_sequence_full repeat {k}', flush=True)
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', stage1.PROJECT), stage1.FRAMES,
                                 cvars={**stage1.BASE, **COND}, timing_first=200, clean=True)
        if res['exit_code'] != 0:
            raise Stop('capture', f'repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = [{'dir': w, 'identity': runner.capture_identity(w)} for w in works]

    results = []
    for k, w in enumerate(works):
        log = os.path.join(w, 'run.log')
        p = stage1.parse(log)
        # ---- Q0 --------------------------------------------------------------------------
        if p['grid']['ddgi'] is None or p['grid']['rc'] is None or p['grid']['ddgi'] != p['grid']['rc']:
            raise Stop('Q0', f'r{k}: grids differ or missing: {p["grid"]}')
        if p['d_lines'] != FULL or p['r_lines'] != FULL:
            raise Stop('Q0', f'r{k}: probe lines ddgi {p["d_lines"]}, rc {p["r_lines"]}')
        # ---- S0 --------------------------------------------------------------------------
        idd, idr = p['identity']['ddgi'], p['identity']['rc']
        if idd is None or idr is None:
            raise Stop('S0', f'r{k}: an identity line is missing (ddgi {idd is not None}, rc {idr is not None})')
        same = (idd['skySrv'] == idr['skySrv'] and idd['mip'] == idr['mip'] and idd['swizzle'] == idr['swizzle']
                and idd['rays'] == idr['rays'] and idd['multiplier'] == idr['multiplier'])
        print(f'  S0 r{k}: ddgi {idd}  rc {idr}  same sample identity: {same}; rc sequence: {idr["sequence"]}')
        if not same:
            raise Stop('S0', f'r{k}: the writers do not sample the same sky (identity differs)')
        if idr['sequence'] != 'ddgi-index-count':
            raise Stop('S0', f'r{k}: the cache reports sequence {idr["sequence"]}, asked ddgi-index-count')
        # ---- P1 --------------------------------------------------------------------------
        fd, fr, sl = snapshot_frames(log)
        print(f'  P1 r{k}: snapshot frames ddgi {fd} rc {fr}; cache slice at the snapshot frame: {sl}')
        if fd is None or fr is None or fd != fr:
            raise Stop('P1', f'r{k}: snapshots at different frames (ddgi {fd}, rc {fr})')
        if sl is None or sl['probeOffset'] != 0 or sl['probeBudget'] != FULL:
            raise Stop('P1', f'r{k}: the cache did not trace the whole grid at the snapshot frame: {sl}')
        # ---- S1x / S1p / S3 --------------------------------------------------------------
        d, r = p['d'], p['r']
        ok = np.isfinite(d).all(1) & np.isfinite(r).all(1) & (r[:, 0] >= 5.0)
        both_all = ok & (d[:, 0] >= ALL_SKY) & (r[:, 1] >= ALL_SKY)
        both_part = ok & (d[:, 0] >= stage1.MIN_SKY) & (r[:, 1] >= stage1.MIN_SKY) & ~both_all
        ddgi_all_sdf_hits = ok & (d[:, 0] >= ALL_SKY) & (r[:, 1] < ALL_SKY)
        per_d = d[:, 2] / np.maximum(d[:, 0], 1e-12)
        per_r = r[:, 3] / np.maximum(r[:, 1], 1e-12)
        ratio = per_d / np.maximum(per_r, 1e-12)
        dev = np.abs(ratio[both_all] - 1.0)
        outside_idx = np.nonzero(both_all)[0][dev > EXACT_TOL]
        rep = {'probes_outside_geometry': int(ok.sum()), 'probes_both_all_sky': int(both_all.sum()),
               'S1x': {'outside_count': int(outside_idx.size), 'max_abs_dev': float(dev.max()) if dev.size else None,
                       'outside_probes': outside_idx[:64].tolist(),
                       'outside_ratios': ratio[outside_idx[:64]].tolist()},
               'ddgi_per_sky_ray': stage1.pct(per_d[both_all]) if both_all.any() else None,
               'sdf_per_sky_ray': stage1.pct(per_r[both_all]) if both_all.any() else None,
               'S1p': {'n': int(both_part.sum()),
                       'ratio': stage1.pct(ratio[both_part]) if both_part.any() else None},
               'S3': {'ddgi_all_sky_sdf_hits': int(ddgi_all_sdf_hits.sum()),
                      'sdf_sky_fraction_on_those': stage1.pct(r[ddgi_all_sdf_hits, 1]) if ddgi_all_sdf_hits.any() else None},
               'sdf_lost_fraction': stage1.pct(r[ok, 4]),
               'snapshot': {'ddgi_frame': fd, 'rc_frame': fr, 'rc_slice': sl}, 'identity': p['identity']}
        results.append(rep)
        print(f'  S1x r{k}: both-all-sky n={rep["probes_both_all_sky"]}  outside {EXACT_TOL:g}: {rep["S1x"]["outside_count"]}  '
              f'max |ratio-1| {rep["S1x"]["max_abs_dev"]}  DDGI per-sky-ray median {rep["ddgi_per_sky_ray"]["median"]:.6f}  '
              f'SDF {rep["sdf_per_sky_ray"]["median"]:.6f}')
        print(f'  S1p r{k}: n={rep["S1p"]["n"]}  ratio {rep["S1p"]["ratio"]}')
        print(f'  S3  r{k}: DDGI all-sky & SDF hits: {rep["S3"]["ddgi_all_sky_sdf_hits"]}; lost fraction median '
              f'{rep["sdf_lost_fraction"]["median"]:.4f} p90 {rep["sdf_lost_fraction"]["p90"]:.4f}')
    report['results'] = results

    # ---- C ---------------------------------------------------------------------------------
    if len(results) >= 2:
        outs = [x['S1x']['outside_count'] for x in results]
        meds = [x['ddgi_per_sky_ray']['median'] for x in results]
        spread = max(abs(a - b) for a in meds for b in meds)
        report['C'] = {'outside_counts': outs, 'ddgi_medians': meds, 'spread': spread,
                       'pass': bool(len(set(outs)) == 1 and spread <= 1e-6)}
        if not report['C']['pass']:
            raise Stop('C', f'repeats disagree: outside counts {outs}, DDGI medians {meds}')

    # ---- reading ---------------------------------------------------------------------------
    total_outside = sum(x['S1x']['outside_count'] for x in results)
    verdict = 'FETCH EQUAL' if total_outside == 0 else 'FETCH DIFFERS'
    report['verdict'] = {'verdict': verdict, 'tolerance': EXACT_TOL,
                         'probes_tested': [x['probes_both_all_sky'] for x in results],
                         'outside': [x['S1x']['outside_count'] for x in results]}
    print(f'\n  S1x over {[x["probes_both_all_sky"] for x in results]} both-all-sky probes: '
          f'{[x["S1x"]["outside_count"] for x in results]} outside |ratio-1| <= {EXACT_TOL:g} -> {verdict}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '45b-probe-sky-parity-full'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'sky-only radiance per probe, stage 2: whole grid every frame, same sequence — Camera C, {args.repeats} repeats')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'base': stage1.BASE, 'condition': COND,
              'harness': runner.harness_stamp(),
              'criteria': {'all_sky': ALL_SKY, 'exact_tolerance': EXACT_TOL, 'min_sky_fraction': stage1.MIN_SKY}}
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
        path = runner.report_path(args.out, 'probe_sky_parity_full.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
