"""Reproduce the shader's full recurrence, then remove one mechanism at a time (H-25/26/27 redo).

The earlier screening measured *one-step sensitivities*: it swapped a factor but reused the recorded
history, so it could only say how much a change moves one frame, never how that change propagates
through the accumulation. Review finding (2026-09-10): "the clamp accounts for 11 % of the residual"
was not a claim that measurement could support, and H-26 was rejected by comparing an instantaneous
perturbation against a temporal standard deviation — different statistics, and a small perturbation
applied every frame can accumulate.

This replays the whole recurrence offline, including the parts the screening skipped:

* the **spatial history read** — the shader's 5-tap Catmull-Rom at `uv - motionVector`, with the
  bilinear taps it actually uses, fetched from the replay's own previous output rather than from the
  recorded history;
* the **effective HDR blend weights** — `WeightedLerpFactors(historyWeight, filterWeight, blend)`,
  whose result depends on the current and historical luminance, so the accumulator is *not* a
  constant-coefficient IIR;
* the **clamp** against the recorded box.

Nothing is concluded until the replay reproduces the recorded time series, checked frame by frame
and per pixel, not just at frame 0. The unit checks run first and are reported separately, so a
failure says which stage is wrong:

  history   the Catmull-Rom fetch from the recorded previous output vs the recorded history
  clamp     the recorded history clipped to the recorded box vs the recorded clamped history
  blend     the recorded clamped history blended with the recorded filtered colour vs the output
  recurrence  the whole loop, seeded once, run forward

Only then does each mechanism come out, one at a time, and the **steady-state** residual is measured
over the last full jitter cycle:

  exact history read   nearest-texel fetch instead of Catmull-Rom — the accumulated cost of
                       resampling, which is the intervention H-26 needed and did not have
  fixed clamp box      the box replaced by its temporal mean, propagated through the accumulation
  no clamp             the H-13 endpoint, as an upper bound

Raw dumps are kept (`--keep-raw`, on by default) so phase, harmonics and further interventions can
be recomputed without recapturing.

Usage: python scripts/rendering/run_full_recurrence_replay.py [--frames 64] [--no-keep-raw]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402
import taa_model as tm  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 200
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject')
#: The Catmull-Rom taps reach +-2 texels, and the linear taps land between texels, so the captured
#: rectangle carries a margin and only its interior is analysed.
MARGIN = 4
#: Sub-texel precision of the fixed-function bilinear unit (D3D minimum: 8 bits).
SUBTEXEL_STEPS = 256.0
ANALYSIS = (128, 256)
RECT = ('window', 800 - MARGIN, 200 - MARGIN, ANALYSIS[0] + 2 * MARGIN, ANALYSIS[1] + 2 * MARGIN)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}


def as_history(img):
    """The history texture is RGBA16Float (`scene_temporal_resources.cpp`), so every frame the
    output is quantised to half precision before the next frame reads it back. Omitting this left
    the Catmull-Rom unit check at 8.6e-04 — three orders of magnitude worse than the clamp and
    blend steps — and made the recurrence drift."""
    return np.asarray(img, dtype=np.float16).astype(np.float64)


def bilinear(img, sx, sy):
    """Sample `img` (h, w, 3) at pixel positions where texel centres sit at integer + 0.5."""
    h, w = img.shape[:2]
    px = np.clip(sx - 0.5, 0.0, w - 1.0)
    py = np.clip(sy - 0.5, 0.0, h - 1.0)
    x0 = np.floor(px).astype(np.int64)
    y0 = np.floor(py).astype(np.int64)
    x1 = np.minimum(x0 + 1, w - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    # D3D guarantees only 8 bits of sub-texel precision in the fixed-function bilinear unit, so
    # the hardware snaps the interpolation weight to 1/256. On a high-contrast edge that is worth
    # a few 1e-3 relative, which is exactly where this replay disagreed with the GPU (r = 0.80
    # against the spatial gradient, 10x larger on the top 1 % gradient pixels).
    fx = (np.round((px - x0) * SUBTEXEL_STEPS) / SUBTEXEL_STEPS)[..., None]
    fy = (np.round((py - y0) * SUBTEXEL_STEPS) / SUBTEXEL_STEPS)[..., None]
    return (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x1] * fx * (1 - fy)
            + img[y1, x0] * (1 - fx) * fy + img[y1, x1] * fx * fy)


def catmull_rom_5tap(img, sample_x, sample_y):
    """The shader's 5-tap Catmull-Rom, in pixel coordinates of `img`."""
    tcx = np.floor(sample_x - 0.5) + 0.5
    tcy = np.floor(sample_y - 0.5) + 0.5
    fx, fy = sample_x - tcx, sample_y - tcy

    def basis(f):
        f2, f3 = f * f, f * f * f
        w0 = f2 - 0.5 * (f3 + f)
        w1 = 1.5 * f3 - 2.5 * f2 + 1.0
        w3 = 0.5 * (f3 - f2)
        w2 = 1.0 - w0 - w1 - w3
        return w0, w1, w2, w3

    w0x, w1x, w2x, w3x = basis(fx)
    w0y, w1y, w2y, w3y = basis(fy)
    w12x, w12y = w1x + w2x, w1y + w2y
    s0x, s0y = tcx - 1.0, tcy - 1.0
    s1x = tcx + w2x / np.where(np.abs(w12x) < 1e-12, 1e-12, w12x)
    s1y = tcy + w2y / np.where(np.abs(w12y) < 1e-12, 1e-12, w12y)
    s2x, s2y = tcx + 2.0, tcy + 2.0

    weight0 = w12x * w0y
    weight1 = w0x * w12y
    weight2 = w12x * w12y
    weight3 = w3x * w12y
    weight4 = w12x * w3y

    acc = (bilinear(img, s1x, s0y) * weight0[..., None]
           + bilinear(img, s0x, s1y) * weight1[..., None]
           + bilinear(img, s1x, s1y) * weight2[..., None]
           + bilinear(img, s2x, s1y) * weight3[..., None]
           + bilinear(img, s1x, s2y) * weight4[..., None])
    total = weight0 + weight1 + weight2 + weight3 + weight4
    return acc / np.where(np.abs(total) < 1e-12, 1e-12, total)[..., None]


