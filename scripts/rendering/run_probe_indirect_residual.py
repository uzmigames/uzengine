"""followup-01, review item 2: is the cache's indirect (bounce) term the SOLE remaining difference between
the two probe writers, at the writers, on identical directions?

Pre-registered in the followup-01 notes, 2026-09-18, before this ran. The stage-3 capture protocol is
reused unchanged (Camera C, still, DdgiSequence=1, ProbesPerFrame=4096, 2 repeats, both diagnostics);
per probe the cache logs its split (skyLum, hitSunLum, hitIndirectLum) and the DDGI its mean.

  prediction  ratio_p = (skyLum + hitSunLum) / (skyLum + hitSunLum + hitIndirectLum)   [cache's own split]
  R1  probes outside geometry with cache mean > 0.05: median of ratio_m / ratio_p within 1 +- 0.05,
      >= 90 % of probes within +-0.05 of their prediction; null control ratio_p = 1 must fit worse.
  R2  reported: the indirect share by grid layer.
  Reading: SOLE RESIDUAL (R1 holds) / NOT SOLE (R1 fails; residual reported, nothing attributed).

Usage: python scripts/rendering/run_probe_indirect_residual.py [--repeats 2] [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import runner  # noqa: E402
import run_probe_sky_parity as stage1  # noqa: E402
import run_probe_sky_parity_full as stage2  # noqa: E402
import run_probe_hit_identity as stage3  # noqa: E402

MIN_MEAN = 0.05
BAND = 0.05
FRACTION = 0.90
REPEAT_TOL = 1e-3


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def run(args, report):
    works = []
    for k in range(args.repeats):
        work = os.path.join(args.out, f'residual_r{k}')
        works.append(work)
        if args.reuse:
            continue
        print(f'  ... repeat {k}', flush=True)
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', stage1.PROJECT), stage1.FRAMES,
                                 cvars={**stage1.BASE, **stage2.COND}, timing_first=200, clean=True)
        if res['exit_code'] != 0:
            raise Stop('capture', f'repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = [{'dir': w, 'identity': runner.capture_identity(w)} for w in works]

    rows = []
    for k, w in enumerate(works):
        p = stage3.parse(os.path.join(w, 'run.log'))
        if p['d_lines'] != stage1.TOTAL or p['r_lines'] != stage1.TOTAL:
            raise Stop('Q0', f'r{k}: probe lines ddgi {p["d_lines"]}, rc {p["r_lines"]}')
        idd, idr = p['identity']['ddgi'], p['identity']['rc']
        if idd is None or idr is None:
            raise Stop('S0', f'r{k}: identity line missing')
        same = (idd['skySrv'] == idr['skySrv'] and idd['mip'] == idr['mip'] and idd['swizzle'] == idr['swizzle']
                and idd['rays'] == idr['rays'] and idd['multiplier'] == idr['multiplier'])
        if not same:
            raise Stop('S0', f'r{k}: the writers do not sample the same sky')
        fd, fr, sl = p['snapshot']
        if fd is None or fr is None or fd != fr or sl is None or sl['probeOffset'] != 0 or sl['probeBudget'] != stage1.TOTAL:
            raise Stop('P1', f'r{k}: snapshot/slice not aligned: ddgi {fd}, rc {fr}, slice {sl}')
        d, r = p['d'], p['r']   # d: sky, mean, skyLum, ... | r: field, sky, mean, skyLum, lost, hitLum, escMinD, hitSun, hitInd, hitCount
        ok = np.isfinite(d).all(1) & np.isfinite(r).all(1) & (r[:, 0] >= 5.0) & (r[:, 2] > MIN_MEAN)
        sky, sun, ind = r[ok, 3], r[ok, 7], r[ok, 8]
        # the cache's split is per ray (sky over all rays, sun/indirect over hit rays -> scale to per-ray means)
        hit_frac = 1.0 - r[ok, 1]
        sun_pr, ind_pr = sun * hit_frac, ind * hit_frac
        ratio_p = (sky + sun_pr) / np.maximum(sky + sun_pr + ind_pr, 1e-12)
        ratio_m = d[ok, 1] / np.maximum(r[ok, 2], 1e-12)
        q = ratio_m / np.maximum(ratio_p, 1e-12)
        within = float(np.mean(np.abs(ratio_m - ratio_p) <= BAND))
        err_model = float(np.abs(ratio_m - ratio_p).mean()); err_null = float(np.abs(ratio_m - 1.0).mean())
        z = np.arange(stage1.TOTAL)[ok] // 256
        share = ind_pr / np.maximum(sky + sun_pr + ind_pr, 1e-12)
        by_layer = {int(L): {'n': int((z == L).sum()), 'indirect_share_median': float(np.median(share[z == L])),
                             'ratio_m_median': float(np.median(ratio_m[z == L])), 'ratio_p_median': float(np.median(ratio_p[z == L]))}
                    for L in sorted(set(z.tolist()))}
        row = {'repeat': k, 'probes': int(ok.sum()),
               'R1': {'median': float(np.median(q)), 'p10': float(np.percentile(q, 10)), 'p90': float(np.percentile(q, 90)),
                      'within_band': within, 'err_model': err_model, 'err_null': err_null,
                      'pass': bool(abs(np.median(q) - 1.0) <= BAND and within >= FRACTION and err_model < err_null)},
               'R2_by_layer': by_layer,
               'ratio_m_median': float(np.median(ratio_m)), 'ratio_p_median': float(np.median(ratio_p))}
        rows.append(row)
        print(f'  R1 r{k}: n={row["probes"]}  DDGI/RC measured median {row["ratio_m_median"]:.4f}  predicted {row["ratio_p_median"]:.4f}  '
              f'm/p median {row["R1"]["median"]:.4f} (p10 {row["R1"]["p10"]:.4f}, p90 {row["R1"]["p90"]:.4f})  within +-{BAND}: {within:.1%}  '
              f'|err| model {err_model:.4f} vs null {err_null:.4f}  pass {row["R1"]["pass"]}')
        for L, v in by_layer.items():
            print(f'     layer {L:2d}: n={v["n"]:3d}  indirect share {v["indirect_share_median"]:.3f}  measured {v["ratio_m_median"]:.4f}  predicted {v["ratio_p_median"]:.4f}')
    report['rows'] = rows
    meds = [x['R1']['median'] for x in rows]
    if len(meds) >= 2 and max(abs(a - b) for a in meds for b in meds) > REPEAT_TOL:
        raise Stop('C', f'repeats disagree: {meds}')
    verdict = 'SOLE RESIDUAL' if all(x['R1']['pass'] for x in rows) else 'NOT SOLE'
    report['verdict'] = verdict
    print(f'\n  -> {verdict}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '58-probe-indirect-residual'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'the indirect term as the sole writer residual — Camera C, {args.repeats} repeats')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'harness': runner.harness_stamp(),
              'criteria': {'min_mean': MIN_MEAN, 'band': BAND, 'fraction': FRACTION, 'repeat_tol': REPEAT_TOL}}
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
        path = runner.report_path(args.out, 'probe_indirect_residual.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
