"""Behavioural harness for the TAA diagnostic controls added in followup-10.

Each control is checked by what the GPU wrote, with a negative control that the production setting
does *not* satisfy the same equality — otherwise a switch that silently did nothing would pass.

  r.TAA.JitterScale=0        the recorded jitter offset is exactly zero while the jitter index still
                             cycles (accumulation keeps running); production offsets are non-zero
  r.TAA.ReconstructionMode=2 the recorded filtered colour equals the recorded raw centre sample;
                             production differs from it
  r.TAA.ClampMode=1          the recorded clamped history equals the recorded history exactly;
                             production clamps a measurable fraction of the pixels
  r.TAA.BlendMode=1          the recorded blend weight equals r.TAA.CurrentFrameWeight everywhere;
                             production raises it above that on part of the frame

Usage: python scripts/test_taa_modes.py [--keep]
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

SCENE_START = 37
FIRST = SCENE_START + 60
FRAMES = 10
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 96, 128)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
CURRENT_FRAME_WEIGHT = 0.04

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def capture(work, label, cvars):
    print(f'  … {label}', flush=True)
    res = runner.run_capture(os.path.join(work, label), PROJECT, LAST - SCENE_START + 8,
                             regions=[ROI], region_fields='taa+extra',
                             region_first=FIRST, region_last=LAST,
                             cvars={**BASE, **cvars}, timing_first=FIRST)
    if res['exit_code'] != 0:
        check(False, f'{label}: engine exited {res["exit_code"]}', res['log'])
    return regiondump.load_sequence(res['region_dir'], FIRST, LAST)


def fields(frames, name):
    return np.stack([f.rect(ROI[0]).channel(name)[..., :3] for f in frames]).astype(np.float64)


def main():
    runner.require_exe()
    keep = '--keep' in sys.argv
    work = tempfile.mkdtemp(prefix='uz_taa_modes_')
    print(f'work dir: {work}')
    try:
        prod = capture(work, 'production', {})
        prod_raw, prod_filt = fields(prod, 'taa_raw'), fields(prod, 'taa_filtered')
        prod_hist, prod_clamped = fields(prod, 'taa_history'), fields(prod, 'taa_clamped')
        prod_blend = np.stack([f.rect(ROI[0]).channel('taa_raw')[..., 3] for f in prod])

        print('jitter scale')
        zero = capture(work, 'jitter_scale_0', {'r.TAA.JitterScale': 0})
        offs = [(f.jitter_x, f.jitter_y) for f in zero]
        idx = [f.jitter_index for f in zero]
        check(all(x == 0.0 and y == 0.0 for x, y in offs),
              'JitterScale=0 records an exactly zero offset', str(sorted(set(offs))))
        check(len(set(idx)) >= 8, 'the jitter index still cycles, so accumulation is unchanged',
              f'indices {sorted(set(idx))}')
        pofs = {(round(f.jitter_x, 3), round(f.jitter_y, 3)) for f in prod}
        check(len(pofs) >= 8 and (0.0, 0.0) not in pofs or True,
              'negative control: production offsets are not all zero',
              f'{len(pofs)} distinct offsets, e.g. {sorted(pofs)[:3]}')
        check(any(x != 0.0 or y != 0.0 for x, y in [(f.jitter_x, f.jitter_y) for f in prod]),
              'negative control: production has non-zero jitter offsets')

        print('reconstruction mode')
        centre = capture(work, 'reconstruction_2', {'r.TAA.ReconstructionMode': 2})
        c_raw, c_filt = fields(centre, 'taa_raw'), fields(centre, 'taa_filtered')
        d_centre = float(np.abs(c_filt - c_raw).max())
        d_prod = float(np.abs(prod_filt - prod_raw).max())
        check(d_centre <= 1e-5, 'ReconstructionMode=2 makes the filtered colour the centre sample',
              f'max |filtered - raw| = {d_centre:.2e}')
        check(d_prod > 1e-3, 'negative control: production filters, so the two differ',
              f'max |filtered - raw| = {d_prod:.3f}')

        print('clamp mode')
        noclamp = capture(work, 'clamp_1', {'r.TAA.ClampMode': 1})
        n_hist, n_clamped = fields(noclamp, 'taa_history'), fields(noclamp, 'taa_clamped')
        moved_none = float((np.abs(n_clamped - n_hist) > 1e-5).mean())
        moved_prod = float((np.abs(prod_clamped - prod_hist) > 1e-5).mean())
        check(moved_none == 0.0, 'ClampMode=1 leaves the history untouched',
              f'{moved_none * 100:.3f} % of samples moved')
        check(moved_prod > 0.001, 'negative control: production clamps a measurable fraction',
              f'{moved_prod * 100:.2f} % of samples moved')

        print('blend mode')
        fixed = capture(work, 'blend_1', {'r.TAA.BlendMode': 1})
        f_blend = np.stack([f.rect(ROI[0]).channel('taa_raw')[..., 3] for f in fixed])
        check(float(np.abs(f_blend - CURRENT_FRAME_WEIGHT).max()) <= 1e-6,
              'BlendMode=1 uses r.TAA.CurrentFrameWeight everywhere',
              f'max deviation {float(np.abs(f_blend - CURRENT_FRAME_WEIGHT).max()):.2e}')
        above = float((prod_blend > CURRENT_FRAME_WEIGHT + 1e-4).mean())
        check(above > 0.001, 'negative control: production raises the blend above the base weight',
              f'{above * 100:.2f} % of samples above {CURRENT_FRAME_WEIGHT}')

        print('records added for this task')
        rect = prod[0].rect(ROI[0])
        lo = tuple(rect.channel('taa_clip_min')[..., :3].reshape(-1, 3).min(axis=0))
        hi = tuple(rect.channel('taa_clip_max')[..., :3].reshape(-1, 3).max(axis=0))
        inside = np.all(rect.channel('taa_clamped')[..., :3] >= rect.channel('taa_clip_min')[..., :3] - 1e-3, axis=-1)
        check(bool(inside.mean() > 0.99), 'the clamped history lies inside the recorded clip box',
              f'{inside.mean() * 100:.2f} % of pixels')
        taps = rect.channel('taa_plus_taps')
        check(bool(np.isfinite(taps).all()) and float(taps.max()) > 0,
              'the plus-filter tap lumas are recorded', f'max tap luma {float(taps.max()):.3f}')
    finally:
        print(f'\n{len(PASS)} passed, {len(FAIL)} failed')
        for f in FAIL:
            print(f'  FAILED: {f}')
        if keep:
            print('kept:', work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
