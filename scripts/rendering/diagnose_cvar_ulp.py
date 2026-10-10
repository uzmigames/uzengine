"""Why do two mathematically identical TAA configurations differ by one ULP? (followup-14)

`scripts/rendering/test_correction_gates.py` measures a process-to-process reproducibility floor of exactly
zero, and then finds a 2.7e-07 difference between two pairs that should be identical:

  * omitting `r.TAA.ReconstructionHdrWeightExponent` versus setting it to its default 1.0;
  * exponent 0 versus `r.TAA.ReconstructionMode=1`, which is the same weight by another branch.

Either the harness is wrong or the engine is doing something the harness did not model. This
script decides which, by capturing each configuration twice and comparing every pair: if a
configuration reproduces itself exactly but differs from its twin, the difference is real and
belongs to the shader, not to the measurement.

Usage: python scripts/rendering/diagnose_cvar_ulp.py [--keep]
"""
from __future__ import annotations

import os
import shutil
import struct
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

CVAR = 'r.TAA.ReconstructionHdrWeightExponent'
SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 8
REPEATS = 4
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 128, 256)
BASE = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}

CONFIGS = {
    'default': {},
    'explicit_1': {CVAR: 1.0},
    'exponent_0': {CVAR: 0.0},
    'recon_mode_1': {'r.TAA.ReconstructionMode': 1},
}


def ulps(a, b):
    """Difference in units in the last place, for float32 values of the same sign."""
    ia = np.frombuffer(np.asarray(a, dtype=np.float32).tobytes(), dtype=np.int32)
    ib = np.frombuffer(np.asarray(b, dtype=np.float32).tobytes(), dtype=np.int32)
    return np.abs(ia.astype(np.int64) - ib.astype(np.int64))


def main():
    runner.require_exe()
    keep = '--keep' in sys.argv
    work = tempfile.mkdtemp(prefix='uz_ulp_')
    print(f'work dir: {work}')
    data = {}
    try:
        for name, cvars in CONFIGS.items():
            for rep in range(REPEATS):
                label = f'{name}_{rep}'
                res = runner.run_capture(os.path.join(work, label), PROJECT,
                                         LAST - SCENE_START + 8, regions=[ROI],
                                         region_fields='taa', region_first=FIRST, region_last=LAST,
                                         cvars={**BASE, **cvars}, timing_first=FIRST)
                if res['exit_code'] != 0:
                    raise SystemExit(f'{label}: engine exited {res["exit_code"]}')
                seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
                data[label] = {
                    'out': np.stack([f.rect(ROI[0]).channel('taa_output')[..., :3] for f in seq]),
                    'filtered': np.stack([f.rect(ROI[0]).channel('taa_filtered')[..., :3]
                                          for f in seq]),
                    'raw': np.stack([f.rect(ROI[0]).channel('taa_raw')[..., :3] for f in seq]),
                }
                print(f'  captured {label}', flush=True)

        print('')
        print(f'self-reproducibility ({REPEATS} processes, same configuration):')
        self_floor = {}
        for name in CONFIGS:
            runs = [data[f'{name}_{r}']['out'] for r in range(REPEATS)]
            d = max(float(np.abs(runs[0] - runs[r]).max()) for r in range(1, REPEATS))
            n = max(int((np.abs(runs[0] - runs[r]) > 0).sum()) for r in range(1, REPEATS))
            self_floor[name] = d
            print(f'  {name:14s} max |delta| {d:.3e} over {REPEATS} runs, '
                  f'{n} of {runs[0].size} values differ')

        print('\npairs that should be mathematically identical:')
        for a, b in (('default', 'explicit_1'), ('exponent_0', 'recon_mode_1')):
            for stage in ('raw', 'filtered', 'out'):
                x, y = data[f'{a}_0'][stage], data[f'{b}_0'][stage]
                d = np.abs(x - y)
                n = int((d > 0).sum())
                if n:
                    u = ulps(x[d > 0], y[d > 0])
                    print(f'  {a} vs {b}, {stage:8s}: {n} of {d.size} values differ, '
                          f'max |delta| {d.max():.3e}, max {u.max()} ULP, '
                          f'first differing frame {int(np.argmax(d.reshape(d.shape[0], -1).max(axis=1) > 0))}')
                else:
                    print(f'  {a} vs {b}, {stage:8s}: identical')
    finally:
        if keep:
            print('kept:', work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
