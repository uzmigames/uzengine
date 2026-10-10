"""Exercises every decision rule in `flicker/criteria.py`, including cases that must FAIL.

A criterion that only ever sees inputs it passes has not been tested. Each rule here is given the
input it is meant to accept AND the input it is meant to reject, plus the specific mistake that has
actually been made in this programme, so a regression reproduces as a failing case rather than as a
wrong verdict in a study three weeks later.

Usage: python scripts/rendering/test_criteria_logic.py
"""
from __future__ import annotations

import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import criteria as c  # noqa: E402

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def main():
    print('criteria logic')

    # ---- believable_change / no_believable_worsening ----------------------
    floor = 4.7e-7
    check(c.believable_change(+3.53e-06, floor) == 'worse'
          and c.believable_change(-3.53e-06, floor) == 'better'
          and c.believable_change(+1.0e-07, floor) == 'below'
          and c.believable_change(floor, floor) == 'below',
          'believable_change classifies both directions, and the floor itself does not clear it',
          'worse / better / below / at-the-floor')

    check(c.no_believable_worsening(-3.53e-06, floor) is True,
          'the mistake that was made: a believable IMPROVEMENT must not fail "no worsening"',
          'a test on |delta| returned False here, failing an improvement as if it were a regression')
    check(c.no_believable_worsening(+3.53e-06, floor) is False,
          'negative control: a believable worsening still fails it')

    # ---- unchanged, for controls ------------------------------------------
    check(c.unchanged(-4.0e-10, floor) and c.unchanged(+4.0e-10, floor)
          and not c.unchanged(+3.5e-06, floor) and not c.unchanged(-3.5e-06, floor),
          'a control is two-sided: it must fail on movement in either direction')

    # ---- relative_gap ------------------------------------------------------
    check(abs(c.relative_gap(1.10, 1.00) - 0.0909) < 1e-3,
          'relative_gap divides by the larger magnitude, not by whichever is smaller',
          f'{c.relative_gap(1.10, 1.00):.4f}')
    check(c.relative_gap(0.0, 0.0) == 0.0 and c.relative_gap(1.0, 0.0) == 1.0,
          'relative_gap is finite at zero rather than dividing by it')

    # ---- exceeds_scale, the vacuous-floor fix -------------------------------
    # The mistake: `value > 100 * max(floor, 1e-12)` with a floor of zero becomes `value > 1e-10`.
    deterministic_floor = 0.0
    tiny = 1e-9
    old_style = tiny > 100 * max(deterministic_floor, 1e-12)
    check(old_style is True,
          'reproduces the mistake: with a zero floor, "100x the floor" passes on numerical dust',
          f'{tiny:.0e} > {100 * max(deterministic_floor, 1e-12):.0e} is {old_style}')
    check(c.exceeds_scale(tiny, scale=0.5, fraction=0.01) is False
          and c.exceeds_scale(0.02, scale=0.5, fraction=0.01) is True,
          'exceeds_scale measures against the signal, so a zero floor cannot make it vacuous')
    check(c.exceeds_scale(1.0, scale=0.0, fraction=0.01) is False,
          'exceeds_scale refuses to pass when the scale itself is zero')

    # ---- separates_by_parity ----------------------------------------------
    frames = np.arange(237, 237 + 128)
    disjoint = np.where(frames % 2 == 1, 2.0, 1.0) + np.random.default_rng(0).normal(0, 0.01, 128)
    overlapping = np.random.default_rng(1).normal(1.5, 0.5, 128)
    check(c.separates_by_parity(disjoint, frames) is True,
          'separates_by_parity finds a parity split when the ranges are disjoint')
    check(c.separates_by_parity(overlapping, frames) is False,
          'negative control: it does not find one in noise around a single mean')
    check(c.separates_by_parity(np.ones(4), np.array([2, 4, 6, 8])) is False,
          'it returns False rather than raising when one parity is absent')

    # ---- period_share ------------------------------------------------------
    t = np.arange(128)
    alternating = (-1.0) ** t
    check(c.period_share(alternating, 2) > 0.99,
          'period_share finds an alternating series almost entirely at period 2',
          f'{c.period_share(alternating, 2):.3f}')
    slow = np.sin(2 * np.pi * t / 32.0)
    check(c.period_share(slow, 2) < 0.01 and c.period_share(slow, 32) > 0.9,
          'negative control: a period-32 series is not attributed to period 2',
          f'period 2 {c.period_share(slow, 2):.3f}, period 32 {c.period_share(slow, 32):.3f}')
    check(c.period_share(np.ones(128), 2) == 0.0,
          'a constant series has no variance to attribute, and does not divide by zero')

    # ---- period_share: the one-sided spectrum is not a variance decomposition ----
    # Two components of EQUAL variance, one at Nyquist (period 2, an unpaired bin) and one at
    # period 8 (a conjugate pair). Before the fix this returned 66.7 / 33.3, inflating period 2.
    B = 1.0
    nyquist = B * (-1.0) ** t
    paired = np.sqrt(2.0) * B * np.cos(2 * np.pi * t / 8.0)
    mix = nyquist + paired
    s2, s8 = c.period_share(mix, 2), c.period_share(mix, 8)
    check(abs(s2 - 0.5) < 1e-6 and abs(s8 - 0.5) < 1e-6,
          'period_share weights paired bins, so equal variances give equal shares',
          f'period 2 {s2:.4f}, period 8 {s8:.4f}; unweighted this read 0.6667 / 0.3333')
    check(abs(nyquist.var() - paired.var()) < 1e-9,
          'and the two components really do have equal variance',
          f'{nyquist.var():.6f} vs {paired.var():.6f}')
    check(abs(c.period_share(nyquist, 2) - 1.0) < 1e-9
          and abs(c.period_share(paired, 8) - 1.0) < 1e-9,
          'a pure component still reads as the whole of the variance, paired or not')

    # ---- non-finite input is rejected, not classified ------------------------
    rejected = 0
    for fn, args in ((c.believable_change, (float('nan'), 0.1)),
                     (c.no_believable_worsening, (float('nan'), 0.1)),
                     (c.unchanged, (float('nan'), 0.1)),
                     (c.believable_change, (0.5, float('nan'))),
                     (c.no_believable_worsening, (float('inf'), 0.1))):
        try:
            fn(*args)
        except ValueError:
            rejected += 1
    check(rejected == 5,
          'the mistake that was made: NaN must not pass as "no worsening"',
          f'{rejected}/5 non-finite inputs rejected; previously nan classified as "below" and '
          'no_believable_worsening(nan) returned True')
    check(c.is_measurable(1.0, -2.0) and not c.is_measurable(1.0, float('nan')),
          'is_measurable lets a caller skip an unmeasurable condition instead of judging it')

    # ---- non-finite input: the functions the first fix missed (review 2026-09-15) ----
    nan, inf = float('nan'), float('inf')
    frames8 = np.arange(8)
    split = np.where(frames8 % 2 == 1, 2.0, 1.0)
    with_nan = split.copy()
    with_nan[3] = nan
    odd, even = with_nan[frames8 % 2 == 1], with_nan[frames8 % 2 == 0]
    unguarded = bool(odd.min() > even.max() or even.min() > odd.max())
    check(unguarded is False and (inf >= 0.1 * 1.0) is True,
          'reproduces both mistakes: one NaN read as "no parity separation", and inf exceeded the scale',
          f'the unguarded parity comparison returned {unguarded} on a clearly split series with one NaN')
    cases = (
        ('exceeds_scale(inf, 1, 0.1)', c.exceeds_scale, (inf, 1.0, 0.1)),
        ('exceeds_scale(0.5, nan, 0.1)', c.exceeds_scale, (0.5, nan, 0.1)),
        ('exceeds_scale(0.5, 1, nan)', c.exceeds_scale, (0.5, 1.0, nan)),
        ('relative_gap(nan, 1)', c.relative_gap, (nan, 1.0)),
        ('relative_gap(1, 1, scale=inf)', lambda: c.relative_gap(1.0, 1.0, scale=inf), ()),
        ('separates_by_parity with one NaN value', c.separates_by_parity, (with_nan, frames8)),
        ('separates_by_parity with a NaN frame', c.separates_by_parity,
         (np.ones(8), np.where(frames8 == 2, nan, frames8))),
        ('period_share with one NaN', c.period_share, (with_nan, 2)),
        ('period_share with an infinite period', c.period_share, (np.ones(8), inf)),
        ('oscillation with an inf', c.oscillation, (np.array([[1.0, inf], [2.0, 1.0]]),)),
        ('recovery_censored(nan, 82)', c.recovery_censored, (nan, 82)),
    )
    accepted = []
    for label, fn, args in cases:
        try:
            fn(*args)
            accepted.append(label)
        except ValueError:
            pass
    check(not accepted, 'every criterion rejects non-finite input instead of classifying it',
          f'{len(cases) - len(accepted)}/{len(cases)} rejected'
          + (f'; accepted: {accepted}' if accepted else ''))
    check(c.separates_by_parity(split, frames8) is True and c.exceeds_scale(0.5, 1.0, 0.1) is True
          and abs(c.period_share(split, 2) - 1.0) < 1e-9 and c.relative_gap(2.0, 1.0) == 0.5,
          'negative control: the same calls on finite input still return verdicts')

    # ---- recovery censoring -------------------------------------------------
    # The mistake: shadow_wall's G6 recovery was 82 of 82 frames in both conditions and read as a pass.
    check(c.recovery_censored(82, 82) and not c.recovery_censored(16, 82)
          and not c.recovery_censored(40, 82),
          'a recovery equal to the capture length is censored, not an equal recovery',
          '82 of 82 censored; 16 and 40 of 82 are real measurements')
    check(c.recovery_tolerance_usable(4.9e-02, 9.8e-03)
          and not c.recovery_tolerance_usable(3.2e-06, 8.7e-06),
          'a tolerance below the region noise is unusable, so G6 cannot judge that region',
          'a tolerance over its own error floor is usable, one under it is not; both arguments must be the same quantity')
    try:
        c.recovery_tolerance_usable(float('nan'), 1.0)
        check(False, 'recovery_tolerance_usable rejects a non-finite tolerance')
    except ValueError:
        check(True, 'recovery_tolerance_usable rejects a non-finite tolerance')

    # ---- oscillation -------------------------------------------------------
    # per-pixel std then mean, NOT mean then std: a region whose pixels each oscillate but whose
    # mean is flat must still register.
    a = np.stack([np.array([[1.0, 2.0]]), np.array([[2.0, 1.0]])] * 32)
    check(c.oscillation(a) > 0.49,
          'oscillation reduces the frame axis first, so an anti-correlated pair still registers',
          f'{c.oscillation(a):.3f}; the region mean is flat, so mean-then-std would read 0')
    mean_then_std = float(a.mean(axis=(1, 2)).std())
    check(mean_then_std < 1e-12,
          'and the wrong order really would have read zero on that input',
          f'{mean_then_std:.2e}')

    # ---- spread_verdict ----------------------------------------------------
    check(c.spread_verdict([9.5e-7, 3.218e-3], [0.0, 3.218e-3]) == 'INDISTINGUISHABLE',
          'spread_verdict: cross pairs inside the repeat spread are indistinguishable',
          '40-default-vs-explicit: between and within both reach 3.218e-03')
    check(c.spread_verdict([4e-3, 5e-3], [0.0, 3.218e-3]) == 'SEPARATED'
          and c.spread_verdict([1e-3, 4e-3], [0.0, 3.218e-3]) == 'OVERLAPPING',
          'spread_verdict: a cross pair beyond every repeat pair is never indistinguishable',
          'separated / overlapping')
    check((3.218e-3 <= max(1.473e-3, 2.37e-4)) is False
          and c.spread_verdict([3.218e-3], [1.473e-3, 3.218e-3]) == 'INDISTINGUISHABLE',
          'spread_verdict: the 2026-09-16 failure was the single-pair floor, not the conditions',
          'one pair fails, the repeat spread does not')
    check(c.spread_verdict([0.0], [0.0]) == 'INDISTINGUISHABLE'
          and c.spread_verdict([1e-11], [0.0]) == 'SEPARATED',
          'spread_verdict: a zero spread is guarded against numerical dust only',
          '0 vs 0 indistinguishable; 1e-11 vs 0 separated (guard 1e-12)')
    for bad in (([float('nan')], [0.0]), ([], [0.0])):
        try:
            c.spread_verdict(*bad)
            refused = False
        except ValueError:
            refused = True
        check(refused, 'spread_verdict refuses a NaN or an empty distance list', repr(bad))

    print(f'\n{len(PASS)} passed, {len(FAIL)} failed')
    for f in FAIL:
        print(f'  FAILED: {f}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
