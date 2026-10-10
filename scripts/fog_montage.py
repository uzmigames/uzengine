"""JPEG montage of fog captures: one tile per capture (tonemapped `taa_output` from the frame-wide
`components` dump), optional per-pixel ratio tile. Lighting block B, item 4.

Usage: python scripts/fog_montage.py OUT.jpg DIR[:label] [DIR[:label] ...] [--frame N] [--scale S]
  DIR is a capture directory with region/ (frame-wide dump); --scale downsamples (default 2).
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
import regiondump  # noqa: E402


def tonemap(c, exposure=1.0):
    x = np.clip(c * exposure, 0.0, None)
    x = x / (1.0 + x)
    return (np.clip(x, 0.0, 1.0) ** (1.0 / 2.2) * 255.0).astype(np.uint8)


def load(work, frame):
    fr = regiondump.load_sequence(os.path.join(work, 'region'), frame, frame)[0]
    return fr.rect('FRAME').channel('taa_output')[..., :3].astype(np.float64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('captures', nargs='+')
    ap.add_argument('--frame', type=int, default=60)
    ap.add_argument('--scale', type=int, default=2)
    ap.add_argument('--exposure', type=float, default=1.0)
    args = ap.parse_args()
    tiles = []
    for spec in args.captures:
        work, _, label = spec.partition(':')
        c = load(work, args.frame)[::args.scale, ::args.scale]
        img = Image.fromarray(tonemap(c, args.exposure))
        ImageDraw.Draw(img).text((8, 8), label or os.path.basename(work.rstrip('/\\')), fill=(255, 255, 0))
        tiles.append(img)
    w, h = tiles[0].size
    cols = min(len(tiles), 2)
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * w, rows * h))
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % cols) * w, (i // cols) * h))
    sheet.save(args.out, quality=88)
    print('wrote', args.out, sheet.size)


if __name__ == '__main__':
    main()
