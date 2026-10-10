"""followup-01: the DXR (DDGI) and SDF probe writers compared PER PROBE, before any reconstruction.

The consumer-level comparison (38-ddgi-sdf-parity) put the DXR path at 0.51x the SDF path over one
rectangle. This compares what the two WRITERS record for the same probe, at the same world position,
from the transport statistics each already reads back: the mean ray-radiance luminance and the
fraction of rays that escaped to the sky. Both are per probe, in linear radiance, before the atlas
integration (DDGI) or the SH projection (SDF), and before the consumer.

Pre-registered before the captures (followup-01 notes, 2026-09-16):

  capture   Camera C, still, one process with BOTH diagnostics on — r.GI.DDGI.Diagnostic=1 and
            r.GI.RadianceCache.Diagnostic=1 — under the DDGI configuration (r.RayTracing=1,
            r.RayTracing.GI=1). The SDF cache keeps updating in that configuration (frame_gi_passes),
            so one run yields both writers' snapshots on the same grid. r.TAA=1 as in the consumer
            study; irrelevant to the writers. 2 repeats. Each diagnostic fires once after frame 120.
  pairing   by probe index: both writers lay probes out x-fastest, then y, then z, 16 per axis, and
            the DDGI copies the cache's origin and spacing (frame_constants.cpp). The grid lines of
            both snapshots must report the same origin and spacing (Q0).

  Q0  both snapshots are present in the log, with identical origin and spacing, and every one of the
      4096 probes has a line from each writer. Anything less stops the study.
  Q1  the filter the cache applies to its own grid mean — probes with field distance < 5 cm are
      inside geometry and trace nothing — is applied to the pair, and its count is reported.
  R   over the paired, filtered probes: the per-probe ratio DDGI / SDF of mean radiance (median, p10,
      p90), the per-probe difference of sky fraction (median, p10, p90), the two grid means, and the
      ratio binned by the SDF writer's sky fraction (0-10 %, 10-50 %, 50-100 %), because the sky is
      what the directional-sky change touched and probes that see mostly sky are where the two sky
      sources must agree.
  C   control that can fail: the two repeats must give ratio medians within 5 % of each other.
      Different snapshot frames and cross-process spread can move this; if they do, the medians are
      not trusted.

A ratio is not a causal share. This says whether the writers disagree per probe and where; it does
not yet separate the tracing budget from the Chebyshev visibility, which the review lists next.

Usage: python scripts/rendering/run_probe_parity.py [--repeats 2] [--out DIR] [--reuse]
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
CVARS = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.GI.FinalGather': 0, 'r.RadianceCache': 1, 'r.TAA': 1,
         'r.RayTracing': 1, 'r.RayTracing.GI': 1, 'r.GI.DDGI.Diagnostic': 1, 'r.GI.RadianceCache.Diagnostic': 1}
TOTAL = 16 ** 3
INSIDE_CM = 5.0
REPEAT_TOL = 0.05
NUM = r'([-+0-9.eE]+|nan|inf|-nan|-inf)'
RE_DDGI = re.compile(r'DDGI transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) world=\(' + NUM + ',' + NUM + ',' + NUM
                     + r'\) skyFraction=' + NUM + r' meanRadiance=' + NUM)
RE_RC = re.compile(r'RC transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) fieldDistance=' + NUM + r' skyFraction='
                   + NUM + r' frontHit=' + NUM + r' sunLum=' + NUM + r' meanRadiance=' + NUM)
RE_DDGI_GRID = re.compile(r'DDGI transport: grid snapshotFrame=(\d+) readFrame=(\d+) probes=(\d+) meanRadiance=' + NUM
                          + r' skyFraction=' + NUM)
RE_RC_GRID = re.compile(r'RC transport: grid snapshotFrame=(\d+) readFrame=(\d+) probes=(\d+) meanRadiance=' + NUM
                        + r' skyFraction=' + NUM)
RE_DDGI_QUEUE = re.compile(r'DDGI transport: queued diagnostic snapshot frame=(\d+) origin=\(' + NUM + ',' + NUM + ','
                           + NUM + r'\) spacing=' + NUM)
RE_RC_QUEUE = re.compile(r'RC diagnostic: queued SH snapshot frame=(\d+) .*origin=\(' + NUM + ',' + NUM + ',' + NUM
                         + r'\) spacing=' + NUM)


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def parse(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    ddgi = np.full((TOTAL, 2), np.nan)   # sky, radiance
    rc = np.full((TOTAL, 5), np.nan)     # fieldDistance, sky, frontHit, sunLum, radiance
    for m in RE_DDGI.finditer(text):
        i = int(m.group(1))
        ddgi[i] = [float(m.group(8)), float(m.group(9))]
    for m in RE_RC.finditer(text):
        i = int(m.group(1))
        rc[i] = [float(m.group(5)), float(m.group(6)), float(m.group(7)), float(m.group(8)), float(m.group(9))]
    grids = {'ddgi': RE_DDGI_GRID.search(text), 'rc': RE_RC_GRID.search(text)}
    queued = {'ddgi': RE_DDGI_QUEUE.search(text), 'rc': RE_RC_QUEUE.search(text)}
    out = {'ddgi': ddgi, 'rc': rc,
           'ddgi_lines': int(np.count_nonzero(~np.isnan(ddgi[:, 1]))),
           'rc_lines': int(np.count_nonzero(~np.isnan(rc[:, 4]))),
           'grid': {k: ({'snapshot_frame': int(g.group(1)), 'probes': int(g.group(3)),
                         'mean_radiance': float(g.group(4)), 'sky_fraction': float(g.group(5))} if g else None)
                    for k, g in grids.items()},
           'origin': {k: ([float(q.group(2)), float(q.group(3)), float(q.group(4))] if q else None)
                      for k, q in queued.items()},
           'spacing': {k: (float(q.group(5)) if q else None) for k, q in queued.items()}}
    return out


def pct(a):
    return {'median': float(np.median(a)), 'p10': float(np.percentile(a, 10)), 'p90': float(np.percentile(a, 90)),
            'n': int(a.size)}


def run(args, report):
    works = []
    for k in range(args.repeats):
        work = os.path.join(args.out, f'r{k}')
        works.append(work)
        if args.reuse:
            continue
        print(f'  ... repeat {k}', flush=True)
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', PROJECT), FRAMES, cvars=CVARS,
                                 timing_first=200, clean=True)
        if res['exit_code'] != 0:
            raise Stop('capture', f'repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = [{'dir': w, 'identity': runner.capture_identity(w)} for w in works]

    parsed = [parse(os.path.join(w, 'run.log')) for w in works]
    report['snapshots'] = [{'grid': p['grid'], 'origin': p['origin'], 'spacing': p['spacing'],
                            'ddgi_probe_lines': p['ddgi_lines'], 'rc_probe_lines': p['rc_lines']} for p in parsed]

    # ---- Q0 ----------------------------------------------------------------------------------
    for k, p in enumerate(parsed):
        if p['grid']['ddgi'] is None or p['grid']['rc'] is None:
            raise Stop('Q0', f'repeat {k}: a writer produced no snapshot (ddgi {p["grid"]["ddgi"] is not None}, '
                             f'rc {p["grid"]["rc"] is not None})')
        if p['origin']['ddgi'] is None or p['origin']['rc'] is None or p['origin']['ddgi'] != p['origin']['rc'] \
                or p['spacing']['ddgi'] != p['spacing']['rc']:
            raise Stop('Q0', f'repeat {k}: the two writers report different grids: {p["origin"]}, {p["spacing"]}')
        if p['ddgi_lines'] != TOTAL or p['rc_lines'] != TOTAL:
            raise Stop('Q0', f'repeat {k}: probe lines ddgi {p["ddgi_lines"]}, rc {p["rc_lines"]}, expected {TOTAL}')
        print(f'  Q0 repeat {k}: both snapshots on origin {p["origin"]["rc"]} spacing {p["spacing"]["rc"]}, '
              f'{TOTAL} probes each (ddgi frame {p["grid"]["ddgi"]["snapshot_frame"]}, '
              f'rc frame {p["grid"]["rc"]["snapshot_frame"]})')

    # ---- Q1 / R ------------------------------------------------------------------------------
    per_repeat = []
    for k, p in enumerate(parsed):
        d, r = p['ddgi'], p['rc']
        finite = np.isfinite(d).all(axis=1) & np.isfinite(r).all(axis=1)
        outside = finite & (r[:, 0] >= INSIDE_CM)
        traced = outside & (r[:, 4] > 0) & (d[:, 1] > 0)
        ratio = d[traced, 1] / r[traced, 4]
        dsky = d[outside, 0] - r[outside, 1]
        bins = {'sky 0-10 %': outside & (r[:, 1] < 0.1), 'sky 10-50 %': outside & (r[:, 1] >= 0.1) & (r[:, 1] < 0.5),
                'sky 50-100 %': outside & (r[:, 1] >= 0.5)}
        by_sky = {}
        for name, m in bins.items():
            mm = m & traced
            by_sky[name] = pct(d[mm, 1] / r[mm, 4]) if mm.any() else None
        rep = {'finite': int(finite.sum()), 'outside_geometry': int(outside.sum()), 'traced_both': int(traced.sum()),
               'ratio_ddgi_over_sdf': pct(ratio) if ratio.size else None,
               'sky_fraction_difference': pct(dsky) if dsky.size else None,
               'ratio_by_sdf_sky_fraction': by_sky,
               'grid_means': {'ddgi': p['grid']['ddgi'], 'rc': p['grid']['rc']}}
        per_repeat.append(rep)
        print(f'\n  repeat {k}: finite {rep["finite"]}, outside geometry (field >= {INSIDE_CM:g} cm) '
              f'{rep["outside_geometry"]}, traced by both {rep["traced_both"]}')
        if rep['ratio_ddgi_over_sdf']:
            q = rep['ratio_ddgi_over_sdf']
            print(f'    ratio DDGI/SDF mean radiance per probe: median {q["median"]:.4f}, p10 {q["p10"]:.4f}, '
                  f'p90 {q["p90"]:.4f} (n={q["n"]})')
        if rep['sky_fraction_difference']:
            q = rep['sky_fraction_difference']
            print(f'    sky fraction DDGI - SDF per probe:      median {q["median"]:+.4f}, p10 {q["p10"]:+.4f}, '
                  f'p90 {q["p90"]:+.4f}')
        for name, q in by_sky.items():
            print(f'    {name:<14} ratio median {q["median"]:.4f} (n={q["n"]})' if q else f'    {name:<14} no probes')
        print(f'    grid means: ddgi {p["grid"]["ddgi"]["mean_radiance"]:.4e} (sky {p["grid"]["ddgi"]["sky_fraction"]:.3f}), '
              f'rc {p["grid"]["rc"]["mean_radiance"]:.4e} (sky {p["grid"]["rc"]["sky_fraction"]:.3f})')
    report['per_repeat'] = per_repeat

    # ---- C -----------------------------------------------------------------------------------
    meds = [r['ratio_ddgi_over_sdf']['median'] for r in per_repeat if r['ratio_ddgi_over_sdf']]
    if len(meds) >= 2:
        spread = max(abs(a - b) / max(abs(a), abs(b)) for i, a in enumerate(meds) for b in meds[i + 1:])
        report['C'] = {'medians': meds, 'worst_relative_spread': spread, 'tolerance': REPEAT_TOL,
                       'pass': bool(spread <= REPEAT_TOL)}
        print(f'\n  C repeats: ratio medians {["%.4f" % m for m in meds]}, spread {spread:.2%} '
              f'({"ok" if report["C"]["pass"] else "FAILS — the medians are not trusted"})')
        if not report['C']['pass']:
            raise Stop('C', 'the repeats disagree on the ratio median beyond the tolerance')
    else:
        report['C'] = {'medians': meds, 'pass': None, 'note': 'fewer than two repeats produced a ratio'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '43-probe-parity'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'DDGI vs SDF writers, per probe — Camera C, both diagnostics in one run, {args.repeats} repeats')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'cvars': CVARS,
              'harness': runner.harness_stamp(),
              'criteria': {'inside_geometry_cm': INSIDE_CM, 'repeat_tolerance': REPEAT_TOL}}
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
        path = os.path.join(args.out, 'probe_parity.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
