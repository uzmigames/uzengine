"""Gate of phase0_capture-preload-determinism (registered 2026-10-09, before the fix was built).

Processes of one build and one configuration (Sponza pose W, `scripts/sponza-flicker-w.uzproject`, fixed
exposure, jitter 8) screenshot frame FRAME; each run log states its pre-load length ("Pre-load complete ...
after N frames").

Amendment 1 (registered before the fix's first run): the first negative-control run (6 processes, build
45545f4e) gave one pre-load length (39) in every process, and so did a run under 32 CPU busy loops, while the
same binary gave 44 an hour earlier and 43 / 44 in the followup-14 and followup-09 runs. The variation is
worker readiness (mesh loads are 4 per frame in every run; the frames on which the distance-field bakes start
differ), which machine load does not reliably move. So the gate is an intervention inside one build, through
two CVars of the fix: `r.GI.MeshSdf.PreloadWait` (the fix; default 1) and
`r.GI.MeshSdf.Diagnostic.PrepareDelayMs` (a slow worker).

`--gate` runs ROUNDS rounds of three conditions, interleaved:
  A  defaults                                   (the fix, no delay)
  B  PrepareDelayMs=30                          (the fix, slow workers)
  C  PreloadWait=0, PrepareDelayMs=30           (negative control: no wait, slow workers)
PASS when every A and B process reports the same pre-load length L, every pair of A and B screenshots agrees
within MAX_CODE code values, and C differs from L in at least one process (the delay demonstrably changes the
length without the wait). If C equals L everywhere, the gate reports that it could not show the dependence.

Without `--gate`: N processes of one configuration (`--cvar NAME=VALUE`, repeatable), PASS when they share
one pre-load length and agree within MAX_CODE; `--expect-split` only reports whether the lengths split;
`--stress N` runs every second process beside N CPU busy loops.

Usage: python scripts/test_preload_determinism.py --out DIR --gate [--rounds 3]
       python scripts/test_preload_determinism.py --out DIR [--processes 6] [--cvar N=V] [--stress N] [--expect-split]
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import re
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
import runner  # noqa: E402

PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
FRAME = 237
MAX_CODE = 1
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
DELAY_MS = 30
CONDITIONS = {
    'A': {},
    'B': {'r.GI.MeshSdf.Diagnostic.PrepareDelayMs': DELAY_MS},
    'C': {'r.GI.MeshSdf.PreloadWait': 0, 'r.GI.MeshSdf.Diagnostic.PrepareDelayMs': DELAY_MS},
}
PRELOAD = re.compile(r'Pre-load complete: assets ready and exposure seeded after (\d+) frames')
BAKES = re.compile(r'batch SDF bake')


def capture(out, label, cvars, stress=0):
    work = os.path.join(out, label)
    hogs = [subprocess.Popen([sys.executable, '-c', 'while True: pass']) for _ in range(stress)]
    try:
        res = runner.run_capture(work, PROJECT, FRAME + 2, cvars={**BASE, **cvars}, screenshot_frames=str(FRAME))
    finally:
        for h in hogs:
            h.kill()
        for h in hogs:
            h.wait()
    log = open(res['log'], encoding='utf-8', errors='replace').read()
    m = PRELOAD.search(log)
    png = os.path.join(work, 'frames', f'f_{FRAME}.png')
    run = {'label': label, 'dir': work, 'exit_code': res['exit_code'], 'cvars': cvars, 'stressed': stress > 0,
           'preload_frames': int(m.group(1)) if m else None, 'bake_batches': len(BAKES.findall(log)),
           'exe_hash': (res.get('manifest') or {}).get('executable', {}).get('hash'),
           'manifest_preload': ((res.get('manifest') or {}).get('frames') or {}).get('preload'),
           'png': png if os.path.exists(png) else None}
    print(f"  {label}: exit {run['exit_code']}, pre-load {run['preload_frames']} "
          f"(manifest {run['manifest_preload']}), bake batches {run['bake_batches']}", flush=True)
    return run


def pairwise(runs):
    images = {r['label']: runner.png_array(r['png']).astype(np.int16) if r['png'] else None for r in runs}
    out = []
    for x, y in itertools.combinations(runs, 2):
        a, b = images[x['label']], images[y['label']]
        if a is None or b is None:
            out.append({'pair': [x['label'], y['label']], 'max': None})
            continue
        d = np.abs(a - b)
        out.append({'pair': [x['label'], y['label']], 'max': int(d.max()), 'mean': float(d.mean())})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', required=True)
    ap.add_argument('--gate', action='store_true')
    ap.add_argument('--rounds', type=int, default=3)
    ap.add_argument('--processes', type=int, default=6)
    ap.add_argument('--cvar', action='append', default=[])
    ap.add_argument('--stress', type=int, default=0)
    ap.add_argument('--expect-split', action='store_true')
    a = ap.parse_args()
    runner.require_exe()
    os.makedirs(a.out, exist_ok=True)
    report = {'harness': runner.harness_stamp(), 'frame': FRAME, 'base_cvars': BASE}

    if a.gate:
        runs = []
        for r in range(a.rounds):
            for name, cv in CONDITIONS.items():
                runs.append(capture(a.out, f'{name}{r}', cv))
        ab = [x for x in runs if x['label'][0] in 'AB']
        c = [x for x in runs if x['label'][0] == 'C']
        lengths_ab = sorted({x['preload_frames'] for x in ab})
        pairs = pairwise(ab)
        worst = max((p['max'] for p in pairs if p['max'] is not None), default=None)
        missing = [x['label'] for x in ab if x['png'] is None or x['exit_code'] != 0]
        L = lengths_ab[0] if len(lengths_ab) == 1 else None
        c_differs = L is not None and any(x['preload_frames'] != L for x in c)
        ok = L is not None and worst is not None and worst <= MAX_CODE and not missing and c_differs
        report.update(runs=runs, pairs_ab=pairs, lengths_ab=lengths_ab, lengths_c=[x['preload_frames'] for x in c],
                      worst_code_delta_ab=worst, missing=missing, negative_control_differs=c_differs)
        verdict = ('PASS' if ok else 'FAIL') + (
            '' if c_differs or L is None else ' (the negative control did not change the pre-load length: '
                                              'the dependence could not be shown)')
        print(f'A/B pre-load lengths {lengths_ab}; C {report["lengths_c"]}; worst A/B code delta {worst}; '
              f'missing {missing}')
    else:
        cvars = {}
        for kv in a.cvar:
            k, v = kv.split('=', 1)
            cvars[k] = float(v)
        runs = [capture(a.out, f'p{i}', cvars, a.stress if (a.stress and i % 2 == 1) else 0)
                for i in range(a.processes)]
        lengths = sorted({x['preload_frames'] for x in runs})
        pairs = pairwise(runs)
        worst = max((p['max'] for p in pairs if p['max'] is not None), default=None)
        missing = [x['label'] for x in runs if x['png'] is None or x['exit_code'] != 0]
        report.update(runs=runs, pairs=pairs, preload_lengths=lengths, worst_code_delta=worst, missing=missing)
        if a.expect_split:
            verdict = 'split reproduced' if len(lengths) > 1 else 'NOT reproduced (no split in this run)'
            ok = True
        else:
            ok = len(lengths) == 1 and lengths[0] is not None and worst is not None and worst <= MAX_CODE and not missing
            verdict = 'PASS' if ok else 'FAIL'
        print(f'pre-load lengths {lengths}; worst pairwise code delta {worst}; missing {len(missing)}')

    report['verdict'] = verdict
    with open(os.path.join(a.out, 'preload-determinism.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(verdict)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
