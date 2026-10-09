"""The decision rules the flicker studies use, in one place so they can be tested.

Every automatic verdict in this programme has at some point been wrong in a way that only showed up
when someone read the code: a two-sided test where a one-sided one was meant, a mean compared with a
max, a ratio measured against a floor of zero, a threshold defined for one quantity applied to
another. Auditing them by eye after the fact has caught each one late. They live here instead, and
`scripts/test_criteria_logic.py` exercises them with cases that must pass and cases that must fail.

The rules are deliberately small and deliberately named for what they decide, not for how they are
computed, because the recurring error has been using a correct computation to answer the wrong
question.
"""
from __future__ import annotations

import numpy as np


def _require_finite(value: float, what: str) -> float:
    """An invalid number is not a verdict. NaN and infinity are rejected rather than classified.

    `believable_change(nan, floor)` used to return 'below', so `no_believable_worsening(nan, ...)`
    returned True and an unmeasurable condition passed as "no regression". A caller that can
    legitimately produce NaN — a ratio with no events in the window, say — must decide what that
    means before asking for a verdict; see `is_measurable`.
    """
    v = float(value)
    if not np.isfinite(v):
        raise ValueError(f'{what} is {value!r}, which is not a measurement; '
                         'handle the unmeasurable case before asking for a verdict')
    return v


def _require_finite_array(values, what: str) -> np.ndarray:
    """The array form of `_require_finite`. One NaN turns every min/max comparison false and a
    spectrum into NaN, so a series holding one is rejected before any verdict is formed."""
    a = np.asarray(values, dtype=np.float64)
    bad = int(np.count_nonzero(~np.isfinite(a)))
    if bad:
        raise ValueError(f'{what} holds {bad} non-finite value(s) of {a.size}, which is not a '
                         'measurement; handle the unmeasurable case before asking for a verdict')
    return a


def is_measurable(*values) -> bool:
    """True when every value is finite, for callers that must skip a condition rather than judge it."""
    return all(np.isfinite(float(v)) for v in values)


def believable_change(delta: float, floor: float) -> str:
    """Classify a signed change against a repeatability floor.

    `floor` is followup-09's per-ROI value: `max(3 x 95 % half-width, 5 % of the mean)`, defined
    there as the bar a difference must clear before it may be called an effect at all. A change at
    exactly the floor does not clear it.

    Returns 'worse' (delta above the floor), 'better' (below minus the floor) or 'below'.
    Raises on a non-finite delta or floor.
    """
    delta = _require_finite(delta, 'delta')
    floor = _require_finite(floor, 'floor')
    if delta > floor:
        return 'worse'
    if -delta > floor:
        return 'better'
    return 'below'


def no_believable_worsening(delta: float, floor: float) -> bool:
    """One-sided: only a rise beyond the floor fails. An improvement must not fail a 'no worsening'
    criterion, which a test on |delta| would have done."""
    return believable_change(delta, floor) != 'worse'


def unchanged(delta: float, floor: float) -> bool:
    """Two-sided, for a control that must not move in either direction. Raises on a non-finite
    input rather than reporting a control as moved or unmoved on the strength of a NaN."""
    return abs(_require_finite(delta, 'delta')) <= _require_finite(floor, 'floor')


def relative_gap(a: float, b: float, scale: float | None = None) -> float:
    """|a - b| relative to a stated scale, never to whichever of the two happens to be smaller.

    A relative difference needs a denominator that cannot go to zero on its own. When `scale` is not
    given it is the larger magnitude of the two, which is bounded below by the signal itself.
    """
    a = _require_finite(a, 'a')
    b = _require_finite(b, 'b')
    scale = max(abs(a), abs(b)) if scale is None else _require_finite(scale, 'scale')
    if scale <= 0.0:
        return float('inf') if a != b else 0.0
    return abs(a - b) / scale


def exceeds_scale(value: float, scale: float, fraction: float) -> bool:
    """`value` is at least `fraction` of `scale`.

    Use this instead of "n times the repeatability floor" whenever the floor can legitimately be
    zero. On a deterministic renderer the floor IS zero, and `value > 100 * max(floor, 1e-12)`
    degenerates to `value > 1e-10`, which almost nothing fails — a bar that cannot fail is not a
    control. A fraction of the signal's own scale stays meaningful.

    Raises on non-finite input. Before 2026-09-15 `exceeds_scale(inf, 1, 0.1)` returned True.
    """
    value = _require_finite(value, 'value')
    scale = _require_finite(scale, 'scale')
    fraction = _require_finite(fraction, 'fraction')
    return scale > 0.0 and value >= fraction * scale


