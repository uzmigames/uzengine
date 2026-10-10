"""GPU experiments on the production window (followup-10, checklist 1.1/1.2/1.5/1.6).

Frozen before any variant is run:

* the evaluation ROI (pose W `window`, 192x512) and three masks derived from the **baseline**
  capture — bright side (the pane), dark side (the bars) and boundary (pixels whose raw sample
  crosses between the two during a jitter cycle);
* the reference each variant is judged against: the mean of the raw pre-TAA samples over one full
  jitter cycle, which is what a temporal supersampler is supposed to converge to. Comparing a frame
  with the previous frame cannot tell a stable-but-wrong image from a correct one.

Then one factor at a time: jitter length (with an explicit zero-offset control that is not
Halton23(1)), reconstruction mode, clamp mode, blend mode, and TAA off.

Metrics per mask: temporal standard deviation of the linear Rec.709 radiance (the primary metric of
followup-09), mean radiance, the energy ratio against the supersampled reference, the clamp
activity, and the share of the output change that happens on clamped frames.

Usage: python scripts/rendering/run_taa_window_experiments.py [--out DIR] [--frames 24]
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

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject')
ROI = ('window', 770, 150, 192, 512)

#: Fixed for every run so the display side is comparable and the exposure cannot drift.
BASE_CVARS = {'r.AutoExposure': 0, 'r.Exposure': 1.0}

VARIANTS = [
    ('baseline jitter 8', {'r.TAA.JitterSequenceLength': 8}),
    ('jitter 16', {'r.TAA.JitterSequenceLength': 16}),
    ('jitter 4', {'r.TAA.JitterSequenceLength': 4}),
    ('jitter 1 (constant Halton23(1))', {'r.TAA.JitterSequenceLength': 1}),
    ('zero jitter, accumulation on', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.JitterScale': 0}),
    ('TAA off', {'r.TAA': 0}),
    ('reconstruction 1: no HDR weighting', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.ReconstructionMode': 1}),
    ('reconstruction 2: centre tap only', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.ReconstructionMode': 2}),
    ('reconstruction 3: full 3x3', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.ReconstructionMode': 3}),
    ('clamp 1: no clamp', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.ClampMode': 1}),
    ('clamp 2: variance box only', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.ClampMode': 2}),
    ('clamp 3: AABB only', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.ClampMode': 3}),
    ('blend 1: fixed weight', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.BlendMode': 1}),
    ('reconstruction 1 + blend 1', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.ReconstructionMode': 1,
                                    'r.TAA.BlendMode': 1}),
]


def capture(work, label, cvars, frames, keep_raw=False):
    last = FIRST + frames - 1
    out_dir = os.path.join(work, label.replace(' ', '_').replace(':', '').replace(',', ''))
    res = runner.run_capture(out_dir, PROJECT, last - SCENE_START + 8, regions=[ROI],
                             region_fields='taa+extra', region_first=FIRST, region_last=last,
                             cvars={**BASE_CVARS, **cvars}, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    return res, last


def analyse(res, first, last, masks=None):
    """Per-mask statistics of one variant, plus the supersampled reference it is judged against."""
    frames = regiondump.load_sequence(res['region_dir'], first, last, require_valid=False)
    taa_missing = any(f.flags & regiondump.FLAG_TAA_MISSING for f in frames)
    rect0 = frames[0].rect(ROI[0])
    if taa_missing:
        # TAA off: the resolve never runs, so only the lighting records are current. The scene
        # colour still reaches the display, but this instrument cannot read the resolved image;
        # report it as unmeasurable instead of returning stale records.
        return {'unmeasurable': 'TAA disabled: the resolve wrote no records this run'}, masks

    out = np.stack([f.rect(ROI[0]).luminance('taa_output') for f in frames]).astype(np.float64)
    raw = np.stack([f.rect(ROI[0]).luminance('taa_raw') for f in frames]).astype(np.float64)
    hist = np.stack([f.rect(ROI[0]).luminance('taa_history') for f in frames]).astype(np.float64)
    clamped = np.stack([f.rect(ROI[0]).luminance('taa_clamped') for f in frames]).astype(np.float64)
    jitter_idx = np.array([f.jitter_index for f in frames])

    if masks is None:
        # Frozen from the baseline: bright = raw always high, dark = raw always low,
        # boundary = the raw sample crosses between the two during the cycle.
        peak = np.percentile(raw.max(axis=0), 99)
        bright = raw.min(axis=0) >= 0.9 * peak
        dark = raw.max(axis=0) <= 0.05 * peak
        boundary = ~bright & ~dark
        masks = {'bright': bright, 'dark': dark, 'boundary': boundary,
                 'all': np.ones_like(bright, dtype=bool)}

    # The reference: mean of the raw pre-TAA samples over whole jitter cycles.
    cycle = int(jitter_idx.max()) if jitter_idx.max() > 0 else 1
    whole = (len(frames) // cycle) * cycle
    reference = raw[:whole].mean(axis=0) if whole else raw.mean(axis=0)

    stats = {}
    for name, m in masks.items():
        if not m.any():
            continue
        o = out[:, m]
        stats[name] = {
            'pixels': int(m.sum()),
            'mean': float(o.mean()),
            'std_mean': float(o.std(axis=0).mean()),
            'std_p99': float(np.percentile(o.std(axis=0), 99)),
            'abs_diff_mean': float(np.abs(np.diff(o, axis=0)).mean()),
            'energy_ratio_vs_reference': float(o.mean() / reference[m].mean()) if reference[m].mean() else float('nan'),
            'reference_mean': float(reference[m].mean()),
        }
    # clamp activity and its share of the output change
    moved = np.abs(clamped - hist) > 1e-5
    step = np.abs(np.diff(out, axis=0))
    moved_pairs = moved[1:]
    stats['clamp'] = {
        'active_fraction': float(moved.mean()),
        'output_change_on_clamped_frames': float(step[moved_pairs].sum() / max(1e-12, step.sum())),
        'mean_step_clamped': float(step[moved_pairs].mean()) if moved_pairs.any() else 0.0,
        'mean_step_unclamped': float(step[~moved_pairs].mean()) if (~moved_pairs).any() else 0.0,
    }
    stats['jitter'] = {
        'indices': sorted(set(int(i) for i in jitter_idx)),
        'offsets': [[float(f.jitter_x), float(f.jitter_y)] for f in frames[:8]],
    }
    return stats, masks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=24)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '10-isolate-taa-window'))
    ap.add_argument('--keep-raw', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_taa_window_experiments')

    results, masks = {}, None
    for label, cvars in VARIANTS:
        print(f'  … {label}', flush=True)
        res, last = capture(work, label, cvars, args.frames)
        stats, masks = analyse(res, FIRST, last, masks)
        stats['cvars'] = cvars
        stats['frame_ms_mean'] = res['manifest']['frame_ms']['mean']
        results[label] = stats
        shutil.copy(res['manifest_path'],
                    os.path.join(args.out, 'manifests', label.replace(' ', '_').replace(':', '') + '.json'))
        if not args.keep_raw:
            shutil.rmtree(res['region_dir'], ignore_errors=True)
        if 'unmeasurable' in stats:
            print(f'      unmeasurable: {stats["unmeasurable"]}')
        else:
            b = stats.get('boundary', {})
            print(f'      boundary std {b.get("std_mean", float("nan")):.5f}  '
                  f'energy vs reference {b.get("energy_ratio_vs_reference", float("nan")):.3f}  '
                  f'bright energy {stats.get("bright", {}).get("energy_ratio_vs_reference", float("nan")):.3f}')

    payload = {'roi': ROI, 'first_frame': FIRST, 'frames': args.frames,
               'masks': {k: int(v.sum()) for k, v in masks.items()} if masks else {},
               'base_cvars': BASE_CVARS, 'results': results,
               'source_fingerprint': runner.source_fingerprint()}
    path = os.path.join(args.out, 'window-experiments.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
