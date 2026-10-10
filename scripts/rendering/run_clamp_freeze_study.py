"""H-25 in steady state, on the GPU: freeze the clamp bounds and see what actually changes.

The offline replay can only screen a one-step sensitivity, so the steady-state question is settled
here instead. `r.TAA.ClampFreezeAfterFrame` captures the clamp box during warm-up and reuses it from
then on: the bounds stop moving while the reconstruction, the exposure and the lighting are
untouched. That is the intervention H-25 always needed.

Two quantities, because either alone is misleading:

  oscillation   the temporal std of the output — what H-25 predicts should fall;
  error         the distance to the correctly resampled image (the cycle-averaged raw input) and
                the ROI energy — because freezing a *wrong* box would also reduce oscillation, by
                pinning the accumulation to something that is not the right answer.

Three validity controls run first, and the study refuses to interpret anything if they fail:

  freeze took effect   the recorded clip box must be constant over time in the frozen run and
                       varying in the baseline. A silently ignored CVar has produced a wrong
                       conclusion in this programme before;
  reconstruction equal the recorded filtered colour must match between variants — it is computed
                       from the current frame's input and cannot depend on the clamp, so any
                       difference means the intervention is not isolated;
  exposure equal       the recorded exposure must match.

**Preregistered decision rule.** H-25 is supported in steady state only if the oscillation falls by
more than the followup-09 threshold for this ROI (4.8e-4 linear radiance) **and** the error against
the cycle-averaged raw input does not grow. A fall in oscillation with a growing error is a
rejection, not a partial success: it means the image was stabilised onto the wrong value. Frozen
screen-space bounds are a diagnostic and cannot be a general correction — they are only defined for
a static camera, and a moving camera needs its own experiment either way.

**Scope limit, stated up front.** The box is frozen at a *single jitter phase* and the measurement
starts 8 frames later. That is enough to reject this variant; it is not enough to rule out bound
stabilisation in general (an average over a whole jitter cycle, or a slow temporal filter on the
bounds, are different interventions), nor to claim the whole measured window is a settled steady
state. The raw dumps are kept so those questions can be revisited without recapturing.

Usage: python scripts/rendering/run_clamp_freeze_study.py [--frames 128] [--no-keep-raw]
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

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 128, 256)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
#: The followup-09 decision threshold for this ROI, in linear radiance.
THRESHOLD = 4.8e-4


def luma(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--no-keep-raw', action='store_true')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '15-isolate-clamp-bounds'))
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_clamp_freeze')

    # Freeze from a frame inside the warm-up, so the box is captured on a converged image and the
    # whole measured window runs frozen.
    freeze_at = FIRST - 8
    # The baseline is captured twice so the pair sets the floor every other comparison is judged
    # against: this renderer is not bit-reproducible across processes.
    variants = [('baseline', {}), ('baseline_repeat', {}),
                ('frozen_bounds', {'r.TAA.ClampFreezeAfterFrame': freeze_at})]
    payload = {'roi': ROI, 'frames': args.frames, 'first_frame': FIRST, 'freeze_after': freeze_at,
               'threshold': THRESHOLD, 'variants': {}}
    data = {}

    for vname, cvars in variants:
        print(f'  ... {vname}', flush=True)
        res = runner.run_capture(os.path.join(work, vname), PROJECT, last - SCENE_START + 8,
                                 regions=[ROI], region_fields='taa+extra', region_first=FIRST,
                                 region_last=last, cvars={**BASE, **cvars}, timing_first=FIRST)
        if res['exit_code'] != 0:
            raise RuntimeError(f'{vname}: engine exited {res["exit_code"]}')
        seq = regiondump.load_sequence(res['region_dir'], FIRST, last)
        rect = [f.rect(ROI[0]) for f in seq]
        g = lambda ch: np.stack([r.channel(ch)[..., :3] for r in rect]).astype(np.float64)  # noqa: E731
        d = {'output': g('taa_output'), 'filtered': g('taa_filtered'), 'raw': g('taa_raw'),
             'clip_min': g('taa_clip_min'), 'clip_max': g('taa_clip_max'),
             'exposure': np.stack([r.channel('taa_output')[..., 3] for r in rect]).astype(np.float64)}
        data[vname] = d
        shutil.copy(res['manifest_path'], os.path.join(args.out, 'manifests', f'freeze-{vname}.json'))
        if not args.no_keep_raw:
            # Kept so the temporal evolution can be re-examined — a different jitter phase, a
            # later start, a per-frame view — without recapturing.
            dst = os.path.join(args.out, 'raw', f'clamp-freeze-{vname}')
            if os.path.isdir(dst):
                shutil.rmtree(dst)
            shutil.copytree(res['region_dir'], dst)
        shutil.rmtree(res['region_dir'], ignore_errors=True)

    # ---------------------------------------------------------------- controls
    def box_motion(v):
        """Temporal std of the box, per bound, averaged — NOT across the min/max gap."""
        lo = tm.rgb_to_ycocg(data[v]['clip_min'])[..., 0].std(axis=0).mean()
        hi = tm.rgb_to_ycocg(data[v]['clip_max'])[..., 0].std(axis=0).mean()
        return float(0.5 * (lo + hi))

    box_std = {v: box_motion(v) for v in data}
    # Judged on a *relative* criterion, not against the baseline-pair floor. Both quantities are
    # ~1e-10 against a signal of ~2.5 radiance, and a ratio between two noise-level numbers is
    # meaningless: the measured floor swung from 3.73e-10 to 2.80e-12 across two runs of this very
    # study, flipping the verdict both ways while the reconstruction was identical to eleven
    # digits. The criterion is therefore "the reconstruction differs by less than 1e-6 of its own
    # magnitude", which is the question the control is actually asking, and the floor is reported
    # beside it for context rather than used as the gate.
    floor = float(np.abs(data['baseline']['filtered'] - data['baseline_repeat']['filtered']).mean())
    floor_max = float(np.abs(data['baseline']['filtered'] - data['baseline_repeat']['filtered']).max())
    recon_delta = float(np.abs(data['baseline']['filtered'] - data['frozen_bounds']['filtered']).mean())
    recon_delta_max = float(np.abs(data['baseline']['filtered'] - data['frozen_bounds']['filtered']).max())
    recon_scale = float(np.abs(data['baseline']['filtered']).mean())
    recon_rel = recon_delta / max(1e-30, recon_scale)
    exposure_delta = float(np.abs(data['baseline']['exposure'] - data['frozen_bounds']['exposure']).max())
    controls = {
        'clip_box_temporal_std_baseline': box_std['baseline'],
        'clip_box_temporal_std_frozen': box_std['frozen_bounds'],
        'freeze_took_effect': bool(box_std['frozen_bounds'] < 1e-9 < box_std['baseline']),
        'reconstruction_floor_baseline_pair_mean': floor,
        'reconstruction_floor_baseline_pair_max': floor_max,
        'reconstruction_mean_delta': recon_delta,
        'reconstruction_max_delta': recon_delta_max,
        'reconstruction_relative_delta': recon_rel,
        'reconstruction_identical': bool(recon_rel < 1e-6),
        'exposure_max_delta': exposure_delta,
        'exposure_identical': bool(exposure_delta < 1e-9),
    }
    payload['controls'] = controls
    print(f"      clip box temporal std: baseline {box_std['baseline']:.4e}, frozen "
          f"{box_std['frozen_bounds']:.4e} "
          f"({'freeze took effect' if controls['freeze_took_effect'] else 'FREEZE DID NOT APPLY'})")
    print(f"      reconstruction relative delta {recon_rel:.2e} of its own magnitude "
          f"(mean {recon_delta:.2e}; baseline-pair floor {floor:.2e}, reported not used) "
          f"({'within the floor' if controls['reconstruction_identical'] else 'DIFFERS — not isolated'}), "
          f"exposure delta {exposure_delta:.2e}")

    # ---------------------------------------------------------------- measurements
    for vname in ('baseline', 'frozen_bounds'):
        out = luma(data[vname]['output'])
        raw = luma(data[vname]['raw'])
        n = (len(raw) // 8) * 8
        reference = raw[:n].reshape(-1, 8, *raw.shape[1:]).mean(axis=1).mean(axis=0)
        payload['variants'][vname] = {
            'oscillation_std_mean': float(out.std(axis=0).mean()),
            'error_vs_reference': float(np.abs(out - reference[None]).mean()),
            'roi_mean': float(out.mean()),
            'reference_mean': float(reference.mean()),
            'energy_vs_reference': float(out.mean() / reference.mean()),
        }
        e = payload['variants'][vname]
        print(f"      {vname:14s} oscillation {e['oscillation_std_mean']:.4e}, error vs reference "
              f"{e['error_vs_reference']:.4f}, energy {e['energy_vs_reference']:.4f}")

    b, f = payload['variants']['baseline'], payload['variants']['frozen_bounds']
    d_osc = f['oscillation_std_mean'] - b['oscillation_std_mean']
    d_err = f['error_vs_reference'] - b['error_vs_reference']
    payload['oscillation_change'] = d_osc
    payload['oscillation_change_pct'] = d_osc / b['oscillation_std_mean'] * 100
    payload['error_change_pct'] = d_err / b['error_vs_reference'] * 100
    if not (controls['freeze_took_effect'] and controls['reconstruction_identical']
            and controls['exposure_identical']):
        payload['verdict'] = 'invalid — a control failed, nothing is interpreted'
    elif -d_osc > THRESHOLD and d_err <= 0:
        payload['verdict'] = 'supported'
    elif -d_osc > THRESHOLD:
        payload['verdict'] = 'rejected — oscillation fell but the error against the reference grew'
    else:
        payload['verdict'] = 'rejected — the oscillation did not fall beyond the ROI threshold'
    print(f"\n  oscillation {payload['oscillation_change_pct']:+.1f} %, error vs reference "
          f"{payload['error_change_pct']:+.1f} %")
    print(f"  verdict by the preregistered rule: {payload['verdict']}")

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'clamp-freeze.json')
    with open(path, 'w', encoding='utf-8') as f_:
        json.dump(payload, f_, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
