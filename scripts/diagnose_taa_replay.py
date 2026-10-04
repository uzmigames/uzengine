"""Which step of an offline TAA replay stops matching the GPU? (ledger H-25)

`scripts/run_clamp_bounds_study.py` replays the accumulation from recorded inputs so the clamp
bounds can be swapped for a counterfactual. Its validity check failed: replaying with the *recorded*
box reproduced the GPU output only to 4.6e-3 relative, so nothing built on that replay can be
believed. This finds the step that breaks, rather than widening the tolerance until it passes.

Three checks, from the inside out:

  blend      predict output[i] from the recorded clamped[i], filtered[i], blend weight and exposure.
             This is the step `scripts/validate_taa_model.py` already validates to 2e-7, so it is
             the control: if it fails here, the capture or the reader is at fault.
  clamp      predict clamped[i] from the recorded history[i] and the recorded box.
  history    is the history entering frame i simply the output of frame i-1 at the same pixel?
             A replay that accumulates its own history assumes so. With a static camera the
             reprojection should be the identity, but the history is still a *sampled* texture
             fetch, and any resampling breaks the assumption.

Usage: python scripts/diagnose_taa_replay.py [--keep]
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402
import taa_model as tm  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 16
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 128, 256)
BASE = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}


def rel(a, b):
    return float(np.abs(a - b).max() / max(1e-9, float(np.abs(b).max())))


def main():
    runner.require_exe()
    keep = '--keep' in sys.argv
    work = tempfile.mkdtemp(prefix='uz_taa_replay_')
    try:
        res = runner.run_capture(os.path.join(work, 'cap'), PROJECT, LAST - SCENE_START + 8,
                                 regions=[ROI], region_fields='taa+extra', region_first=FIRST,
                                 region_last=LAST, cvars=BASE, timing_first=FIRST)
        if res['exit_code'] != 0:
            raise SystemExit(f'engine exited {res["exit_code"]}')
        seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
        g = lambda ch, k=3: np.stack([f.rect(ROI[0]).channel(ch)[..., :k] for f in seq]).astype(np.float64)  # noqa: E731
        filtered, history = g('taa_filtered'), g('taa_history')
        clamped, output = g('taa_clamped'), g('taa_output')
        clip_min, clip_max = g('taa_clip_min'), g('taa_clip_max')
        blend = np.stack([f.rect(ROI[0]).channel('taa_raw')[..., 3] for f in seq]).astype(np.float64)
        exposure = np.stack([f.rect(ROI[0]).channel('taa_output')[..., 3] for f in seq]).astype(np.float64)

        # 1. blend — the step already validated against the GPU
        cy = tm.rgb_to_ycocg(clamped)[..., 0]
        fy = tm.rgb_to_ycocg(filtered)[..., 0]
        a = (1.0 - blend) / (cy * exposure + 4.0)
        b = blend / (fy * exposure + 4.0)
        inv = 1.0 / (a + b)
        pred_out = clamped * (a * inv)[..., None] + filtered * (b * inv)[..., None]
        print(f'  blend   : max relative error {rel(pred_out, output):.2e}')

        # 2. clamp — history clipped to the recorded box
        h = tm.rgb_to_ycocg(history)
        pred_clamped = tm.ycocg_to_rgb(np.clip(h, tm.rgb_to_ycocg(clip_min),
                                               tm.rgb_to_ycocg(clip_max)))
        print(f'  clamp   : max relative error {rel(pred_clamped, clamped):.2e}')

        # 3. history — is it the previous frame's output at the same pixel?
        d = np.abs(history[1:] - output[:-1])
        scale = max(1e-9, float(np.abs(output).max()))
        frac = float((d.max(axis=-1) > 1e-4 * scale).mean())
        print(f'  history : max |history[i] - output[i-1]| = {d.max():.3e} '
              f'({d.max() / scale * 100:.3f} % of peak), differing on {frac * 100:.2f} % of pixels')
        if frac > 0.01:
            print('            -> the history is NOT the previous output at the same pixel, so a '
                  'replay that accumulates its own history diverges. It must be driven from the '
                  'recorded history, or the counterfactual must be applied one step at a time.')
        else:
            print('            -> the history is the previous output, so self-accumulation is safe.')

        # 4. how far does a self-accumulating replay drift, step by step?
        hist = history[0].copy()
        drift = []
        for i in range(len(seq)):
            hh = tm.rgb_to_ycocg(hist)
            cl = tm.ycocg_to_rgb(np.clip(hh, tm.rgb_to_ycocg(clip_min[i]),
                                         tm.rgb_to_ycocg(clip_max[i])))
            ccy = tm.rgb_to_ycocg(cl)[..., 0]
            aa = (1.0 - blend[i]) / (ccy * exposure[i] + 4.0)
            bb = blend[i] / (fy[i] * exposure[i] + 4.0)
            ii = 1.0 / (aa + bb)
            o = cl * (aa * ii)[..., None] + filtered[i] * (bb * ii)[..., None]
            drift.append(rel(o, output[i]))
            hist = o
        print('  drift   : ' + ', '.join(f'{v:.1e}' for v in drift[:8]) + ' ...')
        print(f'            first frame {drift[0]:.2e}, last {drift[-1]:.2e}')
    finally:
        if keep:
            print('kept:', work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
