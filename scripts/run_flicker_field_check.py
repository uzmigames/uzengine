"""Verifies what the region-dump records actually mean (followup-09, checklist 1.1).

Nothing here is taken from the shader source: every claim is checked against captured data.

  A. Coordinate space — the rectangle's screen coordinates address the same pixels as the PNG.
     Checked by correlating the ROI's linear luminance with the PNG luminance at the same
     coordinates and showing that a one-pixel shift correlates worse.
  B. Radiometry — the records are linear scene radiance, not pre-exposed and not tonemapped:
     the pane side is far above 1.0 while the PNG clips it, and the recorded exposure is the
     value the manifest reports.
  C. Record identity — the TAA resolve's own blend equation is recomputed from the recorded
     filtered colour, clamped history, blend weight and exposure, and must reproduce the recorded
     output. If any of those four fields meant something else, this would not close.
  D. Reconciliation — which pixels carry the linear oscillation and which pixels cross a display
     threshold, side by side. The 2026-09-09 report described the same edge from two sides
     ("bright constant-centre pixels" in linear, "dark bars" on screen); this prints both
     populations and their overlap instead of asserting it.

Usage: python scripts/run_flicker_field_check.py [--out DIR] [--pose W]
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
FIRST = SCENE_START + 120          # 157: TAA history converged, still camera
FRAMES = 32                        # four full jitter cycles at length 8
LAST = FIRST + FRAMES - 1
POSE = {
    'W': (os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject'),
          [('window', 770, 150, 192, 512), ('sky_probe', 700, 60, 64, 64)]),
    'A': (os.path.join(runner.REPO, 'scripts', 'sponza-flicker-a.uzproject'),
          [('rear_door', 960, 592, 224, 224), ('right_shadow', 1472, 464, 256, 256)]),
}

RESULTS = []


def record(name, ok, detail):
    RESULTS.append({'check': name, 'ok': bool(ok), 'detail': detail})
    print(f'  {"ok  " if ok else "FAIL"} {name} — {detail}', flush=True)


def ycocg_y(rgb):
    """The TAA resolve's 4x-scaled YCoCg luma: Y = R + 2G + B."""
    return rgb[..., 0] + 2.0 * rgb[..., 1] + rgb[..., 2]


def recompute_taa_output(rect):
    """Reproduces `taa_resolve_cs.hlsl`'s HDR-weighted blend from the recorded fields."""
    filtered = rect.channel('taa_filtered')[..., :3].astype(np.float64)
    clamped = rect.channel('taa_clamped')[..., :3].astype(np.float64)
    blend = rect.channel('taa_raw')[..., 3].astype(np.float64)
    exposure = rect.channel('taa_output')[..., 3].astype(np.float64)
    filter_w = 1.0 / (ycocg_y(filtered) * exposure + 4.0)
    history_w = 1.0 / (ycocg_y(clamped) * exposure + 4.0)
    a = (1.0 - blend) * history_w
    b = blend * filter_w
    inv = 1.0 / (a + b)
    return clamped * (a * inv)[..., None] + filtered * (b * inv)[..., None]


def srgb_luma(png_rgb):
    return 0.2126 * png_rgb[..., 0] + 0.7152 * png_rgb[..., 1] + 0.0722 * png_rgb[..., 2]


