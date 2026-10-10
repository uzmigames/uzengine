"""Before/after display artifacts for the candidate correction (followup-14 item 1.7).

Captures the same converged frame with the correction off and on, at every pose, and writes:

  frames/<pose>-<variant>.png      full-resolution 8-bit sRGB, exactly what the engine presented
  montage-<pose>.jpg               baseline | candidate | 8x amplified difference, ROIs outlined
  montage-rois-<pose>.jpg          each ROI cropped and enlarged, baseline above candidate

The amplified difference is labelled as such and uses a fixed gain, so two montages from different
runs can be compared with each other. Nothing here decides anything — the linear-radiance numbers
in `report.md` do — but a change that is invisible or that visibly darkens the image should be
seen, not only tabulated.

Usage: python scripts/rendering/make_correction_montage.py [--frame 300] [--out DIR]
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import runner  # noqa: E402
from run_correction_validation import BASE, CVAR, POSES, SCENE_START  # noqa: E402

DIFF_GAIN = 8.0
LABEL_H = 22


def draw_label(img, text, x, y):
    from PIL import ImageDraw
    d = ImageDraw.Draw(img)
    d.rectangle([x, y, x + 8 * len(text) + 8, y + LABEL_H], fill=(0, 0, 0))
    d.text((x + 4, y + 5), text, fill=(255, 255, 255))


def outline(img, rois, colour=(255, 220, 0)):
    from PIL import ImageDraw
    d = ImageDraw.Draw(img)
    for name, x, y, w, h in rois:
        d.rectangle([x, y, x + w - 1, y + h - 1], outline=colour, width=2)
        d.text((x + 3, max(0, y - 14)), name, fill=colour)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frame', type=int, default=SCENE_START + 300,
                    help='absolute frame to capture (well past TAA convergence)')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '14-validate-flicker-correction'))
    args = ap.parse_args()
    runner.require_exe()
    from PIL import Image

    frames_dir = os.path.join(args.out, 'frames')
    os.makedirs(frames_dir, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_correction_montage')

    for pose_key, pose in POSES.items():
        shots = {}
        for vname, exponent in (('baseline', 1.0), ('candidate', 0.0)):
            res = runner.run_capture(os.path.join(work, f'{pose_key}_{vname}'),
                                     os.path.join(runner.REPO, 'scripts', 'rendering', pose['project']),
                                     args.frame + 8,
                                     cvars={**BASE, **pose.get('cvars', {}), CVAR: f'{exponent:g}'},
                                     screenshot_frames=f'{args.frame}-{args.frame}',
                                     timing_first=args.frame - 50)
            if res['exit_code'] != 0:
                raise SystemExit(f'{pose_key} {vname}: engine exited {res["exit_code"]}')
            src = os.path.join(res['frames_dir'], f'f_{args.frame}.png')
            dst = os.path.join(frames_dir, f'{pose_key}-{vname}.png')
            shutil.copy(src, dst)
            shots[vname] = np.asarray(Image.open(dst).convert('RGB')).astype(np.int16)
            print(f'  {pose_key} {vname}: {dst}', flush=True)

        diff = np.clip(np.abs(shots['candidate'] - shots['baseline']) * DIFF_GAIN, 0, 255)
        panels = [shots['baseline'], shots['candidate'], diff]
        labels = ['baseline (exponent 1.0)', 'candidate (exponent 0.0)',
                  f'|difference| x{DIFF_GAIN:g}']
        h, w, _ = panels[0].shape
        sheet = Image.new('RGB', (w, h * len(panels)))
        for i, (p, lab) in enumerate(zip(panels, labels)):
            tile = Image.fromarray(p.astype(np.uint8))
            outline(tile, pose['rois'])
            draw_label(tile, f'{pose_key}: {lab}', 8, 8)
            sheet.paste(tile, (0, i * h))
        sheet = sheet.resize((w // 2, (h * len(panels)) // 2), Image.LANCZOS)
        path = os.path.join(args.out, f'montage-{pose_key}.jpg')
        sheet.save(path, quality=88)
        print(f'  wrote {path}', flush=True)

        # per-ROI crops at 2x, baseline above candidate above the amplified difference
        scale, pad = 2, 8
        crops = []
        for name, x, y, cw, ch in pose['rois']:
            col = []
            for p in panels:
                tile = Image.fromarray(p.astype(np.uint8)[y:y + ch, x:x + cw])
                col.append(tile.resize((cw * scale, ch * scale), Image.NEAREST))
            crops.append((name, col))
        sheet_w = sum(c[1][0].width + pad for c in crops) + pad
        sheet_h = max(sum(t.height for t in c[1]) + 3 * pad + LABEL_H for c in crops)
        roi_sheet = Image.new('RGB', (sheet_w, sheet_h), (24, 24, 24))
        ox = pad
        for name, col in crops:
            oy = pad + LABEL_H
            for tile in col:
                roi_sheet.paste(tile, (ox, oy))
                oy += tile.height + pad
            draw_label(roi_sheet, name, ox, pad // 2)
            ox += col[0].width + pad
        path = os.path.join(args.out, f'montage-rois-{pose_key}.jpg')
        roi_sheet.save(path, quality=90)
        print(f'  wrote {path}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
