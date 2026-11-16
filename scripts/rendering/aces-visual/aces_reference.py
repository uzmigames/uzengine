"""Tone curves for phase2_aces-hill-specular-occlusion 2.2.1 and 2.2.3.

`aces_full_srgb` is a vectorised port of the Academy reference CTL at aces-dev tag v1.3
(github.com/ampas/aces-dev, transforms/ctl): RRT.ctl followed by
ODT.Academy.sRGB_100nits_dim.ctl, with ACESlib.RRT_Common, ACESlib.ODT_Common,
ACESlib.Tonescales and ACESlib.Utilities_Color. Linear Rec.709 input is brought to ACES2065-1
through XYZ and a Bradford D65 -> D60 adaptation (CTL `calculate_cat_matrix` default).
The output is the ODT's display code value (sRGB-encoded, clamped to [0, 1]).

`hill`, `narkowicz`, `tonemap_input` and `linear_to_srgb` mirror the shipped shader code
(common/color.hlsli `ACESHillFit` / `ACESFilmic` / `LinearToSRGB`, postprocess/tonemap_ps.hlsl).
`falcor_aces` is Falcor's ToneMapper `Aces` operator (Source/RenderPasses/ToneMapper/
ToneMapping.ps.slang `toneMapAces`): the Narkowicz fit after a 0.6 pre-scale.
"""
from __future__ import annotations

import numpy as np

# ── Colour spaces (ACESlib.Utilities_Color.ctl) ───────────────────────────────────────────────────
AP0 = ((0.73470, 0.26530), (0.00000, 1.00000), (0.00010, -0.07700), (0.32168, 0.33767))
AP1 = ((0.713, 0.293), (0.165, 0.830), (0.128, 0.044), (0.32168, 0.33767))
REC709 = ((0.64, 0.33), (0.30, 0.60), (0.15, 0.06), (0.3127, 0.3290))
# CONE_RESP_MAT_BRADFORD, transposed from CTL's row-vector form to column-vector form.
BRADFORD = np.array([[0.8951, 0.2664, -0.1614],
                     [-0.7502, 1.7135, 0.0367],
                     [0.0389, -0.0685, 1.0296]])
HALF_MIN = 5.96046448e-08
HALF_MAX = 65504.0


def _xyz(xy, big_y=1.0):
    x, y = xy
    return np.array([x * big_y / y, big_y, (1.0 - x - y) * big_y / y])


def rgb_to_xyz(prims):
    """Normalised primary matrix, XYZ = M @ rgb (CTL `RGBtoXYZ(prims, 1.0)`)."""
    p = np.stack([_xyz(prims[0]), _xyz(prims[1]), _xyz(prims[2])], axis=1)
    return p * np.linalg.solve(p, _xyz(prims[3]))


def cat(src_white, dst_white):
    """Von Kries adaptation in Bradford cone space (CTL `calculate_cat_matrix`)."""
    s, d = BRADFORD @ _xyz(src_white), BRADFORD @ _xyz(dst_white)
    return np.linalg.inv(BRADFORD) @ np.diag(d / s) @ BRADFORD


AP0_TO_XYZ, AP1_TO_XYZ, REC709_TO_XYZ = rgb_to_xyz(AP0), rgb_to_xyz(AP1), rgb_to_xyz(REC709)
XYZ_TO_AP0, XYZ_TO_AP1, XYZ_TO_REC709 = (np.linalg.inv(m) for m in (AP0_TO_XYZ, AP1_TO_XYZ, REC709_TO_XYZ))
AP0_TO_AP1 = XYZ_TO_AP1 @ AP0_TO_XYZ
AP1_TO_AP0 = XYZ_TO_AP0 @ AP1_TO_XYZ
AP1_RGB2Y = AP1_TO_XYZ[1]
D65_TO_D60 = cat(REC709[3], AP0[3])
D60_TO_D65 = cat(AP0[3], REC709[3])
REC709_TO_AP0 = XYZ_TO_AP0 @ D65_TO_D60 @ REC709_TO_XYZ
REC709_TO_AP1 = XYZ_TO_AP1 @ D65_TO_D60 @ REC709_TO_XYZ


def _apply(m, rgb):
    return rgb @ m.T


def sat_matrix(factor):
    """CTL `calc_sat_adjust_matrix(factor, AP1_RGB2Y)` in column-vector form."""
    return factor * np.eye(3) + (1.0 - factor) * np.outer(np.ones(3), AP1_RGB2Y)


RRT_SAT = sat_matrix(0.96)
ODT_SAT = sat_matrix(0.93)

