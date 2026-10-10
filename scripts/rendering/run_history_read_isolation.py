"""Separate a position error, a filtering difference and accumulated divergence (H-25/26/27).

The full-recurrence replay disagrees with the GPU on 5.6 % of samples, concentrated at high
gradients, and two candidate explanations (RGBA16Float history storage, 8-bit sub-texel
quantisation) failed to account for it. Review guidance (2026-09-10): the dilation was only a
hypothesis — and it turned out to be wrong, since the recorded `velocityPixels` is already loaded at
the dilated coordinate — so the difference must lie in the coordinate conversion, in numerical
precision, or in the filter's reproduction. Those are separable, in this order:

  1. coordinates   the sample position the replay reconstructs versus the one the shader recorded,
                   in pixels. If this is zero, the coordinate conversion is exonerated.
  2. isolated read the Catmull-Rom evaluated at the *recorded* position against the recorded
                   history, from the previous frame's recorded output at half precision. With the
                   position removed as a variable, what is left is filtering and precision.
  3. chained       only once the isolated read reproduces, run the 64-frame recurrence again and
                   apply the same 1e-3 threshold. That keeps accumulated divergence from being
                   confused with a per-frame error.

Each step gates the next, and each reports a distribution rather than a maximum, because a handful
of pixels failing is a different finding from the whole image failing.

Usage: python scripts/rendering/run_history_read_isolation.py [--frames 64]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
from run_full_recurrence_replay import (  # noqa: E402
    BASE, FIRST, MARGIN, PROJECT, RECT, SCENE_START, as_history, blend_and_weights,
    catmull_rom_5tap, clamp_ycocg, luma)


def dist(err, ref=None):
    e = np.asarray(err)
    if ref is not None:
        e = e / max(1e-9, float(np.abs(ref).max()))
    return {'mean': float(e.mean()), 'p50': float(np.percentile(e, 50)),
            'p99': float(np.percentile(e, 99)), 'max': float(e.max()),
            'fraction_above_1e_4': float((e > 1e-4).mean())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=64)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '15-isolate-clamp-bounds'))
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    raw_dir = os.path.join(args.out, 'raw', 'history-read')

    print('  ... capturing with the recorded history sample position', flush=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_history_read')
    res = runner.run_capture(work, PROJECT, last - SCENE_START + 8, regions=[RECT],
                             region_fields='taa+extra', region_first=FIRST, region_last=last,
                             cvars=BASE, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise SystemExit(f'engine exited {res["exit_code"]}')
    seq = regiondump.load_sequence(res['region_dir'], FIRST, last)
    rect = [f.rect(RECT[0]) for f in seq]
    g = lambda ch: np.stack([r.channel(ch)[..., :3] for r in rect]).astype(np.float64)  # noqa: E731
    filtered, history_rec = g('taa_filtered'), g('taa_history')
    output_rec = g('taa_output')
    clip_min, clip_max = g('taa_clip_min'), g('taa_clip_max')
    blend = np.stack([r.channel('taa_raw')[..., 3] for r in rect]).astype(np.float64)
    exposure = np.stack([r.channel('taa_output')[..., 3] for r in rect]).astype(np.float64)
    vel_x = np.stack([r.channel('taa_filtered')[..., 3] for r in rect]).astype(np.float64)
    vel_y = np.stack([r.channel('taa_history')[..., 3] for r in rect]).astype(np.float64)
    sample = np.stack([r.channel('taa_history_sample') for r in rect]).astype(np.float64)

    shutil.copy(res['manifest_path'], os.path.join(args.out, 'manifests', 'history-read.json'))
    if os.path.isdir(raw_dir):
        shutil.rmtree(raw_dir)
    shutil.copytree(res['region_dir'], raw_dir)
    shutil.rmtree(res['region_dir'], ignore_errors=True)
    print(f'      raw dumps kept in {raw_dir}', flush=True)

    h, w = filtered.shape[1:3]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    inner = (slice(MARGIN, h - MARGIN), slice(MARGIN, w - MARGIN))
    report = {'rect': RECT, 'frames': args.frames, 'first_frame': FIRST, 'steps': {}}

    # ---- step 1: coordinates -------------------------------------------------
    # The recorded position is in *global* pixels; the replay works in rectangle-local pixels.
    rec_px = sample[..., 0] - RECT[1]
    rec_py = sample[..., 1] - RECT[2]
    own_px = xx[None] + 0.5 - vel_x
    own_py = yy[None] + 0.5 - vel_y
    dx = np.abs(own_px - rec_px)[(slice(None),) + inner]
    dy = np.abs(own_py - rec_py)[(slice(None),) + inner]
    report['steps']['1_coordinates_px'] = {'x': dist(dx), 'y': dist(dy)}
    coord_ok = max(dx.max(), dy.max()) < 1e-3
    print(f"      1. coordinates: |dx| mean {dx.mean():.2e} max {dx.max():.2e}, "
          f"|dy| mean {dy.mean():.2e} max {dy.max():.2e} px "
          f"({'exonerated' if coord_ok else 'POSITION ERROR — this is the cause'})", flush=True)

    # ---- step 2: isolated read ----------------------------------------------
    if coord_ok:
        pred = np.stack([catmull_rom_5tap(as_history(output_rec[i - 1]), rec_px[i], rec_py[i])
                         for i in range(1, len(seq))])
        e = np.abs(pred - history_rec[1:])[(slice(None),) + inner].max(axis=-1)
        report['steps']['2_isolated_read'] = dist(e, history_rec[(slice(None),) + inner])
        st = report['steps']['2_isolated_read']
        read_ok = st['max'] < 1e-3
        print(f"      2. isolated read at the recorded position: mean {st['mean']:.2e}, "
              f"p99 {st['p99']:.2e}, max {st['max']:.2e}, above 1e-4 on "
              f"{st['fraction_above_1e_4'] * 100:.3f} % "
              f"({'reproduced' if read_ok else 'NOT reproduced — filtering or precision'})",
              flush=True)
    else:
        read_ok = False
        print('      2. skipped: the position differs, so a read comparison would confound the two')

    # ---- step 3: chained -----------------------------------------------------
    if read_ok:
        img = output_rec[0].copy()
        errs = []
        for i in range(1, len(seq)):
            hist = catmull_rom_5tap(as_history(img), rec_px[i], rec_py[i])
            cl = clamp_ycocg(hist, clip_min[i], clip_max[i])
            img, _ = blend_and_weights(cl, filtered[i], blend[i], exposure[i])
            errs.append(float(np.abs(img[inner] - output_rec[i][inner]).max()
                              / max(1e-9, float(np.abs(output_rec[i][inner]).max()))))
        report['steps']['3_chained'] = {'max_relative_error': float(max(errs)),
                                        'final': float(errs[-1]),
                                        'per_frame': [float(v) for v in errs]}
        report['chain_valid'] = bool(max(errs) < 1e-3)
        print(f"      3. chained over {len(errs)} frames: max {max(errs):.2e}, final {errs[-1]:.2e} "
              f"({'VALID — attribution can proceed' if report['chain_valid'] else 'invalid'})",
              flush=True)
    else:
        report['chain_valid'] = False
        print('      3. skipped: chaining an unreproduced read would only measure its accumulation')

    report['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'history-read-isolation.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
