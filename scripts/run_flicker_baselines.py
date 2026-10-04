"""Baseline captures for the flicker elimination programme (followup-09, checklist 1.5/1.6).

Three independent runs per pose, each internally continuous: 200 warm-up frames before the first
measured frame and 256 measured frames with the camera still. Two poses cover the three reported
regions:

  W (barred window)  -> `window` (bar/pane edge) and `shadow_wall` (the dark wall beside it)
  A (atrium nave)    -> `rear_door` (the dark doorway at the end) and `right_shadow`
                        (the shadowed right arcade), plus `lit_floor` as a bright reference

For every run the harness records a manifest (executable hash, effective CVars, exposure, frame
timing), streams the full-resolution linear-HDR records into per-frame CSV and per-pixel maps, and
then reports the run-to-run repeatability that later tasks must beat before claiming an effect.

Usage:
    python scripts/run_flicker_baselines.py [--runs 3] [--poses W,A] [--keep-raw] [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import analysis  # noqa: E402
import metrics  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

WARMUP_FRAMES = 200
MEASURED_FRAMES = 256
SCENE_START = 37                    # frames the preload consumes before the main loop
FIRST = SCENE_START + WARMUP_FRAMES               # 237
LAST = FIRST + MEASURED_FRAMES - 1                # 492
RENDER_FRAMES = LAST - SCENE_START + 8            # presented frames to request

POSES = {
    'W': {
        'project': os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject'),
        'description': 'barred window, camera at (175, 1130, 165) looking at (246, 1420, 175)',
        'rois': [
            ('window', 770, 150, 192, 512),
            ('shadow_wall', 150, 260, 192, 384),
        ],
    },
    'A': {
        'project': os.path.join(runner.REPO, 'scripts', 'sponza-flicker-a.uzproject'),
        'description': 'atrium nave, camera at (86, 881, 159) looking at (67.8, 784.6, 178.4)',
        'rois': [
            ('rear_door', 960, 592, 224, 224),
            ('right_shadow', 1472, 464, 256, 256),
            ('lit_floor', 832, 816, 256, 64),
        ],
    },
}

PRIMARY_METRIC = 'std_mean'   # mean over ROI pixels of the temporal std of linear Rec.709 luminance


def capture_and_analyse(work, pose_key, run_index, out, keep_raw, jitter_len=8):
    pose = POSES[pose_key]
    label = f'{pose_key}{run_index}'
    run_dir = os.path.join(work, f'baseline_{label}')
    print(f'  [{label}] capturing {MEASURED_FRAMES} measured frames after {WARMUP_FRAMES} warm-up', flush=True)
    t0 = time.time()
    res = runner.run_capture(run_dir, pose['project'], RENDER_FRAMES,
                             regions=pose['rois'], region_fields='output',
                             region_first=FIRST, region_last=LAST,
                             cvars={'r.TAA.JitterSequenceLength': jitter_len},
                             timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    print(f'  [{label}] captured in {time.time() - t0:.0f} s, analysing', flush=True)
    result = analysis.analyse_run(res['region_dir'], FIRST, LAST,
                                  csv_dir=os.path.join(out, 'csv'), label=f'baseline-{label}')
    shutil.copy(res['manifest_path'], os.path.join(out, 'manifests', f'baseline-{label}.json'))
    for name, s in result['rois'].items():
        path = os.path.join(out, 'masks', f'std-{pose_key}-{name}-run{run_index}.png')
        analysis.mask_png(path, s['per_pixel_std'])
        s.pop('per_pixel_std', None)
        s.pop('per_pixel_mean', None)
    result['manifest'] = res['manifest']
    result['label'] = label
    if not keep_raw:
        shutil.rmtree(res['region_dir'], ignore_errors=True)
    return result


def capture_masks(work, pose_key, out):
    """Short capture with the lighting records, used for the annotated ROI masks."""
    pose = POSES[pose_key]
    run_dir = os.path.join(work, f'mask_{pose_key}')
    res = runner.run_capture(run_dir, pose['project'], SCENE_START + 60,
                             regions=pose['rois'], region_fields='taa',
                             region_first=SCENE_START + 50, region_last=SCENE_START + 56,
                             cvars={'r.TAA.JitterSequenceLength': 8},
                             screenshot_frames=f'{SCENE_START + 52}-{SCENE_START + 52}',
                             timing_first=SCENE_START + 50)
    if res['exit_code'] != 0:
        raise RuntimeError(f'mask {pose_key}: engine exited {res["exit_code"]}')
    frames = regiondump.load_sequence(res['region_dir'], SCENE_START + 50, SCENE_START + 56)
    fr = frames[0]
    info = {}
    for name, x, y, w, h in pose['rois']:
        rect = fr.rect(name)
        geo = rect.geometry_mask()
        rough = rect.channel('lighting_reflection')[..., 3]
        dist = rect.channel('lighting_normal')[..., 3]
        lum = rect.luminance('taa_output')
        analysis.mask_png(os.path.join(out, 'masks', f'geometry-{pose_key}-{name}.png'), geo.astype(float), 1.0)
        analysis.mask_png(os.path.join(out, 'masks', f'luminance-{pose_key}-{name}.png'), lum)
        info[name] = {
            'rect': [x, y, w, h],
            'pixels': int(geo.size),
            'geometry_px': int(geo.sum()),
            'sky_px': int((~geo).sum()),
            'roughness_mean': float(rough[geo].mean()) if geo.any() else None,
            'distance_cm_mean': float(dist[geo].mean()) if geo.any() else None,
            'luminance_mean': float(lum.mean()),
            'luminance_p99': float(np.percentile(lum, 99)),
            'luminance_min': float(lum.min()),
        }
    shot = os.path.join(res['frames_dir'], f'f_{SCENE_START + 52}.png')
    if os.path.exists(shot):
        shutil.copy(shot, os.path.join(out, 'masks', f'reference-{pose_key}.png'))
        annotate(shot, pose['rois'], os.path.join(out, 'masks', f'reference-{pose_key}-annotated.png'))
    shutil.rmtree(res['region_dir'], ignore_errors=True)
    return info


def annotate(png_path, rois, out_path):
    """Draws the ROI rectangles on a reference frame so the masks are readable."""
    from PIL import Image, ImageDraw
    with Image.open(png_path) as im:
        img = im.convert('RGB')
    draw = ImageDraw.Draw(img)
    for name, x, y, w, h in rois:
        draw.rectangle([x, y, x + w - 1, y + h - 1], outline=(255, 80, 80), width=3)
        draw.text((x + 6, max(0, y - 16)), name, fill=(255, 160, 160))
    img.save(out_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runs', type=int, default=3)
    ap.add_argument('--poses', default='W,A')
    ap.add_argument('--keep-raw', action='store_true')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '09-validate-flicker-capture'))
    args = ap.parse_args()
    runner.require_exe()

    out = args.out
    for sub in ('csv', 'masks', 'manifests'):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_flicker_baselines')
    os.makedirs(work, exist_ok=True)

    results, roi_info = {}, {}
    for pose_key in args.poses.split(','):
        print(f'pose {pose_key}: {POSES[pose_key]["description"]}', flush=True)
        roi_info[pose_key] = capture_masks(work, pose_key, out)
        results[pose_key] = [capture_and_analyse(work, pose_key, i + 1, out, args.keep_raw)
                             for i in range(args.runs)]

    summary = {'poses': {}, 'protocol': {
        'warmup_frames': WARMUP_FRAMES, 'measured_frames': MEASURED_FRAMES,
        'first_frame': FIRST, 'last_frame': LAST, 'runs_per_pose': args.runs,
        'primary_metric': PRIMARY_METRIC,
        'primary_metric_definition': 'mean over ROI pixels of the temporal standard deviation of '
                                     'linear Rec.709 luminance of the TAA output (scene radiance units)',
        'channel': 'taa_output (TAA resolve output, linear HDR, not pre-exposed)',
    }}
    for pose_key, runs in results.items():
        pose_out = {'description': POSES[pose_key]['description'], 'rois': {}, 'runs': [r['label'] for r in runs],
                    'roi_info': roi_info[pose_key],
                    'frame_ms_mean': [r['manifest']['frame_ms']['mean'] for r in runs]}
        for name, *_ in POSES[pose_key]['rois']:
            per_run = [r['rois'][name] for r in runs if name in r['rois']]
            if not per_run:
                continue
            entry = {'runs': [{k: v for k, v in s.items() if k != 'csv'} for s in per_run],
                     'repeatability': {}}
            for metric in (PRIMARY_METRIC, 'mean', 'abs_diff_mean', 'std_p99'):
                entry['repeatability'][metric] = metrics.repeatability([s[metric] for s in per_run])
            periods = [s['period_lag'] for s in per_run]
            entry['period_lag_per_run'] = periods
            pose_out['rois'][name] = entry
        summary['poses'][pose_key] = pose_out

    with open(os.path.join(out, 'baselines.json'), 'w', encoding='utf-8') as f:
        json.dump(summary, f, indent=2)
    print(f'\nwrote {os.path.join(out, "baselines.json")}')
    for pose_key, pose_out in summary['poses'].items():
        for name, entry in pose_out['rois'].items():
            rep = entry['repeatability'][PRIMARY_METRIC]
            print(f'  {pose_key}/{name}: {PRIMARY_METRIC} = {rep["mean"]:.6g} '
                  f'(runs {rep["min"]:.6g}..{rep["max"]:.6g}, relative range {rep["relative_range"]*100:.1f} %, '
                  f'95 % half-width {rep["half_width_95"]:.3g})')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