# ── Tonescales (ACESlib.Tonescales.ctl) ───────────────────────────────────────────────────────────
_B = np.array([[0.5, -1.0, 0.5], [-1.0, 1.0, 0.5], [0.5, 0.0, 0.0]])


def _spline(x, coefs_low, coefs_high, lo, mid, hi, slope_low, slope_high, knots):
    logx = np.log10(np.maximum(x, HALF_MIN))
    lmin, lmid, lmax = np.log10(lo[0]), np.log10(mid[0]), np.log10(hi[0])
    out = np.empty_like(logx)
    below = logx <= lmin
    out[below] = logx[below] * slope_low + (np.log10(lo[1]) - slope_low * lmin)
    above = logx >= lmax
    out[above] = logx[above] * slope_high + (np.log10(hi[1]) - slope_high * lmax)
    for coefs, start, end, sel in ((coefs_low, lmin, lmid, (logx > lmin) & (logx < lmid)),
                                   (coefs_high, lmid, lmax, (logx >= lmid) & (logx < lmax))):
        kc = (knots - 1) * (logx[sel] - start) / (end - start)
        j = kc.astype(np.int64)
        t = kc - j
        cf = np.stack([np.asarray(coefs)[j + k] for k in range(3)], axis=-1) @ _B
        out[sel] = t * t * cf[:, 0] + t * cf[:, 1] + cf[:, 2]
    return np.power(10.0, out)


RRT_PARAMS = dict(
    coefs_low=(-4.0000000000, -4.0000000000, -3.1573765773, -0.4852499958, 1.8477324706, 1.8477324706),
    coefs_high=(-0.7185482425, 2.0810307172, 3.6681241237, 4.0000000000, 4.0000000000, 4.0000000000),
    lo=(0.18 * 2.0 ** -15, 0.0001), mid=(0.18, 4.8), hi=(0.18 * 2.0 ** 18, 10000.0),
    slope_low=0.0, slope_high=0.0, knots=4)


def c5_fwd(x):
    return _spline(np.asarray(x, np.float64), **RRT_PARAMS)


ODT_48NITS = dict(
    coefs_low=(-1.6989700043, -1.6989700043, -1.4779000000, -1.2291000000, -0.8648000000,
               -0.4480000000, 0.0051800000, 0.4511080334, 0.9113744414, 0.9113744414),
    coefs_high=(0.5154386965, 0.8470437783, 1.1358000000, 1.3802000000, 1.5197000000,
                1.5985000000, 1.6467000000, 1.6746091357, 1.6878733390, 1.6878733390),
    lo=(float(c5_fwd(np.array([0.18 * 2.0 ** -6.5]))[0]), 0.02),
    mid=(float(c5_fwd(np.array([0.18]))[0]), 4.8),
    hi=(float(c5_fwd(np.array([0.18 * 2.0 ** 6.5]))[0]), 48.0),
    slope_low=0.0, slope_high=0.04, knots=8)


def c9_fwd(x):
    return _spline(np.asarray(x, np.float64), **ODT_48NITS)


# ── RRT (RRT.ctl + ACESlib.RRT_Common.ctl) ────────────────────────────────────────────────────────
def _saturation(rgb):
    mx, mn = rgb.max(-1), rgb.min(-1)
    return (np.maximum(mx, 1e-10) - np.maximum(mn, 1e-10)) / np.maximum(mx, 1e-2)


def _yc(rgb, radius_weight=1.75):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    chroma = np.sqrt(np.maximum(b * (b - g) + g * (g - r) + r * (r - b), 0.0))
    return (b + g + r + radius_weight * chroma) / 3.0


