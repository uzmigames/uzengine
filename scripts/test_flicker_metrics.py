"""Behavioural validation of the flicker measurement code (`scripts/flicker/`).

Every check is a synthetic sequence with a known answer, including the negative controls that
the 2026-09-08/09 packages got wrong:

* a constant sequence must report *undefined* autocorrelation, not 0 or 1;
* a sinusoid's amplitude and period must be recovered from the metric, not assumed;
* a pure translation of an edge must NOT be reported as a lighting change;
* a missing, duplicated or renumbered frame must fail the integrity check instead of silently
  changing the temporal statistics;
* a frame whose GPU stamp disagrees with the file must be rejected;
* subsampled counts must be shown to disagree with exact counts (why full resolution is required);
* masked samples must be excluded, not averaged in.

Run: python scripts/test_flicker_metrics.py    (or python -m unittest discover -s scripts)
"""
from __future__ import annotations

import math
import os
import struct
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import metrics  # noqa: E402
import regiondump  # noqa: E402


# ── helpers ─────────────────────────────────────────────────────────────────

def write_region_file(path, frame, rects, fields, pixel_fn, stamp_override=None,
                      flags=regiondump.FLAG_TAA_ACTIVE, jitter=(0.0, 0.0), exposure=1.0,
                      jitter_index=1, resolution=(1920, 1080), camera=None):
    """Writes a synthetic region dump. `pixel_fn(rect_name, y, x, record)` -> 4 floats.

    Version 2 by default; passing `camera=(viewProj, prevViewProj, cameraPos)` writes version 3.
    """
    rpp = regiondump.records_per_pixel(fields)
    version = 3 if camera is not None else 2
    header = struct.pack('<12I', regiondump.MAGIC_V2, version, frame, len(rects), fields, rpp, flags,
                         resolution[0], resolution[1], resolution[0], resolution[1], jitter_index)
    header += struct.pack('<3f', jitter[0], jitter[1], exposure) + struct.pack('<I', 0)
    assert len(header) == 64
    if camera is not None:
        vp, prev_vp, cam = camera
        header += (np.asarray(vp, dtype=np.float32).tobytes()
                   + np.asarray(prev_vp, dtype=np.float32).tobytes()
                   + np.asarray(list(cam) + [0.0], dtype=np.float32).tobytes())
        assert len(header) == 64 + 144
    rect_block, stamp_block, data = b'', b'', []
    base = 0
    for i, (name, x, y, w, h) in enumerate(rects):
        rect_block += struct.pack('<16s4I', name.encode(), x, y, w, h)
        stamp = frame if stamp_override is None else stamp_override
        stamp_block += struct.pack('<4I', stamp, stamp, 0, base)
        for py in range(h):
            for px in range(w):
                for rec in range(rpp):
                    data.append(pixel_fn(name, py, px, rec))
        data.extend([(float(stamp), resolution[0], resolution[1], 1.0)] * 4)  # footer
        base += w * h * rpp + 4
    payload = np.asarray(data, dtype=np.float32).tobytes()
    with open(path, 'wb') as f:
        f.write(header + rect_block + stamp_block + payload)


def edge_frame(h, w, edge_x, bright=8.0, dark=0.0):
    """A vertical edge: `bright` left of `edge_x`, `dark` from `edge_x` on."""
    img = np.full((h, w), dark, dtype=np.float64)
    img[:, :edge_x] = bright
    return img


# ── metric validation ───────────────────────────────────────────────────────

class ConstantSequence(unittest.TestCase):
    def test_zero_variance_autocorrelation_is_undefined(self):
        x = [3.5] * 64
        self.assertIsNone(metrics.autocorr(x, 8), 'a constant series has no correlation structure')
        self.assertIsNone(metrics.dominant_period(x))

    def test_constant_sequence_has_no_temporal_error(self):
        stack = np.full((32, 16, 16), 2.0)
        st = metrics.temporal_stats(stack)
        self.assertEqual(st['std_mean'], 0.0)
        self.assertEqual(st['abs_diff_mean'], 0.0)
        pair = metrics.frame_pair_stats(stack[0], stack[1], thresholds=(0.0, 0.01))
        self.assertEqual(pair['count_gt_0.0'], 0)
        self.assertEqual(pair['max_abs_diff'], 0.0)

    def test_classify_constant_pair_as_none(self):
        a = edge_frame(24, 24, 12)
        self.assertEqual(metrics.classify_change(a, a.copy())['kind'], 'none')


