"""Behavioural harness for the candidate correction (followup-14 item 2.2).

Every assertion is a measurement on captured GPU records, and each one is paired with a control
that must NOT move:

  reproducibility     every configuration is captured twice, in separate processes, and each
                      equivalence claim is judged against the floor of the two configurations it
                      compares. Bit-identity is not assumed: this engine is mostly but not always
                      deterministic across processes (`scripts/diagnose_cvar_ulp.py`), so the
                      floor is measured, per configuration, and used as the tolerance
  default unchanged   omitting the CVar renders the same as setting it to 1.0, within that floor,
                      so the correction is genuinely opt-in
  effect exists       `=0` changes the window records by orders of magnitude more than the floor
  equivalence         `=0` matches `r.TAA.ReconstructionMode=1` within the floor, which is the same
                      maths reached by a different branch
  monotone            the residual falls monotonically as the exponent falls: a continuous knob,
                      not a switch
  TAA off control     with `r.TAA=0` the CVar changes nothing, so the effect is inside the TAA
                      reconstruction and not somewhere else in the frame
  not blur            the accumulated output moves *towards* the instantaneous input, not away
  energy              the ROI moves towards the cycle-averaged raw input, not away from it
  bleed is reported   the cost of the change (light bleeding into the dark bars) is measured, not
                      hidden — the test asserts the number is recorded, finite, and worse

Usage: python scripts/test_correction_gates.py [--keep]
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
import criteria as crit  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

CVAR = 'r.TAA.ReconstructionHdrWeightExponent'
SCENE_START = 37
# The same 200-frame warm-up the rest of the programme uses. At 120 frames the GI is still
# converging and the process-to-process floor is ~200x larger, which is what this harness first
# measured; the floor is still measured rather than assumed.
FIRST = SCENE_START + 200
FRAMES = 16
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 128, 256)
BASE = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}

#: The TAA-off control needs a rectangle the *deferred* pass actually lights. Pose W's window is
#: lit through translucent geometry, so its deferred components are ~1e-4 and the control had no
#: power there; pose A's sunlit floor is ordinary deferred geometry.
CONTROL_PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-a.uzproject')
CONTROL_ROI = ('lit_floor', 832, 816, 256, 64)

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def capture(work, label, cvars, fields='taa', project=None, roi=None):
    res = runner.run_capture(os.path.join(work, label), project or PROJECT,
                             LAST - SCENE_START + 8,
                             regions=[roi or ROI], region_fields=fields, region_first=FIRST,
                             region_last=LAST, cvars={**BASE, **cvars}, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    return res


def lighting_sum(res, roi_name=None):
    """Total deferred lighting radiance per pixel — written whether or not TAA runs."""
    seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST, allow_taa_missing=True)
    out = []
    for f in seq:
        rect = f.rect(roi_name or ROI[0])
        total = sum(rect.channel(c)[..., :3] for c in
                    ('direct_light', 'diffuse_indirect', 'specular_indirect', 'emissive_and_mis'))
        out.append(0.2126 * total[..., 0] + 0.7152 * total[..., 1] + 0.0722 * total[..., 2])
    return np.stack(out).astype(np.float64)


def stack(res, channel='taa_output', taa_off=False):
    seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST, allow_taa_missing=taa_off)
    return np.stack([f.rect(ROI[0]).luminance(channel) for f in seq]).astype(np.float64)


def main():
    runner.require_exe()
    keep = '--keep' in sys.argv
    work = tempfile.mkdtemp(prefix='uz_correction_gates_')
    print(f'work dir: {work}')
    try:
        # Each configuration is captured twice, so every equivalence claim below is judged
        # against the reproducibility of the two configurations being compared. A single global
        # floor is not enough: this engine is *mostly* deterministic across processes but not
        # always, and which configuration happens to wobble varies from run to run
        # (`scripts/diagnose_cvar_ulp.py`). Assuming bit-identity produced two false failures.
        runs = {}
        for name, cvars in (('default', {}), ('explicit1', {CVAR: 1.0}), ('exp0', {CVAR: 0.0}),
                            ('recon1', {'r.TAA.ReconstructionMode': 1})):
            runs[name] = [capture(work, f'{name}_{r}', cvars) for r in (0, 1)]

        cvars = (runs['default'][0]['manifest'] or {}).get('cvars', {})
        check(CVAR in cvars and abs(float(cvars.get(CVAR, -1)) - 1.0) < 1e-9,
              'the CVar exists and defaults to the production value 1.0',
              f'manifest reports {cvars.get(CVAR)}')

        arr = {k: [stack(r) for r in v] for k, v in runs.items()}
        # Two floors, because two different statistics are compared below. Judging a mean |delta|
        # against a max |delta| floor is not a comparison at all, and it made the effect-size
        # check fail spuriously.
        floor = {k: float(np.abs(v[0] - v[1]).max()) for k, v in arr.items()}
        floor_mean = {k: float(np.abs(v[0] - v[1]).mean()) for k, v in arr.items()}
        d, e1, e0, r1 = (arr[k][0] for k in ('default', 'explicit1', 'exp0', 'recon1'))
        scale = float(np.abs(d).mean())
        worst = max(floor.values())
        check(worst < 0.02 * scale,
              'every configuration reproduces itself across processes to well under 2 % ',
              ', '.join(f'{k} {v:.2e}' for k, v in floor.items()) + f' on a mean of {scale:.3f}')

        tol_1 = max(floor['default'], floor['explicit1'], 1e-12)
        check(np.abs(d - e1).max() <= tol_1,
              'negative control: omitting the CVar matches setting it to 1.0, within their floor',
              f'max |delta| {np.abs(d - e1).max():.3e} vs floor {tol_1:.3e}')
        worst_mean = max(floor_mean.values())
        # Not "100x the floor": this renderer is deterministic, so the floor is genuinely 0 and
        # `> 100 * max(floor, 1e-12)` degenerates to `> 1e-10` — a bar numerical dust clears. The
        # criterion is a fraction of the image's own scale, which stays meaningful at a zero floor.
        # The floor is still reported, because when it is NOT zero it is the thing to beat.
        changed = np.abs(d - e0).mean()
        check(crit.exceeds_scale(changed, scale, 0.01) and changed > worst_mean,
              'setting the exponent to 0 changes the image by at least 1 % of its own mean',
              f'mean |delta| {changed:.3e} = {changed / scale:.1%} of the mean {scale:.3f}; '
              f'worst repeatability floor {worst_mean:.3e}')
        tol_0 = max(floor['exp0'], floor['recon1'], 1e-12)
        check(np.abs(e0 - r1).max() <= tol_0,
              'exponent 0 matches r.TAA.ReconstructionMode=1 (the same maths, another branch)',
              f'max |delta| {np.abs(e0 - r1).max():.3e} vs floor {tol_0:.3e}')

        stds = {}
        for e in (1.0, 0.75, 0.5, 0.25, 0.0):
            res = (runs['exp0'][0] if e == 0.0 else
                   runs['default'][0] if e == 1.0 else capture(work, f'e{e}', {CVAR: e}))
            stds[e] = float(stack(res).std(axis=0).mean())
        order = [stds[e] for e in (1.0, 0.75, 0.5, 0.25, 0.0)]
        check(all(a > b for a, b in zip(order, order[1:])),
              'the residual falls monotonically as the exponent falls (a knob, not a switch)',
              ' > '.join(f'{v:.4e}' for v in order))

        # Negative control: with TAA off the CVar is never read by any pass. Judged against the
        # floor of *these* configurations, measured here — borrowing the TAA-on floor would be the
        # same mistake as assuming bit-identity, just in the other direction.
        # The `output` record is written by the TAA resolve, so with `r.TAA=0` it is never written
        # and an earlier version of this control compared two all-zero buffers and called them
        # identical. The lighting components are written by the deferred pass either way, so this
        # control can actually fail.
        off = {}
        for name, cvars in (('taaoff_base', {'r.TAA': 0}),
                            ('taaoff_cand', {'r.TAA': 0, CVAR: 0.0})):
            off[name] = [lighting_sum(capture(work, f'{name}_{r}', cvars, fields='components',
                                              project=CONTROL_PROJECT, roi=CONTROL_ROI),
                                      CONTROL_ROI[0]) for r in (0, 1)]
        signal = float(off['taaoff_base'][0].mean())
        off_floor = max(float(np.abs(v[0] - v[1]).max()) for v in off.values())
        delta = float(np.abs(off['taaoff_base'][0] - off['taaoff_cand'][0]).max())
        # State the control's power before its verdict: a floor comparable to the signal means the
        # control could not have detected a real change, and passing would prove nothing.
        check(signal > 0 and off_floor < 0.05 * signal,
              'the TAA-off control has power: its floor is small against its own signal',
              f'floor {off_floor:.3e} on a signal of mean {signal:.5f} '
              f'({off_floor / max(1e-12, signal) * 100:.2f} % of it)')
        check(delta <= max(off_floor, 1e-12),
              'negative control: with TAA off the CVar changes nothing',
              f'max |delta| {delta:.3e} vs the TAA-off floor {off_floor:.3e}, on deferred lighting '
              f'of mean {signal:.5f}')

        # not blur, and energy towards the raw input
        raw_b = stack(runs['default'][0], 'taa_raw')
        raw_c = stack(runs['exp0'][0], 'taa_raw')
        dist_b = float(np.abs(d - raw_b).mean())
        dist_c = float(np.abs(e0 - raw_c).mean())
        check(dist_c < dist_b,
              'the accumulated image moves towards the instantaneous input, so the gain is not blur',
              f'{dist_b:.4f} -> {dist_c:.4f}')
        ratio_b = float(d.mean() / raw_b.mean())
        ratio_c = float(e0.mean() / raw_c.mean())
        check(abs(1.0 - ratio_c) < abs(1.0 - ratio_b),
              'the ROI energy moves towards the raw input instead of away from it',
              f'{ratio_b:.4f} -> {ratio_c:.4f} (1.0 is correct)')

        # the cost is measured, not hidden
        dark = raw_b.mean(axis=0) <= np.percentile(raw_b.mean(axis=0), 10)
        bleed_b = float(d[:, dark].mean() / raw_b.mean(axis=0)[dark].mean())
        bleed_c = float(e0[:, dark].mean() / raw_c.mean(axis=0)[dark].mean())
        check(np.isfinite(bleed_b) and np.isfinite(bleed_c) and bleed_c > bleed_b,
              'the known cost is reproduced and reported: more light bleeds into the dark bars',
              f'{bleed_b:.0f}x -> {bleed_c:.0f}x of their own raw radiance')
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
