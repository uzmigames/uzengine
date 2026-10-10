"""Is the surviving residual caused by the clamp *bounds* moving? (ledger H-25)

followup-14 closed the reconstruction-weighting family: at its unbiased endpoint (exponent 0) the
pose W window residual still only falls 35 %, so about 65 % of it is not weighting bias. Both clamp
routes are also closed — H-13 rejected removing the clamp (invalid history never clears) and H-23
rejected the AABB-only variant (recovery after a cut 16 -> 48 frames). What none of them separates
is that the *bounds themselves move*: `mu` and `sigma` are computed from the same jitter-displaced
neighbourhood as the filter, so a converged history is re-clipped against a different box every
frame.

This is screened **offline**, before any shader is touched. The region dump records the clip box,
the history, the clamped history, the filtered colour and the blend weight, and the offline model
reproduces the GPU's blend to 2e-7.

A first version replayed the whole accumulation with its own history and swapped the box. Its
validity check failed (4.6e-3 against a 2e-3 tolerance) and `scripts/rendering/diagnose_taa_replay.py` found
why: the blend and clamp steps are exact to 1e-7, but **the history entering a frame is not the
previous frame's output at the same pixel** — it differs on 6.5 % of pixels by up to 7.9e-3, even
with a static camera, because the history is a resampled texture fetch. A self-accumulating replay
therefore drifts (2.5e-07 at frame 0 to 3.5e-3 by frame 16) and no counterfactual built on it means
anything. That check is kept, and the study now measures two things it *can* measure soundly:

  one-step sensitivity   for each frame, recompute the output from the **recorded** history with the
                         box replaced (its temporal mean, or removed). No accumulation, so no drift.
                         This is the instantaneous contribution of the bounds, not the steady state.
  co-location            which mechanism sits where the residual is. Per pixel: the temporal std of
                         the output (the residual), the size of the history-resampling perturbation
                         |history[i] - output[i-1]|, and how often the clamp bites. A mechanism that
                         causes the residual should be large exactly where the residual is.

**Preregistered decision rule.** The bounds move because they are recomputed from jittered samples;
movement alone proves nothing, and neither does a correlation on its own. H-25 is *screened in* only
if fixing the bounds removes at least half of the clamp's one-step contribution **and** the clamp's
per-pixel activity correlates with the residual at least as strongly as the history-resampling
perturbation does. Screening in does not confirm it: the steady-state question needs the bounds
stabilised in the shader and the static residual and cut recovery measured together, which is the
engine-side experiment this screening exists to justify or avoid.

Usage: python scripts/rendering/run_clamp_bounds_study.py [--out DIR]
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
import taa_model as tm  # noqa: E402

CVAR = 'r.TAA.ReconstructionHdrWeightExponent'
SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 32
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 128, 256)
BASE = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}
VARIANTS = [('baseline', 1.0), ('exponent_0', 0.0)]


def blend_step(clamped_rgb, filtered_rgb, blend, exposure):
    """The shader's HDR-weighted lerp, as validated against the GPU to 2e-7."""
    cy = tm.rgb_to_ycocg(clamped_rgb)[..., 0]
    fy = tm.rgb_to_ycocg(filtered_rgb)[..., 0]
    hwt = 1.0 / (cy * exposure + 4.0)
    fw = 1.0 / (fy * exposure + 4.0)
    a = (1.0 - blend) * hwt
    b = blend * fw
    inv = 1.0 / (a + b)
    return clamped_rgb * (a * inv)[..., None] + filtered_rgb * (b * inv)[..., None]


def one_step(rec, mode):
    """Recompute each frame's output from the RECORDED history with the box swapped.

    No accumulation, so the drift that invalidated the full replay cannot occur. What this measures
    is the instantaneous sensitivity of the output to the bounds, not the steady state.
    """
    lo_mean = rec['clip_min'].mean(axis=0)
    hi_mean = rec['clip_max'].mean(axis=0)
    out = []
    for i in range(len(rec['filtered'])):
        history = rec['history'][i]
        if mode == 'recorded':
            lo, hi = rec['clip_min'][i], rec['clip_max'][i]
        elif mode == 'fixed_box':
            lo, hi = lo_mean, hi_mean
        elif mode == 'no_clamp':
            lo = hi = None
        else:
            raise ValueError(mode)
        if lo is None:
            clamped = history
        else:
            # The shader clips in YCoCg; the recorded bounds are that box converted to RGB, and the
            # conversion is linear and invertible, so this is the operation the GPU performed.
            h = tm.rgb_to_ycocg(history)
            clamped = tm.ycocg_to_rgb(np.clip(h, tm.rgb_to_ycocg(lo), tm.rgb_to_ycocg(hi)))
        out.append(blend_step(clamped, rec['filtered'][i], rec['blend'][i], rec['exposure'][i]))
    return np.stack(out)


