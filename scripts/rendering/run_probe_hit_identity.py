"""followup-01, radiance stage 3: what the DDGI's hits are, and where the cache's escapes leave.

Stage 2 (`run_probe_sky_parity_full.py`) closed the sky term: with identical directions the two
writers' sky fetch is the same number to 6e-9 on every probe where both escape every ray. It left,
post-hoc and without a verdict, the disagreement inside the building: the DDGI classifies a median
0 % of rays as sky and returns exactly 0 mean radiance on 1272 probes, the cache 59-69 % sky and ~2.

Neither writer recorded anything about its hits, so stage 3 adds them (diagnostic only) and reads
them here. Criteria pre-registered in the followup-01 notes, 2026-09-17, before this ran:

  capture   Camera C, still, stage 2's condition (DdgiSequence=1, ProbesPerFrame=4096), 2 repeats.
  Q0/S0/P1  as in stage 2 — grids equal, sample identity equal, whole grid traced at the snapshot.
  population  the probes the DDGI classifies as ALL-HIT (sky fraction < 0.001), outside geometry.
  H1  fraction of those probes with DDGI mean radiance exactly 0; lit fraction of their hit rays.
  H2  mean albedo luminance at their hits.
  H3b the cache's escapes on the same probes: the mean CLOSEST approach to a surface along each
      escaped ray, per probe (median/p10/p90). H3, the escape distance, was void by construction and
      is kept in the record as such. Pre-registered 2026-09-17 before the re-run.
  H4  the cache's hit radiance split: median share of the indirect (previous-frame SH) term.
  C   the repeats must agree on H1's zero fraction and lit fraction to 1e-3.

  Reading (H1 decides): NO LIGHT / SHADOWED+NOBOUNCE / BLACK ALBEDO / MIXED — see the notes.

Usage: python scripts/rendering/run_probe_hit_identity.py [--repeats 2] [--out DIR] [--reuse]
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
import run_probe_sky_parity_full as stage2  # noqa: E402

TOTAL = stage1.TOTAL
ALL_HIT = 0.001          # DDGI sky fraction below this = the writer says every ray hit something
ZERO_RADIANCE = 1e-9
LIT_BAND = 0.01          # "no lit hits" threshold, pre-registered
ALBEDO_BAND = 0.01
REPEAT_TOL = 1e-3
NUM = stage1.NUM
RE_DDGI = re.compile(r'DDGI transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) world=\(' + NUM + ',' + NUM + ',' + NUM
                     + r'\) skyFraction=' + NUM + r' meanRadiance=' + NUM + r' skyLum=' + NUM + r' hitDist=' + NUM
                     + r' hitLitFraction=' + NUM + r' hitAlbedo=' + NUM + r' hitCount=' + NUM)
RE_RC = re.compile(r'RC transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) fieldDistance=' + NUM + r' skyFraction='
                   + NUM + r' frontHit=' + NUM + r' sunLum=' + NUM + r' meanRadiance=' + NUM + r' skyLum=' + NUM
                   + r' lost=' + NUM + r' hitLum=' + NUM + r' escapeMinDist=' + NUM + r' hitSunLum=' + NUM
                   + r' hitIndirectLum=' + NUM + r' hitCount=' + NUM)
RE_ID_DDGI = re.compile(r'DDGI transport: identity skySrv=(\d+) mip=(\d+) swizzle=\(([^)]*)\) rays=(\d+) sequence=(\S+) '
                        r'skyMultiplier=\(' + NUM + ',' + NUM + ',' + NUM + r'\) lightColor=\(' + NUM + ',' + NUM + ','
                        + NUM + r'\) lightDir=\(' + NUM + ',' + NUM + ',' + NUM + r'\) lightIntensity=' + NUM)
RE_ID_RC = stage1.RE_ID_RC


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def parse(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    d = np.full((TOTAL, 7), np.nan)   # sky, mean, skyLum, hitDist, hitLit, hitAlbedo, hitCount
    r = np.full((TOTAL, 10), np.nan)  # field, sky, mean, skyLum, lost, hitLum, escapeDist, hitSun, hitInd, hitCount
    for m in RE_DDGI.finditer(text):
        g = m.groups()
        d[int(g[0])] = [float(g[k]) for k in (7, 8, 9, 10, 11, 12, 13)]
    for m in RE_RC.finditer(text):
        g = m.groups()
        r[int(g[0])] = [float(g[k]) for k in (4, 5, 8, 9, 10, 11, 12, 13, 14, 15)]
    md, mr = RE_ID_DDGI.search(text), RE_ID_RC.search(text)
    ident = {'ddgi': None, 'rc': None}
    if md:
        ident['ddgi'] = {'skySrv': int(md.group(1)), 'mip': int(md.group(2)), 'swizzle': md.group(3),
                         'rays': int(md.group(4)), 'sequence': md.group(5),
                         'multiplier': [float(md.group(k)) for k in (6, 7, 8)],
                         'lightColor': [float(md.group(k)) for k in (9, 10, 11)],
                         'lightDir': [float(md.group(k)) for k in (12, 13, 14)],
                         'lightIntensity': float(md.group(15))}
    if mr:
        ident['rc'] = {'skySrv': int(mr.group(1)), 'mip': int(mr.group(2)), 'swizzle': mr.group(3),
                       'rays': int(mr.group(4)), 'sequence': mr.group(5),
                       'multiplier': [float(mr.group(k)) for k in (6, 7, 8)]}
    fd, fr, sl = stage2.snapshot_frames(log_path)
    return {'d': d, 'r': r, 'identity': ident, 'snapshot': (fd, fr, sl),
            'd_lines': int(np.count_nonzero(np.isfinite(d[:, 1]))),
            'r_lines': int(np.count_nonzero(np.isfinite(r[:, 2])))}


def run(args, report):
    works = []
    for k in range(args.repeats):
        work = os.path.join(args.out, f'hit_identity_r{k}')
        works.append(work)
        if args.reuse:
            continue
        print(f'  ... repeat {k}', flush=True)
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', stage1.PROJECT),
                                 stage1.FRAMES, cvars={**stage1.BASE, **stage2.COND}, timing_first=200, clean=True)
        if res['exit_code'] != 0:
            raise Stop('capture', f'repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = [{'dir': w, 'identity': runner.capture_identity(w)} for w in works]

    results = []
    for k, w in enumerate(works):
        p = parse(os.path.join(w, 'run.log'))
        if p['d_lines'] != TOTAL or p['r_lines'] != TOTAL:
            raise Stop('Q0', f'r{k}: probe lines ddgi {p["d_lines"]}, rc {p["r_lines"]} (the new fields may be missing)')
        idd, idr = p['identity']['ddgi'], p['identity']['rc']
        if idd is None or idr is None:
            raise Stop('S0', f'r{k}: identity line missing (ddgi {idd is not None}, rc {idr is not None})')
        same = (idd['skySrv'] == idr['skySrv'] and idd['mip'] == idr['mip'] and idd['swizzle'] == idr['swizzle']
                and idd['rays'] == idr['rays'] and idd['multiplier'] == idr['multiplier'])
        if not same:
            raise Stop('S0', f'r{k}: the writers do not sample the same sky')
        if idr['sequence'] != 'ddgi-index-count':
            raise Stop('S0', f'r{k}: cache sequence {idr["sequence"]}, asked ddgi-index-count')
        fd, fr, sl = p['snapshot']
        if fd is None or fr is None or fd != fr or sl is None or sl['probeOffset'] != 0 or sl['probeBudget'] != TOTAL:
            raise Stop('P1', f'r{k}: snapshot/slice not aligned: ddgi {fd}, rc {fr}, slice {sl}')
        print(f'  S0 r{k}: identity equal; DDGI light colour {idd["lightColor"]} dir {idd["lightDir"]} '
              f'intensity {idd["lightIntensity"]}; cache sequence {idr["sequence"]}')

        d, r = p['d'], p['r']
        ok = np.isfinite(d).all(1) & np.isfinite(r).all(1) & (r[:, 0] >= 5.0)
        pop = ok & (d[:, 0] < ALL_HIT)
        if pop.sum() == 0:
            raise Stop('population', f'r{k}: no probe has a DDGI sky fraction below {ALL_HIT}')
        zero = d[pop, 1] <= ZERO_RADIANCE
        lit = d[pop, 4]
        albedo = d[pop, 5]
        esc = r[pop, 6]                      # mean closest approach to a surface, escaped rays
        esc_valid = np.isfinite(esc) & (esc > 0.0) & (esc < 1e6)
        voxel0 = 2500.0 * 2.0 / 256.0        # clipmap 0 voxel, 19.53 cm — a stated scale, not a threshold
        sun, ind = r[pop, 7], r[pop, 8]
        share = ind / np.maximum(sun + ind, 1e-12)
        rep = {'probes_outside_geometry': int(ok.sum()), 'ddgi_all_hit_probes': int(pop.sum()),
               'H1': {'zero_radiance_fraction': float(zero.mean()),
                      'lit_fraction_median': float(np.median(lit)), 'lit_fraction_max': float(lit.max()),
                      'hit_count_median': float(np.median(d[pop, 6])),
                      'hit_dist_median': float(np.median(d[pop, 3]))},
               'H2': {'albedo_median': float(np.median(albedo)), 'albedo_max': float(albedo.max())},
               'H3b': {'escape_probes': int(esc_valid.sum()),
                       'closest_approach_median': float(np.median(esc[esc_valid])) if esc_valid.any() else None,
                       'closest_approach_p10': float(np.percentile(esc[esc_valid], 10)) if esc_valid.any() else None,
                       'closest_approach_p90': float(np.percentile(esc[esc_valid], 90)) if esc_valid.any() else None,
                       'within_one_voxel_fraction': float((esc[esc_valid] <= voxel0).mean()) if esc_valid.any() else None,
                       'voxel0_cm': voxel0},
               'H4': {'indirect_share_median': float(np.median(share)),
                      'sun_median': float(np.median(sun)), 'indirect_median': float(np.median(ind))},
               'identity': p['identity']}
        results.append(rep)
        print(f'  H1 r{k}: n={rep["ddgi_all_hit_probes"]}  zero radiance {rep["H1"]["zero_radiance_fraction"]:.1%}  '
              f'lit fraction median {rep["H1"]["lit_fraction_median"]:.4f} max {rep["H1"]["lit_fraction_max"]:.4f}  '
              f'hits/probe {rep["H1"]["hit_count_median"]:.1f}  hit distance {rep["H1"]["hit_dist_median"]:.1f} cm')
        print(f'  H2 r{k}: albedo luminance at hits median {rep["H2"]["albedo_median"]:.4f} max {rep["H2"]["albedo_max"]:.4f}')
        print(f'  H3b r{k}: escaped rays came no closer to a surface than (mean per probe) median '
              f'{rep["H3b"]["closest_approach_median"]} cm, p10 {rep["H3b"]["closest_approach_p10"]}, '
              f'p90 {rep["H3b"]["closest_approach_p90"]}; within one clipmap-0 voxel: '
              f'{rep["H3b"]["within_one_voxel_fraction"]}')
        print(f'  H4 r{k}: cache hit radiance = sun {rep["H4"]["sun_median"]:.4f} + indirect {rep["H4"]["indirect_median"]:.4f}  '
              f'(indirect share median {rep["H4"]["indirect_share_median"]:.3f})')
    report['results'] = results

    if len(results) >= 2:
        z = [x['H1']['zero_radiance_fraction'] for x in results]
        l = [x['H1']['lit_fraction_median'] for x in results]
        spread = max(max(abs(a - b) for a in z for b in z), max(abs(a - b) for a in l for b in l))
        report['C'] = {'zero_fractions': z, 'lit_medians': l, 'spread': spread, 'pass': bool(spread <= REPEAT_TOL)}
        if not report['C']['pass']:
            raise Stop('C', f'repeats disagree by {spread:.2e} (zero {z}, lit {l})')

    idd = results[0]['identity']['ddgi']
    no_light = idd['lightIntensity'] <= 0.0 or max(idd['lightColor']) <= 0.0
    lit_med = float(np.mean([x['H1']['lit_fraction_median'] for x in results]))
    lit_max = max(x['H1']['lit_fraction_max'] for x in results)
    alb_med = float(np.mean([x['H2']['albedo_median'] for x in results]))
    if no_light:
        verdict = 'NO LIGHT'
    elif lit_max <= LIT_BAND:
        verdict = 'SHADOWED+NOBOUNCE'
    elif alb_med <= ALBEDO_BAND:
        verdict = 'BLACK ALBEDO'
    else:
        verdict = 'MIXED'
    report['verdict'] = {'verdict': verdict, 'light_intensity': idd['lightIntensity'],
                         'light_color': idd['lightColor'], 'lit_fraction_median': lit_med,
                         'lit_fraction_max': lit_max, 'albedo_median': alb_med}
    print(f'\n  DDGI light intensity {idd["lightIntensity"]}, colour {idd["lightColor"]}; lit fraction of hit rays '
          f'median {lit_med:.4f} max {lit_max:.4f}; albedo {alb_med:.4f} -> {verdict}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '48-probe-hit-identity'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'what the writers HIT — Camera C, whole grid, DDGI sequence, {args.repeats} repeats')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'base': stage1.BASE,
              'condition': stage2.COND, 'harness': runner.harness_stamp(),
              'criteria': {'all_hit': ALL_HIT, 'zero_radiance': ZERO_RADIANCE, 'lit_band': LIT_BAND,
                           'albedo_band': ALBEDO_BAND, 'repeat_tolerance': REPEAT_TOL}}
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
        path = runner.report_path(args.out, 'probe_hit_identity.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
