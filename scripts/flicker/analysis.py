"""Per-ROI temporal analysis of a region-dump capture, streamed frame by frame.

The primary quantity is the linear Rec.709 luminance of the TAA output, in scene radiance units.
Display-referred numbers (PNG code values, threshold counts) are secondary and always labelled as
such: a hypothesis must never be decided by a post-tonemap threshold count alone, because exposure
and bloom change that count without changing the radiance (2026-09-09 finding).

`analyse_run` never holds the whole sequence in memory: per-pixel accumulators are updated frame by
frame, so a 256-frame full-resolution capture costs one frame of RAM.
"""
from __future__ import annotations

import csv
import os

import numpy as np

import metrics
import regiondump

#: Absolute differences (linear radiance) used for exact per-frame counts.
DEFAULT_THRESHOLDS = (0.001, 0.01, 0.1)


class RoiAccumulator:
    """Streaming per-pixel statistics for one rectangle."""

    def __init__(self, name: str, shape):
        self.name = name
        self.shape = shape
        self.n = 0
        self.sum = np.zeros(shape, dtype=np.float64)
        self.sumsq = np.zeros(shape, dtype=np.float64)
        self.abs_diff_sum = np.zeros(shape, dtype=np.float64)
        self.pairs = 0
        self.prev = None
        self.valid_all = np.ones(shape, dtype=bool)   # pixels valid in EVERY frame
        self.geometry_any = np.zeros(shape, dtype=bool)
        self.rows = []            # per-frame records
        self.frames = []

    def add(self, frame: regiondump.RegionFrame, lum: np.ndarray, valid: np.ndarray,
            thresholds=DEFAULT_THRESHOLDS):
        self.n += 1
        self.frames.append(frame.frame)
        self.sum += lum
        self.sumsq += lum * lum
        self.valid_all &= valid
        self.geometry_any |= valid
        row = {
            'frame': frame.frame,
            'jitter_index': frame.jitter_index,
            'jitter_x': frame.jitter_x,
            'jitter_y': frame.jitter_y,
            'exposure': frame.exposure,
            'mean': float(lum.mean()),
            'mean_valid': float(lum[valid].mean()) if valid.any() else float('nan'),
            'valid_px': int(valid.sum()),
        }
        if self.prev is not None:
            d = np.abs(lum - self.prev)
            self.abs_diff_sum += d
            self.pairs += 1
            row['abs_diff_mean'] = float(d.mean())
            row['abs_diff_max'] = float(d.max())
            for t in thresholds:
                row[f'count_gt_{t}'] = int((d > t).sum())
        self.prev = lum.copy()
        self.rows.append(row)

    def summary(self, mask: np.ndarray = None) -> dict:
        """Aggregate statistics over `mask` (default: pixels valid in every frame)."""
        if self.n < 2:
            return {'frames': self.n}
        m = self.valid_all if mask is None else mask
        mean = self.sum / self.n
        var = np.maximum(self.sumsq / self.n - mean * mean, 0.0)
        std = np.sqrt(var)
        d1 = self.abs_diff_sum / max(1, self.pairs)
        sel_std, sel_mean, sel_d1 = std[m], mean[m], d1[m]
        series = [r.get('abs_diff_mean') for r in self.rows if 'abs_diff_mean' in r]
        period = metrics.dominant_period(series) if len(series) > 8 else None
        return {
            'roi': self.name,
            'frames': self.n,
            'pairs': self.pairs,
            'pixels': int(self.shape[0] * self.shape[1]),
            'pixels_valid': int(m.sum()),
            'mean': float(sel_mean.mean()),
            'std_mean': float(sel_std.mean()),
            'std_p95': float(np.percentile(sel_std, 95)),
            'std_p99': float(np.percentile(sel_std, 99)),
            'std_max': float(sel_std.max()),
            'abs_diff_mean': float(sel_d1.mean()),
            'abs_diff_p99': float(np.percentile(sel_d1, 99)),
            'period_lag': None if period is None else period[0],
            'period_r': None if period is None else period[1],
            'per_pixel_std': std,
            'per_pixel_mean': mean,
        }


def analyse_run(region_dir: str, first: int, last: int, rois=None, channel: str = None,
                thresholds=DEFAULT_THRESHOLDS, csv_dir: str = None, label: str = '',
                allow_taa_missing: bool = False) -> dict:
    """Streams one capture and returns `{roi_name: summary}` plus the integrity report.

    Raises if the capture is not a contiguous, stamp-valid sequence: an incomplete capture must
    not reach the statistics at all.
    """
    report = regiondump.check_sequence(region_dir, first, last, allow_taa_missing)
    if not report['ok']:
        raise ValueError(f'{region_dir}: capture failed integrity check: '
                         f"missing={report['missing']} duplicates={report['duplicates']} "
                         f"invalid={report['invalid'][:5]}")
    paths = regiondump.sequence_paths(region_dir, first, last)
    acc = {}
    for _, path in paths:
        fr = regiondump.load_frame(path)
        if channel is None:
            channel = 'taa_output' if (fr.fields & regiondump.FIELD_TAA) else 'output'
        for rect in fr.rects:
            if rois and rect.name not in rois:
                continue
            lum = rect.luminance(channel).astype(np.float64)
            if fr.fields & regiondump.FIELD_LIGHTING:
                valid = rect.geometry_mask()
            else:
                valid = np.ones(lum.shape, dtype=bool)
            a = acc.get(rect.name)
            if a is None:
                a = acc[rect.name] = RoiAccumulator(rect.name, lum.shape)
            a.add(fr, lum, valid, thresholds)
    out = {'integrity': report, 'channel': channel, 'rois': {}}
    for name, a in acc.items():
        s = a.summary()
        out['rois'][name] = s
        if csv_dir:
            os.makedirs(csv_dir, exist_ok=True)
            path = os.path.join(csv_dir, f'{label or os.path.basename(region_dir)}-{name}.csv')
            fields = list(a.rows[-1].keys())
            with open(path, 'w', newline='', encoding='utf-8') as f:
                w = csv.DictWriter(f, fieldnames=fields)
                w.writeheader()
                for r in a.rows:
                    w.writerow(r)
            s['csv'] = path
    return out


def mask_png(path: str, values: np.ndarray, vmax: float = None):
    """Writes a grayscale PNG of a per-pixel map (annotated ROI mask / std map)."""
    from PIL import Image
    v = np.asarray(values, dtype=np.float64)
    top = vmax if vmax is not None else (np.percentile(v, 99) if v.size else 1.0)
    img = np.clip(v / top, 0.0, 1.0) * 255.0 if top > 0 else np.zeros_like(v)
    Image.fromarray(img.astype(np.uint8), mode='L').save(path)