def blend_and_weights(clamped_rgb, filtered_rgb, blend, exposure):
    """The shader's WeightedLerpFactors blend; also returns the *effective* filtered fraction."""
    cy = tm.rgb_to_ycocg(clamped_rgb)[..., 0]
    fy = tm.rgb_to_ycocg(filtered_rgb)[..., 0]
    a = (1.0 - blend) / (cy * exposure + 4.0)
    b = blend / (fy * exposure + 4.0)
    inv = 1.0 / (a + b)
    wa, wb = a * inv, b * inv
    return clamped_rgb * wa[..., None] + filtered_rgb * wb[..., None], wb


def clamp_ycocg(rgb, lo_rgb, hi_rgb):
    return tm.ycocg_to_rgb(np.clip(tm.rgb_to_ycocg(rgb), tm.rgb_to_ycocg(lo_rgb),
                                   tm.rgb_to_ycocg(hi_rgb)))


def luma(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def rel_err(a, b):
    return float(np.abs(a - b).max() / max(1e-9, float(np.abs(b).max())))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=64)
    ap.add_argument('--no-keep-raw', action='store_true')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '15-isolate-clamp-bounds'))
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    raw_dir = os.path.join(args.out, 'raw', 'full-recurrence')
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)

    print('  ... capturing with a margin for the Catmull-Rom taps', flush=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_full_recurrence')
    res = runner.run_capture(work, PROJECT, last - SCENE_START + 8, regions=[RECT],
                             region_fields='taa+extra', region_first=FIRST, region_last=last,
                             cvars=BASE, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise SystemExit(f'engine exited {res["exit_code"]}')
    seq = regiondump.load_sequence(res['region_dir'], FIRST, last)
    rect = [f.rect(RECT[0]) for f in seq]
    g = lambda ch: np.stack([r.channel(ch)[..., :3] for r in rect]).astype(np.float64)  # noqa: E731
    filtered = g('taa_filtered')
    history_rec = g('taa_history')
    clamped_rec = g('taa_clamped')
    output_rec = g('taa_output')
    clip_min, clip_max = g('taa_clip_min'), g('taa_clip_max')
    blend = np.stack([r.channel('taa_raw')[..., 3] for r in rect]).astype(np.float64)
    exposure = np.stack([r.channel('taa_output')[..., 3] for r in rect]).astype(np.float64)
    vel_x = np.stack([r.channel('taa_filtered')[..., 3] for r in rect]).astype(np.float64)
    vel_y = np.stack([r.channel('taa_history')[..., 3] for r in rect]).astype(np.float64)

    shutil.copy(res['manifest_path'], os.path.join(args.out, 'manifests', 'full-recurrence.json'))
    if not args.no_keep_raw:
        if os.path.isdir(raw_dir):
            shutil.rmtree(raw_dir)
        shutil.copytree(res['region_dir'], raw_dir)
        print(f'      raw dumps kept in {raw_dir}', flush=True)
    shutil.rmtree(res['region_dir'], ignore_errors=True)

    h, w = filtered.shape[1:3]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    inner = (slice(MARGIN, h - MARGIN), slice(MARGIN, w - MARGIN))
    report = {'rect': RECT, 'margin': MARGIN, 'frames': args.frames, 'first_frame': FIRST,
              'checks': {}, 'interventions': {}}

    # ---------------------------------------------------------------- unit checks
    # The sample position the shader computes: pixel centre minus the velocity, in pixels.
    sx = xx[None] + 0.5 - vel_x
    sy = yy[None] + 0.5 - vel_y
    hist_pred = np.stack([catmull_rom_5tap(as_history(output_rec[i - 1]), sx[i], sy[i])
                          for i in range(1, len(seq))])
    report['checks']['history_catmull_rom'] = rel_err(hist_pred[(slice(None),) + inner],
                                                      history_rec[1:][(slice(None),) + inner])
    clamp_pred = clamp_ycocg(history_rec, clip_min, clip_max)
    report['checks']['clamp'] = rel_err(clamp_pred[(slice(None),) + inner],
                                        clamped_rec[(slice(None),) + inner])
    out_pred, wb = blend_and_weights(clamped_rec, filtered, blend, exposure)
    report['checks']['blend'] = rel_err(out_pred[(slice(None),) + inner],
                                        output_rec[(slice(None),) + inner])
    report['effective_alpha'] = {
        'median': float(np.median(wb[(slice(None),) + inner])),
        'mean': float(wb[(slice(None),) + inner].mean()),
        'p05': float(np.percentile(wb[(slice(None),) + inner], 5)),
        'p95': float(np.percentile(wb[(slice(None),) + inner], 95)),
        'blend_input_median': float(np.median(blend[(slice(None),) + inner])),
    }
    for k in ('history_catmull_rom', 'clamp', 'blend'):
        print(f"      unit check {k:20s} max relative error {report['checks'][k]:.2e}")
    a = report['effective_alpha']
    print(f"      effective blend weight (the IIR coefficient): median {a['median']:.4f}, "
          f"p05-p95 {a['p05']:.4f}-{a['p95']:.4f}, against a blendFinal of "
          f"{a['blend_input_median']:.4f}", flush=True)

    # ---------------------------------------------------------------- full recurrence
    def run(mode):
        img = output_rec[0].copy()
        outs = []
        lo_mean, hi_mean = clip_min.mean(axis=0), clip_max.mean(axis=0)
        for i in range(1, len(seq)):
            stored = as_history(img)
            if mode == 'exact_history':
                hist = stored
            elif mode == 'exact_history_full_precision':
                hist = img.copy()
            else:
                hist = catmull_rom_5tap(stored, sx[i], sy[i])
            if mode == 'no_clamp':
                cl = hist
            elif mode == 'fixed_box':
                cl = clamp_ycocg(hist, lo_mean, hi_mean)
            else:
                cl = clamp_ycocg(hist, clip_min[i], clip_max[i])
            img, _ = blend_and_weights(cl, filtered[i], blend[i], exposure[i])
            outs.append(img)
        return np.stack(outs)

    faithful = run('recorded')
    per_frame = [rel_err(faithful[i][inner], output_rec[i + 1][inner])
                 for i in range(len(faithful))]
    report['recurrence'] = {
        'max_relative_error': float(max(per_frame)),
        'final_relative_error': float(per_frame[-1]),
        'per_frame_relative_error': [float(v) for v in per_frame],
    }
    report['recurrence_valid'] = bool(max(per_frame) < 1e-3)
    print(f"      full recurrence: max relative error {max(per_frame):.2e} over {len(per_frame)} "
          f"frames, final {per_frame[-1]:.2e} "
          f"({'valid' if report['recurrence_valid'] else 'INVALID — interventions not run'})",
          flush=True)

    # ---------------------------------------------------------------- interventions
    if report['recurrence_valid']:
        cycle = 8
        for mode in ('recorded', 'exact_history', 'exact_history_full_precision',
                     'fixed_box', 'no_clamp'):
            r = run(mode) if mode != 'recorded' else faithful
            tail = luma(r[-cycle:])[(slice(None),) + inner]
            report['interventions'][mode] = {
                'steady_state_residual': float(tail.std(axis=0).mean()),
                'mean': float(tail.mean()),
            }
        base = report['interventions']['recorded']['steady_state_residual']
        for mode, ent in report['interventions'].items():
            ent['change_pct'] = (ent['steady_state_residual'] / base - 1) * 100 if base else None
            print(f"      {mode:16s} steady-state residual {ent['steady_state_residual']:.4e} "
                  f"({ent['change_pct']:+.1f} %)")
    else:
        print('      interventions skipped: an unfaithful replay cannot attribute anything')

    report['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'full-recurrence.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