def hue_degrees(rgb):
    """CTL `rgb_2_hue`: geometric hue in [0, 360); NaN for neutral triplets."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    hue = np.degrees(np.arctan2(np.sqrt(3.0) * (g - b), 2.0 * r - g - b))
    hue = np.where(hue < 0.0, hue + 360.0, hue)
    return np.where((r == g) & (g == b), np.nan, hue)


def _sigmoid_shaper(x):
    t = np.maximum(1.0 - np.abs(x / 2.0), 0.0)
    return (1.0 + np.sign(x) * (1.0 - t * t)) / 2.0


def _glow_fwd(yc, gain, mid):
    return np.where(yc <= 2.0 / 3.0 * mid, gain, np.where(yc >= 2.0 * mid, 0.0, gain * (mid / np.maximum(yc, 1e-30) - 0.5)))


def _cubic_basis_shaper(x, w):
    knot = (x + w / 2.0) * 4.0 / w
    inside = (x > -w / 2.0) & (x < w / 2.0)
    j = np.where(inside, np.floor(np.where(inside, knot, 0.0)), -1)
    t = np.where(inside, knot - j, 0.0)
    y = np.select([j == 3, j == 2, j == 1, j == 0],
                  [(-t ** 3 + 3 * t ** 2 - 3 * t + 1) / 6.0, (3 * t ** 3 - 6 * t ** 2 + 4) / 6.0,
                   (-3 * t ** 3 + 3 * t ** 2 + 3 * t + 1) / 6.0, t ** 3 / 6.0], 0.0)
    return y * 1.5


def _center_hue(hue, center):
    c = hue - center
    return np.where(c < -180.0, c + 360.0, np.where(c > 180.0, c - 360.0, c))


def rrt(aces):
    aces = np.array(aces, np.float64)
    saturation = _saturation(aces)
    s = _sigmoid_shaper((saturation - 0.4) / 0.2)
    aces = aces * (1.0 + _glow_fwd(_yc(aces), 0.05 * s, 0.08))[..., None]
    hue = hue_degrees(aces)
    weight = np.nan_to_num(_cubic_basis_shaper(_center_hue(hue, 0.0), 135.0), nan=0.0)
    aces[..., 0] = aces[..., 0] + weight * saturation * (0.03 - aces[..., 0]) * (1.0 - 0.82)
    rgb_pre = np.clip(_apply(AP0_TO_AP1, np.maximum(aces, 0.0)), 0.0, HALF_MAX)
    rgb_pre = _apply(RRT_SAT, rgb_pre)
    return _apply(AP1_TO_AP0, c5_fwd(rgb_pre))


# ── ODT (ODT.Academy.sRGB_100nits_dim.ctl + ACESlib.ODT_Common.ctl) ────────────────────────────────
def _dark_to_dim(linear_cv):
    xyz = _apply(AP1_TO_XYZ, linear_cv)
    div = xyz.sum(-1)
    div = np.where(div == 0.0, 1e-10, div)
    x, y = xyz[..., 0] / div, xyz[..., 1] / div
    big_y = np.power(np.maximum(xyz[..., 1], 0.0), 0.9811)
    ydiv = np.maximum(y, 1e-10)
    xyz = np.stack([x * big_y / ydiv, big_y, (1.0 - x - y) * big_y / ydiv], axis=-1)
    return _apply(XYZ_TO_AP1, xyz)


def moncurve_r(y, gamma=2.4, offs=0.055):
    yb = (offs * gamma / ((gamma - 1.0) * (1.0 + offs))) ** gamma
    rs = ((gamma - 1.0) / offs) ** (gamma - 1.0) * ((1.0 + offs) / gamma) ** gamma
    return np.where(y >= yb, (1.0 + offs) * np.power(np.maximum(y, 0.0), 1.0 / gamma) - offs, y * rs)


def odt_srgb100_dim(oces):
    rgb_post = c9_fwd(_apply(AP0_TO_AP1, oces))
    cinema_black = 10.0 ** np.log10(0.02)
    linear_cv = (rgb_post - cinema_black) / (48.0 - cinema_black)
    linear_cv = _apply(ODT_SAT, _dark_to_dim(linear_cv))
    xyz = _apply(D60_TO_D65, _apply(AP1_TO_XYZ, linear_cv))
    return moncurve_r(np.clip(_apply(XYZ_TO_REC709, xyz), 0.0, 1.0))


def aces_full_srgb(rec709_linear):
    """Academy RRT + sRGB 100-nit dim-surround ODT; returns display code values in [0, 1]."""
    return odt_srgb100_dim(rrt(_apply(REC709_TO_AP0, np.asarray(rec709_linear, np.float64))))


# ── Shipped engine curves and Falcor's operator ───────────────────────────────────────────────────
HILL_IN = np.array([[0.59719, 0.35458, 0.04823], [0.07600, 0.90834, 0.01566], [0.02840, 0.13383, 0.83777]])
HILL_OUT = np.array([[1.60475, -0.53108, -0.07367], [-0.10208, 1.10813, -0.00605], [-0.00327, -0.07276, 1.07602]])


def hill(x):
    """common/color.hlsli `ACESHillFit` (r.TonemapOperator 1): linear display RGB in [0, 1]."""
    v = _apply(HILL_IN, np.asarray(x, np.float64))
    a = v * (v + 0.0245786) - 0.000090537
    b = v * (0.983729 * v + 0.4329510) + 0.238081
    return np.clip(_apply(HILL_OUT, a / b), 0.0, 1.0)


def narkowicz(x):
    """common/color.hlsli `ACESFilmic` (r.TonemapOperator 2)."""
    x = np.asarray(x, np.float64)
    return np.clip((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0)


def falcor_aces(x):
    """Falcor ToneMapper `Aces`: `color *= 0.6` then the Narkowicz fit (no clamp-free variant)."""
    return narkowicz(0.6 * np.asarray(x, np.float64))


def linear_to_srgb(c):
    """common/color.hlsli `LinearToSRGB` (IEC 61966-2-1)."""
    c = np.asarray(c, np.float64)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.maximum(c, 0.0), 1.0 / 2.4) - 0.055)


def srgb_to_linear(c):
    c = np.asarray(c, np.float64)
    return np.where(c <= 0.04045, c / 12.92, np.power((c + 0.055) / 1.055, 2.4))


def tonemap_input(hdr, exposure, saturation=1.1, contrast=1.0):
    """The colour tonemap_ps.hlsl hands to its curve: exposure, saturation, contrast (no bloom)."""
    c = np.asarray(hdr, np.float64) * exposure
    luma = c @ np.array([0.2126, 0.7152, 0.0722])
    c = np.maximum(luma[..., None] + saturation * (c - luma[..., None]), 0.0)
    return np.power(np.maximum(c / 0.18, 0.0), contrast) * 0.18


def psnr(a, b, peak=1.0):
    mse = float(np.mean((np.asarray(a, np.float64) - np.asarray(b, np.float64)) ** 2))
    return float('inf') if mse == 0.0 else 10.0 * np.log10(peak * peak / mse)


def selfcheck() -> dict:
    """Pins the port to values the CTL itself defines, before any engine data is used."""
    knots = {
        'c5(0.18)': (float(c5_fwd(np.array([0.18]))[0]), 4.8),
        'c5(0.18*2^-15)': (float(c5_fwd(np.array([0.18 * 2.0 ** -15]))[0]), 1e-4),
        'c5(0.18*2^18)': (float(c5_fwd(np.array([0.18 * 2.0 ** 18]))[0]), 1e4),
        'c9(c5(0.18*2^-6.5))': (float(c9_fwd(np.array([ODT_48NITS['lo'][0]]))[0]), 0.02),
        'c9(c5(0.18))': (float(c9_fwd(np.array([ODT_48NITS['mid'][0]]))[0]), 4.8),
        'c9(c5(0.18*2^6.5))': (float(c9_fwd(np.array([ODT_48NITS['hi'][0]]))[0]), 48.0),
    }
    for name, (got, want) in knots.items():
        if abs(got / want - 1.0) > 1e-6:
            raise AssertionError(f'{name} = {got}, CTL defines {want}')
    # Hill's published matrices are sRGB->XYZ->D65_2_D60->AP1->RRT_SAT and ODT_SAT->XYZ->D60_2_D65->sRGB,
    # printed to 5 decimals: rebuilding them from the CTL primaries, Bradford CAT and saturation
    # matrices pins the colour plumbing this port shares with the reference.
    hill_in = RRT_SAT @ REC709_TO_AP1
    hill_out = XYZ_TO_REC709 @ D60_TO_D65 @ AP1_TO_XYZ @ ODT_SAT
    err_in, err_out = float(np.abs(hill_in - HILL_IN).max()), float(np.abs(hill_out - HILL_OUT).max())
    if err_in > 1e-5 or err_out > 1e-5:
        raise AssertionError(f'colour plumbing disagrees with Hill matrices: {err_in}, {err_out}')
    grey = np.array([[g, g, g] for g in (0.01, 0.18, 1.0, 4.0, 16.0)])
    return {
        'tonescale_knots': {k: v[0] for k, v in knots.items()},
        'hill_matrix_max_abs_error': {'input': err_in, 'output': err_out},
        'grey_ramp_display_code_values': {
            f'{g[0]:g}': {'aces_full': float(aces_full_srgb(g[None])[0, 0]),
                          'hill': float(linear_to_srgb(hill(g[None]))[0, 0]),
                          'narkowicz': float(linear_to_srgb(narkowicz(g[None]))[0, 0]),
                          'falcor_aces': float(linear_to_srgb(falcor_aces(g[None]))[0, 0])}
            for g in grey},
    }


if __name__ == '__main__':
    import json
    print(json.dumps(selfcheck(), indent=2))
