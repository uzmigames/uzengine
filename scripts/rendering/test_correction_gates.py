"""Behavioural harness for the candidate correction (followup-14 item 2.2).

Every assertion is a measurement on captured GPU records, and each one is paired with a control
that must NOT move:

  reproducibility     every configuration is captured `--repeats` times (default 4), each in its
                      own process, INTERLEAVED across configurations (all of them once per round),
                      so a drift over the session cannot land on one configuration. Bit-identity is
                      not assumed: this engine is mostly but not always deterministic across
                      processes (`scripts/rendering/diagnose_cvar_ulp.py`)
  equivalence rule    a claim that two configurations render the same is judged BETWEEN against
                      WITHIN, on the max |delta| statistic: every cross-configuration pair must sit
                      inside the largest distance between repeats of the same configuration,
                      max(between) <= max(within) (`criteria.spread_verdict`, the rule pre-registered in
                      `scripts/rendering/run_default_vs_explicit.py`). Until 2026-10-08 the floor came from
                      ONE pair per configuration, and whether a check passed depended on which pair
                      that was (followup-14 notes, 2026-09-16 and 2026-10-08). The single-pair
                      comparison is still printed beside each verdict, not judged
  default unchanged   omitting the CVar renders the same as setting it to 1.0, by that rule, so the
                      correction is genuinely opt-in
  effect exists       `=0` changes the window records by at least 1 % of their mean, and by more
                      than any repeat pair of any configuration differs
  equivalence         `=0` matches `r.TAA.ReconstructionMode=1` by that rule, which is the same
                      maths reached by a different branch
  monotone            the residual falls monotonically as the exponent falls: a continuous knob,
                      not a switch
  TAA off control     with `r.TAA=0` the CVar changes nothing (the same rule, on the deferred
                      lighting of a sunlit floor whose power is checked first), so the effect is
                      inside the TAA reconstruction and not somewhere else in the frame
  not blur            the accumulated output moves *towards* the instantaneous input, not away
  energy              the ROI moves towards the cycle-averaged raw input, not away from it
  bleed is reported   the cost of the change (light bleeding into the dark bars) is measured, not
                      hidden — the test asserts the number is recorded, finite, and worse

Usage: python scripts/rendering/test_correction_gates.py [--repeats 4] [--keep]

Cost: 6 x repeats + 3 captures (27 at the default), about 40 s each on the 2026-09 builds, so allow
20 minutes or more; run it in the background rather than under a short timeout.
"""
from __future__ import annotations

import argparse
import itertools
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
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 128, 256)
BASE = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}

#: The TAA-off control needs a rectangle the *deferred* pass actually lights. Pose W's window is
#: lit through translucent geometry, so its deferred components are ~1e-4 and the control had no
#: power there; pose A's sunlit floor is ordinary deferred geometry.
CONTROL_PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-a.uzproject')
CONTROL_ROI = ('lit_floor', 832, 816, 256, 64)

#: Repeats per configuration. Two repeats give one pair per configuration, which is the single-pair
#: floor this harness used until 2026-10-08; three is the least that measures a spread at all.
REPEATS = 4
MIN_REPEATS = 3
CONFIGS = (('default', {}), ('explicit1', {CVAR: 1.0}), ('exp0', {CVAR: 0.0}),
           ('recon1', {'r.TAA.ReconstructionMode': 1}))
OFF_CONFIGS = (('taaoff_base', {'r.TAA': 0}), ('taaoff_cand', {'r.TAA': 0, CVAR: 0.0}))

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def pair_distance(a, b):
    """(max, mean) of |a - b| over every pixel and frame of the arrays of two captures."""
    d = np.abs(a - b)
    return float(d.max()), float(d.mean())


def within_pairs(arrs):
    """Distances between every pair of repeats of ONE configuration."""
    return [pair_distance(arrs[i], arrs[j]) for i, j in itertools.combinations(range(len(arrs)), 2)]


def between_pairs(xs, ys):
    """Distances between every repeat of one configuration and every repeat of the other."""
    return [pair_distance(x, y) for x in xs for y in ys]


