"""Renders the followup-11 evidence package report from `display-study.json`.

Corrected 2026-10-08. The bloom paragraph and the attribution row said that bloom's effect on the
threshold count changes sign with the exposure. The 2026-09-09 package they cited measured the same
direction at the operating point (bloom off raised px > 40 from 39 to 1547 per frame), so the bloom
text is now derived from the data and states the direction it finds (ledger H-16b). The narrative
numbers are computed from the JSON instead of being written in, so a re-run cannot inherit the first
run's values. The equivalence wording follows what the study measured: single-process bit-identity
in the 2026-09-09 package, a same-configuration floor from 2026-10-08 on. Reports of new packages also
show the repeats and the operating-exposure bloom step that `run_display_study.py` now records. An
existing report.md is only replaced with `--overwrite`.

Usage: python scripts/report_display_study.py --dir DIR [--overwrite]
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

#: The one earlier measurement of bloom at the auto-exposure operating point. Fixed historical data.
HISTORICAL_BLOOM = ('the 2026-09-09 package (`.screenshots/flicker-region/report.md`, post_bloom0 '
                    'against post_aefix, lines 256-268 and 322) measured bloom once at the auto-exposure '
                    'operating point (~67): turning it off raised px > 40 from 39 to 1547 per frame')

BLOOM_HEADER = ['| run | dark floor (code) | dark p99 | bright (code) | local contrast bright/dark '
                '| px > 40 per frame | px > 8 per frame |',
                '|---|---|---|---|---|---|---|']


def direction(before, after):
    if after < before:
        return 'lowers'
    if after > before:
        return 'raises'
    return 'does not change'


def bloom_row(label, v):
    return (f'| {label} | {v["dark_floor_mean_code"]:.2f} | {v["dark_floor_p99_code"]:.1f} | '
            f'{v["bright_mean_code"]:.1f} | {v["local_contrast_bright_over_dark"]:.1f} | '
            f'{v["count_gt_40_per_frame"]:.2f} | {v["count_gt_8_per_frame"]:.1f} |')


def count_phrase(v, key):
    if f'{key}_total' in v:
        return f'{v[f"{key}_total"]} crossings over {v["frame_pairs"]} frame pairs'
    return f'{v[f"{key}_per_frame"]:.2f} per frame pair'


def bloom_sentence(off, on):
    floor = on['dark_floor_mean_code'] / max(1e-9, off['dark_floor_mean_code'])
    contrast = on['local_contrast_bright_over_dark'] / max(1e-9, off['local_contrast_bright_over_dark'])
    return (f'Bloom multiplies the dark floor by {floor:.1f}x and the local contrast by {contrast:.2f}x. '
            f'It {direction(off["count_gt_40_per_frame"], on["count_gt_40_per_frame"])} the px > 40 count '
            f'({count_phrase(off, "count_gt_40")} without it, {count_phrase(on, "count_gt_40")} with it) '
            f'and {direction(off["count_gt_8_per_frame"], on["count_gt_8_per_frame"])} the px > 8 count '
            f'({off["count_gt_8_per_frame"]:.1f} -> {on["count_gt_8_per_frame"]:.1f} per frame pair).')


def equivalence_phrase(eq, what):
    if eq.get('ok'):
        return f'{what} indistinguishable from process-to-process variation ({eq["detail"]})'
    return f'{what} **not** indistinguishable from process-to-process variation ({eq["detail"]})'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=DEFAULT_DIR)
    ap.add_argument('--overwrite', action='store_true',
                    help='replace an existing report.md (the default directory holds the 2026-09-09 one)')
    args = ap.parse_args()
    if os.path.exists(os.path.join(args.dir, 'report.md')) and not args.overwrite:
        print(f'{args.dir} already holds a report.md; pass --overwrite to replace it '
              '(evidence packages are not overwritten by default)', file=sys.stderr)
        return 2
    d = json.load(open(os.path.join(args.dir, 'display-study.json'), encoding='utf-8'))
    u = d['units']

    out = ['# followup-11 — exposure, bloom and the display side', '',
           f'ROI {d["roi"]}, {d["frames"]} frames from frame {d["first_frame"]}, camera still'
           + (f', {d["repeats"]} processes per compared configuration.' if 'repeats' in d else '.'),
           f'Tonemap: ACES Hill fit, saturation {d["tonemap"]["saturation"]}, contrast '
           f'{d["tonemap"]["contrast"]} — the QA project\'s settings.', '',
           '## The offline replay reproduces the engine (checklist 1.1)', '',
           f'With bloom off the tonemap is a per-pixel function of the recorded radiance, and the replay',
           f'in `scripts/flicker/display_model.py` matches the engine PNG to at most',
           f'{max(d["replay_validation"]["max_code_delta"])} code value',
           f'({d["replay_validation"]["fraction_within_1"] * 100:.3f} % of pixels within 1, '
           f'{len(d["replay_validation"]["max_code_delta"])} frames). Every',
           'display claim below is made through that replay or through captured PNGs, never by assuming',
           'the transform.', '',
           '## The same pixels in four units (checklist 1.2)', '',
           '| mask | pixels | linear Rec.709 radiance | its temporal std | resolve YCoCg Y | display code value | its temporal std |',
           '|---|---|---|---|---|---|---|']
    for k, v in u.items():
        out.append(f'| {k} | {v["pixels"]} | {v["linear_rec709_mean"]:.5f} | {v["linear_rec709_std"]:.5f} | '
                   f'{v["ycocg_y_mean"]:.4f} | {v["display_code_mean"]:.2f} | {v["display_code_std"]:.3f} |')
    ycocg_ratio = u['all']['ycocg_y_mean'] / max(1e-12, u['all']['linear_rec709_mean'])
    out += ['', f'The resolve\'s YCoCg Y is about {ycocg_ratio:.1f}x the Rec.709 luminance for this content: '
            'the two are', 'different quantities and must never be compared to one another. The effective '
            'exposure is', f'recorded per frame ({d["exposure_per_frame"][0]} here, fixed).', '']

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
    if gpu and all('equivalence' in v for v in gpu.values()):
        gpu_sentence = ('On the GPU, '
                        + '; '.join(equivalence_phrase(v['equivalence'], f'at {k} the linear records are')
                                    for k, v in gpu.items()) + '.')
    else:
        gpu_desc = ', '.join(f'{k}: max delta {v["max_hdr_delta_vs_baseline"]:g}' for k, v in gpu.items())
        gpu_sentence = ('On the GPU the same exposures left the linear records **bit-identical** in these '
                        f'single-process runs ({gpu_desc}); since followup-14 that is not guaranteed '
                        'across processes.')
    lo, hi = sw['0.5x']['count_gt_40_per_frame'], sw['2.0x']['count_gt_40_per_frame']
    change = (f'multiplies the px > 40 count by {hi / lo:.1f}x' if lo > 0 else
              f'takes the px > 40 count from {lo:.1f} to {hi:.1f} per frame')
    out += ['', 'The count column comes from one HDR sequence replayed at three exposures, so it is a '
            f'property of the display transform alone: a 4x exposure change {change} with the scene '
            f'untouched. {gpu_sentence}', '']
    ae = d.get('auto_exposure')
    if ae:
        if 'equivalence_vs_fixed_baseline' in ae:
            ae_eq = equivalence_phrase(ae['equivalence_vs_fixed_baseline'],
                                       'leaving the linear records')
        else:
            ae_eq = ('leaving the linear records bit-identical to the fixed-exposure run in these '
                     'single-process runs')
        out += [f'Auto-exposure on this pose settles at **{ae["mean"]:.2f}** and swings '
                f'{ae["relative_swing"] * 100:.3f} % over the measured sequence, {ae_eq}. With the camera '
                'still it therefore sets the gain but does not itself oscillate; its frame-time dependence '
                '(followup-09) is what makes it a confound between runs of different cost.', '']

    bl = d.get('bloom', {})
    bloom_dirs = []
    if bl:
        up = d.get('bloom_upstream')
        exposure = d.get('bloom_exposure', 1.0)
        if up:
            up_text = equivalence_phrase(up, 'the recorded radiance is')
        else:
            up_text = ('the recorded radiance was bit-identical with bloom on and off in these '
                       'single-process runs')
        out += ['## Bloom (checklist 1.4)', '',
                f'Captured on the identical HDR sequence at a fixed exposure of {exposure:g}: {up_text}, '
                'so everything below is display-side only.', ''] + BLOOM_HEADER
        for k, v in bl.items():
            out.append(bloom_row(k, v))
        for k, reps in d.get('bloom_repeats', {}).items():
            for i, v in enumerate(reps[1:], 1):
                out.append(bloom_row(f'{k} (process {i})', v))
        off, on = bl['bloom_off'], bl['bloom_on']
        here = direction(off['count_gt_40_per_frame'], on['count_gt_40_per_frame'])
        bloom_dirs.append((exposure, here))
        out += ['', bloom_sentence(off, on), '',
                f'For comparison, {HISTORICAL_BLOOM}, so bloom lowered the count there. '
                + ('Here it lowers it too: bloom masks the residual rather than revealing it, and a count '
                   'difference between runs with and without bloom says nothing about the oscillation.'
                   if here == 'lowers' else
                   'Here it does not change it, so this exposure shows no direction.'
                   if here == 'does not change' else
                   f'Here it {here} it, so on this content the direction depends on the exposure.'),
                '*(Corrected 2026-10-08: the first version of this report said the direction changes with '
                'the exposure, which misread the 2026-09-09 package; ledger H-16b.)*', '']

    bo = d.get('bloom_operating_exposure')
    if bo:
        out += ['### Bloom at the operating exposure (step 5b, added 2026-10-08)', '',
                f'Fixed exposure {bo["exposure"]:g}, the auto-exposure operating point measured below, '
                f'{len(bo["bloom_off"])} processes per condition: '
                f'{equivalence_phrase(bo["upstream"], "the recorded radiance is")}.', ''] + BLOOM_HEADER
        for k in ('bloom_off', 'bloom_on'):
            for i, v in enumerate(bo[k]):
                out.append(bloom_row(f'{k} (process {i})', v))
        per_process = [direction(a['count_gt_40_per_frame'], b['count_gt_40_per_frame'])
                       for a, b in zip(bo['bloom_off'], bo['bloom_on'])]
        bloom_dirs.append((bo['exposure'], per_process[0]))
        agree = ('the processes agree on the direction' if len(set(per_process)) == 1 else
                 f'the processes DISAGREE on the direction ({", ".join(per_process)})')
        out += ['', bloom_sentence(bo['bloom_off'][0], bo['bloom_on'][0]) + f' Across processes: {agree}.',
                '']

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
        b, dk, al = pe.get('bright'), pe.get('dark'), pe.get('all')
        if b and dk and al:
            bright_x = b['std_mean_preexpose'] / max(1e-12, b['std_mean_control'])
            dark_share = dk['mean_preexpose'] / max(1e-12, dk['mean_control']) * 100
            text = (f'At the exposure the scene actually runs at, the flag multiplies the bright side\'s '
                    f'temporal std by {bright_x:.1f}x (whole-ROI p99 {al["std_p99_control"]:.2f} → '
                    f'{al["std_p99_preexpose"]:.2f} radiance) and leaves the dark side at {dark_share:.0f} % of '
                    f'its radiance ({dk["mean_control"]:.4f} → {dk["mean_preexpose"]:.5f}).')
            if bright_x > 1.0 and dark_share < 100.0:
                text += (' A lower display count obtained this way is the dark side being crushed, not a '
                         'smaller oscillation.')
            out += ['', text + ' The flag is off by default (ledger H-08b).', '']

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
                f'{rc["boundary_std_ratio_off_over_on"]:.2f}x |', '']
        substitutes = (off['dark_mean_radiance'] > on['dark_mean_radiance']
                       and off['auto_exposure_mean'] < on['auto_exposure_mean'])
        bs = rc['boundary_std_ratio_off_over_on']
        if substitutes:
            text = ('`r.GI.Reflections=0` is **not** a subtraction. In `deferred_lighting_ps.hlsl` the '
                    'specular radiance is initialised from the prefiltered IBL cubemap and only *overwritten* '
                    'by the SDF trace when reflections are enabled, so turning them off replaces an occluded '
                    'traced term with an unoccluded environment lookup. Inside this interior that makes the '
                    f'dark side {rc["dark_radiance_ratio_off_over_on"]:.1f}x brighter, which lowers the '
                    f'auto-exposure operating point {1.0 / rc["exposure_ratio_off_over_on"]:.1f}x, which is what '
                    'moved the display counts in the 2026-09-09 package.')
        else:
            text = ('With reflections off the dark side and the auto-exposure did not move the way the '
                    '2026-09-09 substitution did (ledger H-02b), so the shader path needs re-reading before '
                    'that lesson is reused.')
        text += (f' The oscillation moves {bs:.2f}x (boundary temporal std {on["boundary_std_radiance"]:.4f} '
                 f'-> {off["boundary_std_radiance"]:.4f})'
                 + (', so reflections are not its source' if abs(bs - 1.0) < 0.05 else '')
                 + ', and no energy-amplification factor may be derived from a count ratio.')
        out += [text, '']

    b, bd = u['bright'], u['boundary']
    bloom_cell = '—'
    if bl:
        off, on = bl['bloom_off'], bl['bloom_on']
        bloom_cell = (f'multiplies the dark floor {on["dark_floor_mean_code"] / max(1e-9, off["dark_floor_mean_code"]):.1f}x '
                      f'and local contrast {on["local_contrast_bright_over_dark"] / max(1e-9, off["local_contrast_bright_over_dark"]):.2f}x')
    bloom_measured = '; '.join(f'{dirn} the px > 40 count at exposure {e:g}' for e, dirn in bloom_dirs)
    if bloom_measured:
        bloom_measured += '; lowered it at ~67 on 2026-09-09 (39 vs 1547 per frame without it)'
    out += ['## Radiometric attribution', '',
            '| stage | what it contributes | measured |', '|---|---|---|',
            f'| upstream HDR | the oscillation that exists at all | boundary radiance std '
            f'{bd["linear_rec709_std"]:.3f} on a mean of {bd["linear_rec709_mean"]:.2f} '
            f'({bd["linear_rec709_std"] / bd["linear_rec709_mean"] * 100:.1f} %); bright side '
            f'{b["linear_rec709_std"]:.3f} on {b["linear_rec709_mean"]:.2f} '
            f'({b["linear_rec709_std"] / b["linear_rec709_mean"] * 100:.2f} %) |',
            '| exposure | a gain that scales how many pixels cross a display threshold | identical HDR: '
            + ' / '.join(f'{sw[k]["count_gt_40_per_frame"]:.1f}' for k in ('0.5x', '1.0x', '2.0x'))
            + ' px > 40 per frame at 0.5x / 1x / 2x |']
    if ae:
        out.append(f'| auto-exposure | sets that gain ({ae["mean"]:.2f} here) and is stable when still | swing '
                   f'{ae["relative_swing"] * 100:.3f} % over the sequence; frame-time dependent between runs |')
    out += [f'| bloom | {bloom_cell} | {bloom_measured or "—"} |',
            f'| tonemap curve | compresses the bright side | the pane sits at {b["display_code_mean"]:.0f} code '
            f'values and its {b["linear_rec709_std"] / b["linear_rec709_mean"] * 100:.2f} % radiance swing is '
            f'{b["display_code_std"]:.2f} code values |', '',
            'The visible flicker is therefore an upstream radiance oscillation of a few per cent, amplified',
            'into visibility by the exposure the interior needs, on pixels whose own radiance is small.', '']
    path = os.path.join(args.dir, 'report.md')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
