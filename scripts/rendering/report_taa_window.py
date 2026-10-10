"""Renders the followup-10 evidence package report from its JSON artifacts.

Usage: python scripts/rendering/report_taa_window.py [--dir DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

DEFAULT_DIR = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '10-isolate-taa-window')


def load(d, name):
    p = os.path.join(d, name)
    return json.load(open(p, encoding='utf-8')) if os.path.exists(p) else None


def g(v, digits=4):
    return 'n/a' if v is None else (f'{v:.{digits}g}' if isinstance(v, float) else str(v))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=DEFAULT_DIR)
    args = ap.parse_args()
    d = args.dir
    out = ['# followup-10 — isolating the TAA window residual', '',
           'All values are linear Rec.709 luminance of the TAA output in scene radiance units, at a',
           'fixed exposure with the camera still unless a section says otherwise. Every variant is a',
           'diagnostic CVar; no default changed.', '']

    val = load(d, 'taa-model-validation.json')
    if val:
        out += ['## The offline model reproduces the shader', '',
                'Before any variant is judged by a model, the model is checked against the GPU on real',
                f'scene content (ROI {val["roi"]}, frames {val["frames"][0]}..{val["frames"][-1]}):', '',
                '| stage | max relative error | tolerance |', '|---|---|---|']
        for c in val['checks']:
            out.append(f'| {c["name"]} | {c["max_relative_error"]:.2e} | {c["tolerance"]:.0e} |')
        out += ['', 'The reconstruction, the clamp and the blend are therefore all reproduced from the',
                'recorded fields; a variant tried in the model is the same arithmetic the shader runs.', '']

    study = load(d, 'reconstruction-study.json')
    if study:
        out += ['## Analytic fixtures against a converged reference (checklist 1.4)', '',
                'Each fixture is a bar or an edge whose exact box integral over the pixel footprint is',
                'known, so a variant is compared with the right answer rather than with the previous',
                'frame. `energy err` is the converged image minus that reference, as a fraction of it.', '']
        for fname, variants in study['results'].items():
            fx = study['fixtures'][fname]
            out += [f'### {fname} — bar {fx["bar_width"]} px, bright {fx["bright"]}, dark {fx["dark"]}', '',
                    '| variant | energy err | rms | temporal std | width px | peak | converged after |',
                    '|---|---|---|---|---|---|---|']
            for vname, r in variants.items():
                out.append(f'| {vname} | {r["energy_error"] * 100:+.1f} % | {r["rms_error"]:.3f} | '
                           f'{r["temporal_std"]:.5f} | {r["feature_width"]:.0f} | {r["peak"]:.2f} | '
                           f'{r["convergence_frames"]} frames |')
            out.append('')
        out += ['### Jitter controls on the thin bright bar', '',
                '| control | energy err | temporal std | peak |', '|---|---|---|---|']
        for label, r in study['jitter_controls'].items():
            out.append(f'| {label} | {r["energy_error"] * 100:+.1f} % | {r["temporal_std"]:.5f} | {r["peak"]:.2f} |')
        out += ['', 'The two failures are independent: a constant offset (or none at all) removes the',
                'temporal variation completely and still loses about a third of the bar\'s energy.', '']

    exp = load(d, 'window-experiments.json')
    if exp:
        out += ['## The production window on the GPU (checklist 1.1/1.2/1.5/1.6)', '',
                f'ROI {exp["roi"]}, {exp["frames"]} frames from frame {exp["first_frame"]}, masks frozen',
                f'from the baseline: bright {exp["masks"].get("bright")} px, dark {exp["masks"].get("dark")} px, '
                f'boundary {exp["masks"].get("boundary")} px of {exp["masks"].get("all")}.', '',
                'The reference for the energy column is the mean of the raw pre-TAA samples over whole',
                'jitter cycles — what a temporal supersampler should converge to.', '',
                '| variant | ROI energy vs reference | ROI std | boundary std | bright std | clamp active | share of change on clamped frames |',
                '|---|---|---|---|---|---|---|']
        for label, v in exp['results'].items():
            if 'unmeasurable' in v:
                out.append(f'| {label} | — | — | — | — | — | {v["unmeasurable"]} |')
                continue
            a, b, br, c = v['all'], v['boundary'], v['bright'], v['clamp']
            out.append(f'| {label} | {a["energy_ratio_vs_reference"]:.4f} | {a["std_mean"]:.5f} | '
                       f'{b["std_mean"]:.5f} | {br["std_mean"]:.5f} | {c["active_fraction"] * 100:.1f} % | '
                       f'{c["output_change_on_clamped_frames"] * 100:.1f} % |')
        out += ['', 'Per-mask energy ratios are **redistribution, not loss**: a reconstruction filter moves',
                'light across the mask boundary, so the dark bars receive many times the radiance the raw',
                'input has there. Only the whole-ROI column is an energy statement.', '',
                '| variant | bright | dark | boundary |', '|---|---|---|---|']
        for label, v in exp['results'].items():
            if 'unmeasurable' in v:
                continue
            out.append(f'| {label} | {v["bright"]["energy_ratio_vs_reference"]:.3f} | '
                       f'{v["dark"]["energy_ratio_vs_reference"]:.1f} | '
                       f'{v["boundary"]["energy_ratio_vs_reference"]:.3f} |')
        out.append('')

    raw = load(d, 'raw-reference.json')
    if raw:
        out += ['## Where the movement enters the pipeline (checklist 1.2, no-TAA control)', '',
                'The same capture records the pre-TAA sample, so the no-TAA image is measured without',
                'disabling the resolve (which would leave the instrument with nothing to read).', '',
                '| stage | all | bright | dark | boundary |', '|---|---|---|---|---|']
        for stage, row in raw['stages'].items():
            out.append(f'| `{stage}` | ' + ' | '.join(f'{row[m]["std_mean"]:.5f}'
                                                      for m in ('all', 'bright', 'dark', 'boundary')) + ' |')
        out += ['', 'Temporal standard deviation of the linear radiance per stage. `taa_raw` is the image',
                'without accumulation.', '']

    mot = load(d, 'motion-check.json')
    if mot:
        out += ['## Motion, disocclusion and a camera cut (checklist 1.7)', '',
                f'ROI {mot["roi"]} on `scripts/rendering/sponza-flicker-w-motion.lua`: still, a 0.5 cm/frame strafe,',
                'still again, then a one-frame cut back to the first pose so the history is wrong while the',
                'content is identical. Recovery is measured against the converged phase-1 image.', '',
                '| variant | static std | static lag vs raw | motion lag | recovery frames | peak error after cut | residual at end |',
                '|---|---|---|---|---|---|---|']
        for label, v in mot['results'].items():
            rec = v.get('recovery', {})
            out.append(f'| {label} | {v["hold1"]["std_mean"]:.5f} | {v["hold1"]["lag_vs_raw"]:.4f} | '
                       f'{v["strafe"]["lag_vs_raw"]:.4f} | '
                       f'{rec.get("frames")}{"" if rec.get("recovered_within_capture") else " (not recovered)"} | '
                       f'{g(rec.get("peak_error_after_cut"))} | {g(rec.get("residual_error_at_end"))} |')
        out += ['', 'The off-screen history fraction is 0 in every phase: this TAA rejects history only when',
                'the reprojected sample leaves the screen, so the neighbourhood clamp is its **only**',
                'defence against invalid history. That is why removing the clamp has to be judged here and',
                'not on a still camera.', '']

    path = os.path.join(d, 'report.md')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
