"""Behavioural harness for the offline display replay (followup-11).

The replay is only allowed to stand in for the engine where it has been shown to match, so this
checks both directions: it must reproduce the engine's PNG under the conditions it models, and it
must **fail** to reproduce it when a parameter is wrong or when a stage it does not model (bloom) is
switched on.

Rewritten 2026-10-08. Two checks asserted that separate engine processes give bit-identical linear
records (`max |delta| == 0.0`), which followup-14 showed this renderer does not guarantee
(`docs/analysis/flicker-hypothesis-ledger.md`, "The renderer is not bit-reproducible across
processes"). The three configurations are now each captured in four processes, interleaved, and the
two checks are judged by `run_display_study.indistinguishable()`: max(between) <= max(within) on
max |delta|, the rule `scripts/test_correction_gates.py` adopted on 2026-10-08, plus a mean |delta|
that must not read SEPARATED. On same-path captures that rule fails by chance in about 1 split in 35
at four processes (see its docstring), so a failure is read as a change only if it repeats. The
window also moved from SCENE_START + 60 to SCENE_START + 200: early in a run Sponza's colour state
follows the loading timing (block B found it at frame 60; `docs/analysis/lighting-block-b-2026-10.md`,
"Conventions and traps"), and after a 120-frame warm-up the process-to-process floor is still ~200x
larger (`scripts/test_correction_gates.py`).

Usage: python scripts/test_display_replay.py [--keep]
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import display_model as dm  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402
from run_display_study import DEFAULT_REPEATS, indistinguishable  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 200
LAST = FIRST + 3
REPEATS = DEFAULT_REPEATS
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 128, 192)
SATURATION, CONTRAST, OPERATOR, EXPOSURE = 1.0, 0.65, 1, 1.0

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def capture(work, label, cvars):
    print(f'  … {label}', flush=True)
    res = runner.run_capture(os.path.join(work, label), PROJECT, LAST - SCENE_START + 8,
                             regions=[ROI], region_fields='taa',
                             region_first=FIRST, region_last=LAST,
                             cvars={'r.AutoExposure': 0, 'r.Exposure': EXPOSURE,
                                    'r.TAA.JitterSequenceLength': 8, **cvars},
                             screenshot_frames=f'{FIRST}-{LAST}', timing_first=FIRST)
    if res['exit_code'] != 0:
        check(False, f'{label}: engine exited {res["exit_code"]}', res['log'])
    return res


def capture_interleaved(work, plan):
    """Every configuration of `plan` in REPEATS processes, round-robin (repeat 0 of each, then
    repeat 1 of each, ...), so a drift over the session cannot line up with one configuration.
    Returns {label: [res, ...]}; element 0 is the process the replay checks read."""
    got = {label: [] for label, _ in plan}
    for r in range(REPEATS):
        for label, cvars in plan:
            got[label].append(capture(work, f'{label}_{r}', cvars))
    return got


def linear(res):
    """A run's linear records over the whole captured window, (frames, h, w, 3)."""
    seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
    return np.stack([f.rect(ROI[0]).channel('taa_output')[..., :3] for f in seq]).astype(np.float64)


def png_roi(res, frame):
    img = runner.png_array(os.path.join(res['frames_dir'], f'f_{frame}.png'))
    return img[ROI[2]:ROI[2] + ROI[4], ROI[1]:ROI[1] + ROI[3]].astype(np.int32)


def max_delta(hdr, res, frame, **kw):
    got = dm.to_code_values(dm.replay(hdr, exposure=kw.pop('exposure', EXPOSURE),
                                      saturation=SATURATION, contrast=CONTRAST,
                                      operator=kw.pop('operator', OPERATOR)))
    return int(np.abs(got - png_roi(res, frame)).max())


def main():
    runner.require_exe()
    keep = '--keep' in sys.argv
    work = tempfile.mkdtemp(prefix='uz_display_replay_')
    print(f'work dir: {work}')
    try:
        runs = capture_interleaved(work, [('bloom_off', {'r.Bloom': 0}),
                                          ('bloom_on', {}),
                                          ('exposure_2x', {'r.Bloom': 0, 'r.Exposure': EXPOSURE * 2})])
        nob_runs, withb_runs, bright_runs = runs['bloom_off'], runs['bloom_on'], runs['exposure_2x']
        nob = nob_runs[0]
        frames = regiondump.load_sequence(nob['region_dir'], FIRST, LAST)
        hdr = frames[0].rect(ROI[0]).channel('taa_output')[..., :3].astype(np.float64)
        f0 = frames[0].frame

        d = max_delta(hdr, nob, f0)
        check(d <= 1, 'the replay reproduces the engine PNG with bloom off',
              f'max code-value difference {d}')
        d_exp = max_delta(hdr, nob, f0, exposure=EXPOSURE * 2)
        check(d_exp > 10, 'negative control: the wrong exposure does not reproduce it',
              f'max code-value difference {d_exp}')
        d_op = max_delta(hdr, nob, f0, operator=3)
        check(d_op > 10, 'negative control: the wrong tonemap operator does not reproduce it',
              f'max code-value difference {d_op} (Reinhard instead of the ACES Hill fit)')

        nob_linear = [linear(r) for r in nob_runs]
        withb = withb_runs[0]
        fb = regiondump.load_sequence(withb['region_dir'], FIRST, LAST)
        hdr_b = fb[0].rect(ROI[0]).channel('taa_output')[..., :3].astype(np.float64)
        eq = indistinguishable(nob_linear, [linear(r) for r in withb_runs])
        check(eq['ok'],
              'bloom leaves the recorded linear radiance indistinguishable from process-to-process '
              'variation', eq['detail'])
        d_bloom = max_delta(hdr_b, withb, fb[0].frame)
        check(d_bloom > 5,
              'negative control: with bloom on the replay (which does not model it) fails',
              f'max code-value difference {d_bloom} — bloom is spatial and is not part of the replay')

        bright = bright_runs[0]
        fbr = regiondump.load_sequence(bright['region_dir'], FIRST, LAST)
        hdr_e = fbr[0].rect(ROI[0]).channel('taa_output')[..., :3].astype(np.float64)
        eq = indistinguishable(nob_linear, [linear(r) for r in bright_runs])
        check(eq['ok'],
              'a 2x exposure leaves the recorded linear radiance indistinguishable from '
              'process-to-process variation', eq['detail'])
        disp_delta = int(np.abs(png_roi(bright, fbr[0].frame) - png_roi(nob, f0)).max())
        check(disp_delta > 10, 'negative control: the same radiance does change on screen',
              f'max code-value difference {disp_delta} between the two exposures')
        d_e = max_delta(hdr_e, bright, fbr[0].frame, exposure=EXPOSURE * 2)
        check(d_e <= 1, 'the replay reproduces the engine at the second exposure too',
              f'max code-value difference {d_e}')
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