def luma(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '15-isolate-clamp-bounds'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_clamp_bounds')
    payload = {'roi': ROI, 'frames': FRAMES, 'first_frame': FIRST, 'base_cvars': BASE,
               'variants': {}}

    for vname, exponent in VARIANTS:
        print(f'  ... capturing {vname}', flush=True)
        res = runner.run_capture(os.path.join(work, vname), PROJECT, LAST - SCENE_START + 8,
                                 regions=[ROI], region_fields='taa+extra', region_first=FIRST,
                                 region_last=LAST, cvars={**BASE, CVAR: f'{exponent:g}'},
                                 timing_first=FIRST)
        if res['exit_code'] != 0:
            raise RuntimeError(f'{vname}: engine exited {res["exit_code"]}')
        seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
        g = lambda ch, k=3: np.stack([f.rect(ROI[0]).channel(ch)[..., :k] for f in seq]).astype(np.float64)  # noqa: E731
        rec = {
            'filtered': g('taa_filtered'), 'history': g('taa_history'),
            'clamped': g('taa_clamped'), 'output': g('taa_output'),
            'clip_min': g('taa_clip_min'), 'clip_max': g('taa_clip_max'),
            'blend': np.stack([f.rect(ROI[0]).channel('taa_raw')[..., 3] for f in seq]).astype(np.float64),
            'exposure': np.stack([f.rect(ROI[0]).channel('taa_output')[..., 3] for f in seq]).astype(np.float64),
        }
        shutil.copy(res['manifest_path'], os.path.join(args.out, 'manifests', f'{vname}.json'))
        shutil.rmtree(res['region_dir'], ignore_errors=True)

        entry = {'exponent': exponent}

        # ---- how much do the bounds actually move, and how often do they bite?
        box_y_lo = tm.rgb_to_ycocg(rec['clip_min'])[..., 0]
        box_y_hi = tm.rgb_to_ycocg(rec['clip_max'])[..., 0]
        width = box_y_hi - box_y_lo
        hist_y = tm.rgb_to_ycocg(rec['history'])[..., 0]
        clamped_y = tm.rgb_to_ycocg(rec['clamped'])[..., 0]
        bit = np.abs(clamped_y - hist_y) > 1e-6
        entry['bounds'] = {
            'centre_temporal_std': float(((box_y_lo + box_y_hi) * 0.5).std(axis=0).mean()),
            'width_temporal_std': float(width.std(axis=0).mean()),
            'width_mean': float(width.mean()),
            'centre_std_over_width': float(((box_y_lo + box_y_hi) * 0.5).std(axis=0).mean()
                                           / max(1e-12, width.mean())),
            'clamp_active_fraction': float(bit.mean()),
            'clamp_correction_mean': float(np.abs(clamped_y - hist_y)[bit].mean()) if bit.any() else 0.0,
        }

        # ---- one-step counterfactuals, with `recorded` as the validity check
        modes = {}
        for mode in ('recorded', 'fixed_box', 'no_clamp'):
            r = luma(one_step(rec, mode))
            modes[mode] = {'std_mean': float(r.std(axis=0).mean()), 'mean': float(r.mean())}
        recorded_out = luma(rec['output'])
        replayed = luma(one_step(rec, 'recorded'))
        rel = float(np.abs(replayed - recorded_out).max() / max(1e-9, np.abs(recorded_out).max()))
        modes['recorded']['max_relative_error_vs_gpu'] = rel
        entry['one_step'] = modes
        entry['one_step_valid'] = bool(rel < 2e-3)

        # ---- co-location: where does the residual actually live?
        resid = recorded_out.std(axis=0)                       # per pixel
        hist_delta = np.abs(rec['history'][1:] - rec['output'][:-1]).max(axis=-1).mean(axis=0)
        clamp_rate = bit.mean(axis=0)

        def corr(a, b):
            a, b = a.ravel(), b.ravel()
            if a.std() < 1e-15 or b.std() < 1e-15:
                return None
            return float(np.corrcoef(a, b)[0, 1])

        entry['co_location'] = {
            'residual_vs_history_resampling': corr(resid, hist_delta),
            'residual_vs_clamp_rate': corr(resid, clamp_rate),
            'history_resampling_mean': float(hist_delta.mean()),
            'history_resampling_max': float(hist_delta.max()),
            'residual_mean': float(resid.mean()),
        }

        gap = modes['recorded']['std_mean'] - modes['no_clamp']['std_mean']
        removed = modes['recorded']['std_mean'] - modes['fixed_box']['std_mean']
        entry['gpu_std_mean'] = float(recorded_out.std(axis=0).mean())
        entry['clamp_one_step_contribution'] = gap
        entry['removed_by_fixing_the_box'] = removed
        entry['fraction_from_moving_bounds'] = (removed / gap) if abs(gap) > 1e-12 else None

        payload['variants'][vname] = entry

        b = entry['bounds']
        c = entry['co_location']
        print(f"      one-step check reproduces the GPU to {rel:.2e} "
              f"({'valid' if entry['one_step_valid'] else 'INVALID'})")
        print(f"      box centre moves {b['centre_temporal_std']:.4f} against a mean width of "
              f"{b['width_mean']:.4f} ({b['centre_std_over_width'] * 100:.1f} %); the clamp bites on "
              f"{b['clamp_active_fraction'] * 100:.1f} % of samples")
        print(f"      one-step residual: recorded {modes['recorded']['std_mean']:.4e}, fixed box "
              f"{modes['fixed_box']['std_mean']:.4e}, no clamp {modes['no_clamp']['std_mean']:.4e}")
        if entry['fraction_from_moving_bounds'] is not None:
            print(f"      fixing the bounds removes "
                  f"{entry['fraction_from_moving_bounds'] * 100:.1f} % of the clamp's one-step "
                  f"contribution")
        print(f"      co-location with the residual: history resampling r="
              f"{c['residual_vs_history_resampling']}, clamp activity r="
              f"{c['residual_vs_clamp_rate']}", flush=True)

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'clamp-bounds.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
