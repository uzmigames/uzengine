"""Behavioural harness for the TAA diagnostic controls added in followup-10.

Each control is checked by what the GPU wrote, with a negative control that the production setting
does *not* satisfy the same equality — otherwise a switch that silently did nothing would pass.

  r.TAA.JitterScale=0        the recorded jitter offset is exactly zero while the jitter index still
                             cycles (accumulation keeps running); production offsets are non-zero
  r.TAA.ReconstructionMode=2 the recorded filtered colour equals the recorded raw centre sample;
                             production differs from it
  r.TAA.ClampMode=1          the recorded clamped history equals the recorded history exactly;
                             production clamps a measurable fraction of the pixels
  r.TAA.BlendMode=1          the recorded blend weight equals r.TAA.CurrentFrameWeight everywhere,
                             with that CVar set to a NON-default value so a dead CVar fails the
                             check (ledger H-39); production raises the weight above its base value
                             on part of the frame

Usage: python scripts/rendering/test_taa_modes.py [--keep]
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
import taa_model  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 60
FRAMES = 10
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 96, 128)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
CURRENT_FRAME_WEIGHT = 0.04
#: r.TAA.CurrentFrameWeight for the BlendMode=1 capture; it must differ from the default. Until
#: 2026-10-08 that capture never set the CVar and compared the weight against the default 0.04, so
#: the check passed in followup-10 (2026-09-09) while the CVar was dead: the constant buffer took a
#: hardcoded 0.04 until the H-39 fix (followup-12, landed in 7eea6b95). Any value in the CVar's
#: declared range 0.01-0.2 other than 0.04 works.
BLEND_PROBE_WEIGHT = 0.1
assert BLEND_PROBE_WEIGHT != CURRENT_FRAME_WEIGHT

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
        # Until 2026-10-08 this condition ended in `or True`, so it could never fail and the
        # "8 distinct production offsets" it printed was never asserted.
        check(len(pofs) >= 8 and (0.0, 0.0) not in pofs,
              'negative control: production cycles through 8 distinct offsets, none of them (0, 0)',
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
        fixed = capture(work, 'blend_1', {'r.TAA.BlendMode': 1,
                                          'r.TAA.CurrentFrameWeight': BLEND_PROBE_WEIGHT})
        f_blend = np.stack([f.rect(ROI[0]).channel('taa_raw')[..., 3] for f in fixed])
        dev = float(np.abs(f_blend - BLEND_PROBE_WEIGHT).max())
        check(dev <= 1e-6,
              f'BlendMode=1 uses the r.TAA.CurrentFrameWeight it is given ({BLEND_PROBE_WEIGHT}, '
              f'not the {CURRENT_FRAME_WEIGHT} default)',
              f'max deviation {dev:.2e}')
        above = float((prod_blend > CURRENT_FRAME_WEIGHT + 1e-4).mean())
        check(above > 0.001, 'negative control: production raises the blend above the base weight',
              f'{above * 100:.2f} % of samples above {CURRENT_FRAME_WEIGHT}')

        print('records added for this task')
        rect = prod[0].rect(ROI[0])
        # The resolve clamps in (4x-scaled) YCoCg and the records hold RGB, so the box is checked in
        # YCoCg, on both bounds, for pixels whose history was not reset as off-screen. Until
        # 2026-10-08 this compared RGB with the lower corner only against a 99 % bar and never
        # compared with taa_clip_max; on saved production dumps of pose A's rear door
        # (42-taa-reconstruction) it failed 24 of 64 genuine frames, where this form passes all 64
        # (on pose W's window both forms pass).
        valid = rect.history_valid_mask()
        y_cl = taa_model.rgb_to_ycocg(rect.channel('taa_clamped')[..., :3].astype(np.float64))
        y_lo = taa_model.rgb_to_ycocg(rect.channel('taa_clip_min')[..., :3].astype(np.float64))
        y_hi = taa_model.rgb_to_ycocg(rect.channel('taa_clip_max')[..., :3].astype(np.float64))
        tol = 1e-4 * np.maximum(1.0, np.abs(y_cl))
        inside = np.all((y_cl >= y_lo - tol) & (y_cl <= y_hi + tol), axis=-1)[valid]
        check(bool(valid.any()) and bool(inside.all()),
              'the clamped history lies inside the recorded clip box (YCoCg, both bounds)',
              f'{inside.mean() * 100:.3f} % of {int(valid.sum())} pixels')
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