def equivalent(xs, ys, what):
    """Checks that the repeats `xs` and `ys` of two configurations render the same.

    Judged on the max |delta| statistic only. The mean |delta| verdict is printed but not judged:
    on the eight same-path captures of `40-default-vs-explicit`, split into two groups of four in
    all 35 possible ways, the max rule holds in 35 and the mean rule in only 25, because the mean
    of the cross-process differences of this renderer behaves like continuous noise, where
    max(between) <= max(within) fails by chance (in 10 of those 35 splits; followup-14 notes,
    2026-10-08). The single-pair rule used until 2026-10-08 (repeat 0 of each against the floor of
    repeats 0 and 1) is printed beside it for comparison, also not judged.
    """
    within = within_pairs(xs) + within_pairs(ys)
    between = between_pairs(xs, ys)
    b_max, w_max = [b[0] for b in between], [w[0] for w in within]
    v_max = crit.spread_verdict(b_max, w_max)
    v_mean = crit.spread_verdict([b[1] for b in between], [w[1] for w in within])
    old_floor = max(pair_distance(xs[0], xs[1])[0], pair_distance(ys[0], ys[1])[0], 1e-12)
    old_delta = pair_distance(xs[0], ys[0])[0]
    check(v_max == 'INDISTINGUISHABLE', what,
          f'max |delta| between {min(b_max):.3e}..{max(b_max):.3e} ({len(b_max)} pairs), within '
          f'{min(w_max):.3e}..{max(w_max):.3e} ({len(w_max)} pairs) -> {v_max}')
    print(f'       not judged: mean |delta| -> {v_mean}; single-pair rule: {old_delta:.3e} vs a '
          f'floor of {old_floor:.3e} -> {"would pass" if old_delta <= old_floor else "would FAIL"}',
          flush=True)
    return v_max


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
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=REPEATS,
                    help=f'captures per configuration, at least {MIN_REPEATS} (default {REPEATS})')
    ap.add_argument('--keep', action='store_true', help='keep the work directory')
    args = ap.parse_args()
    if args.repeats < MIN_REPEATS:
        ap.error(f'--repeats must be at least {MIN_REPEATS}: two repeats are one pair per '
                 'configuration, the single-pair floor this harness no longer uses')
    runner.require_exe()
    keep = args.keep
    work = tempfile.mkdtemp(prefix='uz_correction_gates_')
    print(f'work dir: {work}; {args.repeats} interleaved repeats per configuration')
    try:
        # Every configuration is captured `--repeats` times, interleaved, so every equivalence claim
        # below is judged against the spread its two configurations show across repeats. A single
        # global floor is not enough: this engine is *mostly* deterministic across processes but
        # not always, and which configuration happens to wobble varies from run to run
        # (`scripts/rendering/diagnose_cvar_ulp.py`). Assuming bit-identity produced two false failures, and a
        # floor from one pair per configuration produced one more (2026-09-16).
        runs = {name: [] for name, _ in CONFIGS}
        for r in range(args.repeats):
            for name, cvars in CONFIGS:
                runs[name].append(capture(work, f'{name}_{r}', cvars))

        cvars = (runs['default'][0]['manifest'] or {}).get('cvars', {})
        check(CVAR in cvars and abs(float(cvars.get(CVAR, -1)) - 1.0) < 1e-9,
              'the CVar exists and defaults to the production value 1.0',
              f'manifest reports {cvars.get(CVAR)}')

        arr = {k: [stack(r) for r in v] for k, v in runs.items()}
        # Two statistics, kept apart: judging a mean |delta| against a max |delta| spread is not a
        # comparison at all, and it once made the effect-size check fail spuriously.
        within = {k: within_pairs(v) for k, v in arr.items()}
        spread = {k: max(p[0] for p in w) for k, w in within.items()}
        spread_mean = {k: max(p[1] for p in w) for k, w in within.items()}
        d, e0 = arr['default'][0], arr['exp0'][0]
        scale = float(np.abs(d).mean())
        worst = max(spread.values())
        check(worst < 0.02 * scale,
              'every configuration reproduces itself across processes to well under 2 % ',
              ', '.join(f'{k} {v:.2e}' for k, v in spread.items())
              + f' (largest of {len(within["default"])} repeat pairs each) on a mean of {scale:.3f}')

        equivalent(arr['default'], arr['explicit1'],
                   'negative control: omitting the CVar matches setting it to 1.0, '
                   'within the spread of their repeats')
        worst_mean = max(spread_mean.values())
        # Not "100x the floor": this renderer is deterministic, so the floor is genuinely 0 and
        # `> 100 * max(floor, 1e-12)` degenerates to `> 1e-10` — a bar numerical dust clears. The
        # criterion is a fraction of the image's own scale, which stays meaningful at a zero floor.
        # The spread is still required, because when it is NOT zero it is the thing to beat: the
        # smallest of every default x exp0 pair has to beat the largest of every repeat pair.
        changed = min(p[1] for p in between_pairs(arr['default'], arr['exp0']))
        check(crit.exceeds_scale(changed, scale, 0.01) and changed > worst_mean,
              'setting the exponent to 0 changes the image by at least 1 % of its own mean',
              f'smallest mean |delta| over every default x exp0 pair {changed:.3e} = '
              f'{changed / scale:.1%} of the mean {scale:.3f}; largest repeat-pair mean |delta| '
              f'{worst_mean:.3e}')
        equivalent(arr['exp0'], arr['recon1'],
                   'exponent 0 matches r.TAA.ReconstructionMode=1 (the same maths, another branch)')

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
        # Repeated and interleaved like the TAA-on configurations: until 2026-10-08 this control
        # also took its floor from one pair, and on 2026-09-09 it passed at exact equality with it.
        off = {name: [] for name, _ in OFF_CONFIGS}
        for r in range(args.repeats):
            for name, cvars in OFF_CONFIGS:
                off[name].append(lighting_sum(capture(work, f'{name}_{r}', cvars,
                                                      fields='components',
                                                      project=CONTROL_PROJECT, roi=CONTROL_ROI),
                                              CONTROL_ROI[0]))
        signal = float(np.mean([v.mean() for v in off['taaoff_base']]))
        off_floor = max(p[0] for v in off.values() for p in within_pairs(v))
        # State the power of the control before its verdict: a spread comparable to the signal means
        # the control could not have detected a real change, and passing would prove nothing.
        check(signal > 0 and off_floor < 0.05 * signal,
              'the TAA-off control has power: its repeat spread is small against its own signal',
              f'spread {off_floor:.3e} on a signal of mean {signal:.5f} '
              f'({off_floor / max(1e-12, signal) * 100:.2f} % of it)')
        equivalent(off['taaoff_base'], off['taaoff_cand'],
                   'negative control: with TAA off the CVar changes nothing '
                   f'(deferred lighting of mean {signal:.5f})')

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
