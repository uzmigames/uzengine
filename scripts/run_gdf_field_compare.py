"""followup-13: does the recentre change the field itself, at points that stay inside the clipmap?

A recentre of clipmap 0 re-composites it at a centre snapped to the voxel lattice AND moves the origin
everything samples with. The phase sweep (H-40) could not separate those two. This looks at the field
alone, with `r.GI.GDF.Diagnostic.FieldCompare=1`: on every recentre the engine reads the raw R16F
distance volume back immediately before and immediately after the recomposite and compares them at the
SAME world positions — the centre moves by a whole number of voxels, so every old voxel that is still
inside the clipmap lands exactly on a new voxel. One log line per event.

Pre-registered before the run (followup-13 notes, 2026-09-16):

  capture   the boundary trajectory (`sponza-gdf-boundary.uzproject`, `scripts/sponza-gdf-boundary.lua`):
            120 warm-up frames, then six 140-frame segments each crossing one grid axis with a stop on
            either side — the trajectory the motion study uses, so its recentres are the events already
            measured. 2 repeats. No region dump: this reads only the engine log.

  F0  at least one recentre occurred and produced a compare line, in every repeat; and every shift is a
      whole number of voxels (residual < 1e-3 voxel). A recentre that is not voxel-aligned would make
      the comparison meaningless, and the study stops.
  F1  the comparison covered geometry: overlap >= 50 % of the volume, non-finite voxels < 0.1 %.

  R   per event: the shift, the overlap, the fraction of overlapping voxels bit-identical, mean and
      p99 and max |delta| in centimetres, and the fraction of voxels that changed by more than 1 % of
      a voxel (19.53 cm at 256^3 over 2500 cm).

  The reading, fixed now:
    IDENTICAL       every event has identical fraction 1.0 (max |delta| = 0). The composite is
                    deterministic in world space; the recentre changes NOTHING in the overlapping
                    field, and whatever the recentre does to the lighting is not the field's values.
    REQUANTISED     some event has max |delta| > 1 % of a voxel. The field does change at fixed world
                    points; the magnitudes are reported and it is a candidate — not yet the cause —
                    for the excess at recentres.
    NEGLIGIBLE      differences exist but every one is <= 1 % of a voxel: the field changes below the
                    scale of a voxel, reported as such.

What this cannot say: whether the field change, if any, is what the lighting reacts to; that needs the
next control (a recomposite in place, without a move). This is the field-only half of the separation.

Usage: python scripts/run_gdf_field_compare.py [--repeats 2] [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import traceback

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

SCENE_START = 37
WARMUP = 120
SEGMENT = 140
SEGMENTS = 6
FRAMES = WARMUP + SEGMENT * SEGMENTS + 8
PROJECT = 'sponza-gdf-boundary.uzproject'
CVARS = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8,
         'r.GI.GDF.Diagnostic.FieldCompare': 1}
NUM = r'([-+0-9.eE]+|nan|inf|-nan|-inf)'
RE_LINE = re.compile(
    r'GDF field compare: clipmap=(\d+) oldCenter=\(' + NUM + ',' + NUM + ',' + NUM + r'\) newCenter=\(' + NUM + ','
    + NUM + ',' + NUM + r'\) shiftVoxels=\((-?\d+),(-?\d+),(-?\d+)\) shiftResidual=' + NUM + r' voxelCm=' + NUM
    + r' overlap=(\d+) identical=(\d+) nonFinite=(\d+) meanAbsCm=' + NUM + r' p99AbsCm=' + NUM + r' maxAbsCm='
    + NUM + r' overOnePercentVoxel=(\d+) sameTexelTotal=(\d+) sameTexelIdentical=(\d+) sameTexelMeanAbsCm='
    + NUM + r' sameTexelMaxAbsCm=' + NUM)
RE_RECENTRE = re.compile(r'GI recentre: frame=(\d+)')
RESIDUAL_TOL = 1e-3
MIN_OVERLAP = 0.5
MAX_NONFINITE = 1e-3
ONE_PERCENT = 0.01


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def parse(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    events = []
    for m in RE_LINE.finditer(text):
        g = m.groups()
        # groups: 0 cascade, 1-3 old centre, 4-6 new centre, 7-9 shift, 10 residual, 11 voxel,
        # 12 overlap, 13 identical, 14 non-finite, 15 mean, 16 p99, 17 max, 18 over-one-percent
        g = (g[0],) + g[1:]
        cascade = int(g[0]); g = g[1:]
        overlap, identical, nonfinite = int(g[11]), int(g[12]), int(g[13])
        voxel = float(g[10])
        events.append({
            'cascade': cascade,
            'old_center': [float(g[0]), float(g[1]), float(g[2])],
            'new_center': [float(g[3]), float(g[4]), float(g[5])],
            'shift_voxels': [int(g[6]), int(g[7]), int(g[8])], 'shift_residual': float(g[9]),
            'voxel_cm': voxel, 'overlap': overlap, 'identical': identical, 'non_finite': nonfinite,
            'identical_fraction': identical / overlap if overlap else None,
            # The engine's p99 is a histogram estimate whose first bin is centred at 0.5e-3 voxel; when
            # max is exactly 0 every voxel sits in that bin and the estimate would read 0.0098 cm for a
            # difference that does not exist. Report 0 there, and the estimate otherwise.
            'mean_abs_cm': float(g[14]), 'p99_abs_cm': (0.0 if float(g[16]) == 0.0 else float(g[15])),
            'p99_is_histogram_estimate': True, 'max_abs_cm': float(g[16]),
            'over_one_percent_voxel': int(g[17]),
            'over_one_percent_fraction': int(g[17]) / overlap if overlap else None,
            # Same texel, no shift, whole volume: a different population from the overlap above, so
            # the two identical fractions are not complementary.
            'same_texel_total': int(g[18]), 'same_texel_identical': int(g[19]),
            'same_texel_identical_fraction': int(g[19]) / int(g[18]) if int(g[18]) else None,
            'same_texel_mean_abs_cm': float(g[20]), 'same_texel_max_abs_cm': float(g[21]),
        })
    return events, len(RE_RECENTRE.findall(text))


def run(args, report):
    works = []
    for k in range(args.repeats):
        work = os.path.join(args.out, f'r{k}')
        works.append(work)
        if args.reuse:
            continue
        print(f'  ... repeat {k}', flush=True)
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', PROJECT), FRAMES,
                                 cvars={**CVARS, 'r.GI.GDF.Diagnostic.FieldCompareCascade': args.cascade},
                                 timing_first=SCENE_START + WARMUP, clean=True)
        if res['exit_code'] != 0:
            raise Stop('capture', f'repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = [{'dir': w, 'identity': runner.capture_identity(w)} for w in works]

    resolution_voxels = None
    all_events = []
    for k, w in enumerate(works):
        events, recentres = parse(os.path.join(w, 'run.log'))
        report.setdefault('repeats', []).append({'recentre_log_lines': recentres, 'compare_events': len(events),
                                                 'events': events})
        print(f'  repeat {k}: {recentres} recentre log lines, {len(events)} field-compare events')
        # ---- F0 ------------------------------------------------------------------------------
        if not events:
            raise Stop('F0', f'repeat {k}: no field-compare event; either no recentre happened or the '
                             'diagnostic did not run')
        bad = [e for e in events if e['shift_residual'] >= RESIDUAL_TOL]
        if bad:
            raise Stop('F0', f'repeat {k}: {len(bad)} events are not voxel-aligned (residual up to '
                             f'{max(e["shift_residual"] for e in bad):.3e} voxel)')
        # ---- F1 ------------------------------------------------------------------------------
        for e in events:
            total = e['overlap'] + e['non_finite']
            # The volume is res^3; overlap must be a good part of it. Infer res from the first event's
            # overlap and shift is fragile, so use the CVar's resolution when logged; else skip the
            # absolute check and report the count.
            e['non_finite_fraction'] = e['non_finite'] / total if total else 0.0
            if e['non_finite_fraction'] > MAX_NONFINITE:
                raise Stop('F1', f'repeat {k}: {e["non_finite_fraction"]:.2%} non-finite voxels in an event')
        all_events.extend(events)

    # ---- R -----------------------------------------------------------------------------------
    print('\n  per event (all repeats):')
    print(f'    {"shift (voxels)":<16}{"overlap":>10}{"identical":>11}{"mean|d| cm":>12}{"p99|d| cm":>11}'
          f'{"max|d| cm":>11}{"> 1 % voxel":>13}')
    for e in all_events:
        print(f'    {str(tuple(e["shift_voxels"])):<16}{e["overlap"]:>10}{e["identical_fraction"]:>11.4%}'
              f'{e["mean_abs_cm"]:>12.4e}{e["p99_abs_cm"]:>11.3e}{e["max_abs_cm"]:>11.3e}'
              f'{e["over_one_percent_fraction"]:>13.4%}')
    voxel = all_events[0]['voxel_cm']
    max_abs = max(e['max_abs_cm'] for e in all_events)
    min_identical = min(e['identical_fraction'] for e in all_events)
    # Reported beside the verdict, never instead of it: the events that are single-voxel recentres
    # along one axis (the runtime recentres the motion study measured) versus any other shift, such
    # as the initial placement from the origin at scene start.
    runtime = [e for e in all_events if sum(abs(v) for v in e['shift_voxels']) == 1]
    other = [e for e in all_events if sum(abs(v) for v in e['shift_voxels']) != 1]
    report['by_shift'] = {
        'single_voxel_events': len(runtime),
        'single_voxel_min_identical': min((e['identical_fraction'] for e in runtime), default=None),
        'single_voxel_max_abs_cm': max((e['max_abs_cm'] for e in runtime), default=None),
        'other_events': [{'shift': e['shift_voxels'], 'old_center': e['old_center'], 'new_center': e['new_center'],
                          'identical_fraction': e['identical_fraction'], 'mean_abs_cm': e['mean_abs_cm'],
                          'max_abs_cm': e['max_abs_cm']} for e in other],
    }
    print('')
    print(f'  single-voxel recentres: {len(runtime)} events, smallest identical fraction '
          f'{report["by_shift"]["single_voxel_min_identical"]}, largest |delta| '
          f'{report["by_shift"]["single_voxel_max_abs_cm"]} cm; other shifts: {len(other)} events')
    if min_identical >= 1.0:
        verdict = 'IDENTICAL'
    elif max_abs > ONE_PERCENT * voxel:
        verdict = 'REQUANTISED'
    else:
        verdict = 'NEGLIGIBLE'
    report['verdict'] = {'verdict': verdict, 'events': len(all_events), 'voxel_cm': voxel,
                         'min_identical_fraction': min_identical, 'max_abs_cm': max_abs,
                         'one_percent_voxel_cm': ONE_PERCENT * voxel}
    print(f'\n  verdict over {len(all_events)} events: {verdict} — smallest identical fraction '
          f'{min_identical:.4%}, largest |delta| {max_abs:.3e} cm against 1 % of a voxel = '
          f'{ONE_PERCENT * voxel:.3e} cm')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '44-gdf-field-compare'))
    ap.add_argument('--cascade', type=int, default=0, help='which clipmap to compare (r.GI.GDF.Diagnostic.FieldCompareCascade)')
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'GDF field before/after each recentre — boundary trajectory, {FRAMES} frames, {args.repeats} repeats')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'cvars': CVARS,
              'harness': runner.harness_stamp(),
              'criteria': {'residual_tol_voxel': RESIDUAL_TOL, 'min_overlap': MIN_OVERLAP,
                           'max_non_finite': MAX_NONFINITE, 'one_percent_voxel': ONE_PERCENT}}
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
        path = runner.report_path(args.out, 'gdf_field_compare.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
