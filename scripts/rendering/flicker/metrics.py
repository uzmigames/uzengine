"""Temporal metrics for flicker analysis, with the failure modes of the 2026-09-08/09 packages
designed out:

* counts are exact at full resolution (no subsampling estimate, no rounding before thresholding);
* autocorrelation of a constant signal is **undefined** (`None`), never 0 or 1;
* invalid samples (no geometry, rejected history, out-of-mask) are excluded rather than averaged in;
* a translation of the image is separated from a change of lighting, so moving an edge across a
  pixel is not reported as flicker.

Units: every function takes linear values. Give it linear HDR radiance for energy statements and
say so; display code values are a different, secondary quantity.

`scripts/rendering/test_flicker_metrics.py` validates each of these on synthetic sequences.
"""
from __future__ import annotations

import math

import numpy as np


# ── basic reductions ────────────────────────────────────────────────────────

def masked(values: np.ndarray, mask: np.ndarray = None) -> np.ndarray:
    """Flattens `values`, keeping only the samples where `mask` is True."""
    v = np.asarray(values)
    if mask is None:
        return v.reshape(-1)
    m = np.asarray(mask)
    if m.shape != v.shape:
        raise ValueError(f'mask shape {m.shape} does not match values {v.shape}')
    return v[m]


def frame_pair_stats(a: np.ndarray, b: np.ndarray, thresholds=(), mask: np.ndarray = None) -> dict:
    """Statistics of the change between two frames of the same quantity.

    `thresholds` are absolute differences in the unit of the input; counts are exact numbers of
    samples above each threshold (strictly greater), never estimates.
    """
    da = masked(np.abs(np.asarray(b, dtype=np.float64) - np.asarray(a, dtype=np.float64)), mask)
    out = {
        'samples': int(da.size),
        'mean_abs_diff': float(da.mean()) if da.size else float('nan'),
        'max_abs_diff': float(da.max()) if da.size else float('nan'),
        'rms_diff': float(np.sqrt((da ** 2).mean())) if da.size else float('nan'),
    }
    for t in thresholds:
        out[f'count_gt_{t}'] = int((da > t).sum())
    return out


def temporal_stats(stack: np.ndarray, mask: np.ndarray = None) -> dict:
    """Per-pixel temporal statistics of a (frames, ...) stack, aggregated over the mask.

    `std_mean` is the mean over pixels of each pixel's temporal standard deviation — the primary
    "how much does this ROI wobble" number. `energy_mean` is the mean value (drift check).
    """
    s = np.asarray(stack, dtype=np.float64)
    if s.ndim < 2:
        raise ValueError('stack must be (frames, ...)')
    per_pixel_std = s.std(axis=0)
    per_pixel_mean = s.mean(axis=0)
    d1 = np.abs(np.diff(s, axis=0)).mean(axis=0) if s.shape[0] > 1 else np.zeros_like(per_pixel_std)
    std_v = masked(per_pixel_std, mask)
    mean_v = masked(per_pixel_mean, mask)
    d1_v = masked(d1, mask)
    if std_v.size == 0:
        return {'samples': 0}
    return {
        'samples': int(std_v.size),
        'frames': int(s.shape[0]),
        'std_mean': float(std_v.mean()),
        'std_p95': float(np.percentile(std_v, 95)),
        'std_p99': float(np.percentile(std_v, 99)),
        'std_max': float(std_v.max()),
        'mean': float(mean_v.mean()),
        'abs_diff_mean': float(d1_v.mean()),
    }


# ── periodicity ─────────────────────────────────────────────────────────────

def autocorr(x, lag: int):
    """Autocorrelation of a 1-D series at `lag`, or None when it is undefined.

    Undefined means: the series is constant (zero variance), or there are fewer than two
    overlapping samples. A constant series has no correlation structure — reporting 0 (or 1)
    invites the reader to compare it with a real correlation, so this returns None instead.
    """
    v = np.asarray(x, dtype=np.float64).reshape(-1)
    n = v.size
    if lag <= 0 or lag >= n - 1:
        return None
    var = float(((v - v.mean()) ** 2).sum())
    if var <= 0.0 or not math.isfinite(var):
        return None
    c = v - v.mean()
    return float((c[:n - lag] * c[lag:]).sum() / var)


def dominant_period(x, lags=range(2, 33)):
    """Lag with the highest autocorrelation, or None when the series has no variance.

    Returns `(lag, r)`. Only lags with a defined autocorrelation are considered.
    """
    scored = [(lag, autocorr(x, lag)) for lag in lags]
    scored = [(lag, r) for lag, r in scored if r is not None]
    if not scored:
        return None
    return max(scored, key=lambda t: t[1])


def sine_amplitude(x) -> float:
    """Amplitude of a sinusoid recovered from its standard deviation (A = sqrt(2) * std).

    Exact for a pure sine sampled over whole periods; the harness checks the recovery error.
    """
    v = np.asarray(x, dtype=np.float64).reshape(-1)
    return float(math.sqrt(2.0) * v.std())


