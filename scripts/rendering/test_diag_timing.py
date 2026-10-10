"""Per-pass GPU timings in `--diag` (lighting block B, item 1; 2026-10-05).

`uzengine.exe --diag` printed `"timing": []` on every run: the GPU profiler was never enabled for a
diagnostic run and `PassInspector::SetTimingData` had no caller. The DX12 profiler also resolved
the timestamps of frame N-2 while that frame could still be executing (ring depth 3 = frames in
flight), labelled them with frame N's pass names (one shared name table) and the multi-scatter
passes registered dangling `std::string` names.

Scene `scripts/rendering/sky-rebake-reference.uzproject` (640 x 360): `sun_rotate_test.lua` turns the sun at
frame 100, which queues one sky IBL re-bake (`AtmosphereBake`, a timed scope that runs on exactly one
frame of the measured window) while every other pass runs every frame. Two runs:

  A  --diag --frames 200 --timing-first 60 --run-manifest   the measured window covers the re-bake
  B  --diag --frames 60  --timing-first 100000              control: a window after the last frame

Checks (pre-registered):
  T0 control B reports no timing and no gpu_ms (the statistics come from the window, not from a
     stale or global source)
  T1 run A reports >= 8 passes with unique names, every name a plain identifier (no garbage from a
     dangling pointer)
  T2 every statistic is finite and 0 <= min <= mean <= max <= 100 ms
  T3 per-frame passes (GBuffer, DeferredLighting, TAA, Tonemap) carry as many samples as the window
     has frames (the manifest's timing_samples)
  T4 SkyCubemapCapture (a graph pass registered only on the re-bake frame) carries 1..3 samples
     under its own name, while the AtmosphereBake scope (bracketed on every frame around the
     post-frame bake) is sampled on every window frame with a max above 1 ms on the bake frame:
     with the old shared name table a one-frame pass would have been labelled with a neighbouring
     frame's pass list
  T5 the GPU frame span `_frame` is >= every pass mean and >= 0.98 x the sum of the pass means
     (passes execute serially on one queue), and <= 3 x the sum + 2 ms
  T6 the manifest's gpu_ms and the report's timing list the same passes with equal means (1e-3 ms)

Usage: python scripts/rendering/test_diag_timing.py [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sky-rebake-reference.uzproject')
PER_FRAME = ('GBuffer', 'DeferredLighting', 'TAA', 'Tonemap')
# The graph pass that captures the sky cubemap is registered only on a bake frame; the
# `AtmosphereBake` scope around the post-frame bake is bracketed on EVERY frame (near 0 ms except
# on the bake frame, where it spans the bake plus its WaitIdle), so it is the every-frame control.
ONCE = 'SkyCubemapCapture'
EVERY_FRAME_SCOPE = 'AtmosphereBake'
NAME_RE = re.compile(r'^[A-Za-z0-9_]+$')


def run_diag(out_dir, frames, timing_first, manifest):
    os.makedirs(out_dir, exist_ok=True)
    cmd = [runner.EXE, '--project', PROJECT, '--frames', str(frames), '--diag',
           '--timing-first', str(timing_first)]
    if manifest:
        cmd += ['--run-manifest', os.path.join(out_dir, 'manifest.json')]
    with open(os.path.join(out_dir, 'stdout.txt'), 'w', encoding='utf-8') as fh:
        res = subprocess.run(cmd, cwd=runner.REPO, stdout=fh, stderr=subprocess.STDOUT, timeout=900)
    if res.returncode != 0:
        raise SystemExit(f'engine exited {res.returncode} ({out_dir})')


def load(out_dir, manifest):
    diag = None
    with open(os.path.join(out_dir, 'stdout.txt'), encoding='utf-8', errors='replace') as fh:
        for line in fh:
            if line.startswith('DIAG_JSON:'):
                diag = json.loads(line[len('DIAG_JSON:'):])
    if diag is None:
        raise SystemExit(f'no DIAG_JSON line in {out_dir}')
    man = None
    if manifest:
        with open(os.path.join(out_dir, 'manifest.json'), encoding='utf-8') as fh:
            man = json.load(fh)
    return diag, man


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', 'test-diag-timing'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    a_dir, b_dir = os.path.join(args.out, 'run-a'), os.path.join(args.out, 'run-b')
    if not args.reuse:
        runner.require_exe()
        run_diag(a_dir, 200, 60, manifest=True)
        run_diag(b_dir, 60, 100000, manifest=True)
    diag_a, man_a = load(a_dir, True)
    diag_b, man_b = load(b_dir, True)
    timing = {t['name']: t for t in diag_a.get('timing', [])}
    passes = {n: t for n, t in timing.items() if not n.startswith('_')}
    frame = timing.get('_frame')
    samples_expected = (man_a.get('frames') or {}).get('timing_samples')
    results = []

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}')

    for n, t in sorted(timing.items(), key=lambda kv: -kv[1]['gpuMs']):
        print(f'  {n:28} mean {t["gpuMs"]:8.3f}  min {t["minMs"]:8.3f}  max {t["maxMs"]:8.3f}  samples {t["samples"]}')
    print(f'  window frames (manifest timing_samples): {samples_expected}')

    check('T0 control window after the last frame reports nothing',
          not diag_b.get('timing') and not (man_b or {}).get('gpu_ms'),
          f'timing {len(diag_b.get("timing", []))} entries, gpu_ms {sorted((man_b or {}).get("gpu_ms", {}).keys())}')
    names_ok = all(NAME_RE.match(n) for n in timing)
    check('T1 >= 8 passes with plain unique names', len(passes) >= 8 and names_ok, f'{len(passes)} passes, names ok {names_ok}')
    finite = all(math.isfinite(t['gpuMs']) and math.isfinite(t['minMs']) and math.isfinite(t['maxMs']) for t in timing.values())
    ordered = all(0.0 <= t['minMs'] <= t['gpuMs'] + 1e-6 and t['gpuMs'] <= t['maxMs'] + 1e-6 and t['maxMs'] <= 100.0 for t in timing.values())
    check('T2 finite, ordered statistics within 0..100 ms', finite and ordered, f'finite {finite}, ordered {ordered}')
    per_frame = {n: timing[n]['samples'] if n in timing else None for n in PER_FRAME}
    check('T3 per-frame passes sampled on every window frame', samples_expected and all(v == samples_expected for v in per_frame.values()),
          f'{per_frame} vs {samples_expected}')
    once = timing.get(ONCE)
    every = timing.get(EVERY_FRAME_SCOPE)
    check('T4 the sky re-bake pass is sampled once, under its own name',
          once is not None and 1 <= once['samples'] <= 3
          and every is not None and every['samples'] == samples_expected and every['maxMs'] > 1.0,
          f'{ONCE}: {once}; {EVERY_FRAME_SCOPE}: {every}')
    pass_sum = sum(t['gpuMs'] for t in passes.values())
    pass_max = max((t['gpuMs'] for t in passes.values()), default=0.0)
    check('T5 frame span bounds the passes', frame is not None and frame['gpuMs'] >= pass_max - 1e-6
          and frame['gpuMs'] >= 0.98 * pass_sum and frame['gpuMs'] <= 3.0 * pass_sum + 2.0,
          f'_frame {frame and frame["gpuMs"]}, max pass {pass_max:.3f}, sum {pass_sum:.3f}')
    gpu_ms = {k: v for k, v in (man_a.get('gpu_ms') or {}).items() if not k.startswith('_')}
    agree = set(gpu_ms) == set(passes) and all(abs(gpu_ms[k] - passes[k]['gpuMs']) <= 1e-3 for k in gpu_ms)
    check('T6 manifest and report agree', agree, f'manifest {len(gpu_ms)} passes, report {len(passes)}, equal means {agree}')
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    with open(os.path.join(args.out, 'diag_timing.json'), 'w', encoding='utf-8') as fh:
        json.dump({'timing': timing, 'window_frames': samples_expected,
                   'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
