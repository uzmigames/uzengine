"""Acceptance run for the candidate flicker correction (followup-14, checklist 1.4-1.7).

The gates were frozen in `docs/analysis/rendering-arch/flicker/flicker-hypothesis-ledger.md` before the candidate existed;
this script measures them. It never decides anything on its own — it writes the numbers, and
`scripts/rendering/report_correction_validation.py` prints the pass/fail table against the frozen thresholds.

Candidate: `r.TAA.ReconstructionHdrWeightExponent` (production 1.0). It softens the HDR weighting
inside the TAA reconstruction filter, the mechanism demonstrated by ledger rows H-11 (the
reconstruction, not the accumulation, creates the residual) and H-12 (the weighting costs energy on
thin bright features). Exponent 0 is exactly `r.TAA.ReconstructionMode=1`; 0.5 is the partial
variant H-14 asked followup-14 to test.

Stages (`--stage`, repeatable; `all` runs them in order):

  gates      3 poses x 3 variants x 3 repeats, still camera, 200 warm-up + 256 measured frames.
             G1 (residual), G2 (energy), G3 (bright-side p99), G5 (cost), and the exposure half of
             G7. Masks are frozen from the baseline's first repeat and reused for every variant.
  stages     pose W with the full TAA record set: distance between the accumulated output and the
             instantaneous input (the blur check of G7) and the bleed into the dark bars.
  materials  roughness/metallic/normal coverage of every ROI and the active lighting-path bitmask,
             from the full record set: the material-class part of item 1.5 and the "no missing
             lighting" half of G7.
  motion     the hold/strafe/hold/cut sequence, 3 variants x 3 repeats: G6.
  matrix     TAA off (negative control: the CVar must do nothing), 50 % internal resolution,
             reflections off / 4 samples / 16 samples: the rest of item 1.5.

Usage: python scripts/rendering/run_correction_validation.py [--stage gates] [--repeats 3] [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import analysis  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

CVAR = 'r.TAA.ReconstructionHdrWeightExponent'
VARIANTS = [('baseline', 1.0), ('candidate_0.5', 0.5), ('candidate_0.0', 0.0)]

SCENE_START = 37
WARMUP = 200
MEASURED = 256
FIRST = SCENE_START + WARMUP
LAST = FIRST + MEASURED - 1
RENDER_FRAMES = LAST - SCENE_START + 8
BASE = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}

POSES = {
    'W': {'project': 'sponza-flicker-w.uzproject',
          'rois': [('window', 770, 150, 192, 512), ('shadow_wall', 150, 260, 192, 384)]},
    'A': {'project': 'sponza-flicker-a.uzproject',
          'rois': [('rear_door', 960, 592, 224, 224), ('right_shadow', 1472, 464, 256, 256),
                   ('lit_floor', 832, 816, 256, 64)]},
    # Pose C ROIs chosen from a reference frame (see report): two blown-out barred windows — an
    # independent instance of the same bright-feature-against-dark-interior geometry — plus a deep
    # vault and a mid-tone pillar as controls.
    'C': {'project': 'sponza-lighting-camera-c.uzproject',
          'rois': [('arch_window', 130, 376, 160, 168), ('small_window', 500, 452, 112, 112),
                   ('dark_vault', 1664, 384, 192, 192), ('lit_pillar', 300, 700, 160, 200)],
          # Pose C's project predates the flicker programme and renders with TAA OFF and a
          # different GI configuration, so its records are not comparable with W and A. The first
          # attempt at this pose was rejected by the capture integrity check (every frame flagged
          # `taa_stamp_missing`) rather than silently analysed. These overrides give it the same
          # pipeline as the other two poses; the project file itself is left alone.
          'cvars': {'r.TAA': 1, 'r.GI.Reflections': 1, 'r.GI.GDF.Resolution': 256,
                    'r.GI.MeshSdf.Resolution': 128, 'r.GI.FinalGather': 1}},
}

MOTION_PROJECT = 'sponza-flicker-w-motion.uzproject'
MOTION_ROI = ('window', 770, 150, 192, 512)
MOTION_PHASES = {'hold1': (60, 118), 'strafe': (125, 238), 'hold2': (245, 298),
                 'after_cut': (303, 378)}
MOTION_FIRST = SCENE_START + 40
MOTION_LAST = SCENE_START + 382

MATRIX = [
    # NOTE: a `('taa_off', {'r.TAA': 0})` row used to live here as a negative control. It was
    # vacuous: the `output` record is written by the TAA resolve, so with the pass disabled
    # both variants recorded all zeros and the control 'passed' by comparing nothing with
    # nothing. The real control now lives in `scripts/rendering/test_correction_gates.py`, on the
    # lighting components, which the deferred pass writes either way.

    ('screen_percentage_50', {'r.ScreenPercentage': 50}),
    ('reflections_off', {'r.GI.Reflections': 0}),
    ('reflections_4', {'r.GI.Reflections': 1, 'r.GI.Reflections.Samples': 4}),
    ('reflections_16', {'r.GI.Reflections': 1, 'r.GI.Reflections.Samples': 16}),
    # The best combination of demonstrated mechanisms that is not already rejected. H-13 rejected
    # `ClampMode=1` (no clamp at all) because it keeps invalid history; `ClampMode=3` keeps the 3x3
    # AABB, which is still a disocclusion defence, and H-04b measured it at -9 % on this ROI. Run
    # here to put a number on the ceiling of what the demonstrated mechanisms can reach together.
    ('aabb_clamp_only', {'r.TAA.ClampMode': 3}),
]

#: Extra motion variants: anything the matrix suggests could pass G1 must also face the cut.
MOTION_EXTRA = [('combined_exp0_aabb', 0.0, {'r.TAA.ClampMode': 3}),
                ('baseline_aabb', 1.0, {'r.TAA.ClampMode': 3})]


def project(name):
    return os.path.join(runner.REPO, 'scripts', 'rendering', name)


def check_cvar_applied(res, value):
    """The manifest must show the CVar at the value we asked for.

    A silently ignored CVar produced a wrong conclusion once in this programme (the radiance-cache
    freeze that was a no-op); every capture is checked instead of trusted.
    """
    cvars = (res['manifest'] or {}).get('cvars', {})
    if CVAR not in cvars:
        raise RuntimeError(f'{CVAR} is not in the run manifest — the CVar does not exist in this '
                           f'build (see {res["manifest_path"]})')
    got = float(cvars[CVAR])
    if abs(got - value) > 1e-6:
        raise RuntimeError(f'{CVAR} is {got} in the manifest but {value} was requested')


def capture(work, label, proj, frames, *, regions, fields, first, last, cvars, exponent,
            timing_first=None):
    res = runner.run_capture(os.path.join(work, label), project(proj), frames, regions=regions,
                             region_fields=fields, region_first=first, region_last=last,
                             cvars={**BASE, **cvars, CVAR: f'{exponent:g}'},
                             timing_first=timing_first if timing_first is not None else first)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    check_cvar_applied(res, exponent)
    return res


# --------------------------------------------------------------------------- gates

def stage_gates(work, out, repeats, poses=None):
    payload = {'variants': [v for v, _ in VARIANTS], 'poses': {}, 'protocol': {
        'warmup_frames': WARMUP, 'measured_frames': MEASURED, 'first_frame': FIRST,
        'base_cvars': BASE, 'repeats': repeats}}
    # Re-running one pose must not discard the others: merge into what is already on disk.
    existing = os.path.join(out, 'gates.json')
    if poses and os.path.exists(existing):
        with open(existing, encoding='utf-8') as f:
            payload = json.load(f)
        payload['protocol']['repeats'] = repeats
    os.makedirs(os.path.join(out, 'masks'), exist_ok=True)
    os.makedirs(os.path.join(out, 'manifests'), exist_ok=True)
    os.makedirs(os.path.join(out, 'csv'), exist_ok=True)

    for pose_key, pose in POSES.items():
        if poses and pose_key not in poses:
            continue
        entry = {'project': pose['project'], 'rois': {}, 'runs': {},
                 'pose_cvars': pose.get('cvars', {})}
        bright_masks = {}
        for vname, exponent in VARIANTS:
            entry['runs'][vname] = []
            for rep in range(repeats):
                label = f'gates_{pose_key}_{vname}_{rep}'.replace('.', 'p')
                print(f'  … {pose_key} {vname} repeat {rep + 1}/{repeats}', flush=True)
                res = capture(work, label, pose['project'], RENDER_FRAMES, regions=pose['rois'],
                              fields='output', first=FIRST, last=LAST,
                              cvars=pose.get('cvars', {}), exponent=exponent)
                r = analysis.analyse_run(res['region_dir'], FIRST, LAST,
                                         csv_dir=os.path.join(out, 'csv') if rep == 0 else None,
                                         label=f'{pose_key}-{vname}')
                run = {'repeat': rep, 'rois': {},
                       'frame_ms': (res['manifest'] or {}).get('frame_ms', {}),
                       'exposure': (res['manifest'] or {}).get('exposure'),
                       'integrity': r['integrity']['ok']}
                for name, s in r['rois'].items():
                    std, mean = s['per_pixel_std'], s['per_pixel_mean']
                    if vname == 'baseline' and rep == 0:
                        # Frozen once, from the baseline's first repeat, and reused unchanged for
                        # every variant and repeat: no candidate ever selects its own pixels.
                        thr = float(np.percentile(mean, 75))
                        bright_masks[name] = mean >= thr
                        analysis.mask_png(os.path.join(out, 'masks',
                                                       f'bright-{pose_key}-{name}.png'),
                                          bright_masks[name].astype(float), 1.0)
                        analysis.mask_png(os.path.join(out, 'masks',
                                                       f'std-{pose_key}-{name}-baseline.png'), std)
                        entry['rois'][name] = {'rect': [x for x in
                                                        next(r for r in pose['rois']
                                                             if r[0] == name)[1:]],
                                               'pixels': int(mean.size),
                                               'bright_threshold': thr,
                                               'bright_pixels': int(bright_masks[name].sum())}
                    bm = bright_masks[name]
                    run['rois'][name] = {
                        'mean': s['mean'], 'std_mean': s['std_mean'], 'std_p99': s['std_p99'],
                        'abs_diff_mean': s['abs_diff_mean'], 'pixels_valid': s['pixels_valid'],
                        'bright_std_p99': float(np.percentile(std[bm], 99)),
                        'bright_std_mean': float(std[bm].mean()),
                        'bright_mean': float(mean[bm].mean()),
                        'dark_std_mean': float(std[~bm].mean()),
                        'dark_mean': float(mean[~bm].mean()),
                    }
                    if vname == 'candidate_0.0' and rep == 0:
                        analysis.mask_png(os.path.join(out, 'masks',
                                                       f'std-{pose_key}-{name}-candidate.png'), std)
                entry['runs'][vname].append(run)
                if rep == 0:
                    shutil.copy(res['manifest_path'],
                                os.path.join(out, 'manifests', f'{label}.json'))
                shutil.rmtree(res['region_dir'], ignore_errors=True)
        payload['poses'][pose_key] = entry
        # Written after every pose: a failure in a later pose must not discard hours of captures.
        payload['source_fingerprint'] = runner.source_fingerprint()
        with open(os.path.join(out, 'gates.json'), 'w', encoding='utf-8') as f:
            json.dump(payload, f, indent=2)
        for name in entry['rois']:
            b = entry['runs']['baseline'][0]['rois'][name]
            c = entry['runs']['candidate_0.0'][0]['rois'][name]
            print(f"      {pose_key} {name}: std_mean {b['std_mean']:.4e} -> {c['std_mean']:.4e} "
                  f"({(c['std_mean'] / b['std_mean'] - 1) * 100:+.1f} %), "
                  f"mean {(c['mean'] / b['mean'] - 1) * 100:+.2f} %")
    return payload


# --------------------------------------------------------------------------- stages / blur

#: Poses and ROIs measured against the cycle-averaged raw input. A change is only a *correction*
#: if it moves the output towards the correctly resampled image; measured against the baseline
#: alone, an error and a fix look the same.
REFERENCE_ROIS = [('W', 'window'), ('A', 'rear_door'), ('C', 'small_window')]


def stage_stages(work, out, frames=24):
    """Per-pixel error against the correctly resampled reference, and the bleed into black pixels."""
    payload = {'frames': frames, 'rois': {}}
    for pose_key, roi_name in REFERENCE_ROIS:
        payload['rois'][f'{pose_key}.{roi_name}'] = stage_stages_one(work, pose_key, roi_name,
                                                                     frames)
    # Kept for the report's existing shape: pose W is the dominant ROI.
    payload['variants'] = payload['rois']['W.window']['variants']
    payload['dark_pixels'] = payload['rois']['W.window']['dark_pixels']
    payload['bright_pixels'] = payload['rois']['W.window']['bright_pixels']
    return payload


def stage_stages_one(work, pose_key, roi_name, frames):
    first, last = SCENE_START + 200, SCENE_START + 200 + frames - 1
    pose = POSES[pose_key]
    roi = next(r for r in pose['rois'] if r[0] == roi_name)
    payload = {'pose': pose_key, 'roi': roi_name, 'first_frame': first, 'frames': frames,
               'variants': {}}
    dark_mask = None
    for vname, exponent in VARIANTS:
        print(f'  … stages {pose_key}.{roi_name} {vname}', flush=True)
        res = capture(work, f'stages_{pose_key}_{roi_name}_{vname}'.replace('.', 'p'),
                      pose['project'], last - SCENE_START + 8, regions=[roi], fields='taa+extra',
                      first=first, last=last, cvars=pose.get('cvars', {}), exponent=exponent)
        seq = regiondump.load_sequence(res['region_dir'], first, last)
        out_l = np.stack([f.rect(roi_name).luminance('taa_output') for f in seq]).astype(np.float64)
        raw_l = np.stack([f.rect(roi_name).luminance('taa_raw') for f in seq]).astype(np.float64)
        filt = np.stack([f.rect(roi_name).luminance('taa_filtered') for f in seq]).astype(np.float64)
        cycle = raw_l.reshape(-1, 8, *raw_l.shape[1:]).mean(axis=1).mean(axis=0) \
            if len(seq) % 8 == 0 else raw_l.mean(axis=0)
        if dark_mask is None:
            # The dark bars: pixels whose own instantaneous input is in the bottom decile of the
            # cycle-averaged raw image. Frozen from the baseline and reused.
            dark_mask = cycle <= np.percentile(cycle, 10)
            bright_mask = cycle >= np.percentile(cycle, 90)
            payload['dark_pixels'] = int(dark_mask.sum())
            payload['bright_pixels'] = int(bright_mask.sum())
        payload['variants'][vname] = {
            'exponent': exponent,
            # blur/lag check (G7): how far the accumulated image sits from the instantaneous input
            'distance_to_instantaneous': float(np.abs(out_l - raw_l).mean()),
            'distance_to_cycle_mean': float(np.abs(out_l - cycle[None]).mean()),
            'energy_ratio_vs_raw': float(out_l.mean() / cycle.mean()),
            'filtered_std_mean': float(filt.std(axis=0).mean()),
            'output_std_mean': float(out_l.std(axis=0).mean()),
            'raw_std_mean': float(raw_l.std(axis=0).mean()),
            # bleed into the dark bars, the cost H-14 recorded for dropping the weighting
            'dark_output_over_raw': float(out_l[:, dark_mask].mean() / cycle[dark_mask].mean()),
            'dark_output_mean': float(out_l[:, dark_mask].mean()),
            'dark_raw_mean': float(cycle[dark_mask].mean()),
            'bright_output_over_raw': float(out_l[:, bright_mask].mean() / cycle[bright_mask].mean()),
        }
        v = payload['variants'][vname]
        print(f"      error vs reference {v['distance_to_cycle_mean']:.4f}, "
              f"bright energy {v['bright_output_over_raw']:.4f}, "
              f"black-pixel bleed {v['dark_output_mean']:.5f} "
              f"(correct {v['dark_raw_mean']:.2e})", flush=True)
        shutil.rmtree(res['region_dir'], ignore_errors=True)
    return payload


# --------------------------------------------------------------------------- materials / paths

def stage_materials(work, out, frames=8):
    """Material classes covered by each ROI, and the active lighting paths, per variant."""
    payload = {'frames': frames, 'poses': {}}
    for pose_key in ('W', 'C'):
        pose = POSES[pose_key]
        first, last = SCENE_START + 200, SCENE_START + 200 + frames - 1
        entry = {}
        # All three variants: G7 asks whether the lighting paths changed, and a variant with
        # no capture makes that clause inconclusive rather than passing.
        for vname, exponent in VARIANTS:
            print(f'  … materials {pose_key} {vname}', flush=True)
            res = capture(work, f'mat_{pose_key}_{vname}'.replace('.', 'p'), pose['project'],
                          last - SCENE_START + 8, regions=pose['rois'], fields='full',
                          first=first, last=last, cvars=pose.get('cvars', {}), exponent=exponent)
            seq = regiondump.load_sequence(res['region_dir'], first, last)
            rois = {}
            for name, *_ in pose['rois']:
                rough = seq[0].rect(name).channel('lighting_reflection')[..., 3]
                metal = seq[0].rect(name).channel('specular_indirect')[..., 3]
                normal = seq[0].rect(name).channel('lighting_normal')[..., :3]
                paths = np.stack([f.rect(name).channel('emissive_and_mis')[..., 3].view(np.uint32)
                                  for f in seq])
                lum = np.stack([f.rect(name).luminance('taa_output') for f in seq]).astype(np.float64)
                geo = rough >= 0.0
                bins = {'mirror<=0.1': (rough >= 0) & (rough <= 0.1),
                        'glossy<=0.3': (rough > 0.1) & (rough <= 0.3),
                        'rough<=0.6': (rough > 0.3) & (rough <= 0.6),
                        'very_rough': rough > 0.6}
                rois[name] = {
                    'geometry_pixels': int(geo.sum()),
                    'roughness_p05': float(np.percentile(rough[geo], 5)) if geo.any() else None,
                    'roughness_median': float(np.median(rough[geo])) if geo.any() else None,
                    'metallic_pixels': int((metal > 0.5).sum()),
                    # normal maps: a flat-shaded surface has one normal, a normal-mapped one does
                    # not. The spread of the shading normal over the rectangle is the evidence.
                    'normal_spread_deg': float(np.degrees(np.arccos(np.clip(
                        (normal[geo] * normal[geo].mean(axis=0)).sum(axis=-1)
                        / max(1e-9, np.linalg.norm(normal[geo].mean(axis=0))), -1, 1))).std())
                    if geo.any() else None,
                    'path_bitmask_values': sorted(int(v) for v in np.unique(paths)),
                    'bins': {k: {'pixels': int(m.sum()),
                                 'std_mean': float(lum[:, m].std(axis=0).mean()) if m.any() else None}
                             for k, m in bins.items()},
                }
            entry[vname] = rois
            shutil.rmtree(res['region_dir'], ignore_errors=True)
        payload['poses'][pose_key] = entry
    return payload


# --------------------------------------------------------------------------- motion

def motion_stats(region_dir):
    seq = regiondump.load_sequence(region_dir, MOTION_FIRST, MOTION_LAST)
    out_l = np.stack([f.rect('window').luminance('taa_output') for f in seq]).astype(np.float64)
    raw_l = np.stack([f.rect('window').luminance('taa_raw') for f in seq]).astype(np.float64)
    rel = np.array([f.frame - SCENE_START for f in seq])
    res = {'phases': {}}
    for name, (lo, hi) in MOTION_PHASES.items():
        idx = np.flatnonzero((rel >= lo) & (rel <= hi))
        res['phases'][name] = {
            'frames': [int(rel[idx[0]]), int(rel[idx[-1]])],
            'std_mean': float(out_l[idx].std(axis=0).mean()),
            'lag': float(np.abs(out_l[idx] - raw_l[idx]).mean()),
            'mean': float(out_l[idx].mean()),
        }
    hold1 = np.flatnonzero((rel >= MOTION_PHASES['hold1'][0]) & (rel <= MOTION_PHASES['hold1'][1]))
    converged = out_l[hold1].mean(axis=0)
    tol = 0.02 * float(converged.mean())
    after = np.flatnonzero(rel >= 301)
    err = np.abs(out_l[after] - converged[None]).mean(axis=(1, 2))
    bad = np.flatnonzero(err > tol)
    res['recovery'] = {
        'tolerance': tol,
        'frames': int(bad[-1] + 1) if len(bad) else 0,
        'captured_after_cut': int(len(after)),
        'peak_error': float(err.max()) if len(err) else float('nan'),
        'error_8_frames_after_cut': float(err[min(8, len(err) - 1)]) if len(err) else float('nan'),
        'error_80_frames_after_cut': float(err[min(79, len(err) - 1)]) if len(err) else float('nan'),
    }
    return res


def stage_motion(work, out, repeats):
    payload = {'phases': MOTION_PHASES, 'variants': {}}
    plan = [(n, e, {}) for n, e in VARIANTS] + MOTION_EXTRA
    for vname, exponent, extra in plan:
        runs = []
        # Every variant gets every repeat. This used to be `repeats if not extra else 1`, so the
        # extra motion variants were single measurements compared against a baseline measured
        # several times — the same defect fixed in run_gdf_motion_study.py on 2026-09-15.
        reps = repeats
        for rep in range(reps):
            print(f'  … motion {vname} repeat {rep + 1}/{reps}', flush=True)
            res = capture(work, f'motion_{vname}_{rep}'.replace('.', 'p'), MOTION_PROJECT,
                          MOTION_LAST - SCENE_START + 8, regions=[MOTION_ROI], fields='taa',
                          first=MOTION_FIRST, last=MOTION_LAST, cvars=extra, exponent=exponent)
            runs.append(motion_stats(res['region_dir']))
            if rep == 0:
                shutil.copy(res['manifest_path'],
                            os.path.join(out, 'manifests', f'motion_{vname}.json'.replace('.', 'p', 1)))
            shutil.rmtree(res['region_dir'], ignore_errors=True)
        payload['variants'][vname] = {'exponent': exponent, 'cvars': extra, 'runs': runs}
        r = runs[0]
        print(f"      hold1 std {r['phases']['hold1']['std_mean']:.4e}, "
              f"strafe lag {r['phases']['strafe']['lag']:.4f}, "
              f"recovery {r['recovery']['frames']} frames, "
              f"err@80 {r['recovery']['error_80_frames_after_cut']:.4f}")
    return payload


# --------------------------------------------------------------------------- matrix

def stage_matrix(work, out):
    pose = POSES['W']
    payload = {'conditions': {}}
    for cond, cvars in MATRIX:
        entry = {'cvars': cvars, 'variants': {}}
        for vname, exponent in (VARIANTS[0], VARIANTS[2]):
            print(f'  … matrix {cond} {vname}', flush=True)
            res = capture(work, f'mx_{cond}_{vname}'.replace('.', 'p'), pose['project'],
                          RENDER_FRAMES, regions=pose['rois'], fields='output',
                          first=FIRST, last=LAST, cvars=cvars, exponent=exponent)
            r = analysis.analyse_run(res['region_dir'], FIRST, LAST,
                                     allow_taa_missing=cvars.get('r.TAA') == 0)
            entry['variants'][vname] = {
                name: {k: s[k] for k in ('mean', 'std_mean', 'std_p99', 'abs_diff_mean')}
                for name, s in r['rois'].items()}
            entry['variants'][vname]['frame_ms'] = (res['manifest'] or {}).get('frame_ms', {})
            entry['variants'][vname]['resolution'] = (res['manifest'] or {}).get('resolution', {})
            shutil.rmtree(res['region_dir'], ignore_errors=True)
        b = entry['variants']['baseline']['window']
        c = entry['variants']['candidate_0.0']['window']
        entry['window_std_change_pct'] = (c['std_mean'] / b['std_mean'] - 1) * 100 \
            if b['std_mean'] else float('nan')
        if cvars.get('r.TAA') == 0:
            # Negative control: with no TAA pass the CVar cannot be read, so the two runs must
            # agree to the last bit. Anything else means the measurement is picking up something
            # other than the intervention, and the whole matrix is suspect.
            entry['negative_control_identical'] = bool(
                b['std_mean'] == c['std_mean'] and b['mean'] == c['mean'])
        print(f"      {cond}: window std {b['std_mean']:.4e} -> {c['std_mean']:.4e} "
              f"({entry['window_std_change_pct']:+.1f} %)")
        payload['conditions'][cond] = entry
    return payload


STAGES = {'gates': None, 'stages': None, 'materials': None, 'motion': None, 'matrix': None}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', action='append', choices=list(STAGES) + ['all'], default=None)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--poses', help='comma-separated subset for the gates stage (default: all)')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '14-validate-flicker-correction'))
    args = ap.parse_args()
    runner.require_exe()
    wanted = args.stage or ['all']
    if 'all' in wanted:
        wanted = list(STAGES)
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_correction_validation')

    for name in wanted:
        print(f'\n== stage {name} ==', flush=True)
        if name == 'gates':
            data = stage_gates(work, args.out, args.repeats,
                               poses=args.poses.split(',') if args.poses else None)
        elif name == 'stages':
            data = stage_stages(work, args.out)
        elif name == 'materials':
            data = stage_materials(work, args.out)
        elif name == 'motion':
            data = stage_motion(work, args.out, args.repeats)
        else:
            data = stage_matrix(work, args.out)
        data['source_fingerprint'] = runner.source_fingerprint()
        path = os.path.join(args.out, f'{name}.json')
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2)
        print('wrote', path, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
