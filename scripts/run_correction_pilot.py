"""Choose the exponent of the candidate correction, before the acceptance run (followup-14, 1.3).

The demonstrated mechanism (ledger H-11/H-12/H-14) is the HDR weighting inside the TAA
reconstruction filter: it suppresses bright taps, which both loses energy on small bright features
and makes the filtered colour depend on where the jitter put the samples, so a pixel whose own
sample never changes still moves frame to frame. `r.TAA.ReconstructionMode=1` removes the weighting
outright and costs 16 frames of recovery after a cut; this sweep looks for the smallest departure
from production that still removes most of the residual.

Two stages, both reported:

* **offline** — the validated model on the analytic fixtures, which have a known correct answer.
  Free, so it sweeps finely and provides the energy/sharpness/recovery trade curve.
* **GPU** — one capture per exponent on pose W, to confirm the offline prediction on real content.

This is a *tuning* step. It is deliberately separate from the acceptance run, which re-measures the
chosen value with independent repeats against the gates frozen in the ledger.

Usage: python scripts/run_correction_pilot.py [--offline-only] [--out DIR]
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
import runner  # noqa: E402
from run_taa_reconstruction_study import FIXTURES, HALTON8, study  # noqa: E402

EXPONENTS = [1.0, 0.875, 0.75, 0.625, 0.5, 0.375, 0.25, 0.125, 0.0]

SCENE_START = 37
WARMUP = 200
MEASURED = 256
FIRST = SCENE_START + WARMUP
LAST = FIRST + MEASURED - 1
RENDER_FRAMES = LAST - SCENE_START + 8
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROIS = [('window', 770, 150, 192, 512), ('shadow_wall', 150, 260, 192, 384)]
BASE_CVARS = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}


def offline_sweep():
    out = {}
    for fname, fixture in FIXTURES.items():
        out[fname] = {}
        print(f'\n{fname} (bright {fixture["bright"]}, bar {fixture["bar_width"]} px)')
        print(f'{"exponent":>9s} {"energy err":>11s} {"rms":>9s} {"temporal std":>13s} '
              f'{"width px":>9s} {"peak":>8s} {"conv":>5s}')
        for e in EXPONENTS:
            r = study(fixture, dict(hdr_weight_exponent=e))
            out[fname][f'{e:g}'] = r
            print(f'{e:9g} {r["energy_error"] * 100:10.2f}% {r["rms_error"]:9.4f} '
                  f'{r["temporal_std"]:13.5f} {r["feature_width"]:9.1f} {r["peak"]:8.3f} '
                  f'{r["convergence_frames"]:5d}')
    return out


def gpu_sweep(work, out_dir, exponents):
    rows = {}
    for e in exponents:
        label = f'exp{e:g}'.replace('.', 'p')
        print(f'  … GPU pilot exponent {e:g}', flush=True)
        res = runner.run_capture(os.path.join(work, label), PROJECT, RENDER_FRAMES, regions=ROIS,
                                 region_fields='output', region_first=FIRST, region_last=LAST,
                                 cvars={**BASE_CVARS,
                                        'r.TAA.ReconstructionHdrWeightExponent': f'{e:g}'},
                                 timing_first=FIRST)
        if res['exit_code'] != 0:
            raise RuntimeError(f'exponent {e}: engine exited {res["exit_code"]} (see {res["log"]})')
        r = analysis.analyse_run(res['region_dir'], FIRST, LAST)
        entry = {}
        for name, s in r['rois'].items():
            entry[name] = {k: s[k] for k in ('mean', 'std_mean', 'std_p99', 'abs_diff_mean',
                                             'pixels_valid')}
        entry['frame_ms'] = (res['manifest'] or {}).get('timing', {})
        rows[f'{e:g}'] = entry
        w = entry['window']
        print(f"      window std_mean {w['std_mean']:.4e}  mean {w['mean']:.4f}", flush=True)
        shutil.rmtree(res['region_dir'], ignore_errors=True)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--offline-only', action='store_true')
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '14-validate-flicker-correction'))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    payload = {'exponents': EXPONENTS, 'offline': offline_sweep()}

    if not args.offline_only:
        runner.require_exe()
        work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_correction_pilot')
        payload['gpu'] = gpu_sweep(work, args.out, EXPONENTS)
        base = payload['gpu']['1']['window']
        print(f'\n{"exponent":>9s} {"std_mean":>12s} {"vs baseline":>12s} {"ROI mean":>10s} '
              f'{"energy drift":>13s}')
        for e in EXPONENTS:
            w = payload['gpu'][f'{e:g}']['window']
            print(f'{e:9g} {w["std_mean"]:12.4e} '
                  f'{(w["std_mean"] / base["std_mean"] - 1) * 100:11.1f}% {w["mean"]:10.4f} '
                  f'{(w["mean"] / base["mean"] - 1) * 100:12.2f}%')
        payload['source_fingerprint'] = runner.source_fingerprint()

    path = os.path.join(args.out, 'correction-pilot.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
