"""H-31 step 2b: the whole ray set, by parity — coverage, weights and contribution.

One ray's direction says nothing about what the other seven do, so this reads all eight: where they
point, how much each carries, and how the structure of the sequence becomes a difference in
illumination. Nothing in the engine changes; the records are read from an ordinary capture.

  Completeness and weights, first and as a gate. The diffuse estimator is a plain mean — the cosine
  PDF cancels the cosine and pi — so every ray's weight is exactly 1/count, and the eight recorded
  contributions must average to the recorded trace output up to one constant factor, `rcIntensity`.
  That factor is not recorded, so it is recovered as the ratio and required to be the SAME constant
  at every pixel and every frame. If the ratio is not constant, the records are not the rays the
  shader traced, or the weights are not uniform, and nothing below can be read.

  Coverage. Elevation relative to the surface, and the angular spread within the set. A rotation of
  a cosine-weighted sequence should move the azimuths and leave the elevation distribution alone; a
  difference in elevation or in spread would mean the parity changes how well the hemisphere is
  covered, not merely which way the set is turned.

  How structure becomes light. Per pixel, `b` is the contribution-weighted mean direction pooled
  over the whole sequence — the direction the radiance actually comes from, estimated from the data
  rather than assumed. `s(t)` is the mean alignment of that frame's set with `b`. If the parity
  alternation is the sample set turning toward and away from the bright side, then s separates by
  parity and predicts the contribution; the regression of the region-mean contribution on the
  region-mean s reports how much it accounts for.

  What this cannot do: it cannot evaluate radiance in a direction that was never traced, so it
  describes the relationship between where the rays pointed and what they returned. It does not by
  itself exclude a term that co-varies with parity elsewhere.

Usage: python scripts/rendering/run_gather_ray_study.py [--frames 128]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
JITTER_PERIOD = 8
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': JITTER_PERIOD}
ROI = ('shadow_wall', 150, 260, 64, 64)
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
RAYS = 8
RATIO_CONSTANT_TOLERANCE = 1e-4   # relative spread of the recovered rcIntensity


def luma(rgb):
    return rgb @ REC709


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '21-gather-rays'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()

    name, x0, y0, w, h = ROI
    last = FIRST + args.frames - 1
    work = os.path.join(args.out, 'capture')
    print(f'H-31 step 2b — the whole ray set, frames {FIRST}..{last}, ROI {ROI}')
    if not args.reuse:
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject'),
                                 last - SCENE_START + 8, regions=[ROI], region_fields='gather',
                                 region_first=FIRST, region_last=last, cvars=BASE,
                                 timing_first=FIRST, clean=True)
        if res['exit_code'] != 0:
            raise RuntimeError(f'engine exited {res["exit_code"]}')
    seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, last)
    frames = np.array([f.frame for f in seq])
    ys, xs = np.arange(0, h, 2), np.arange(0, w, 2)

    def ch(f, cname):
        return f.rect(name).channel(cname)[np.ix_(ys, xs)]

    # (T, gh, gw, RAYS, 4): direction xyz and contribution luminance
    rays = np.stack([np.stack([ch(f, f'gather_ray_{i}') for i in range(RAYS)], axis=-2)
                     for f in seq]).astype(np.float64)
    trace = np.stack([ch(f, 'gather_trace') for f in seq]).astype(np.float64)
    geo = np.stack([ch(f, 'gather_geo_normal') for f in seq]).astype(np.float64)
    reported = trace[..., 3]

    directions = rays[..., :3]
    contributions = rays[..., 3]

    # ---- gate: completeness and uniform weights ---------------------------
    print(f'\n  rays recorded per pixel-frame: '
          f'{sorted(set(reported.ravel().astype(int).tolist()))}')
    mean_contribution = contributions.mean(axis=-1)
    out_luma = luma(trace[..., :3])
    usable = mean_contribution > 1e-9
    recovered = out_luma[usable] / mean_contribution[usable]
    spread = float(recovered.std() / max(abs(recovered.mean()), 1e-12))
    print(f'  recovered scale factor (rcIntensity): mean {recovered.mean():.6f}, '
          f'relative spread {spread:.3e} over {usable.sum()} pixel-frames')
    if spread > RATIO_CONSTANT_TOLERANCE:
        raise SystemExit(
            f'the ratio of the trace output to the mean ray contribution is not constant '
            f'(relative spread {spread:.3e} > {RATIO_CONSTANT_TOLERANCE:.0e}). Either the recorded '
            'rays are not the set that was traced or the weights are not uniform; nothing below '
            'would be readable, so nothing is reported.')
    print(f'  gate — the eight contributions average to the trace output up to one constant, '
          f'so the set is complete and every ray carries 1/{RAYS}')

    odd, even = frames % 2 == 1, frames % 2 == 0

    # ---- coverage ----------------------------------------------------------
    # the surface axis: the geometric normal, normalised, per pixel (constant enough to serve)
    axis = geo[..., :3].mean(axis=0)
    axis /= np.maximum(np.linalg.norm(axis, axis=-1, keepdims=True), 1e-12)
    elevation = np.einsum('tijrk,ijk->tijr', directions, axis)     # cos of angle to the normal
    # angular spread inside one set: mean pairwise cosine
    gram = np.einsum('tijrk,tijsk->tijrs', directions, directions)
    iu = np.triu_indices(RAYS, k=1)
    pair_cos = gram[..., iu[0], iu[1]].mean(axis=-1)

    def by_parity(a):
        return float(a[odd].mean()), float(a[even].mean())

    e_odd, e_even = by_parity(elevation.mean(axis=-1))
    s_odd, s_even = by_parity(pair_cos)
    print(f'\n  coverage, mean over pixels and frames:')
    print(f'    mean elevation cos(theta) to the normal   odd {e_odd:.4f}   even {e_even:.4f}   '
          f'difference {abs(e_odd - e_even):.4f}')
    print(f'    mean pairwise cosine within the set       odd {s_odd:.4f}   even {s_even:.4f}   '
          f'difference {abs(s_odd - s_even):.4f}')

    # centroid of the set, and the angle between the two parities' centroids
    centroid = directions.mean(axis=-2)
    c_odd = centroid[odd].mean(axis=0)
    c_even = centroid[even].mean(axis=0)
    cn_odd = c_odd / np.maximum(np.linalg.norm(c_odd, axis=-1, keepdims=True), 1e-12)
    cn_even = c_even / np.maximum(np.linalg.norm(c_even, axis=-1, keepdims=True), 1e-12)
    angle = np.degrees(np.arccos(np.clip((cn_odd * cn_even).sum(axis=-1), -1, 1)))
    print(f'    angle between the parities\' mean directions: median {np.median(angle):.1f} deg, '
          f'5th/95th {np.percentile(angle, 5):.1f}/{np.percentile(angle, 95):.1f}')

    # ---- per-ray contribution ---------------------------------------------
    print(f'\n  contribution by ray index (region mean of the luminance each ray returned):')
    print(f'    {"ray":>4}{"odd":>14}{"even":>14}{"odd/even":>11}')
    per_ray = []
    for i in range(RAYS):
        o, e = by_parity(contributions[..., i])
        per_ray.append({'ray': i, 'odd': o, 'even': e})
        print(f'    {i:>4}{o:>14.6e}{e:>14.6e}{(o / e if e else float("inf")):>11.3f}')

    # ---- how the structure becomes light ----------------------------------
    weighted = (directions * contributions[..., None]).sum(axis=(0, 3))
    b = weighted / np.maximum(np.linalg.norm(weighted, axis=-1, keepdims=True), 1e-12)
    alignment = np.einsum('tijrk,ijk->tijr', directions, b).mean(axis=-1)
    a_odd, a_even = by_parity(alignment)
    series_a = alignment.mean(axis=(1, 2))
    series_c = mean_contribution.mean(axis=(1, 2))
    sep_a = bool(series_a[odd].min() > series_a[even].max()
                 or series_a[even].min() > series_a[odd].max())
    corr = float(np.corrcoef(series_a, series_c)[0, 1])
    slope, intercept = np.polyfit(series_a, series_c, 1)
    pred = slope * series_a + intercept
    r2 = float(1.0 - ((series_c - pred) ** 2).sum() / ((series_c - series_c.mean()) ** 2).sum())
    print(f'\n  alignment of the set with the direction the radiance comes from:')
    print(f'    mean alignment      odd {a_odd:+.4f}   even {a_even:+.4f}')
    print(f'    separates by parity in the region-mean series: {sep_a}')
    print(f'    correlation with the region-mean contribution: {corr:+.4f}  (R^2 {r2:.4f})')

    report = {
        'first_frame': FIRST, 'frames': args.frames, 'roi': ROI, 'rays': RAYS,
        'scale_factor': {'mean': float(recovered.mean()), 'relative_spread': spread},
        'coverage': {'elevation_odd': e_odd, 'elevation_even': e_even,
                     'pairwise_cosine_odd': s_odd, 'pairwise_cosine_even': s_even,
                     'centroid_angle_deg_median': float(np.median(angle))},
        'per_ray': per_ray,
        'alignment': {'odd': a_odd, 'even': a_even, 'separates_by_parity': sep_a,
                      'correlation': corr, 'r_squared': r2},
    }
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, 'ray_study.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n  -> {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