def separates_by_parity(series: np.ndarray, frames: np.ndarray) -> bool:
    """Do the odd-frame and even-frame values occupy disjoint ranges?

    Threshold-free, which is why it is used for the parity question. Note it is a statement about
    the series handed in: a region-mean series and a per-pixel series answer different questions and
    must not be reported as if they were the same one.

    Raises on non-finite input. Before 2026-09-15 one NaN made both range comparisons false, so a
    clearly split series holding a NaN read as "no separation".
    """
    values = _require_finite_array(series, 'series')
    idx = _require_finite_array(frames, 'frames')
    if values.shape != idx.shape:
        raise ValueError(f'series {values.shape} and frames {idx.shape} differ in shape')
    if np.any(idx != np.round(idx)):
        raise ValueError('frames must be whole frame numbers')
    idx = idx.astype(np.int64)
    odd = values[idx % 2 == 1]
    even = values[idx % 2 == 0]
    if odd.size == 0 or even.size == 0:
        return False
    return bool(odd.min() > even.max() or even.min() > odd.max())


def period_share(series: np.ndarray, period: float) -> float:
    """Share of a real series' variance held by one named period.

    The one-sided spectrum from `rfft` is not a variance decomposition on its own. Every bin except
    DC and, for an even-length series, Nyquist stands for a CONJUGATE PAIR of frequencies and so
    carries twice the variance its magnitude suggests. Summing the one-sided bins untouched
    therefore under-counts every paired bin and inflates the unpaired ones — and period 2 IS the
    Nyquist bin, so it was the one being inflated.

    Measured before the fix, on two components of equal variance (one at Nyquist, one at period 8):
    66.7 % / 33.3 % instead of 50 % / 50 %. The weights below correct that.
    """
    m = _require_finite_array(series, 'series')
    period = _require_finite(period, 'period')
    if period <= 0.0:
        raise ValueError(f'period must be positive, got {period}')
    m = m - m.mean()
    n = len(m)
    power = np.abs(np.fft.rfft(m)) ** 2
    weight = np.full(power.shape, 2.0)
    weight[0] = 1.0                     # DC has no partner
    if n % 2 == 0:
        weight[-1] = 1.0                # nor does Nyquist, and it only exists for even n
    power = power * weight
    power[0] = 0.0                      # the mean was removed; DC carries no variance
    total = power.sum()
    if total <= 0.0:
        return 0.0
    bin_index = int(round(n / period))
    if bin_index <= 0 or bin_index >= len(power):
        return 0.0
    return float(power[bin_index] / total)


def oscillation(series_per_pixel: np.ndarray) -> float:
    """The programme's oscillation metric: per-pixel temporal standard deviation, averaged over the
    region. Takes (frames, ...) and reduces the frame axis first — averaging over pixels before
    taking the standard deviation would measure something else entirely."""
    a = _require_finite_array(series_per_pixel, 'series_per_pixel')
    return float(a.std(axis=0).mean())


def recovery_censored(recovery_frames: int, captured_after_cut: int) -> bool:
    """True when a recovery measurement ran out of frames instead of recovering.

    A recovery count is the number of frames until the error stays under tolerance. When every
    captured frame after the event is still over tolerance, the count equals the capture length and
    says only "did not recover within the observed window" - not that it could never recover. Two
    censored counts are not equal recoveries, and a gate that compares them passes vacuously.

    Seen on 2026-09-15: `shadow_wall` recovered in 82 of 82 frames in BOTH conditions of the
    VarianceClipGamma motion gate, which read as a pass until the capture length was checked.
    """
    return (int(_require_finite(recovery_frames, 'recovery_frames'))
            >= int(_require_finite(captured_after_cut, 'captured_after_cut')))


def recovery_tolerance_usable(tolerance: float, noise: float) -> bool:
    """A recovery tolerance must sit above the floor of the SAME error it bounds, or that error can
    never fall under it and every recovery is censored.

    `noise` must be that recovery error measured in a converged, undisturbed state - the same
    spatial mean of |output - reference| the tolerance bounds - not another measure of variation.
    On 2026-09-15 G6's tolerance on `shadow_wall` (3.2e-06) was compared with the per-pixel
    temporal standard deviation (8.7e-06), a different quantity; that comparison hinted the
    criterion was inadequate there but did not establish it.
    """
    return _require_finite(tolerance, 'tolerance') > _require_finite(noise, 'noise')


def spread_verdict(between, within, guard: float = 1e-12) -> str:
    """Between-condition distances against within-condition (repeat) distances, on one statistic.

    'INDISTINGUISHABLE' when max(between) <= max(within): every cross-condition distance sits inside
    the spread the conditions show against their own repeats. 'SEPARATED' when
    min(between) > max(within). 'OVERLAPPING' otherwise, which does not support a claim of sameness.
    `guard` keeps two perfectly reproducible conditions (a within spread of exactly 0) from failing
    on numerical dust. Use it on max |delta|: on mean |delta| the cross-process differences of this
    renderer behave like continuous noise and the rule fails by chance (followup-14, 2026-10-08).
    Raises on non-finite or empty input.
    """
    b = _require_finite_array(between, 'between')
    w = _require_finite_array(within, 'within')
    if b.size == 0 or w.size == 0:
        raise ValueError('spread_verdict needs at least one between and one within distance')
    top = max(float(w.max()), _require_finite(guard, 'guard'))
    if float(b.max()) <= top:
        return 'INDISTINGUISHABLE'
    if float(b.min()) > top:
        return 'SEPARATED'
    return 'OVERLAPPING'
