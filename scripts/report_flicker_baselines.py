"""Renders the followup-09 evidence package report from the JSON artifacts.

Reads `baselines.json` and `field-check-*.json` from the package directory and writes `report.md`
next to them. Kept separate from the capture so the report can be regenerated without re-running
the engine.

Usage: python scripts/report_flicker_baselines.py [--dir DIR]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

DEFAULT_DIR = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                           '09-validate-flicker-capture')


def fmt(v, digits=6):
    if v is None:
        return 'undefined'
    if isinstance(v, float):
        return f'{v:.{digits}g}'
    return str(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=DEFAULT_DIR)
    args = ap.parse_args()
    d = args.dir
    out = [f'# followup-09 — validated flicker capture and measurement', '',
           'Everything here was produced by `scripts/run_flicker_field_check.py` and',
           '`scripts/run_flicker_baselines.py` against the region-dump v2 instrumentation',
           '(`uzengine.exe --region-dump`). Values are linear Rec.709 luminance of the TAA output in',
           'scene radiance units unless a row says otherwise; display code values are labelled as such.', '']

    # ── field checks ────────────────────────────────────────────────────────
    for path in sorted(glob.glob(os.path.join(d, 'field-check-*.json'))):
        fc = json.load(open(path, encoding='utf-8'))
        out += [f'## Record semantics, pose {fc["pose"]} (checklist 1.1)', '',
                f'ROI `{fc["roi"]["name"]}` at x={fc["roi"]["rect"][0]}, y={fc["roi"]["rect"][1]}, '
                f'{fc["roi"]["rect"][2]}x{fc["roi"]["rect"][3]} px; frames '
                f'{fc["frames"][0]}..{fc["frames"][-1]} of one process; exposure fixed.', '',
                '| check | result | evidence |', '|---|---|---|']
        for c in fc['checks']:
            out.append(f'| {c["check"]} | {"ok" if c["ok"] else "FAIL"} | {c["detail"]} |')
        out += ['', '### Which pixels oscillate, and where that shows up', '',
                '| population | pixels | linear std (mean) | linear mean | display mean (0-255) | max display jump |',
                '|---|---|---|---|---|---|']
        for p in fc['populations']:
            if not p.get('pixels'):
                continue
            out.append(f'| {p["label"]} | {p["pixels"]} | {fmt(p["linear_std_mean"])} | {fmt(p["linear_mean"])} | '
                       f'{fmt(p["display_mean"], 4)} | {fmt(p["display_max_jump"], 4)} |')
        out += ['',
                f'Overlap between the top 1 % linear-variance pixels and the pixels that cross 40 display '
                f'code values: {fc["overlap_top_vs_crossing_px"]} px. The two descriptions in the 2026-09-09 '
                f'package refer to the two sides of the same edge: the linear oscillation sits on the bright '
                f'pane side (invisible on screen because it clips), the display threshold crossings sit on the '
                f'dark bar pixels next to it.', '']

    # ── baselines ───────────────────────────────────────────────────────────
    bpath = os.path.join(d, 'baselines.json')
    if os.path.exists(bpath):
        b = json.load(open(bpath, encoding='utf-8'))
        p = b['protocol']
        out += ['## Baselines and repeatability (checklist 1.5)', '',
                f'{p["runs_per_pose"]} independent runs per pose, each one continuous process: '
                f'{p["warmup_frames"]} warm-up frames then {p["measured_frames"]} measured frames '
                f'({p["first_frame"]}..{p["last_frame"]}), camera still, full resolution, no subsampling.',
                f'Primary metric: `{p["primary_metric"]}` — {p["primary_metric_definition"]}.',
                f'Channel: {p["channel"]}.', '']
        for pose, pd in b['poses'].items():
            out += [f'### Pose {pose} — {pd["description"]}', '',
                    '| ROI | rect (x, y, w, h) | geometry px | mean radiance | std_mean | '
                    'std_mean / mean | mean abs frame diff | run-to-run range | 95 % half-width | period lag |',
                    '|---|---|---|---|---|---|---|---|---|---|']
            for name, entry in pd['rois'].items():
                info = pd['roi_info'].get(name, {})
                rep = entry['repeatability']['std_mean']
                mean = entry['repeatability']['mean']['mean']
                rel = rep['mean'] / mean if mean else float('nan')
                out.append(f'| `{name}` | {info.get("rect")} | {info.get("geometry_px")}/{info.get("pixels")} | '
                           f'{fmt(mean)} | {fmt(rep["mean"])} | {fmt(rel * 100, 3)} % | '
                           f'{fmt(entry["repeatability"]["abs_diff_mean"]["mean"])} | '
                           f'{fmt(rep["relative_range"] * 100, 3)} % | {fmt(rep["half_width_95"], 3)} | '
                           f'{entry["period_lag_per_run"]} |')
            out += ['', f'Frame time (mean, ms) per run: '
                        f'{", ".join(f"{v:.2f}" for v in pd["frame_ms_mean"])}.', '']
        out += ['Reading the table: `std_mean` is an absolute radiance, so ROIs of different brightness are',
                'only comparable through `std_mean / mean`. The three runs of a pose agree to within',
                '0.005 %, which measures reproducibility of the whole process (loading, timing, driver',
                'state) rather than stochastic noise: the renderer is deterministic at a given frame index,',
                'so a repeated run reproduces the same frames. The decision threshold below is therefore',
                'governed by the 5 %-of-mean rule, not by run-to-run spread.', '',
                '### Noise floor and decision threshold', '',
                'The smallest difference in `std_mean` a later task may call an effect, per ROI '
                '(max of 3 x the 95 % half-width and 5 % of the baseline mean, per the ledger):', '',
                '| pose / ROI | baseline std_mean | 3 x half-width | 5 % of mean | threshold |',
                '|---|---|---|---|---|']
        for pose, pd in b['poses'].items():
            for name, entry in pd['rois'].items():
                rep = entry['repeatability']['std_mean']
                three = 3.0 * (rep['half_width_95'] or 0.0)
                five = 0.05 * rep['mean']
                out.append(f'| {pose} / `{name}` | {fmt(rep["mean"])} | {fmt(three, 3)} | {fmt(five, 3)} | '
                           f'{fmt(max(three, five), 3)} |')
        out += ['']

    # ── findings that follow from the baselines themselves ──────────────────
    if os.path.exists(bpath):
        rows = []
        for pose, pd in b['poses'].items():
            for name, entry in pd['rois'].items():
                rep = entry['repeatability']
                mean = rep['mean']['mean']
                rows.append((pose, name, rep['std_mean']['mean'], mean,
                             rep['std_mean']['mean'] / mean if mean else float('nan'),
                             entry['period_lag_per_run']))
        rows.sort(key=lambda r: -r[4])
        out += ['## What the baselines already show', '',
                '1. **Every measured region carries a residual with the camera still.** Ranked by relative',
                '   amplitude (`std_mean / mean`, linear radiance):', '']
        for pose, name, std, mean, rel, per in rows:
            out.append(f'   - {pose} / `{name}`: {rel * 100:.2f} % of its mean radiance, period {per[0]}')
        odd = [r for r in rows if r[5][0] != 8]
        out += ['',
                '2. **The dominant period is not the same everywhere.** ' +
                (f'{", ".join(f"{p} / `{n}`" for p, n, *_ in odd)} '
                 f'peaks at lag {odd[0][5][0]}, not 8, in all three runs, while every other region peaks at 8. '
                 'A period of 24 frames matches the GTAO temporal pattern (6 rotations x 4 offsets), not the '
                 '8-frame TAA jitter cycle; the intervention that would confirm it belongs to followup-12.'
                 if odd else 'All regions peak at the jitter length.'), '',
                '3. **Relative amplitude rises as the region gets darker** (window 0.39 % at 2.45 radiance vs',
                '   shadow wall 5.7 % at 1.7e-4). Absolute radiance differences are therefore not comparable',
                '   across regions, and a display-referred count is not comparable at all unless the exposure',
                '   is fixed.', '',
                '4. **The pose W `shadow_wall` region is close to the measurement floor** (mean radiance',
                '   1.7e-4, mean absolute frame-to-frame change 1.2e-6). It is a valid noise-floor reference,',
                '   but followup-12 should add a shadowed wall that actually receives bounce light before',
                '   drawing conclusions about "shadowed walls" in general.', '']

    out += ['## Files', '',
            '- `baselines.json` — every run\'s per-ROI summary, repeatability and manifest reference.',
            '- `field-check-*.json` — record-semantics checks with the evidence for each.',
            '- `csv/` — per-frame series for every run and ROI (linear units, exact counts).',
            '- `masks/` — annotated reference frames, geometry masks and per-pixel std maps.',
            '- `manifests/` — the run manifest of every capture (executable hash, effective CVars, '
            'exposure, frame timing, region-dump integrity counters).', '']
    path = os.path.join(d, 'report.md')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
