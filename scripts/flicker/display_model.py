"""Offline replay of the production display chain (`tonemap_ps.hlsl`).

Used to separate what the eye sees from what the renderer computes: the same linear HDR frame can be
replayed at several exposures, with or without bloom, without re-rendering anything. The replay is
validated against the engine's own PNG output by `scripts/run_display_study.py`; a claim about the
display is only made through a replay that matched.

Stage order, exactly as the shader:

    color  = hdr                       linear scene radiance (the TAA output)
    color += bloom * bloomIntensity    spatial, so it is replayed only when the bloom buffer is known
    color *= exposure                  "pre-exposed colour" — the first display-referred quantity
    luma   = Rec.709(color)
    color  = max(lerp(luma, color, saturation), 0)
    color  = pow(max(color / 0.18, 0), contrast) * 0.18
    color  = ACES fit (r.TonemapOperator: 1 = Hill, 2 = Narkowicz, 3 = Reinhard, 0 = none)
    color  = LinearToSRGB(color)       display code values in [0, 1]

Film grain, vignette, chromatic aberration and the 3D LUT are off by default (`r.FilmGrain`,
`r.Vignette` default 0, `r.Tonemap.ChromaAbScale` is not read by the live tonemap, no LUT bound);
the replay asserts that assumption instead of silently ignoring them.
"""
from __future__ import annotations

import numpy as np

REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
MIDGRAY = 0.18

ACES_INPUT = np.array([[0.59719, 0.35458, 0.04823],
                       [0.07600, 0.90834, 0.01566],
                       [0.02840, 0.13383, 0.83777]], dtype=np.float64)
ACES_OUTPUT = np.array([[1.60475, -0.53108, -0.07367],
                        [-0.10208, 1.10813, -0.00605],
                        [-0.00327, -0.07276, 1.07602]], dtype=np.float64)


def aces_hill_fit(rgb):
    x = rgb @ ACES_INPUT.T
    a = x * (x + 0.0245786) - 0.000090537
    b = x * (0.983729 * x + 0.432951) + 0.238081
    x = a / b
    return np.clip(x @ ACES_OUTPUT.T, 0.0, 1.0)


def aces_filmic(rgb):
    """Narkowicz 2015 fit (`r.TonemapOperator=2`)."""
    x = rgb
    return np.clip((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0)


def reinhard(rgb):
    return rgb / (1.0 + rgb)


TONEMAP_OPERATORS = {0: lambda c: c, 1: aces_hill_fit, 2: aces_filmic, 3: reinhard}


def linear_to_srgb(rgb):
    lo = rgb * 12.92
    hi = 1.055 * np.power(np.maximum(rgb, 0.0), 1.0 / 2.4) - 0.055
    return np.where(rgb <= 0.0031308, lo, hi)


def replay(hdr_rgb, *, exposure=1.0, saturation=1.0, contrast=1.0, operator=1,
           bloom_rgb=None, bloom_intensity=0.0):
    """Runs the display chain on linear HDR RGB and returns display code values in [0, 1]."""
    color = np.asarray(hdr_rgb, dtype=np.float64).copy()
    if bloom_rgb is not None and bloom_intensity > 0.0:
        color = color + np.asarray(bloom_rgb, dtype=np.float64) * bloom_intensity
    color *= exposure
    luma = color @ REC709
    color = np.maximum(luma[..., None] + (color - luma[..., None]) * saturation, 0.0)
    color = np.power(np.maximum(color / MIDGRAY, 0.0), contrast) * MIDGRAY
    color = TONEMAP_OPERATORS[operator](color)
    return np.clip(linear_to_srgb(color), 0.0, 1.0)


def to_code_values(display01):
    """Display values in [0, 1] to 8-bit code values, the way a PNG stores them."""
    return np.clip(np.rint(np.asarray(display01) * 255.0), 0, 255).astype(np.int32)


def stages(hdr_rgb, *, exposure=1.0, saturation=1.0, contrast=1.0, operator=1):
    """Every labelled stage of the chain, for a radiometric attribution table."""
    color = np.asarray(hdr_rgb, dtype=np.float64)
    out = {'linear_radiance': color.copy(),
           'linear_rec709_luminance': color @ REC709}
    pre = color * exposure
    out['pre_exposed'] = pre
    luma = pre @ REC709
    sat = np.maximum(luma[..., None] + (pre - luma[..., None]) * saturation, 0.0)
    con = np.power(np.maximum(sat / MIDGRAY, 0.0), contrast) * MIDGRAY
    out['after_contrast'] = con
    tm = TONEMAP_OPERATORS[operator](con)
    out['after_tonemap'] = tm
    disp = np.clip(linear_to_srgb(tm), 0.0, 1.0)
    out['display_01'] = disp
    out['display_code_values'] = to_code_values(disp)
    return out
