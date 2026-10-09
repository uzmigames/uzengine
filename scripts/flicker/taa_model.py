"""Offline model of `taa_resolve_cs.hlsl`, plus analytic fixtures and a converged reference.

The model exists so a reconstruction, clamp or blend variant can be changed **one factor at a
time** against a signal whose correct answer is known analytically, instead of comparing one
rendered frame with the next. It is validated against the GPU on captured records
(`scripts/validate_taa_model.py`): the filtered colour, the clamp and the blend it predicts must
reproduce what the shader wrote.

Conventions, matching the shader:

* working space is the 4x-scaled YCoCg of Karis 2014 (`Y = R + 2G + B`);
* the plus filter weights each tap by `exp(-spatial_mul * |offset - jitter|^2)` times the HDR
  weight `(1 / (Y * exposure + 4)) ** hdr_weight_exponent`, then normalises (the exponent is
  `r.TAA.ReconstructionHdrWeightExponent`; 1 is the production Karis 2014 weighting, 0 removes it);
* the clamp box is the 3x3 AABB intersected with the variance box `mu +- gamma * sigma`;
* the blend is `WeightedLerpFactors(historyWeight, filterWeight, blend)`.

`jitter` is in pixels, positive x to the right and positive y down, exactly as the shader's
`jitterOffset` (which is subtracted from the tap offsets).
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

OFFSETS_3X3 = np.array([(-1, -1), (0, -1), (1, -1),
                        (-1, 0), (0, 0), (1, 0),
                        (-1, 1), (0, 1), (1, 1)], dtype=np.float64)
PLUS_INDICES = (1, 3, 4, 5, 7)


def rgb_to_ycocg(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    return np.stack([r + 2 * g + b, 2 * r - 2 * b, -r + 2 * g - b], axis=-1)


def ycocg_to_rgb(ycocg):
    y, co, cg = ycocg[..., 0] * 0.25, ycocg[..., 1] * 0.25, ycocg[..., 2] * 0.25
    return np.stack([y + co - cg, y + cg, y - co - cg], axis=-1)


def hdr_weight(y, exposure):
    return 1.0 / (y * exposure + 4.0)


@dataclass
class TaaParams:
    """The shader's CVar-driven constants (defaults match the engine defaults)."""
    current_frame_weight: float = 0.04
    variance_clip_gamma: float = 1.25
    anti_flicker_max: float = 0.3
    spatial_weight_mul: float = 2.29
    exposure_scale: float = 1.0
    velocity_threshold: float = 40.0
    hdr_weight_exponent: float = 1.0  # r.TAA.ReconstructionHdrWeightExponent
    reconstruction_mode: int = 0      # r.TAA.ReconstructionMode
    clamp_mode: int = 0               # r.TAA.ClampMode
    blend_mode: int = 0               # r.TAA.BlendMode


@dataclass
class ResolveResult:
    filtered: np.ndarray              # YCoCg
    clip_min: np.ndarray
    clip_max: np.ndarray
    history_in: np.ndarray            # YCoCg, before the clamp
    clamped: np.ndarray               # YCoCg
    blend: float
    output: np.ndarray                # YCoCg
    weights: np.ndarray = field(default_factory=lambda: np.zeros(0))


def reconstruct(neighbors_ycocg, jitter, p: TaaParams):
    """The shader's reconstruction step. `neighbors_ycocg` is (9, 3) in raster order."""
    n = np.asarray(neighbors_ycocg, dtype=np.float64)
    if p.reconstruction_mode == 2:
        return n[4].copy(), np.array([1.0])
    idx = range(9) if p.reconstruction_mode == 3 else PLUS_INDICES
    weights, taps = [], []
    for i in idx:
        off = OFFSETS_3X3[i] - np.asarray(jitter, dtype=np.float64)
        spatial = np.exp(-p.spatial_weight_mul * float(off @ off))
        hw = (1.0 if p.reconstruction_mode == 1
              else hdr_weight(n[i, 0], p.exposure_scale) ** p.hdr_weight_exponent)
        weights.append(spatial * hw)
        taps.append(n[i])
    w = np.asarray(weights)
    return (np.asarray(taps) * w[:, None]).sum(axis=0) / w.sum(), w / w.sum()


def clamp_box(neighbors_ycocg, p: TaaParams):
    n = np.asarray(neighbors_ycocg, dtype=np.float64)
    aabb_min, aabb_max = n.min(axis=0), n.max(axis=0)
    mu = n.mean(axis=0)
    sigma = np.sqrt(np.abs((n * n).mean(axis=0) - mu * mu))
    var_min, var_max = mu - p.variance_clip_gamma * sigma, mu + p.variance_clip_gamma * sigma
    if p.clamp_mode == 1:
        return np.full(3, -1e30), np.full(3, 1e30)
    if p.clamp_mode == 2:
        return var_min, var_max
    if p.clamp_mode == 3:
        return aabb_min, aabb_max
    return np.maximum(aabb_min, var_min), np.minimum(aabb_max, var_max)


