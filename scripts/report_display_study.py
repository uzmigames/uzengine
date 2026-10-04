"""Renders the followup-11 evidence package report from `display-study.json`.

Usage: python scripts/report_display_study.py [--dir DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

DEFAULT_DIR = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                           '11-isolate-flicker-display')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=DEFAULT_DIR)
    args = ap.parse_args()
    d = json.load(open(os.path.join(args.dir, 'display-study.json'), encoding='utf-8'))

    out = ['# followup-11 — exposure, bloom and the display side', '',
           f'ROI {d["roi"]}, {d["frames"]} frames from frame {d["first_frame"]}, camera still.',
           f'Tonemap: ACES Hill fit, saturation {d["tonemap"]["saturation"]}, contrast '
           f'{d["tonemap"]["contrast"]} — the QA project\'s settings.', '',
           '## The offline replay reproduces the engine (checklist 1.1)', '',
           f'With bloom off the tonemap is a per-pixel function of the recorded radiance, and the replay',
           f'in `scripts/flicker/display_model.py` matches the engine PNG to at most',
           f'{max(d["replay_validation"]["max_code_delta"])} code value',
           f'({d["replay_validation"]["fraction_within_1"] * 100:.3f} % of pixels within 1). Every',
           'display claim below is made through that replay or through captured PNGs, never by assuming',
           'the transform.', '',
           '## The same pixels in four units (checklist 1.2)', '',
           '| mask | pixels | linear Rec.709 radiance | its temporal std | resolve YCoCg Y | display code value | its temporal std |',
           '|---|---|---|---|---|---|---|']
    for k, v in d['units'].items():
        out.append(f'| {k} | {v["pixels"]} | {v["linear_rec709_mean"]:.5f} | {v["linear_rec709_std"]:.5f} | '
                   f'{v["ycocg_y_mean"]:.4f} | {v["display_code_mean"]:.2f} | {v["display_code_std"]:.3f} |')
    out += ['', 'The resolve\'s YCoCg Y is about 3.9x the Rec.709 luminance for this content: the two are',
            'different quantities and must never be compared to one another. The effective exposure is',
            f'recorded per frame ({d["exposure_per_frame"][0]} here, fixed).', '']

    sw = d['exposure_sweep_offline']
    out += ['## Exposure (checklist 1.3)', '',
            'The identical HDR sequence replayed at three exposures — the radiance never changes, only',
            'the display transform:', '',
            '| exposure | display mean | display std | px > 40 per frame | px > 8 per frame |',
            '|---|---|---|---|---|']
    for k, v in sw.items():
        out.append(f'| {k} ({v["exposure"]:.2f}) | {v["display_mean"]:.2f} | {v["display_std_mean"]:.4f} | '
                   f'{v["count_gt_40_per_frame"]:.1f} | {v["count_gt_8_per_frame"]:.1f} |')
    gpu = d.get('exposure_sweep_gpu', {})
    gpu_desc = ', '.join(f'{k}: max delta {v["max_hdr_delta_vs_baseline"]:g}' for k, v in gpu.items())
    count_ratio = sw['2.0x']['count_gt_40_per_frame'] / max(1e-9, sw['0.5x']['count_gt_40_per_frame'])
    out += ['', 'On the GPU the same exposures leave the linear records **bit-identical** '
            f'({gpu_desc}), so the count column above is entirely a property of the display '
            f'transform: a 4x exposure change multiplies the px > 40 count by {count_ratio:.0f}x '
            'with the scene untouched.', '']
    ae = d.get('auto_exposure')
    if ae:
        out += [f'Auto-exposure on this pose settles at **{ae["mean"]:.2f}** and swings '
                f'{ae["relative_swing"] * 100:.3f} % over the measured sequence, leaving the linear records '
                f'bit-identical to the fixed-exposure run. With the camera still it therefore sets the gain '
                'but does not itself oscillate; its frame-time dependence (followup-09) is what makes it a '
                'confound between runs of different cost.', '']

    bl = d.get('bloom', {})
    if bl:
        out += ['## Bloom (checklist 1.4)', '',
                'Captured on the identical HDR sequence — the recorded radiance is bit-identical with bloom',
                'on and off, so everything below is display-side only.', '',
                '| run | dark floor (code) | dark p99 | bright (code) | local contrast bright/dark | px > 40 per frame | px > 8 per frame |',
                '|---|---|---|---|---|---|---|']
        for k, v in bl.items():
            out.append(f'| {k} | {v["dark_floor_mean_code"]:.2f} | {v["dark_floor_p99_code"]:.1f} | '
                       f'{v["bright_mean_code"]:.1f} | {v["local_contrast_bright_over_dark"]:.1f} | '
                       f'{v["count_gt_40_per_frame"]:.1f} | {v["count_gt_8_per_frame"]:.1f} |')
        out += ['', 'Bloom lifts the dark floor by 2.4x and halves the local contrast. Its effect on the',
                'threshold counts is **not one-directional**: at this exposure it lowers them, while the',
                '2026-09-09 package measured the opposite at an auto-exposure of ~67. A count that changes',
                'sign with the exposure cannot be used as a measure of the underlying oscillation.', '']

    pe = d.get('preexpose', {})
    if pe:
        out += ['## `r.TAA.PreExposeWeights` as a diagnostic (checklist 1.5)', '',
                f'The flag multiplies the resolve\'s HDR weights by the frame exposure, so at a fixed',
                f'exposure of 1.0 it is a **no-op by construction**. It is therefore evaluated at the',
                f'auto-exposure operating point of this pose ({pe.get("operating_exposure")}), against a',
                'control at the same exposure.', '',
                '| mask | radiance mean control → flag | temporal std control → flag | std p99 control → flag | mean abs frame change control → flag |',
                '|---|---|---|---|---|']
        for k in ('bright', 'dark', 'boundary', 'all'):
            v = pe.get(k)
            if not v:
                continue
            out.append(f'| {k} | {v["mean_control"]:.5f} → {v["mean_preexpose"]:.5f} | '
                       f'{v["std_mean_control"]:.5f} → {v["std_mean_preexpose"]:.5f} | '
                       f'{v["std_p99_control"]:.5f} → {v["std_p99_preexpose"]:.5f} | '
                       f'{v["temporal_response_abs_diff_control"]:.5f} → '
                       f'{v["temporal_response_abs_diff_preexpose"]:.5f} |')
        out += ['', 'At the exposure the scene actually runs at, the flag makes the bright side oscillate 11x',
                'more (p99 error 0.18 → 2.58 radiance) and crushes the dark side to 2 % of its radiance',
                '(0.0039 → 0.00008). The lower display counts reported for it on 2026-09-09 are that',
                'crushing, not a reduction of the oscillation. It stays off.', '']

    rc = d.get('reflections_confound')
    if rc:
        on, off = rc['reflections_on'], rc['reflections_off']
        out += ['## The reflections-off confound (checklist 1.6)', '',
                '| quantity | reflections on | reflections off | ratio |', '|---|---|---|---|',
                f'| auto-exposure operating point | {on["auto_exposure_mean"]:.2f} | {off["auto_exposure_mean"]:.2f} | '
                f'{rc["exposure_ratio_off_over_on"]:.2f}x |',
                f'| dark-side radiance | {on["dark_mean_radiance"]:.5f} | {off["dark_mean_radiance"]:.5f} | '
                f'{rc["dark_radiance_ratio_off_over_on"]:.2f}x |',
                f'| boundary radiance | {on["boundary_mean_radiance"]:.4f} | {off["boundary_mean_radiance"]:.4f} | '
                f'{off["boundary_mean_radiance"] / on["boundary_mean_radiance"]:.2f}x |',
                f'| boundary temporal std | {on["boundary_std_radiance"]:.5f} | {off["boundary_std_radiance"]:.5f} | '
                f'{rc["boundary_std_ratio_off_over_on"]:.2f}x |', '',
                '`r.GI.Reflections=0` is **not** a subtraction. In `deferred_lighting_ps.hlsl` the',
                'specular radiance is initialised from the prefiltered IBL cubemap and only *overwritten*',
                'by the SDF trace when reflections are enabled, so turning them off replaces an occluded',
                'traced term with an unoccluded environment lookup. Inside this interior that makes the',
                'dark side 11x brighter, which drops the auto-exposure operating point by 16x, which is',
                'what moved the display counts in the 2026-09-09 package. The oscillation itself barely',
                'moves (boundary temporal std 0.0544 -> 0.0537, 0.99x), so reflections are not its source',
                'and no energy-amplification factor may be derived from the count ratio.', '']

    out += ['## Radiometric attribution', '',
            '| stage | what it contributes | measured |', '|---|---|---|',
            '| upstream HDR | the oscillation that exists at all | boundary radiance std 0.054 on a mean of 1.93 (2.8 %); bright side 0.021 on 8.25 (0.26 %) |',
            '| exposure | a gain that scales how many pixels cross a display threshold | identical HDR: 0.7 / 1.7 / 22.0 px > 40 per frame at 0.5x / 1x / 2x |',
            '| auto-exposure | sets that gain (66.31 here) and is stable when still | swing 0.03 % over the sequence; frame-time dependent between runs |',
            '| bloom | lifts the dark floor 2.4x and halves local contrast | counts move in either direction depending on the exposure |',
            '| tonemap curve | compresses the bright side | the pane sits at 233 code values and its 0.26 % radiance swing is 0.04 code values |', '',
            'The visible flicker is therefore an upstream radiance oscillation of a few per cent, amplified',
            'into visibility by the exposure the interior needs, on pixels whose own radiance is small.', '']
    path = os.path.join(args.dir, 'report.md')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
