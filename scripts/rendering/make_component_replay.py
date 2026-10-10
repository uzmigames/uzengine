"""Builds a component-replay file from a reference run's region dump.

The isolated H-28 condition needs AO and specular to keep the values the *reference* had at the
matching frame and pixel, while the gather is frozen. A single frozen frame would not do: it would
add a second temporal change and confound the separation. So the reference's own per-frame recording
is packed into a buffer the deferred lighting pass indexes by frame and pixel.

Layout (float4 elements, matching `SceneRenderer::EnsureComponentReplay`):

    [0] rect x, y, w, h
    [1] firstFrame, frameCount, 0, 0
    [2..] frameCount * h * w entries of (specular.rgb, ao)

Usage: python scripts/rendering/make_component_replay.py --raw DIR --roi shadow_wall --out FILE
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw', required=True, help='reference run region dump directory')
    ap.add_argument('--roi', required=True)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()

    files = sorted(int(n.split('_')[1].split('.')[0]) for n in os.listdir(args.raw)
                   if n.startswith('region_'))
    if files != list(range(files[0], files[0] + len(files))):
        raise SystemExit('the reference dump has gaps; the replay must be frame-contiguous')
    seq = regiondump.load_sequence(args.raw, files[0], files[-1])
    rects = [r for r in seq[0].rects if r.name == args.roi]
    if not rects:
        raise SystemExit(f'rect {args.roi!r} not in the dump')
    rect0 = rects[0]
    x, y, w, h = rect0.x, rect0.y, rect0.w, rect0.h

    spec = np.stack([f.rect(args.roi).channel('specular_indirect')[..., :3] for f in seq])
    ao = np.stack([f.rect(args.roi).channel('diffuse_indirect')[..., 3] for f in seq])
    data = np.concatenate([spec.astype(np.float32),
                           ao.astype(np.float32)[..., None]], axis=-1)   # (T, h, w, 4)

    header = np.zeros((2, 4), dtype=np.float32)
    header[0] = (x, y, w, h)
    header[1] = (files[0], len(seq), 0, 0)
    out = np.concatenate([header, data.reshape(-1, 4)], axis=0)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    out.astype(np.float32).tofile(args.out)
    print(f'wrote {args.out}: rect ({x},{y},{w},{h}), frames {files[0]}..{files[-1]} '
          f'({len(seq)}), {out.nbytes / 1e6:.1f} MB')
    print(f'  specular mean {spec.mean():.4e}, ao mean {ao.mean():.4f}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
