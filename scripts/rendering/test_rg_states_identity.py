"""Before/after identity harness for the render-graph state registry, stage 0
(phase0_render-graph-state-registry). Registered 2026-10-09, before the build that adds the registry.

Stage 0 only observes: tracked imports are not used yet, explicit import claims keep their barriers, and the
registry records states and logs the claims that disagree with them. The rendered frames must therefore not
change. This harness captures a set of cases with one binary (`--capture DIR`), then compares two capture
sets (`--compare BEFORE AFTER`) and lists the observe-mode lines of the AFTER logs (`--observe AFTER`).

Cases: the scenes where the 2026-10-08 audit located graph-visible state mismatches (froxel fog, local
shadows, decals / DBuffer, the HDRI path for the AP volume, the path tracer's accumulation pair, the
reflection reference as the plain path) and Sponza at fixed exposure. Each case runs PROCESSES processes and
screenshots one frame; the pre-load length of every process is recorded, because processes whose pre-load
took a different number of frames land in different persistent TAA states (followup-14 close-out,
2026-10-09: 43 vs 44 frames gives up to 80 code values on Sponza; between equal pre-loads the dump on/off
pair was bit-identical).

Rule (pre-registered), per case:
  * pair every AFTER process with a BEFORE process of the same pre-load length (cases where no length
    matches are reported as UNMATCHED and fail);
  * a matched pair must be bit-identical when the BEFORE processes of that length are bit-identical among
    themselves (or there is one); otherwise its max |delta| must not exceed the largest BEFORE spread
    between processes of that length.

Usage:
  python scripts/rendering/test_rg_states_identity.py --capture DIR
  python scripts/rendering/test_rg_states_identity.py --compare BEFORE_DIR AFTER_DIR
  python scripts/rendering/test_rg_states_identity.py --observe AFTER_DIR
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
import runner  # noqa: E402

PROCESSES = 2
FIXED = {'r.AutoExposure': 0, 'r.Exposure': 1.0}
S = os.path.join(runner.REPO, 'scripts', 'rendering')
P = os.path.join(runner.REPO, 'sample', 'projects')
#: name -> (project, frame, cvars)
CASES = {
    'reflection': (os.path.join(S, 'reflection-reference.uzproject'), 120, FIXED),
    'fog_shaft': (os.path.join(S, 'fog-shaft-reference.uzproject'), 120, FIXED),
    'local_shadow': (os.path.join(S, 'local-shadow-reference.uzproject'), 120, FIXED),
    'decal': (os.path.join(S, 'decal-reference.uzproject'), 120, FIXED),
    'hdri': (os.path.join(S, 'reflection-reference-hdri.uzproject'), 120, FIXED),
    'path_tracer': (os.path.join(S, 'reflection-reference.uzproject'), 120, {**FIXED, 'r.PathTracer.Enable': 1}),
    'sponza': (os.path.join(S, 'sponza-flicker-w.uzproject'), 237, {**FIXED, 'r.TAA.JitterSequenceLength': 8}),
}
PRELOAD = re.compile(r'Pre-load complete: assets ready and exposure seeded after (\d+) frames')
OBSERVE = re.compile(r'RenderGraph states \(observe\): (.*)$')


def capture(out):
    runner.require_exe()
    os.makedirs(out, exist_ok=True)
    meta = {'harness': runner.harness_stamp(), 'cases': {}}
    for r in range(PROCESSES):          # interleaved: process r of every case, then the next
        for name, (project, frame, cvars) in CASES.items():
            work = os.path.join(out, f'{name}_{r}')
            print(f'  ... {name} #{r}', flush=True)
            res = runner.run_capture(work, project, frame + 2, cvars=cvars, screenshot_frames=str(frame))
            log = open(res['log'], encoding='utf-8', errors='replace').read()
            m = PRELOAD.search(log)
            meta['cases'].setdefault(name, []).append({
                'dir': os.path.basename(work), 'exit_code': res['exit_code'], 'frame': frame,
                'preload_frames': int(m.group(1)) if m else None,
                'exe_hash': (res.get('manifest') or {}).get('executable', {}).get('hash')})
    with open(os.path.join(out, 'capture.json'), 'w', encoding='utf-8') as f:
        json.dump(meta, f, indent=1)
    print(f'wrote {out}')


def load_png(root, entry):
    path = os.path.join(root, entry['dir'], 'frames', f"f_{entry['frame']}.png")
    return runner.png_array(path).astype(np.int16) if os.path.exists(path) else None


def compare(before, after):
    mb = json.load(open(os.path.join(before, 'capture.json'), encoding='utf-8'))
    ma = json.load(open(os.path.join(after, 'capture.json'), encoding='utf-8'))
    failures = 0
    for name in CASES:
        bs, as_ = mb['cases'].get(name, []), ma['cases'].get(name, [])
        bimg = [(e, load_png(before, e)) for e in bs]
        aimg = [(e, load_png(after, e)) for e in as_]
        lines = []
        ok = True
        for ea, ia in aimg:
            if ia is None:
                ok = False
                lines.append(f"{ea['dir']}: no screenshot (exit {ea['exit_code']})")
                continue
            same = [(eb, ib) for eb, ib in bimg if ib is not None and eb['preload_frames'] == ea['preload_frames']]
            if not same:
                ok = False
                lines.append(f"{ea['dir']}: UNMATCHED pre-load {ea['preload_frames']} "
                             f"(before: {[e['preload_frames'] for e, _ in bimg]})")
                continue
            spread = max([int(np.abs(x - y).max()) for (_, x), (_, y) in itertools.combinations(same, 2)] or [0])
            best = min(int(np.abs(ia - ib).max()) for _, ib in same)
            mean = min(float(np.abs(ia - ib).mean()) for _, ib in same)
            passed = best == 0 if spread == 0 else best <= spread
            ok = ok and passed
            lines.append(f"{ea['dir']}: pre-load {ea['preload_frames']}, max |delta| vs before {best} "
                         f"(mean {mean:.4f}); before spread at that length {spread} -> {'ok' if passed else 'FAIL'}")
        failures += 0 if ok else 1
        print(f"{'PASS' if ok else 'FAIL'} {name}")
        for line in lines:
            print('   ', line)
    print(f'{len(CASES) - failures} of {len(CASES)} cases unchanged')
    return 1 if failures else 0


def observe(after):
    ma = json.load(open(os.path.join(after, 'capture.json'), encoding='utf-8'))
    for name, entries in ma['cases'].items():
        seen = []
        for e in entries:
            log = os.path.join(after, e['dir'], 'run.log')
            for line in open(log, encoding='utf-8', errors='replace'):
                m = OBSERVE.search(line.rstrip())
                if m and m.group(1) not in seen:
                    seen.append(m.group(1))
        print(f'{name}: {len(seen)} distinct observe lines')
        for s in seen:
            print('   ', s)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--capture')
    g.add_argument('--compare', nargs=2)
    g.add_argument('--observe')
    a = ap.parse_args()
    if a.capture:
        capture(a.capture)
        return 0
    if a.compare:
        return compare(*a.compare)
    return observe(a.observe)


if __name__ == '__main__':
    sys.exit(main())