class SinusoidalSequence(unittest.TestCase):
    AMPLITUDE = 0.75
    PERIOD = 8
    FRAMES = 128

    def series(self, phase=0.0):
        return [self.AMPLITUDE * math.sin(2 * math.pi * (i + phase) / self.PERIOD) + 4.0
                for i in range(self.FRAMES)]

    def test_amplitude_is_recovered(self):
        got = metrics.sine_amplitude(self.series())
        self.assertAlmostEqual(got, self.AMPLITUDE, delta=0.02 * self.AMPLITUDE,
                               msg='amplitude must be recovered within 2 %')

    def test_period_is_recovered(self):
        lag, r = metrics.dominant_period(self.series())
        self.assertEqual(lag, self.PERIOD)
        self.assertGreater(r, 0.9)

    def test_period_of_a_different_length_is_not_confused(self):
        for period in (4, 6, 12, 16):
            series = [math.sin(2 * math.pi * i / period) for i in range(96)]
            lag, _ = metrics.dominant_period(series)
            self.assertEqual(lag, period, f'period {period} misidentified as {lag}')

    def test_temporal_std_matches_the_known_amplitude(self):
        stack = np.stack([np.full((8, 8), v) for v in self.series()])
        st = metrics.temporal_stats(stack)
        self.assertAlmostEqual(st['std_mean'] * math.sqrt(2.0), self.AMPLITUDE,
                               delta=0.02 * self.AMPLITUDE)


class TranslationVersusLighting(unittest.TestCase):
    def test_pure_translation_is_not_lighting_flicker(self):
        a = edge_frame(64, 64, 30)
        b = edge_frame(64, 64, 31)          # the same edge, one pixel to the right
        raw = metrics.frame_pair_stats(a, b, thresholds=(1.0,))
        self.assertGreater(raw['count_gt_1.0'], 0, 'the raw difference is large by construction')
        result = metrics.classify_change(a, b)
        self.assertEqual(result['kind'], 'translation')
        self.assertEqual(result['shift'], (0, -1))
        self.assertGreater(result['explained_fraction'], 0.99)

    def test_lighting_change_is_reported_as_lighting(self):
        a = edge_frame(64, 64, 30)
        b = edge_frame(64, 64, 30, bright=8.4)   # same geometry, brighter side changed
        result = metrics.classify_change(a, b)
        self.assertEqual(result['kind'], 'lighting')
        self.assertLess(result['explained_fraction'], 0.5)

    def test_translation_plus_lighting_is_reported_as_lighting(self):
        a = edge_frame(64, 64, 30)
        b = edge_frame(64, 64, 31, bright=9.6)
        self.assertEqual(metrics.classify_change(a, b)['kind'], 'lighting')


class ExactCountsAndMasks(unittest.TestCase):
    def test_counts_are_exact_and_unrounded(self):
        a = np.zeros((10, 10))
        b = np.zeros((10, 10))
        b[0, :7] = 0.5000001          # just above the threshold
        b[1, :3] = 0.5                # exactly at it: not counted (strictly greater)
        stats = metrics.frame_pair_stats(a, b, thresholds=(0.5,))
        self.assertEqual(stats['count_gt_0.5'], 7)

    def test_subsampling_disagrees_with_the_exact_count(self):
        rng = np.random.default_rng(7)
        a = np.zeros((64, 64))
        b = np.zeros((64, 64))
        idx = rng.choice(64 * 64, size=200, replace=False)
        b.reshape(-1)[idx] = 1.0
        exact = metrics.frame_pair_stats(a, b, thresholds=(0.5,))['count_gt_0.5']
        sub = metrics.frame_pair_stats(a[::2, ::2], b[::2, ::2], thresholds=(0.5,))['count_gt_0.5'] * 4
        self.assertEqual(exact, 200)
        self.assertNotEqual(sub, exact, 'a 2x2 estimate must not be presented as an exact count')

    def test_masked_samples_are_excluded(self):
        a = np.zeros((4, 4))
        b = np.zeros((4, 4))
        b[0, 0] = 10.0                       # invalid sample
        mask = np.ones((4, 4), dtype=bool)
        mask[0, 0] = False
        self.assertEqual(metrics.frame_pair_stats(a, b, mask=mask)['mean_abs_diff'], 0.0)
        self.assertEqual(metrics.frame_pair_stats(a, b)['samples'], 16)
        self.assertEqual(metrics.frame_pair_stats(a, b, mask=mask)['samples'], 15)

    def test_repeatability_reports_spread(self):
        r = metrics.repeatability([10.0, 10.4, 9.8])
        self.assertEqual(r['runs'], 3)
        self.assertAlmostEqual(r['range'], 0.6, places=6)
        self.assertGreater(r['half_width_95'], 0.0)


