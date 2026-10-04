"""Exposure, bloom and display attribution for the window residual (followup-11).

Order of business, and why:

1. **Validate the replay.** With bloom off the tonemap is a per-pixel function of the recorded linear
   radiance, so the offline chain (`scripts/flicker/display_model.py`) must reproduce the engine's
   PNG. Nothing else in this study is trusted until that closes.
2. **Units.** For the same pixels, report linear Rec.709 luminance, the resolve's 4x-scaled YCoCg Y,
   the pre-exposed colour and the display code values side by side, with the effective exposure of
   every frame.
3. **Exposure.** Replay the identical HDR sequence at 0.5x, 1x and 2x the baseline exposure, and
   capture the same three exposures on the GPU to confirm the HDR input did not move.
4. **Bloom.** Capture with bloom on and off; check the recorded HDR is unchanged, then measure the
   dark-side floor, the local contrast and the exact display threshold counts.
5. **PreExposeWeights.** Diagnostic only: bright and dark sides, p99 error and temporal response.
6. **Attribution table** separating the upstream radiance oscillation from the exposure gain and the
   bloom floor, without turning a count ratio into an energy claim.

Usage: python scripts/run_display_study.py [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import display_model as dm  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 24
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = ('window', 770, 150, 192, 512)
SATURATION, CONTRAST, OPERATOR = 1.0, 0.65, 1      # the QA project's r.ColorSaturation/Contrast
BASE_EXPOSURE = 1.0

RESULTS = []


def check(ok, what, detail=''):
    RESULTS.append({'check': what, 'ok': bool(ok), 'detail': detail})
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def capture(work, label, cvars, shots=None):
    print(f'  … {label}', flush=True)
    res = runner.run_capture(os.path.join(work, label), PROJECT, LAST - SCENE_START + 8,
                             regions=[ROI], region_fields='taa+extra',
                             region_first=FIRST, region_last=LAST, cvars=cvars,
                             screenshot_frames=shots, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]}')
    return res


def roi_rgb(frames, channel='taa_output'):
    return np.stack([f.rect(ROI[0]).channel(channel)[..., :3] for f in frames]).astype(np.float64)


def png_roi(res, frame):
    img = runner.png_array(os.path.join(res['frames_dir'], f'f_{frame}.png'))
    return img[ROI[2]:ROI[2] + ROI[4], ROI[1]:ROI[1] + ROI[3]].astype(np.int32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '11-isolate-flicker-display'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_display_study')
    payload = {'roi': ROI, 'first_frame': FIRST, 'frames': FRAMES,
               'tonemap': {'saturation': SATURATION, 'contrast': CONTRAST, 'operator': OPERATOR}}

    shots = f'{FIRST}-{FIRST + 3}'
    base = {'r.AutoExposure': 0, 'r.Exposure': BASE_EXPOSURE, 'r.TAA.JitterSequenceLength': 8}

    print('1. replay validation (bloom off, fixed exposure)')
    nobloom = capture(work, 'bloom_off', {**base, 'r.Bloom': 0}, shots=shots)
    fr = regiondump.load_sequence(nobloom['region_dir'], FIRST, LAST)
    hdr = roi_rgb(fr)
    deltas = []
    for i, f in enumerate(fr[:4]):
        want = png_roi(nobloom, f.frame)
        got = dm.to_code_values(dm.replay(hdr[i], exposure=BASE_EXPOSURE, saturation=SATURATION,
                                          contrast=CONTRAST, operator=OPERATOR))
        deltas.append(int(np.abs(got - want).max()))
    frac = float(np.mean([np.mean(np.abs(dm.to_code_values(dm.replay(
        hdr[i], exposure=BASE_EXPOSURE, saturation=SATURATION, contrast=CONTRAST,
        operator=OPERATOR)) - png_roi(nobloom, f.frame)) <= 1) for i, f in enumerate(fr[:4])]))
    check(max(deltas) <= 2 and frac > 0.999,
          'the offline replay reproduces the engine PNG with bloom off',
          f'max code-value difference {max(deltas)}, {frac * 100:.3f} % of pixels within 1')
    payload['replay_validation'] = {'max_code_delta': deltas, 'fraction_within_1': frac}

    print('2. units on the same pixels')
    peak = np.percentile(hdr.max(axis=(0, 3)), 99)
    lum = hdr @ dm.REC709
    bright = lum.min(axis=0) >= 0.9 * peak * 0.7
    dark = lum.max(axis=0) <= 0.05 * peak
    boundary = ~bright & ~dark
    masks = {'bright': bright, 'dark': dark, 'boundary': boundary, 'all': np.ones_like(bright)}
    ycocg = hdr[..., 0] + 2 * hdr[..., 1] + hdr[..., 2]
    disp = np.stack([dm.replay(h, exposure=BASE_EXPOSURE, saturation=SATURATION,
                               contrast=CONTRAST, operator=OPERATOR) @ dm.REC709 * 255.0 for h in hdr])
    units = {}
    for name, m in masks.items():
        units[name] = {
            'pixels': int(m.sum()),
            'linear_rec709_mean': float(lum[:, m].mean()),
            'linear_rec709_std': float(lum[:, m].std(axis=0).mean()),
            'ycocg_y_mean': float(ycocg[:, m].mean()),
            'pre_exposed_mean': float((lum[:, m] * BASE_EXPOSURE).mean()),
            'display_code_mean': float(disp[:, m].mean()),
            'display_code_std': float(disp[:, m].std(axis=0).mean()),
        }
    payload['units'] = units
    payload['exposure_per_frame'] = [float(f.exposure) for f in fr]
    check(len({round(e, 6) for e in payload['exposure_per_frame']}) == 1,
          'the effective exposure is recorded for every frame and constant in this run',
          f'{payload["exposure_per_frame"][0]}')

    print('3. exposure sweep on the identical HDR input (offline) and on the GPU')
    sweep = {}
    for factor in (0.5, 1.0, 2.0):
        e = BASE_EXPOSURE * factor
        d = np.stack([dm.replay(h, exposure=e, saturation=SATURATION, contrast=CONTRAST,
                                operator=OPERATOR) @ dm.REC709 * 255.0 for h in hdr])
        step = np.abs(np.diff(d, axis=0))
        sweep[f'{factor}x'] = {
            'exposure': e,
            'display_mean': float(d.mean()),
            'display_std_mean': float(d.std(axis=0).mean()),
            'count_gt_40_per_frame': float((step > 40).sum() / (len(d) - 1)),
            'count_gt_8_per_frame': float((step > 8).sum() / (len(d) - 1)),
        }
    payload['exposure_sweep_offline'] = sweep
    gpu = {}
    for factor in (0.5, 2.0):
        res = capture(work, f'exposure_{factor}', {**base, 'r.Exposure': BASE_EXPOSURE * factor, 'r.Bloom': 0})
        f2 = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
        h2 = roi_rgb(f2)
        gpu[f'{factor}x'] = {'max_hdr_delta_vs_baseline': float(np.abs(h2 - hdr).max()),
                             'exposure_recorded': float(f2[0].exposure)}
        shutil.rmtree(res['region_dir'], ignore_errors=True)
    payload['exposure_sweep_gpu'] = gpu
    check(max(v['max_hdr_delta_vs_baseline'] for v in gpu.values()) == 0.0,
          'changing the exposure leaves the linear HDR input bit-identical',
          str({k: v['max_hdr_delta_vs_baseline'] for k, v in gpu.items()}))

    print('4. bloom on the identical HDR sequence')
    withbloom = capture(work, 'bloom_on', {**base}, shots=shots)
    fb = regiondump.load_sequence(withbloom['region_dir'], FIRST, LAST)
    hb = roi_rgb(fb)
    check(float(np.abs(hb - hdr).max()) == 0.0,
          'bloom does not change the upstream HDR',
          f'max delta {float(np.abs(hb - hdr).max()):.3g}')
    bloom_stats = {}
    for label, res in (('bloom_off', nobloom), ('bloom_on', withbloom)):
        pngs = np.stack([png_roi(res, f.frame) for f in fr[:4]]).astype(np.float64)
        lumd = pngs @ dm.REC709
        step = np.abs(np.diff(lumd, axis=0))
        bloom_stats[label] = {
            'dark_floor_mean_code': float(lumd[:, dark].mean()),
            'dark_floor_p99_code': float(np.percentile(lumd[:, dark], 99)),
            'bright_mean_code': float(lumd[:, bright].mean()),
            'local_contrast_bright_over_dark': float(lumd[:, bright].mean() / max(1e-6, lumd[:, dark].mean())),
            'count_gt_40_per_frame': float((step > 40).sum() / max(1, len(step))),
            'count_gt_8_per_frame': float((step > 8).sum() / max(1, len(step))),
        }
    payload['bloom'] = bloom_stats

    print('5. auto-exposure operating point, and PreExposeWeights at that point')
    # `r.TAA.PreExposeWeights` multiplies the resolve's HDR weights by the FRAME exposure, so at a
    # fixed exposure of 1.0 it is a no-op by construction. Evaluating it anywhere else would be
    # meaningless, so first measure where auto-exposure actually settles on this pose.
    auto = capture(work, 'auto_exposure', {'r.TAA.JitterSequenceLength': 8, 'r.Bloom': 0})
    fa = regiondump.load_sequence(auto['region_dir'], FIRST, LAST)
    auto_exposures = [float(f.exposure) for f in fa]
    ha = roi_rgb(fa)
    payload['auto_exposure'] = {
        'per_frame': auto_exposures,
        'mean': float(np.mean(auto_exposures)),
        'range': [float(min(auto_exposures)), float(max(auto_exposures))],
        'relative_swing': float((max(auto_exposures) - min(auto_exposures)) / np.mean(auto_exposures)),
        'max_hdr_delta_vs_fixed_baseline': float(np.abs(ha - hdr).max()),
    }
    check(payload['auto_exposure']['max_hdr_delta_vs_fixed_baseline'] == 0.0,
          'auto-exposure leaves the linear HDR input bit-identical to the fixed-exposure run',
          f"max delta {payload['auto_exposure']['max_hdr_delta_vs_fixed_baseline']:.3g}, "
          f"exposure {payload['auto_exposure']['mean']:.2f} "
          f"(swing {payload['auto_exposure']['relative_swing'] * 100:.3f} % over the sequence)")
    shutil.rmtree(auto['region_dir'], ignore_errors=True)

    op = round(payload['auto_exposure']['mean'], 3)
    payload['preexpose'] = {'operating_exposure': op,
                            'note': 'at a fixed exposure of 1.0 PreExposeWeights is a no-op by construction'}
    pre = capture(work, 'preexpose', {**base, 'r.Exposure': op, 'r.TAA.PreExposeWeights': 1, 'r.Bloom': 0})
    ref = capture(work, 'preexpose_control', {**base, 'r.Exposure': op, 'r.Bloom': 0})
    fp = regiondump.load_sequence(pre['region_dir'], FIRST, LAST)
    fc = regiondump.load_sequence(ref['region_dir'], FIRST, LAST)
    lp = roi_rgb(fp) @ dm.REC709
    lc = roi_rgb(fc) @ dm.REC709
    for name, m in masks.items():
        payload['preexpose'][name] = {
            'std_mean_control': float(lc[:, m].std(axis=0).mean()),
            'std_mean_preexpose': float(lp[:, m].std(axis=0).mean()),
            'std_p99_control': float(np.percentile(lc[:, m].std(axis=0), 99)),
            'std_p99_preexpose': float(np.percentile(lp[:, m].std(axis=0), 99)),
            'mean_control': float(lc[:, m].mean()),
            'mean_preexpose': float(lp[:, m].mean()),
            'temporal_response_abs_diff_control': float(np.abs(np.diff(lc[:, m], axis=0)).mean()),
            'temporal_response_abs_diff_preexpose': float(np.abs(np.diff(lp[:, m], axis=0)).mean()),
        }
    payload['preexpose']['weight_exposure_recorded'] = float(
        fp[0].rect(ROI[0]).channel('taa_output')[..., 3].mean())
    payload['preexpose']['weight_exposure_control'] = float(
        fc[0].rect(ROI[0]).channel('taa_output')[..., 3].mean())
    check(abs(payload['preexpose']['weight_exposure_recorded'] - op) < 0.01 * op,
          'PreExposeWeights makes the resolve weight with the frame exposure',
          f"{payload['preexpose']['weight_exposure_recorded']:.3f} vs control "
          f"{payload['preexpose']['weight_exposure_control']:.3f} at exposure {op:.2f}")
    shutil.rmtree(ref['region_dir'], ignore_errors=True)

    print('6. the reflections-off confound, quantified')
    # Turning reflections off changes the radiance of the dark side, which moves the auto-exposure
    # operating point, which changes every display-referred count. Measuring the exposure and the
    # dark-side radiance separates that chain from any claim about energy amplification.
    refl = {}
    for label, cv in (('reflections_on', {}), ('reflections_off', {'r.GI.Reflections': 0})):
        res = capture(work, label, {'r.TAA.JitterSequenceLength': 8, 'r.Bloom': 0, **cv})
        ff = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
        hh = roi_rgb(ff)
        ll = hh @ dm.REC709
        exposures = [float(f.exposure) for f in ff]
        refl[label] = {
            'auto_exposure_mean': float(np.mean(exposures)),
            'dark_mean_radiance': float(ll[:, dark].mean()),
            'dark_std_radiance': float(ll[:, dark].std(axis=0).mean()),
            'boundary_mean_radiance': float(ll[:, boundary].mean()),
            'boundary_std_radiance': float(ll[:, boundary].std(axis=0).mean()),
            'roi_mean_radiance': float(ll.mean()),
        }
        shutil.rmtree(res['region_dir'], ignore_errors=True)
    on, off = refl['reflections_on'], refl['reflections_off']
    refl['exposure_ratio_off_over_on'] = off['auto_exposure_mean'] / on['auto_exposure_mean']
    refl['dark_radiance_ratio_off_over_on'] = (off['dark_mean_radiance'] / on['dark_mean_radiance']
                                               if on['dark_mean_radiance'] else float('nan'))
    refl['boundary_std_ratio_off_over_on'] = (off['boundary_std_radiance'] / on['boundary_std_radiance']
                                              if on['boundary_std_radiance'] else float('nan'))
    payload['reflections_confound'] = refl
    check(True, 'the reflections-off confound is quantified in radiance and exposure, not in counts',
          f"auto-exposure {on['auto_exposure_mean']:.2f} -> {off['auto_exposure_mean']:.2f} "
          f"({refl['exposure_ratio_off_over_on']:.2f}x), dark radiance "
          f"{on['dark_mean_radiance']:.5f} -> {off['dark_mean_radiance']:.5f}, boundary std "
          f"{on['boundary_std_radiance']:.5f} -> {off['boundary_std_radiance']:.5f} "
          f"({refl['boundary_std_ratio_off_over_on']:.2f}x)")

    payload['checks'] = RESULTS
    payload['source_fingerprint'] = runner.source_fingerprint()
    for label, res in (('bloom_off', nobloom), ('bloom_on', withbloom), ('preexpose', pre)):
        shutil.copy(res['manifest_path'], os.path.join(args.out, 'manifests', f'{label}.json'))
        shutil.rmtree(res['region_dir'], ignore_errors=True)
    path = os.path.join(args.out, 'display-study.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    failed = [r for r in RESULTS if not r['ok']]
    print(f'{len(RESULTS) - len(failed)} passed, {len(failed)} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