# ── translation vs lighting change ──────────────────────────────────────────

def estimate_shift(a: np.ndarray, b: np.ndarray, max_shift: int = 4):
    """Integer (dy, dx) that best explains `b` as a translation of `a` (minimum SAD).

    Only the interior that both frames cover is compared, so a shift never wins by moving the
    comparison window onto fewer pixels.
    """
    A = np.asarray(a, dtype=np.float64)
    B = np.asarray(b, dtype=np.float64)
    if A.shape != B.shape:
        raise ValueError('frames must have the same shape')
    m = max_shift
    if A.shape[0] <= 2 * m + 1 or A.shape[1] <= 2 * m + 1:
        raise ValueError('frame too small for the requested max_shift')
    core = B[m:B.shape[0] - m, m:B.shape[1] - m]
    best, best_sad = (0, 0), None
    for dy in range(-m, m + 1):
        for dx in range(-m, m + 1):
            shifted = A[m + dy:A.shape[0] - m + dy, m + dx:A.shape[1] - m + dx]
            sad = float(np.abs(shifted - core).mean())
            # Ties go to the smaller displacement: a direction the content does not vary along
            # (a vertical edge has no vertical structure) is degenerate, and inventing motion in
            # it would credit translation for a change that never happened.
            if best_sad is None or sad < best_sad - 1e-12 or (
                    abs(sad - best_sad) <= 1e-12 and (abs(dy) + abs(dx)) < (abs(best[0]) + abs(best[1]))):
                best, best_sad = (dy, dx), sad
    return best, best_sad


def motion_compensated_residual(a: np.ndarray, b: np.ndarray, shift, mask: np.ndarray = None) -> dict:
    """Residual between `b` and `a` translated by `shift = (dy, dx)`, over the shared interior.

    `explained_fraction` is 1 - residual/raw: how much of the raw frame-to-frame change is
    explained by pure translation. Close to 1 means the pixels changed because the image moved,
    not because the lighting changed.
    """
    A = np.asarray(a, dtype=np.float64)
    B = np.asarray(b, dtype=np.float64)
    dy, dx = shift
    m = max(abs(dy), abs(dx))
    if m == 0:
        core_a, core_b = A, B
        sub_mask = mask
    else:
        core_b = B[m:B.shape[0] - m, m:B.shape[1] - m]
        core_a = A[m + dy:A.shape[0] - m + dy, m + dx:A.shape[1] - m + dx]
        sub_mask = None if mask is None else mask[m:mask.shape[0] - m, m:mask.shape[1] - m]
    raw_b = B[m:B.shape[0] - m, m:B.shape[1] - m] if m else B
    raw_a = A[m:A.shape[0] - m, m:A.shape[1] - m] if m else A
    resid = masked(np.abs(core_b - core_a), sub_mask)
    raw = masked(np.abs(raw_b - raw_a), sub_mask)
    raw_mean = float(raw.mean()) if raw.size else float('nan')
    res_mean = float(resid.mean()) if resid.size else float('nan')
    return {
        'shift': (int(dy), int(dx)),
        'samples': int(resid.size),
        'raw_mean_abs_diff': raw_mean,
        'residual_mean_abs_diff': res_mean,
        'explained_fraction': float('nan') if not raw_mean else float(1.0 - res_mean / raw_mean),
    }


def classify_change(a: np.ndarray, b: np.ndarray, max_shift: int = 4,
                    explained_threshold: float = 0.9, mask: np.ndarray = None) -> dict:
    """Classifies the change between two frames as `translation`, `lighting` or `none`.

    A change is `translation` when a single rigid shift explains at least `explained_threshold`
    of it. This is the guard that stops a moving edge from being reported as lighting flicker.
    """
    raw = frame_pair_stats(a, b, mask=mask)
    if raw['mean_abs_diff'] == 0.0:
        return {'kind': 'none', **raw}
    shift, _ = estimate_shift(a, b, max_shift)
    mc = motion_compensated_residual(a, b, shift, mask)
    kind = 'translation' if mc['explained_fraction'] >= explained_threshold else 'lighting'
    return {'kind': kind, **raw, **mc}


# ── repeatability ───────────────────────────────────────────────────────────

def repeatability(values) -> dict:
    """Spread of one metric across independent repeats of the same condition.

    `half_width_95` is the half-width of a normal 95 % interval of the mean; a difference between
    two conditions smaller than this is not distinguishable from run-to-run noise.
    """
    v = np.asarray(values, dtype=np.float64).reshape(-1)
    n = v.size
    if n == 0:
        return {'runs': 0}
    mean = float(v.mean())
    sd = float(v.std(ddof=1)) if n > 1 else 0.0
    return {
        'runs': int(n),
        'mean': mean,
        'std': sd,
        'min': float(v.min()),
        'max': float(v.max()),
        'range': float(v.max() - v.min()),
        'half_width_95': float(1.96 * sd / math.sqrt(n)) if n > 1 else float('nan'),
        'relative_range': float((v.max() - v.min()) / mean) if mean else float('nan'),
    }
