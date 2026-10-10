"""followup-14, reopened: does omitting `r.TAA.ReconstructionHdrWeightExponent` render the same as
setting it to its default of 1.0?

`test_correction_gates.py` says no: its negative control failed, with a maximum absolute difference of
3.218e-03 between the two conditions against a floor of 1.473e-03 measured from each configuration
against itself. That floor is a SINGLE pair per configuration, and this renderer is not
bit-reproducible across processes — the ledger has one process in three departing from the other two.
So the failure has two readings, and this separates them the way the review specified: repeat both
conditions, interleaved, and compare the difference BETWEEN conditions against the variation BETWEEN
REPEATS OF THE SAME condition. The tolerance is not raised; there is no tolerance here at all.

Pre-registered before the captures (followup-14 notes, 2026-09-16):

  capture   exactly what the harness captures, so this is like for like: pose W
            (`sponza-flicker-w.uzproject`), ROI `window` (800, 200, 128, 256), frames 237..252 after
            the 200-frame warm-up, `--region-fields taa`, jitter length 8, exposure fixed at 1.0.
  metric    the per-pixel luminance of `taa_output` over those 16 frames — the harness's own array.
  conditions
            omitted    the CVar is not passed at all
            explicit1  the CVar is passed as 1.0, its default
  order     INTERLEAVED: omitted, explicit1, omitted, explicit1, ... one process each, N repeats
            (default 4), so a drift over the session cannot land on one condition.

  Distances, computed for two statistics because judging a mean against a max is not a comparison:
    max |delta|   the harness's own statistic, which failed
    mean |delta|  the same arrays, averaged

  W  WITHIN-condition distances: every pair of repeats of the same condition (4 repeats -> 6 pairs
     per condition, 12 in all).
  B  BETWEEN-condition distances: every omitted x explicit1 pair (16 in all).

  The verdict, fixed now, on each statistic separately:
    SEPARATED       min(B) > max(W) — every cross pair exceeds every same-condition pair. The
                    conditions really do render differently.
    INDISTINGUISHABLE  max(B) <= max(W) — the cross differences sit inside the spread the same
                    configuration produces on its own. The harness's floor understated that spread.
    OVERLAPPING     anything else: the two sets interleave, and this does not decide it.

  Reported, not judged: the harness's original comparison (one capture of each against a two-capture
  floor) recomputed from these repeats, so the two can be read side by side.

This says whether the two configurations differ, not why. If they do, what differs in the render path
is a separate question this does not open.

Usage: python scripts/run_default_vs_explicit.py [--repeats 4] [--out DIR]
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

CVAR = 'r.TAA.ReconstructionHdrWeightExponent'
SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 16
LAST = FIRST + FRAMES - 1
PROJECT = 'sponza-flicker-w.uzproject'
ROI = ('window', 800, 200, 128, 256)
BASE = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}
CONDITIONS = {'omitted': {}, 'explicit1': {CVAR: 1.0}}
#: The numbers the harness reported, for the record and for comparison.
HARNESS = {'between_max': 3.218e-03, 'floor_max': 1.473e-03}


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def stack(region_dir):
    seq = regiondump.load_sequence(region_dir, FIRST, LAST)
    arr = np.stack([f.rect(ROI[0]).luminance('taa_output') for f in seq]).astype(np.float64)
    if not np.all(np.isfinite(arr)):
        raise Stop('finite', f'{region_dir} holds non-finite luminance')
    return arr


def distances(a, b):
    d = np.abs(a - b)
    return {'max': float(d.max()), 'mean': float(d.mean())}


def verdict(between, within, key):
    b = [d[key] for d in between]
    w = [d[key] for d in within]
    if min(b) > max(w):
        return 'SEPARATED', b, w
    if max(b) <= max(w):
        return 'INDISTINGUISHABLE', b, w
    return 'OVERLAPPING', b, w


def run(args, report):
    if not args.reuse:
        runner.require_exe()
    works = {c: [] for c in CONDITIONS}
    for k in range(args.repeats):
        for cond, cvars in CONDITIONS.items():          # interleaved by construction
            work = os.path.join(args.out, f'{cond}_r{k}')
            works[cond].append(work)
            if args.reuse:
                continue
            print(f'  ... {cond} repeat {k}', flush=True)
            res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', PROJECT),
                                     LAST - SCENE_START + 8, regions=[ROI], region_fields='taa',
                                     region_first=FIRST, region_last=LAST, cvars={**BASE, **cvars},
                                     timing_first=FIRST, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{cond} repeat {k}: exit {res["exit_code"]}')

    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w)} for w in ws]
                          for c, ws in works.items()}
    hashes = {str(e['identity'].get('executable', {}).get('hash'))
              for ws in report['captures'].values() for e in ws}
    if len(hashes) != 1:
        raise Stop('identity', f'the captures do not share one binary: {sorted(hashes)}')
    # Which condition a capture ran under is NOT in its manifest: `cli` holds only a few keys and no
    # --cvar overrides, and `cvars` reports 1 for this CVar in BOTH conditions — setting a CVar to its
    # own default does not make it non-default. The engine's log does record it ("CVar applied: ..."),
    # so that is the evidence used here. The gap itself is recorded in the followup-14 notes.
    for cond, ws in report['captures'].items():
        for e in ws:
            with open(os.path.join(e['dir'], 'run.log'), encoding='utf-8', errors='replace') as fh:
                applied = f'CVar applied: {CVAR}' in fh.read()
            if applied != (cond == 'explicit1'):
                raise Stop('conditions',
                           f'{e["dir"]}: the log {"shows" if applied else "does not show"} {CVAR} '
                           f'being applied, which is wrong for condition {cond}')
    report['condition_evidence'] = ('the engine log line "CVar applied: ' + CVAR + '"; the manifest '
                                    'cannot distinguish these conditions')

    print('  loading captures', flush=True)
    arrays = {c: [stack(os.path.join(w, 'region')) for w in ws] for c, ws in works.items()}

    within = []
    for cond, arrs in arrays.items():
        for i, j in itertools.combinations(range(len(arrs)), 2):
            d = distances(arrs[i], arrs[j])
            d.update({'condition': cond, 'pair': [i, j]})
            within.append(d)
    between = []
    for i, a in enumerate(arrays['omitted']):
        for j, b in enumerate(arrays['explicit1']):
            d = distances(a, b)
            d.update({'pair': [f'omitted_r{i}', f'explicit1_r{j}']})
            between.append(d)
    report['within'] = within
    report['between'] = between

    print(f'\n  within-condition distances ({len(within)} pairs) and between-condition ({len(between)}):')
    for key in ('max', 'mean'):
        v, b, w = verdict(between, within, key)
        report[f'verdict_{key}'] = {
            'verdict': v,
            'between': {'min': min(b), 'max': max(b)},
            'within': {'min': min(w), 'max': max(w)},
        }
        print(f'    {key:<5} |delta|: within {min(w):.3e}..{max(w):.3e}   '
              f'between {min(b):.3e}..{max(b):.3e}   -> {v}')

    # The harness's own comparison, recomputed from the first two repeats of each condition.
    floor = max(distances(arrays['omitted'][0], arrays['omitted'][1])['max'],
                distances(arrays['explicit1'][0], arrays['explicit1'][1])['max'])
    cross = distances(arrays['omitted'][0], arrays['explicit1'][0])['max']
    report['harness_style'] = {'floor_max': floor, 'between_max': cross,
                               'would_fail': bool(cross > floor),
                               'harness_reported': HARNESS}
    print(f'\n  the harness\'s own comparison, recomputed here: between {cross:.3e} against a '
          f'two-capture floor of {floor:.3e} -> {"fails" if cross > floor else "passes"}')
    print(f'  (the harness reported between {HARNESS["between_max"]:.3e}, floor '
          f'{HARNESS["floor_max"]:.3e})')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=4)
    ap.add_argument('--reuse', action='store_true',
                    help='analyse the captures already in --out instead of capturing again')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '40-default-vs-explicit'))
    args = ap.parse_args()
    print(f'{CVAR}: omitted vs explicit 1.0 — pose W, frames {FIRST}..{LAST}, '
          f'{args.repeats} interleaved repeats each')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'cvar': CVAR, 'roi': ROI,
              'frames': [FIRST, LAST], 'repeats': args.repeats, 'conditions': CONDITIONS,
              'harness': runner.harness_stamp()}
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
        path = os.path.join(args.out, 'default_vs_explicit.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