# ── region-dump integrity ───────────────────────────────────────────────────

class RegionDumpIntegrity(unittest.TestCase):
    RECTS = [('window', 100, 50, 4, 3), ('door', 700, 400, 2, 2)]
    FIELDS = regiondump.FIELD_LIGHTING | regiondump.FIELD_TAA

    def make_sequence(self, directory, frames, **kw):
        for n in frames:
            write_region_file(
                os.path.join(directory, f'region_{n}.bin'), n, self.RECTS, self.FIELDS,
                lambda name, y, x, rec, n=n: (float(n), float(y), float(x), float(rec)),
                jitter_index=(n % 8) + 1, **kw)

    def test_reads_records_rects_and_header(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_sequence(d, [10])
            fr = regiondump.load_frame(os.path.join(d, 'region_10.bin'))
            self.assertEqual(fr.frame, 10)
            self.assertTrue(fr.valid)
            self.assertTrue(fr.taa_active)
            self.assertEqual([r.name for r in fr.rects], ['window', 'door'])
            win = fr.rect('window')
            self.assertEqual((win.h, win.w), (3, 4))
            out = win.channel('taa_output')
            self.assertEqual(out.shape, (3, 4, 4))
            # pixel_fn encoded (frame, y, x, record); taa_output is record 2+4 = 6
            self.assertEqual(out[2, 3, 1], 2.0)
            self.assertEqual(out[2, 3, 2], 3.0)
            self.assertEqual(out[2, 3, 3], 6.0)
            self.assertEqual(fr.rect('door').channel('lighting_reflection')[1, 1, 3], 0.0)

    def test_missing_frame_is_detected(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_sequence(d, [10, 11, 13])
            report = regiondump.check_sequence(d)
            self.assertFalse(report['ok'])
            self.assertEqual(report['missing'], [12])
            with self.assertRaises(ValueError):
                regiondump.load_sequence(d)

    def test_range_shorter_than_requested_is_detected(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_sequence(d, [10, 11, 12])
            report = regiondump.check_sequence(d, first=10, last=15)
            self.assertFalse(report['ok'])
            self.assertEqual(report['missing'], [13, 14, 15])

    def test_stamp_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_sequence(d, [10, 11])
            write_region_file(os.path.join(d, 'region_12.bin'), 12, self.RECTS, self.FIELDS,
                              lambda *a: (0.0, 0.0, 0.0, 0.0), stamp_override=9,
                              flags=regiondump.FLAG_TAA_ACTIVE | regiondump.FLAG_TAA_MISMATCH)
            report = regiondump.check_sequence(d)
            self.assertFalse(report['ok'])
            self.assertEqual([f for f, _ in report['invalid']], [12])
            with self.assertRaises(ValueError):
                regiondump.load_sequence(d)

    def test_version_1_files_are_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'region_1.bin')
            with open(p, 'wb') as f:
                f.write(struct.pack('<5I2f', regiondump.MAGIC_V1, 1, 4, 4, 6, 0.0, 0.0) + b'\0' * 64)
            with self.assertRaises(ValueError):
                regiondump.load_frame(p)

    def test_clean_sequence_loads(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_sequence(d, list(range(20, 28)))
            report = regiondump.check_sequence(d)
            self.assertTrue(report['ok'], report)
            frames = regiondump.load_sequence(d)
            self.assertEqual([f.frame for f in frames], list(range(20, 28)))
            self.assertEqual([f.jitter_index for f in frames], [(n % 8) + 1 for n in range(20, 28)])

    def test_geometry_and_history_masks(self):
        with tempfile.TemporaryDirectory() as d:
            # lighting_normal.w (record 1) = -1 marks sky; taa_clamped.w (record 5) = 1 marks
            # a rejected history sample. Only pixel (0,0) is sky, only (0,1) lost its history.
            def pixels(name, y, x, rec):
                if rec == 1:
                    return (0.0, 0.0, 0.0, -1.0 if (y, x) == (0, 0) else 300.0)
                if rec == 5:
                    return (0.0, 0.0, 0.0, 1.0 if (y, x) == (0, 1) else 0.0)
                return (0.0, 0.0, 0.0, 0.0)
            write_region_file(os.path.join(d, 'region_1.bin'), 1, self.RECTS, self.FIELDS, pixels)
            win = regiondump.load_frame(os.path.join(d, 'region_1.bin')).rect('window')
            geo, hist = win.geometry_mask(), win.history_valid_mask()
            self.assertFalse(geo[0, 0])
            self.assertTrue(geo[0, 1])
            self.assertFalse(hist[0, 1])
            self.assertTrue(hist[0, 0])
            self.assertEqual(int(geo.sum()), win.pixel_count - 1)


def look_at_projection(eye, target, up=(0.0, 0.0, 1.0), fov_deg=60.0, aspect=16 / 9,
                       near=1.0, far=10000.0):
    """A row-vector view-projection (clip = [x, y, z, 1] @ M), the convention the reader uses."""
    eye = np.asarray(eye, dtype=np.float64)
    f = np.asarray(target, dtype=np.float64) - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, np.asarray(up, dtype=np.float64))
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    view = np.eye(4)
    view[:3, 0], view[:3, 1], view[:3, 2] = r, u, f
    view[3, :3] = [-eye @ r, -eye @ u, -eye @ f]
    t = 1.0 / np.tan(np.radians(fov_deg) * 0.5)
    proj = np.zeros((4, 4))
    proj[0, 0] = t / aspect
    proj[1, 1] = t
    proj[2, 2] = far / (far - near)
    proj[2, 3] = 1.0
    proj[3, 2] = -near * far / (far - near)
    return view @ proj


class MotionCompensation(unittest.TestCase):
    """The world tracking used to compare frames while the camera moves."""

    RES = (640, 360)
    RECT = ('roi', 200, 100, 8, 6)

    def frame_with_camera(self, directory, frame, eye, target, prev_eye, distance=500.0):
        # The previous camera keeps the same orientation and only moves, so the reprojection has to
        # account for real parallax. Re-aiming it at the same target would cancel most of the shift
        # and would test almost nothing.
        forward = np.asarray(target, dtype=np.float64) - np.asarray(eye, dtype=np.float64)
        vp = look_at_projection(eye, target, aspect=self.RES[0] / self.RES[1])
        prev = look_at_projection(prev_eye, np.asarray(prev_eye, dtype=np.float64) + forward,
                                  aspect=self.RES[0] / self.RES[1])
        fields = regiondump.FIELD_LIGHTING | regiondump.FIELD_OUTPUT

        def pixels(name, y, x, rec):
            if rec == 1:            # lighting_normal: xyz + view distance
                return (0.0, 0.0, 1.0, distance)
            return (0.0, 0.0, 0.0, 0.0)

        path = os.path.join(directory, f'region_{frame}.bin')
        write_region_file(path, frame, [self.RECT], fields, pixels,
                          resolution=self.RES, camera=(vp, prev, eye))
        return regiondump.load_frame(path)

    def test_world_position_round_trips_to_the_same_pixel(self):
        with tempfile.TemporaryDirectory() as d:
            eye = (0.0, 0.0, 0.0)
            fr = self.frame_with_camera(d, 1, eye, (0.0, 500.0, 0.0), eye)
            world = fr.world_positions('roi')
            back = fr.project(world)
            _, rx, ry, rw, rh = self.RECT
            want_x = np.arange(rw) + rx
            want_y = np.arange(rh) + ry
            self.assertLess(float(np.abs(back[..., 0] - want_x[None, :]).max()), 0.01)
            self.assertLess(float(np.abs(back[..., 1] - want_y[:, None]).max()), 0.01)

    def test_reprojection_follows_a_known_camera_translation(self):
        with tempfile.TemporaryDirectory() as d:
            eye = (0.0, 0.0, 0.0)
            prev_eye = (-10.0, 0.0, 0.0)          # the camera moved +10 cm in x since the last frame
            fr = self.frame_with_camera(d, 1, eye, (0.0, 500.0, 0.0), prev_eye)
            world = fr.world_positions('roi')
            here = fr.project(world)
            before = fr.project(world, previous=True)
            shift_x = float(np.mean(before[..., 0] - here[..., 0]))
            # The analytic parallax of a lateral camera move: focal length in pixels times the
            # displacement over the point's depth along the view direction. Computed here from the
            # same parameters the matrix was built with, so the check is a law and not a constant.
            focal_px = 0.5 * self.RES[0] * (1.0 / np.tan(np.radians(60.0) * 0.5)) / (self.RES[0] / self.RES[1])
            forward = np.array([0.0, 1.0, 0.0])
            depth = ((world - np.array(eye)) @ forward).mean()
            expected = focal_px * 10.0 / depth
            self.assertGreater(shift_x, 0.0, 'a camera moving +x must place the point further right before')
            self.assertLess(abs(shift_x - expected), 0.05 * expected,
                            f'reprojection must follow the parallax law: {shift_x:.3f} px vs {expected:.3f} px')
            self.assertLess(float(np.abs(before[..., 1] - here[..., 1]).max()), 0.05,
                            'a pure x translation must not move the point vertically')

    def test_wrong_matrix_does_not_track(self):
        with tempfile.TemporaryDirectory() as d:
            eye = (0.0, 0.0, 0.0)
            fr = self.frame_with_camera(d, 1, eye, (0.0, 500.0, 0.0), eye)
            world = fr.world_positions('roi')
            good = fr.project(world)
            fr.view_proj = look_at_projection((0.0, 0.0, 0.0), (0.0, 500.0, 200.0),
                                              aspect=self.RES[0] / self.RES[1]).astype(np.float32)
            bad = fr.project(world)
            self.assertGreater(float(np.abs(bad - good).max()), 5.0,
                               'negative control: a wrong matrix must not reproduce the tracking')

    def test_version_2_has_no_camera_block(self):
        with tempfile.TemporaryDirectory() as d:
            write_region_file(os.path.join(d, 'region_1.bin'), 1, [self.RECT],
                              regiondump.FIELD_LIGHTING | regiondump.FIELD_OUTPUT,
                              lambda *a: (0.0, 0.0, 0.0, 0.0), resolution=self.RES)
            fr = regiondump.load_frame(os.path.join(d, 'region_1.bin'))
            self.assertIsNone(fr.view_proj)
            with self.assertRaises(ValueError):
                fr.world_positions('roi')


class LuminanceUnits(unittest.TestCase):
    def test_rec709_and_ycocg_are_different_quantities(self):
        with tempfile.TemporaryDirectory() as d:
            write_region_file(os.path.join(d, 'region_5.bin'), 5, [('roi', 0, 0, 2, 2)],
                              regiondump.FIELD_OUTPUT, lambda *a: (1.0, 2.0, 3.0, 1.0))
            roi = regiondump.load_frame(os.path.join(d, 'region_5.bin')).rect('roi')
            rec709 = roi.luminance('output')[0, 0]
            ycocg = roi.luminance('output', regiondump.YCOCG_Y)[0, 0]
            self.assertAlmostEqual(float(rec709), 0.2126 + 2 * 0.7152 + 3 * 0.0722, places=5)
            self.assertAlmostEqual(float(ycocg), 0.25 + 2 * 0.5 + 3 * 0.25, places=5)
            self.assertNotAlmostEqual(float(rec709), float(ycocg), places=2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
