"""Before/after GPU regression for the E/pi screen-validity composite.

Capture before editing the shader, capture after rebuilding, then compare.
Uses the existing stamped region-dump reader rather than testing a copy of
the shader's arithmetic. All captures run serially at rendered frame 240.
"""
import argparse
import glob
import math
import os
import sys
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'flicker'))
import regiondump
import runner


PROJECTS = {'A': 'sponza-flicker-a.uzproject',
            'W': 'sponza-flicker-w.uzproject',
            'C': 'sponza-lighting-camera-c.uzproject'}
ROIS = {'A': [('rear_door', 960, 592, 224, 224),
              ('right_shadow', 1472, 464, 256, 256),
              ('lit_floor', 832, 816, 256, 64)],
        'W': [('window', 770, 150, 192, 512),
              ('shadow_wall', 150, 260, 192, 384),
              ('lit_wall', 350, 260, 192, 384)],
        'C': [('center', 832, 432, 256, 256)]}


def capture(root, stage):
    for pose, gather in [('A', 1), ('A', 0), ('W', 0), ('C', 0)]:
        label = f'{pose}-{"control" if gather else "screen"}'
        work = os.path.join(root, stage, label)
        os.makedirs(work, exist_ok=False)
        cvars = {'r.GI.FinalGather': gather, 'r.SSGI': 1,
                 'r.RadianceCache': 1, 'r.GI.Intensity': 1,
                 'r.AutoExposure': 0, 'r.Exposure': 1, 'r.Show.Stats': 0}
        result = runner.run_capture(
            work, os.path.join(runner.REPO, 'scripts', 'rendering', PROJECTS[pose]),
            248, regions=ROIS[pose], region_fields='components',
            region_first=240, region_last=240, cvars=cvars, clean=False,
            extra_args=('--hidden', '--screenshot', os.path.join(work, 'capture.png'),
                        '--screenshot-frame', '240'))
        if result['exit_code'] != 0:
            raise RuntimeError(f'{label}: exit {result["exit_code"]}')
        with open(result['log'], encoding='utf-8', errors='replace') as f:
            failures = [line.strip() for line in f if 'compile failed' in line.lower()]
        if failures:
            raise RuntimeError(f'{label}: {failures}')
        image = np.asarray(Image.open(os.path.join(work, 'capture.png')).convert('RGB'))
        if not image.max():
            raise RuntimeError(f'{label}: black screenshot')
        print(f'{stage} {label}: non-black, no compile failed', flush=True)


def psnr(a, b):
    mse = np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2)
    return math.inf if mse == 0 else 10 * math.log10(255 ** 2 / mse)


class BlendRenderTests(unittest.TestCase):
    root = None

    def pair(self, label):
        return [os.path.join(self.root, stage, label) for stage in ('before', 'after')]

    def frames(self, label):
        frames = [regiondump.RegionFrame(glob.glob(os.path.join(p, 'region', '*.bin'))[0])
                  for p in self.pair(label)]
        for frame in frames:
            self.assertTrue(frame.valid, frame.flags)
            self.assertEqual(frame.frame, 240)
        return frames

    def test_final_gather_control(self):
        images = [np.asarray(Image.open(os.path.join(p, 'capture.png')).convert('RGB'))
                  for p in self.pair('A-control')]
        value = psnr(*images)
        print(f'A-control PSNR={value:.6f} dB')
        self.assertGreater(value, 60)

    def test_screen_hit_indirect_drops(self):
        before, after = self.frames('A-screen')
        for name in ('rear_door', 'right_shadow'):
            old = before.rect(name).channel('diffuse_indirect')[..., :3]
            new = after.rect(name).channel('diffuse_indirect')[..., :3]
            screen = after.rect(name).channel('ssgi_consumed')
            self.assertTrue(np.any(screen[..., :3] > 0))
            self.assertTrue(np.any(screen[..., 3] == 2))
            print(f'{name}: indirect {old.mean():.9g} -> {new.mean():.9g}')
            self.assertLess(float(new.mean()), float(old.mean()))

    def test_three_pose_report(self):
        for pose in PROJECTS:
            label = f'{pose}-screen'
            images = [np.asarray(Image.open(os.path.join(p, 'capture.png')).convert('RGB'))
                      for p in self.pair(label)]
            print(f'{label} PSNR={psnr(*images):.6f} dB')
            before, after = self.frames(label)
            for name, *_ in ROIS[pose]:
                old = before.rect(name).channel('diffuse_indirect')[..., :3]
                new = after.rect(name).channel('diffuse_indirect')[..., :3]
                self.assertTrue(np.isfinite(new).all())
                print(f'{pose}/{name}: indirect {old.mean():.9g} -> {new.mean():.9g}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True)
    parser.add_argument('--capture', choices=('before', 'after'))
    args = parser.parse_args()
    if args.capture:
        capture(os.path.abspath(args.root), args.capture)
    else:
        BlendRenderTests.root = os.path.abspath(args.root)
        unittest.main(argv=[sys.argv[0]], verbosity=2)
