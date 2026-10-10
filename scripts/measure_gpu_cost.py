"""G5 as it was actually written: the *GPU* cost of the candidate (followup-14).

G5 says "GPU frame time". Only CPU wall time was ever recorded, and the report presented the CPU
number against the GPU threshold — a substitution the review caught. The render graph timestamps
every pass, but per-pass GPU profiling is off by default (`m_gpuProfileEnabled`, no CVar), so the
numbers existed and were never collected. The engine now enables it whenever `--run-manifest` is
given, and this script reads it back.

Reported per variant: the TAA resolve pass on its own (the only pass the candidate can affect) and
the sum over all profiled passes, each averaged over the measured window and repeated so the spread
is visible.

Usage: python scripts/measure_gpu_cost.py [--repeats 3] [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

CVAR = 'r.TAA.ReconstructionHdrWeightExponent'
SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 256
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
BASE = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}
VARIANTS = [('baseline', 1.0), ('candidate_0.5', 0.5), ('candidate_0.0', 0.0)]


def taa_pass_ms(gpu):
    """GPU ms of the TAA resolve, whatever the render graph called it."""
    for key, value in gpu.items():
        if key.startswith('_'):
            continue
        low = key.lower()
        if 'taa' in low and ('resolve' in low or 'upscal' in low or low == 'taa'):
            return key, value
    for key, value in gpu.items():
        if not key.startswith('_') and 'taa' in key.lower():
            return key, value
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '14-validate-flicker-correction'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_gpu_cost')
    payload = {'frames': FRAMES, 'first_frame': FIRST, 'repeats': args.repeats, 'variants': {}}

    for vname, exponent in VARIANTS:
        runs = []
        for rep in range(args.repeats):
            print(f'  ... {vname} repeat {rep + 1}/{args.repeats}', flush=True)
            res = runner.run_capture(os.path.join(work, f'{vname}_{rep}'.replace('.', 'p')),
                                     PROJECT, LAST - SCENE_START + 8,
                                     cvars={**BASE, CVAR: f'{exponent:g}'}, timing_first=FIRST)
            if res['exit_code'] != 0:
                raise RuntimeError(f'{vname}: engine exited {res["exit_code"]}')
            m = res['manifest'] or {}
            gpu = m.get('gpu_ms')
            if not gpu:
                raise RuntimeError(
                    'the manifest carries no gpu_ms — per-pass GPU profiling did not run, so G5 '
                    'cannot be measured on GPU time. Rebuild: the tool enables it only when '
                    '--run-manifest is given.')
            name, ms = taa_pass_ms(gpu)
            runs.append({'gpu_total_ms': gpu.get('_total'), 'taa_pass': name, 'taa_pass_ms': ms,
                         'cpu_frame_ms': m.get('frame_ms', {}).get('mean'),
                         'passes': len([k for k in gpu if not k.startswith('_')])})
        payload['variants'][vname] = {'exponent': exponent, 'runs': runs}
        tot = [r['gpu_total_ms'] for r in runs]
        taa = [r['taa_pass_ms'] for r in runs if r['taa_pass_ms'] is not None]
        print(f"      GPU total {min(tot):.3f}-{max(tot):.3f} ms over {len(runs)} runs"
              + (f", TAA pass '{runs[0]['taa_pass']}' {min(taa):.4f}-{max(taa):.4f} ms" if taa
                 else ', TAA pass not found among the profiled scopes'), flush=True)

    b = payload['variants']['baseline']['runs']
    for vname in ('candidate_0.5', 'candidate_0.0'):
        c = payload['variants'][vname]['runs']
        bt = sum(r['gpu_total_ms'] for r in b) / len(b)
        ct = sum(r['gpu_total_ms'] for r in c) / len(c)
        payload['variants'][vname]['gpu_total_change_pct'] = (ct / bt - 1) * 100 if bt else None
        bp = [r['taa_pass_ms'] for r in b if r['taa_pass_ms'] is not None]
        cp = [r['taa_pass_ms'] for r in c if r['taa_pass_ms'] is not None]
        if bp and cp:
            payload['variants'][vname]['taa_pass_change_pct'] = (
                (sum(cp) / len(cp)) / (sum(bp) / len(bp)) - 1) * 100
        print(f"  {vname}: GPU total {payload['variants'][vname]['gpu_total_change_pct']:+.2f} %"
              + (f", TAA pass {payload['variants'][vname]['taa_pass_change_pct']:+.2f} %"
                 if 'taa_pass_change_pct' in payload['variants'][vname] else ''))

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'gpu-cost.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