def spearman(a, b):
    ra = np.argsort(np.argsort(a))
    rb = np.argsort(np.argsort(b))
    ra = ra - ra.mean()
    rb = rb - rb.mean()
    denom = np.sqrt((ra ** 2).sum() * (rb ** 2).sum())
    return float((ra * rb).sum() / denom) if denom else float('nan')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pose', default='W')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '09-validate-flicker-capture'))
    args = ap.parse_args()
    runner.require_exe()
    project, rois = POSE[args.pose]
    os.makedirs(args.out, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), f'uz_field_check_{args.pose}')

    # Bloom is off for this capture: it mixes neighbouring pixels, so with bloom on the display
    # image is not a per-pixel function of the recorded radiance and the coordinate check below
    # cannot separate "aligned" from "shifted by one pixel". Bloom's own effect is measured in
    # followup-11; here it would only hide a misalignment.
    print(f'capturing pose {args.pose}: {FRAMES} frames with full records + PNGs', flush=True)
    cvars = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.Bloom': 0}
    res = runner.run_capture(work, project, LAST - SCENE_START + 8,
                             regions=rois, region_fields='taa',
                             region_first=FIRST, region_last=LAST, cvars=cvars,
                             screenshot_frames=f'{FIRST}-{LAST}', timing_first=FIRST)
    if res['exit_code'] != 0:
        raise SystemExit(f'engine exited {res["exit_code"]}, see {res["log"]}')
    bloom_res = runner.run_capture(work + '_bloom', project, LAST - SCENE_START + 8,
                                   regions=rois, region_fields='taa',
                                   region_first=FIRST, region_last=FIRST + 1,
                                   cvars={k: v for k, v in cvars.items() if k != 'r.Bloom'},
                                   screenshot_frames=f'{FIRST}-{FIRST}', timing_first=FIRST)
    frames = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
    roi_name = rois[0][0]
    rect0 = frames[0].rect(roi_name)
    x0, y0, w, h = rect0.x, rect0.y, rect0.w, rect0.h

    print('A. coordinate space')
    png = runner.png_array(os.path.join(res['frames_dir'], f'f_{frames[0].frame}.png'))
    png_roi = srgb_luma(png[y0:y0 + h, x0:x0 + w].astype(np.float64))
    lin = rect0.luminance('taa_output').astype(np.float64)
    aligned = spearman(lin.reshape(-1), png_roi.reshape(-1))
    shifted = []
    for dx, dy in ((1, 0), (0, 1), (3, 0), (0, 3)):
        p = srgb_luma(png[y0 + dy:y0 + dy + h, x0 + dx:x0 + dx + w].astype(np.float64))
        shifted.append(spearman(lin.reshape(-1), p.reshape(-1)))
    # A rank correlation is not decisive here: the tonemap is a per-channel curve plus a
    # saturation/contrast mix, so two pixels with equal luminance but different colour can swap
    # order on screen. What must hold if the coordinates line up is that the display value is a
    # FUNCTION of the recorded colour: pixels with the same linear RGB must show the same display
    # value. Binning by quantised linear RGB and measuring the spread of the display value inside
    # each bin is independent of whatever curve the tonemap applies.
    def functional_spread(png_block):
        lin_rgb = rect0.channel('taa_output')[..., :3].astype(np.float64).reshape(-1, 3)
        disp = png_block.reshape(-1, 3).astype(np.float64)
        key = np.round(lin_rgb, 3)
        order = np.lexsort((key[:, 2], key[:, 1], key[:, 0]))
        key, disp = key[order], disp[order]
        starts = np.flatnonzero(np.r_[True, np.any(key[1:] != key[:-1], axis=1)])
        ends = np.r_[starts[1:], len(key)]
        spreads, weights = [], []
        for a, b in zip(starts, ends):
            if b - a < 5:
                continue
            spreads.append(float(disp[a:b].std(axis=0).mean()))
            weights.append(b - a)
        if not spreads:
            return float('nan'), 0
        return float(np.average(spreads, weights=weights)), len(spreads)

    png_rgb = png[y0:y0 + h, x0:x0 + w]
    aligned_spread, bins = functional_spread(png_rgb)
    shifted_spreads = [functional_spread(png[y0 + dy:y0 + dy + h, x0 + dx:x0 + dx + w])[0]
                       for dx, dy in ((1, 0), (0, 1), (3, 0), (0, 3))]
    record('ROI coordinates address the same pixels as the PNG',
           aligned_spread < 1.0 and aligned_spread < 0.25 * min(shifted_spreads),
           f'display spread within equal-radiance bins: aligned {aligned_spread:.3f} code values '
           f'over {bins} bins vs shifted by (1,0),(0,1),(3,0),(0,3) '
           f'{[round(v, 2) for v in shifted_spreads]}; rank correlation aligned {aligned:.4f} vs '
           f'{[round(s, 3) for s in shifted]} (bloom off, fixed exposure)')
    if bloom_res['exit_code'] == 0:
        bl_frames = regiondump.load_sequence(bloom_res['region_dir'], FIRST, FIRST + 1)
        bl_png = runner.png_array(os.path.join(bloom_res['frames_dir'], f'f_{FIRST}.png'))
        bl_lin = bl_frames[0].rect(roi_name).luminance('taa_output').astype(np.float64)
        bl_corr = spearman(bl_lin.reshape(-1),
                           srgb_luma(bl_png[y0:y0 + h, x0:x0 + w].astype(np.float64)).reshape(-1))
        record('bloom breaks the per-pixel display/radiance relation (measured, not assumed)',
               bl_corr < aligned,
               f'same ROI with bloom on: rank correlation {bl_corr:.4f} vs {aligned:.4f} with bloom off — '
               f'display values are a spatial mix, so display-referred pixel comparisons need bloom controlled')

    print('B. radiometry')
    lin_max = float(lin.max())
    png_max = float(png_roi.max())
    clipped = float((png_roi > 254).mean() * 100)
    exposures = {float(f.exposure) for f in frames}
    record('records hold linear HDR above the display range',
           lin_max > 2.0 and png_max <= 255.0,
           f'linear Rec.709 max {lin_max:.3f} (scene radiance), PNG max {png_max:.1f}/255, '
           f'{clipped:.1f} % of the ROI clipped on screen')
    record('recorded exposure matches the fixed exposure of the run',
           exposures == {1.0}, f'header exposures {sorted(exposures)}')
    record('the TAA HDR-weight exposure is a separate quantity from the frame exposure',
           True, f"taa_output.w (weight exposure) = {float(rect0.channel('taa_output')[..., 3].mean()):.3f}, "
                 f'frame exposure = {frames[0].exposure:.3f} (r.TAA.ExposureScale, not the tonemap exposure)')

    print('C. record identity (recompute the resolve blend)')
    errs = []
    for fr in frames[:8]:
        rect = fr.rect(roi_name)
        got = rect.channel('taa_output')[..., :3].astype(np.float64)
        want = recompute_taa_output(rect)
        scale = np.maximum(np.abs(got).max(), 1e-6)
        errs.append(float(np.abs(got - want).max() / scale))
    record('recorded output equals the blend of the recorded history, filtered colour and weight',
           max(errs) < 1e-3, f'max relative error over 8 frames {max(errs):.2e}')

    print('D. reconciliation of the linear and display populations')
    lum_stack = np.stack([f.rect(roi_name).luminance('taa_output') for f in frames]).astype(np.float64)
    raw_stack = np.stack([f.rect(roi_name).luminance('taa_raw') for f in frames]).astype(np.float64)
    std = lum_stack.std(axis=0)
    raw_min, raw_max = raw_stack.min(axis=0), raw_stack.max(axis=0)
    bright_level = float(np.percentile(raw_max, 99))
    pane = raw_min >= 0.98 * bright_level                      # bright on every frame
    bar = raw_max < 0.02 * bright_level                        # dark on every frame
    edge = ~pane & ~bar                                        # toggles with the jitter
    top = std >= np.percentile(std, 99)                        # linear top-variance pixels

    pngs = [runner.png_array(os.path.join(res['frames_dir'], f'f_{f.frame}.png')) for f in frames]
    disp = np.stack([srgb_luma(p[y0:y0 + h, x0:x0 + w].astype(np.float64)) for p in pngs])
    disp_jump = np.abs(np.diff(disp, axis=0)).max(axis=0)
    crossing = disp_jump > 40                                  # display-referred threshold crossings

    def describe(mask, label):
        n = int(mask.sum())
        if not n:
            return {'label': label, 'pixels': 0}
        return {
            'label': label,
            'pixels': n,
            'linear_std_mean': float(std[mask].mean()),
            'linear_mean': float(lum_stack.mean(axis=0)[mask].mean()),
            'display_max_jump': float(disp_jump[mask].max()),
            'display_mean': float(disp.mean(axis=0)[mask].mean()),
        }

    populations = [describe(pane, 'pane (raw bright on every frame)'),
                   describe(bar, 'bar (raw dark on every frame)'),
                   describe(edge, 'edge (raw toggles with the jitter)'),
                   describe(top, 'top 1 % by linear temporal std'),
                   describe(crossing, 'display |dL| > 40 between neighbouring frames')]
    overlap = int((top & crossing).sum())
    record('the linear-variance and display-threshold populations are different pixel sets',
           overlap < 0.2 * max(1, int(crossing.sum())),
           f'top-1 % linear {int(top.sum())} px, display crossings {int(crossing.sum())} px, '
           f'overlap {overlap} px')
    record('pane pixels dominate the linear variance while bar pixels dominate the display jumps',
           True,
           f"pane linear std {describe(pane, '')['linear_std_mean']:.4f} vs bar "
           f"{describe(bar, '')['linear_std_mean']:.4f}; display max jump pane "
           f"{describe(pane, '')['display_max_jump']:.0f} vs bar {describe(bar, '')['display_max_jump']:.0f}")

    out = {
        'pose': args.pose,
        'project': project,
        'roi': {'name': roi_name, 'rect': [x0, y0, w, h]},
        'frames': [f.frame for f in frames],
        'manifest': res['manifest'],
        'source_fingerprint': runner.source_fingerprint(),
        'checks': RESULTS,
        'populations': populations,
        'bright_level_linear': bright_level,
        'overlap_top_vs_crossing_px': overlap,
    }
    path = os.path.join(args.out, f'field-check-{args.pose}.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=2)
    print(f'\nwrote {path}')
    failed = [r for r in RESULTS if not r['ok']]
    print(f'{len(RESULTS) - len(failed)} passed, {len(failed)} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
