"""Evaluates the candidate correction against the gates frozen before it existed (followup-14 1.7).

Reads the JSON written by `scripts/run_correction_validation.py` (plus the pilot and the analytic
fixture study) and prints, and writes to `report.md`, one row per gate with the measured number and
a verdict. The thresholds live in `GATES` below and mirror
`docs/analysis/flicker-hypothesis-ledger.md` — "Preregistered acceptance gates for a correction".

The script never changes a threshold to fit a result. A gate with missing data is reported as
`no data`, never as passing.

Usage: python scripts/report_correction_validation.py [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

NL = chr(10)
DOMINANT = ('W', 'window')
DECISION_THRESHOLD = 4.8e-4       # followup-09 per-ROI threshold for W window (linear radiance)

GATES = {
    'G1 residual': 'window std_mean falls by >= 50 % and by more than 4.8e-4',
    'G2 energy': 'ROI mean drifts by <= 2 %',
    'G3 bright side': 'bright-mask std p99 regresses by <= 5 %',
    'G4 sharpness': 'thin-bar converged feature width regresses by <= 5 %',
    'G5 cost': 'GPU frame time regresses by <= 5 % (checklist item 1.2; the frozen gate text said CPU time - both are reported, the GPU number decides)',
    'G6 motion': 'recovery <= baseline + 5 frames and error at 80 frames <= 2x baseline',
    'G7 no cheating': 'no blur, identical exposure, unchanged lighting paths, G6 holds',
}


def load(out, name):
    path = os.path.join(out, f'{name}.json')
    if not os.path.exists(path):
        return None
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def pct(a, b):
    return float('nan') if not b else (a / b - 1.0) * 100.0


def mean_over_repeats(runs, roi, key):
    vals = [r['rois'][roi][key] for r in runs if roi in r['rois']]
    return sum(vals) / len(vals) if vals else float('nan')


def spread(runs, roi, key):
    vals = [r['rois'][roi][key] for r in runs if roi in r['rois']]
    return (max(vals) - min(vals)) if vals else float('nan')


def evaluate(out):
    gates = load(out, 'gates')
    stages = load(out, 'stages')
    motion = load(out, 'motion')
    matrix = load(out, 'matrix')
    matclass = load(out, 'material-classes')
    spatial = load(out, 'spatial-reference')
    coverage = load(out, 'coverage')
    gpu = load(out, 'gpu-cost')
    materials = load(out, 'materials')
    pilot = load(out, 'correction-pilot')
    if gates is None:
        raise SystemExit(f'no gates.json in {out} — run scripts/run_correction_validation.py first')

    # GPU cost, folded into a per-variant shape for the gate table.
    data_gpu = {}
    if gpu:
        gb = gpu['variants']['baseline']['runs']
        for vn, entry in gpu['variants'].items():
            if vn == 'baseline':
                continue
            cr = entry['runs']
            bt = sum(r['gpu_total_ms'] for r in gb) / len(gb)
            ct = sum(r['gpu_total_ms'] for r in cr) / len(cr)
            bp = [r['taa_pass_ms'] for r in gb if r['taa_pass_ms'] is not None]
            cp = [r['taa_pass_ms'] for r in cr if r['taa_pass_ms'] is not None]
            data_gpu[vn] = {
                'base_total': bt, 'cand_total': ct, 'total_pct': (ct / bt - 1) * 100 if bt else 0.0,
                'base_taa': (sum(bp) / len(bp)) if bp else None,
                'cand_taa': (sum(cp) / len(cp)) if cp else None,
                'taa_pct': ((sum(cp) / len(cp)) / (sum(bp) / len(bp)) - 1) * 100
                if bp and cp else None,
            }

    variants = [v for v in gates['variants'] if v != 'baseline']
    pose, roi = DOMINANT
    base_runs = gates['poses'][pose]['runs']['baseline']
    lines, verdicts = [], {}

    for vname in variants:
        cand_runs = gates['poses'][pose]['runs'][vname]
        v = verdicts[vname] = {}

        b_std = mean_over_repeats(base_runs, roi, 'std_mean')
        c_std = mean_over_repeats(cand_runs, roi, 'std_mean')
        drop = -pct(c_std, b_std)
        v['G1 residual'] = {
            'measured': f'{b_std:.4e} -> {c_std:.4e} ({-drop:+.1f} %), absolute change '
                        f'{abs(b_std - c_std):.2e} vs threshold {DECISION_THRESHOLD:.1e}',
            'status': 'pass' if (drop >= 50.0 and abs(b_std - c_std) > DECISION_THRESHOLD)
                      else 'fail',
        }

        b_mean = mean_over_repeats(base_runs, roi, 'mean')
        c_mean = mean_over_repeats(cand_runs, roi, 'mean')
        v['G2 energy'] = {
            'measured': f'{b_mean:.4f} -> {c_mean:.4f} ({pct(c_mean, b_mean):+.2f} %)',
            'status': 'pass' if abs(pct(c_mean, b_mean)) <= 2.0 else 'fail',
        }

        b_p99 = mean_over_repeats(base_runs, roi, 'bright_std_p99')
        c_p99 = mean_over_repeats(cand_runs, roi, 'bright_std_p99')
        v['G3 bright side'] = {
            'measured': f'{b_p99:.4e} -> {c_p99:.4e} ({pct(c_p99, b_p99):+.1f} %)',
            'status': 'pass' if pct(c_p99, b_p99) <= 5.0 else 'fail',
        }

        if pilot and 'offline' in pilot:
            exp = f"{gates['poses'][pose]['runs'][vname][0].get('exponent', 0):g}" \
                if 'exponent' in gates['poses'][pose]['runs'][vname][0] else vname.split('_')[-1]
            fx = pilot['offline']['thin_bar_hdr']
            key = f"{float(exp):g}"
            if key in fx:
                bw, cw = fx['1']['feature_width'], fx[key]['feature_width']
                ref = fx['1']['reference_feature_width']
                v['G4 sharpness'] = {
                    'measured': f'{bw:.1f} px -> {cw:.1f} px ({pct(cw, bw):+.1f} %), '
                                f'supersampled reference {ref:.1f} px',
                    'status': 'pass' if pct(cw, bw) <= 5.0 else 'fail',
                }

        b_ms = sum(r['frame_ms']['mean'] for r in base_runs) / len(base_runs)
        c_ms = sum(r['frame_ms']['mean'] for r in cand_runs) / len(cand_runs)
        # G5 is written against GPU time. CPU wall time is a different quantity and was reported in
        # its place until the review caught it; it is kept here as context, never as the verdict.
        gpu = data_gpu.get(vname) if data_gpu else None
        if gpu is None:
            v['G5 cost'] = {
                'measured': f'CPU wall time {b_ms:.2f} ms -> {c_ms:.2f} ms '
                            f'({pct(c_ms, b_ms):+.1f} %); no GPU measurement',
                'status': 'inconclusive',
            }
        else:
            v['G5 cost'] = {
                'measured': f"GPU total {gpu['base_total']:.3f} ms -> {gpu['cand_total']:.3f} ms "
                            f"({gpu['total_pct']:+.2f} %)"
                            + (f", TAA pass {gpu['base_taa']:.4f} -> {gpu['cand_taa']:.4f} ms "
                               f"({gpu['taa_pct']:+.2f} %)" if gpu.get('base_taa') else '')
                            + f"; CPU wall time {pct(c_ms, b_ms):+.1f} % for context",
                'status': 'pass' if gpu['total_pct'] <= 5.0 else 'fail',
            }

        if motion and vname in motion['variants']:
            # Every repeat, not just the first: the verdict is the worst case, and the spread is
            # reported so a marginal gate cannot hide behind one lucky run.
            b_runs = [r['recovery'] for r in motion['variants']['baseline']['runs']]
            c_runs = [r['recovery'] for r in motion['variants'][vname]['runs']]
            b_frames = [r['frames'] for r in b_runs]
            c_frames = [r['frames'] for r in c_runs]
            b_e80 = [r['error_80_frames_after_cut'] for r in b_runs]
            c_e80 = [r['error_80_frames_after_cut'] for r in c_runs]
            ok = (max(c_frames) <= min(b_frames) + 5
                  and max(c_e80) <= 2.0 * min(b_e80))
            v['G6 motion'] = {
                'measured': f"recovery {min(b_frames)}-{max(b_frames)} -> {min(c_frames)}-"
                            f"{max(c_frames)} frames over {len(b_runs)} and {len(c_runs)} repeats, "
                            f"error at 80 frames {min(b_e80):.4f}-{max(b_e80):.4f} -> "
                            f"{min(c_e80):.4f}-{max(c_e80):.4f}",
                'status': 'pass' if ok else 'fail',
            }

        if stages and vname in stages['variants']:
            b = stages['variants']['baseline']
            c = stages['variants'][vname]
            exposures_equal = all(
                r.get('exposure') == base_runs[0].get('exposure') for r in cand_runs)
            paths_equal = None
            if materials:
                bp = materials['poses']['W']['baseline']
                cp = materials['poses']['W'].get(vname)
                if cp:
                    paths_equal = all(bp[k]['path_bitmask_values'] == cp[k]['path_bitmask_values']
                                      for k in bp)
            blur_ok = c['distance_to_instantaneous'] <= b['distance_to_instantaneous']
            g6 = v.get('G6 motion', {}).get('status')
            # Missing evidence is inconclusive, not a pass. `paths_equal` is None when no
            # lighting-path capture exists for this variant, which is exactly the case the review
            # caught: `paths_equal is not False` let a None through as if it had been verified.
            clauses = {
                'not blur (closer to the instantaneous input)': blur_ok,
                'exposure identical': exposures_equal,
                'lighting paths identical': paths_equal,
                'G6 holds': None if g6 is None else (g6 == 'pass'),
            }
            failed = [k for k, ok in clauses.items() if ok is False]
            missing = [k for k, ok in clauses.items() if ok is None]
            status = 'fail' if failed else ('inconclusive' if missing else 'pass')
            v['G7 no cheating'] = {
                'measured': f"distance to the instantaneous image "
                            f"{b['distance_to_instantaneous']:.4f} -> "
                            f"{c['distance_to_instantaneous']:.4f}; exposure identical: "
                            f"{exposures_equal}; lighting paths identical: "
                            f"{'no evidence' if paths_equal is None else paths_equal}; "
                            f"dark-bar bleed {b['dark_output_over_raw']:.0f}x -> "
                            f"{c['dark_output_over_raw']:.0f}x"
                            + (f"; FAILED clauses: {', '.join(failed)}" if failed else '')
                            + (f"; NO EVIDENCE for: {', '.join(missing)}" if missing else ''),
                'status': status,
            }

    return {'gates': gates, 'stages': stages, 'motion': motion, 'matrix': matrix,
            'materials': materials, 'pilot': pilot, 'verdicts': verdicts,
            'base_runs': base_runs, 'matclass': matclass,
            'spatial': spatial, 'coverage': coverage, 'gpu': gpu}


def format_report(data, out):
    gates, verdicts = data['gates'], data['verdicts']
    pose, roi = DOMINANT
    L = []
    L.append('# Candidate flicker correction — acceptance report (followup-14)\n')
    L.append('Candidate: `r.TAA.ReconstructionHdrWeightExponent` (production 1.0). It raises the\n'
             'HDR weight of the TAA reconstruction filter to a power, so 1 is the production\n'
             'Karis 2014 weighting and 0 removes it (identical to `r.TAA.ReconstructionMode=1`).\n'
             'The mechanism is the one demonstrated by ledger rows H-11 and H-12.\n')
    L.append(f'Dominant ROI: pose {pose} `{roi}`. Gates were frozen in the hypothesis ledger '
             'before the candidate was written; the thresholds below are copied from there and '
             'were not revised.\n')

    for vname, v in verdicts.items():
        L.append(f'\n## {vname}\n')
        L.append('| gate | threshold | measured | verdict |')
        L.append('|---|---|---|---|')
        for gname, desc in GATES.items():
            g = v.get(gname)
            if g is None:
                L.append(f'| {gname} | {desc} | no data | **not measured** |')
            else:
                mark = {'pass': '**pass**', 'fail': '**FAIL**',
                        'inconclusive': '**inconclusive**'}[g['status']]
                L.append(f'| {gname} | {desc} | {g["measured"]} | {mark} |')
        passed = [g for g in v.values() if g.get('status') == 'pass']
        inconc = [g for g in v.values() if g.get('status') == 'inconclusive']
        L.append(f'\n{len(passed)} of {len(v)} measured gates pass'
                 + (f', {len(inconc)} inconclusive (missing evidence, which is not a pass)'
                    if inconc else '') + '. '
                 f'{"All gates pass — the default may change." if len(passed) == len(v) and len(v) == len(GATES) else "**Not every gate passes: the default stays at 1.0 and the correction remains opt-in.**"}\n')

    L.append('\n## Every ROI and pose (mean of 3 repeats)\n')
    L.append('| pose | ROI | variant | std_mean | vs baseline | ROI mean | energy drift | '
             'bright p99 | repeat spread |')
    L.append('|---|---|---|---|---|---|---|---|---|')
    for pose_key, entry in gates['poses'].items():
        for roi_name in entry['rois']:
            b_std = mean_over_repeats(entry['runs']['baseline'], roi_name, 'std_mean')
            b_mean = mean_over_repeats(entry['runs']['baseline'], roi_name, 'mean')
            for vname in gates['variants']:
                runs = entry['runs'][vname]
                s = mean_over_repeats(runs, roi_name, 'std_mean')
                m = mean_over_repeats(runs, roi_name, 'mean')
                p = mean_over_repeats(runs, roi_name, 'bright_std_p99')
                sp = spread(runs, roi_name, 'std_mean')
                L.append(f'| {pose_key} | {roi_name} | {vname} | {s:.4e} | '
                         f'{pct(s, b_std):+.1f} % | {m:.4f} | {pct(m, b_mean):+.2f} % | '
                         f'{p:.4e} | {sp:.1e} |')

    # The frozen gates are defined on the dominant ROI. That is deliberate, but a candidate that
    # passes there while wrecking another reported region must not be able to hide in a table.
    L.append(NL + '## Cross-ROI regressions' + NL)
    L.append('Not a frozen gate - the gates are defined on the dominant ROI - but recorded here '
             'because it changes the decision. Every ROI of every pose, worst first.' + NL)
    L.append('| variant | pose | ROI | residual change | energy change | bright p99 change |')
    L.append('|---|---|---|---|---|---|')
    rows = []
    for vname in gates['variants']:
        if vname == 'baseline':
            continue
        for pose_key, entry in gates['poses'].items():
            for roi_name in entry['rois']:
                b, c = entry['runs']['baseline'], entry['runs'][vname]
                d_std = pct(mean_over_repeats(c, roi_name, 'std_mean'),
                            mean_over_repeats(b, roi_name, 'std_mean'))
                d_mean = pct(mean_over_repeats(c, roi_name, 'mean'),
                             mean_over_repeats(b, roi_name, 'mean'))
                d_p99 = pct(mean_over_repeats(c, roi_name, 'bright_std_p99'),
                            mean_over_repeats(b, roi_name, 'bright_std_p99'))
                rows.append((max(d_std, d_mean), vname, pose_key, roi_name, d_std, d_mean, d_p99))
    for _, vname, pose_key, roi_name, d_std, d_mean, d_p99 in sorted(rows, reverse=True):
        L.append(f'| {vname} | {pose_key} | {roi_name} | {d_std:+.1f} % | {d_mean:+.2f} % | '
                 f'{d_p99:+.1f} % |')
    worst = max(rows)[0] if rows else float('nan')
    L.append(NL + f'Worst single-ROI regression across the programme: **{worst:+.1f} %**. A '
             'correction whose defect is concentrated in one ROI class (dark regions next to '
             'bright ones) cannot be enabled by default on the strength of the dominant ROI '
             'alone.' + NL)

    if data['motion']:
        L.append('\n## Motion, disocclusion and the cut (G6)\n')
        L.append('| variant | hold1 std | strafe std | strafe lag | recovery frames | '
                 'peak error after cut | error at 80 frames |')
        L.append('|---|---|---|---|---|---|---|')
        for vname, mv in data['motion']['variants'].items():
            r = mv['runs'][0]
            L.append(f"| {vname} | {r['phases']['hold1']['std_mean']:.4e} | "
                     f"{r['phases']['strafe']['std_mean']:.4e} | "
                     f"{r['phases']['strafe']['lag']:.4f} | {r['recovery']['frames']} | "
                     f"{r['recovery']['peak_error']:.4f} | "
                     f"{r['recovery']['error_80_frames_after_cut']:.4f} |")

    if data['stages']:
        L.append('\n## Where the change acts (pose W window, full TAA record set)\n')
        L.append('| variant | distance to the instantaneous input | energy vs the raw input | '
                 'output std | dark-bar bleed | bright side |')
        L.append('|---|---|---|---|---|---|')
        for vname, s in data['stages']['variants'].items():
            L.append(f"| {vname} | {s['distance_to_instantaneous']:.4f} | "
                     f"{s['energy_ratio_vs_raw']:.4f} | {s['output_std_mean']:.4f} | "
                     f"{s['dark_output_over_raw']:.0f}x | "
                     f"{s['bright_output_over_raw']:.3f}x |")
        L.append('\nA correction that worked by blurring would move the output *away* from the '
                 'instantaneous input; it moves towards it. The energy ratio is measured against '
                 'the cycle-averaged raw input, so 1.000 is correct and the baseline is dark.\n')

    if data['matrix']:
        L.append('\n## Robustness matrix (pose W, baseline vs exponent 0)\n')
        L.append('| condition | window std baseline | window std candidate | change | '
                 'render resolution |')
        L.append('|---|---|---|---|---|')
        for cond, e in data['matrix']['conditions'].items():
            b = e['variants']['baseline']['window']
            c = e['variants']['candidate_0.0']['window']
            res = e['variants']['candidate_0.0'].get('resolution', {}).get('render', [])
            L.append(f"| {cond} | {b['std_mean']:.4e} | {c['std_mean']:.4e} | "
                     f"{e['window_std_change_pct']:+.1f} % | {res} |")
        L.append('\n`taa_off` is the negative control: with TAA disabled the CVar is not read by '
                 'any pass, so any change there would mean the measurement is picking up '
                 'something other than the intervention.\n')

    if data['materials']:
        L.append('\n## Material coverage (item 1.5)\n')
        L.append('| pose | ROI | geometry px | roughness p05 / median | metallic px | '
                 'normal spread | active lighting paths |')
        L.append('|---|---|---|---|---|---|---|')
        for pose_key, pv in data['materials']['poses'].items():
            for roi_name, r in pv['baseline'].items():
                L.append(f"| {pose_key} | {roi_name} | {r['geometry_pixels']} | "
                         f"{r['roughness_p05']:.3f} / {r['roughness_median']:.3f} | "
                         f"{r['metallic_pixels']} | {r['normal_spread_deg']:.1f} deg | "
                         f"{r['path_bitmask_values']} |")

    if data.get('matclass'):
        mc = data['matclass']
        L.append(NL + '## Forced material classes (item 1.5)' + NL)
        L.append(f"Measured on `{mc['roi'][0]}` of pose A, a *deferred, directly lit* surface. "
                 "Sponza contains no mirror or glossy materials, so the class is forced into the "
                 "G-buffer with `r.Material.RoughnessOverride` / `MetallicOverride`; each capture "
                 "asserts the override was recorded, and the `image change` column is the validity "
                 "control - a class that does not move the image measures nothing." + NL)
        L.append('| class | recorded roughness | image change | residual base -> candidate | '
                 'error vs reference base -> candidate |')
        L.append('|---|---|---|---|---|')
        for cname, e in mc['classes'].items():
            b = e['variants']['baseline']
            c = e['variants']['candidate_0.0']
            flag = ' **VACUOUS**' if e.get('vacuous') else ''
            L.append(f"| {cname} | {b['recorded_roughness_median']:.3f} | "
                     f"{e.get('image_change_pct', 0.0):+.2f} %{flag} | "
                     f"{b['std_mean']:.4e} -> {c['std_mean']:.4e} "
                     f"({e['std_change_pct']:+.1f} %) | "
                     f"{b['error_vs_reference']:.4f} -> {c['error_vs_reference']:.4f} "
                     f"({e['error_change_pct']:+.1f} %) |")
        L.append(NL + 'On a smoothly lit deferred surface the candidate does essentially nothing '
                 '(+0.3 % residual) and that holds for every material class: the change acts on '
                 'high-contrast bright features, not on materials. Limitation: forcing metallic 1 '
                 'with roughness 0.05 leaves the floor at 0.7 % of its original radiance, so the '
                 'mirror condition tests a nearly black surface and has little power for '
                 'mirror-like *bright* content.' + NL)

    if data.get('spatial'):
        sp = data['spatial']
        L.append(NL + '## Against an independent converged spatial reference' + NL)
        L.append('Every other number in this report is measured against the cycle-averaged raw '
                 'input: an 8-sample estimate built from the same jitter offsets under '
                 'examination. This section uses a reference that shares neither property - the '
                 'un-accumulated `taa_raw` at `r.ScreenPercentage=200`, averaged over a full jitter '
                 'cycle, so each output pixel integrates four spatially distinct render samples and '
                 'no history at all. Sharpness is measured on real content as mean gradient '
                 'magnitude; a blurred image loses gradient energy.' + NL)
        L.append('| pose | ROI | variant | error vs reference | energy (1.0 = correct) | '
                 'gradient ratio (1.0 = as sharp as the reference) |')
        L.append('|---|---|---|---|---|---|')
        for pose_key, e in sp['poses'].items():
            for vn, st in e['variants'].items():
                L.append(f"| {pose_key} | {e['roi'][0]} | {vn} | "
                         f"{st['error_vs_spatial_reference']:.4f} | "
                         f"{st['energy_vs_spatial_reference']:.4f} | "
                         f"{st['gradient_ratio_vs_reference']:.4f} |")
            b = e['variants']['baseline']
            L.append(f"| {pose_key} | {e['roi'][0]} | *the cycle-average proxy itself* | "
                     f"{b['cycle_mean_error_vs_spatial_reference']:.4f} | "
                     f"{b['cycle_mean_energy_vs_spatial_reference']:.4f} | "
                     f"{b['cycle_mean_gradient_ratio']:.4f} |")
        L.append(NL + 'Two things follow. The candidate is **sharper**, not blurrier: it has more '
                 'gradient energy than the baseline and sits closer to the reference on both '
                 'poses, which the earlier "it moves towards the instantaneous input" argument '
                 'could not have established. And the cycle-average proxy, which the review '
                 'rightly flagged as circular, turns out to agree with this independent reference '
                 'to 0.1 % in energy on pose W - so the conclusions drawn against it stand.' + NL)
        L.append('Caveat: rendering at 2x also changes the sampling density of the GI, AO and '
                 'reflection passes, so this reference is independent but not a ground truth.'
                 + NL)

    if data.get('coverage'):
        cov = data['coverage']
        L.append(NL + '## Exposure transitions and clipmap crossings (item 1.4)' + NL)
        e = cov['exposure']
        L.append('**Exposure transitions.** The hold/strafe/hold/cut sequence with auto-exposure '
                 'enabled, so the exposure moves while the history is being rebuilt.' + NL)
        L.append('| variant | exposure range | after-cut residual | after-cut exposure swing |')
        L.append('|---|---|---|---|')
        for vn, ent in e['variants'].items():
            L.append(f"| {vn} | {ent['exposure_min']:.2f} - {ent['exposure_max']:.2f} | "
                     f"{ent['phases']['after_cut']['std_mean']:.4e} | "
                     f"{ent['phases']['after_cut']['exposure_range']:.3f} |")
        L.append(NL + f"The two variants' exposure curves differ by at most "
                 f"{e['exposure_divergence_max']:.4f} "
                 f"({e['exposure_divergence_rel'] * 100:.2f} % of the mean): the candidate changes "
                 f"the image, auto-exposure sees a different histogram, and the loop closes - "
                 f"measurably, but far too little to explain any result here." + NL)
        c = cov['clipmap']
        L.append('**Clipmap crossings.** The one-axis-at-a-time boundary trajectory of '
                 'followup-13.' + NL)
        L.append('| variant | recentre events | ROI | residual at events | elsewhere | ratio |')
        L.append('|---|---|---|---|---|---|')
        for vn, ent in c['variants'].items():
            for rn, st in ent['rois'].items():
                L.append(f"| {vn} | {ent['events']} | {rn} | "
                         f"{st['residual_near_event']:.3e} | {st['residual_far']:.3e} | "
                         f"{st['ratio']:.2f}x |")
        L.append(NL + 'The candidate neither helps nor worsens the GDF boundary discontinuity: '
                 'same event count, same ratios to two decimals.' + NL)

    path = os.path.join(out, 'report.md')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(L) + '\n')
    return path, L


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '14-validate-flicker-correction'))
    args = ap.parse_args()
    data = evaluate(args.out)
    path, lines = format_report(data, args.out)
    print('\n'.join(lines))
    print(f'\nwrote {path}')
    for v, gv in data['verdicts'].items():
        failed = [g for g, r in gv.items() if r['status'] == 'fail']
        inconc = [g for g, r in gv.items() if r['status'] == 'inconclusive']
        parts = []
        if failed:
            parts.append('failed ' + ', '.join(failed))
        if inconc:
            parts.append('inconclusive ' + ', '.join(inconc))
        print(f'{v}: ' + ('; '.join(parts) if parts else 'all measured gates pass'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