def resolve(neighbors_ycocg, history_rgb, jitter, p: TaaParams, velocity_px=0.0,
            off_screen=False) -> ResolveResult:
    """One pixel of the TAA resolve. `neighbors_ycocg` (9, 3), `history_rgb` (3,)."""
    filtered, w = reconstruct(neighbors_ycocg, jitter, p)
    clip_min, clip_max = clamp_box(neighbors_ycocg, p)
    history = rgb_to_ycocg(np.asarray(history_rgb, dtype=np.float64))
    clamped = np.clip(history, clip_min, clip_max)

    blend = p.current_frame_weight
    if p.blend_mode == 0:
        blend = blend + (0.2 - blend) * min(1.0, velocity_px / p.velocity_threshold)
        luma_diff = abs(filtered[0] - clamped[0])
        anti = min(1.0, max(0.0, 0.01 * clamped[0] / max(luma_diff, 0.001)))
        blend = max(blend, min(anti, p.anti_flicker_max))
    if off_screen:
        clamped = filtered.copy()

    fw = hdr_weight(filtered[0], p.exposure_scale)
    hw = hdr_weight(clamped[0], p.exposure_scale)
    a, b = (1.0 - blend) * hw, blend * fw
    inv = 1.0 / (a + b)
    out = clamped * (a * inv) + filtered * (b * inv)
    return ResolveResult(filtered, clip_min, clip_max, history, clamped, blend, out, w)


# ── analytic fixtures ───────────────────────────────────────────────────────

def bar_signal(x, bar_centre, bar_width, bright, dark):
    """Continuous 1-D signal: a bright bar of width `bar_width` centred at `bar_centre`."""
    return np.where(np.abs(x - bar_centre) <= bar_width * 0.5, bright, dark)


def sample_row(width, jitter_x, *, bar_centre, bar_width, bright, dark, subsamples=64):
    """Exact box-filtered samples of the bar for one pixel row, offset by `jitter_x` pixels.

    A rasteriser integrates the signal over the pixel footprint; the jitter moves the footprint,
    which is what makes a static high-contrast edge change from frame to frame. `subsamples` sets
    the accuracy of that integral (64 sub-samples is exact to ~1e-2 of a pixel of edge position).
    """
    centres = np.arange(width) + 0.5 + jitter_x
    offs = (np.arange(subsamples) + 0.5) / subsamples - 0.5
    xs = centres[:, None] + offs[None, :]
    return bar_signal(xs, bar_centre, bar_width, bright, dark).mean(axis=1)


def converged_reference(width, *, bar_centre, bar_width, bright, dark, subsamples=256):
    """The zero-jitter, fully converged image: the box integral over each pixel footprint.

    This is the answer a temporal supersampler should converge to, and it is what variants are
    compared against — never one rendered frame against the next.
    """
    return sample_row(width, 0.0, bar_centre=bar_centre, bar_width=bar_width,
                      bright=bright, dark=dark, subsamples=subsamples)


def run_sequence(width, jitters, *, bar_centre, bar_width, bright, dark, params: TaaParams,
                 frames=64, gray=True):
    """Runs the model over a row for `frames` frames, cycling `jitters`.

    Returns `(history_per_frame, raw_per_frame)` as (frames, width) luminance arrays. The signal is
    grey (R=G=B), so YCoCg Y = 4 * value and the RGB conversion is exact and reversible.
    """
    jitters = np.asarray(jitters, dtype=np.float64)
    hist = None
    out_hist, out_raw = [], []
    for f in range(frames):
        jx = float(jitters[f % len(jitters)][0])
        jy = float(jitters[f % len(jitters)][1])
        row = sample_row(width, jx, bar_centre=bar_centre, bar_width=bar_width,
                         bright=bright, dark=dark)
        if hist is None:
            hist = row.copy()
        new = np.empty_like(row)
        for x in range(width):
            # 3x3 neighbourhood of a 1-D signal: rows above and below repeat the same values.
            cols = np.clip([x - 1, x, x + 1], 0, width - 1)
            vals = row[cols]
            neighbors = np.stack([rgb_to_ycocg(np.full(3, vals[c]))
                                  for c in (0, 1, 2, 0, 1, 2, 0, 1, 2)])
            r = resolve(neighbors, np.full(3, hist[x]), (jx, jy), params)
            new[x] = ycocg_to_rgb(r.output)[0]
        hist = new
        out_hist.append(new.copy())
        out_raw.append(row.copy())
    return np.asarray(out_hist), np.asarray(out_raw)
