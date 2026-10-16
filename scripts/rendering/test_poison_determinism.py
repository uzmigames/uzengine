"""Compare equal-pre-load frames from two allocation-poison patterns.

Usage: python scripts/rendering/test_poison_determinism.py --out DIR
Every case runs PROCESSES processes per pattern. A cross-pattern difference is an
uninitialised-read finding only when matched within-pattern frames are identical.
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import subprocess
import sys

import numpy as np

from test_rg_states_identity import CASES, PRELOAD, PROCESSES, load_png, runner

PATTERNS = ('0x3F', '0xBF')
GATED_CASES = ('reflection', 'fog_shaft', 'path_tracer')


def compare_case(entries, images):
    """Classify all equal-pre-load pairs; missing evidence never produces PASS."""
    pairs = []
    for (left, a), (right, b) in itertools.combinations(zip(entries, images), 2):
        if (a is None or b is None or left['preload_frames'] is None
                or left['preload_frames'] != right['preload_frames']):
            continue
        delta = int(np.abs(a - b).max()) if a.shape == b.shape else None
        pairs.append({
            'left': left['dir'], 'right': right['dir'],
            'preload_frames': left['preload_frames'],
            'kind': 'within' if left['pattern'] == right['pattern'] else 'across',
            'pattern': left['pattern'] if left['pattern'] == right['pattern'] else None,
            'max_abs_delta': delta,
        })
    within = [p for p in pairs if p['kind'] == 'within']
    across = [p for p in pairs if p['kind'] == 'across']
    errors = [e['dir'] for e in entries
              if e.get('error') or e['exit_code'] != 0 or not e['poison_active']]
    errors += [e['dir'] for e, image in zip(entries, images) if image is None]
    if errors or any(p['max_abs_delta'] is None for p in pairs):
        verdict = 'FAIL'
        reason = 'Invalid capture, missing poison-active log, missing frame or incompatible frame shape'
    elif any(p['max_abs_delta'] > 0 for p in within):
        verdict = 'INDETERMINATE'
        reason = 'Matched within-pattern frames differ'
    elif not across or any(not any(p['pattern'] == pattern for p in within) for pattern in PATTERNS):
        verdict = 'UNMATCHED'
        reason = 'No equal-pre-load pair across patterns or within each pattern'
    elif any(p['max_abs_delta'] > 0 for p in across):
        verdict = 'FAIL'
        reason = 'Across-pattern delta with identical within-pattern frames: uninitialised read'
    else:
        verdict = 'PASS'
        reason = 'All equal-pre-load within-pattern and across-pattern pairs are bit-identical'
    return {'verdict': verdict, 'reason': reason, 'runs': entries, 'pairs': pairs,
            'invalid_runs': sorted(set(errors))}


def capture_run(out, name, project, frame, cvars, pattern, process):
    """Capture one process and retain failed-run evidence in the JSON report."""
    work = os.path.join(out, f'{name}_{pattern}_{process}')
    entry = {'dir': os.path.basename(work), 'pattern': pattern, 'process': process,
             'frame': frame, 'preload_frames': None, 'exit_code': None,
             'poison_active': False}
    try:
        result = runner.run_capture(work, project, frame + 2, cvars=cvars,
                                    screenshot_frames=str(frame),
                                    extra_args=('--poison-allocations', pattern))
        entry['exit_code'] = result['exit_code']
        entry['exe_hash'] = (result.get('manifest') or {}).get('executable', {}).get('hash')
    except (RuntimeError, subprocess.TimeoutExpired, OSError, ValueError) as error:
        entry['error'] = str(error)
    log_path = os.path.join(work, 'run.log')
    if os.path.exists(log_path):
        with open(log_path, encoding='utf-8', errors='replace') as log_file:
            log = log_file.read()
        match = PRELOAD.search(log)
        entry['preload_frames'] = int(match.group(1)) if match else None
        entry['poison_active'] = f'allocation poison active: {pattern}' in log
    return entry


def main():
    """Run the seven-case sweep and write its evidence and gated verdict."""
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    report = {'harness': runner.harness_stamp(), 'patterns': PATTERNS,
              'processes_per_pattern': PROCESSES, 'gated_cases': GATED_CASES, 'cases': {}}
    runs = {name: [] for name in CASES}
    for process in range(PROCESSES):
        for pattern in PATTERNS:
            for name, (project, frame, cvars) in CASES.items():
                print(f'  ... {name} {pattern} #{process}', flush=True)
                runs[name].append(capture_run(args.out, name, project, frame, cvars, pattern, process))
    for name, entries in runs.items():
        images = [load_png(args.out, entry) for entry in entries]
        result = compare_case(entries, images)
        report['cases'][name] = result
        print(f"{result['verdict']} {name}: {result['reason']}", flush=True)
    report['gate_passed'] = all(report['cases'][name]['verdict'] == 'PASS' for name in GATED_CASES)
    path = os.path.join(args.out, 'poison_determinism.json')
    with open(path, 'w', encoding='utf-8') as report_file:
        json.dump(report, report_file, indent=2)
    print(f'wrote {path}')
    return 0 if report['gate_passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
