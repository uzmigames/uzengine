"""Is the surviving residual just the accumulation's steady-state ripple? (ledger H-27)

H-25 was screened out (the clamp bounds move, but the clamp contributes ~11 % of the residual and
its activity barely co-locates with it) and H-26 was rejected (the history-resampling perturbation
is 2.3e-4 against a residual of 1.1e-2, and the velocity that would drive it is 0.0002 px). What is
left is much simpler, and it is testable in closed form.

The TAA accumulation is a first-order IIR filter, `y[n] = (1-a) y[n-1] + a x[n]` with
`a = r.TAA.CurrentFrameWeight = 0.04`. Its input, the reconstructed colour, is *periodic in the
jitter cycle*: the plus filter resamples the neighbourhood at a different sub-pixel offset every
frame and repeats every N frames. A first-order filter driven by a periodic input does not converge
to a constant; it settles into a ripple whose amplitude is the input's ripple times the filter's
gain at the fundamental,

    |H(w)| = a / |1 - (1-a) e^{-jw}|,   w = 2*pi/N

This makes a quantitative, falsifiable prediction with no free parameters: measure the input ripple
(the temporal std of the recorded `taa_filtered`), multiply by |H|, and the result should be the
measured output ripple. It also predicts the *direction* of the jitter-length dependence: a longer
cycle is a lower frequency, which the filter attenuates less, so the residual should grow with N.

**Preregistered decision rule.** H-27 is supported only if the predicted residual matches the
measured one within a factor of 1.5 at every jitter length tested, **and** the ordering across
jitter lengths is reproduced. A model that fits one length and misses the trend is rejected: the
trend is the part that is hard to fit by accident. `r.TAA.JitterScale=0` is the negative control -
with no sub-pixel offset the input has no ripple to transfer, so the predicted and measured residual
must both collapse.

Note on scope: this is a *description* of the surviving residual, not a defect. If it holds, the
residual is the designed behaviour of a first-order accumulator on a jittered input, and reducing
it means changing the input's jitter dependence or the filter's gain - the first is the
reconstruction (followup-14, bounded at -35 %), the second is lag. That conclusion belongs in the
ledger either way.

Usage: python scripts/run_accumulation_transfer_study.py [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 64
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = ('window', 800, 200, 128, 256)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0}
ALPHA = 0.04            # r.TAA.CurrentFrameWeight, the engine default

CONDITIONS = [
    ('jitter 4', {'r.TAA.JitterSequenceLength': 4}, 4),
    ('jitter 8 (production)', {'r.TAA.JitterSequenceLength': 8}, 8),
    ('jitter 16', {'r.TAA.JitterSequenceLength': 16}, 16),
    ('no sub-pixel offset', {'r.TAA.JitterSequenceLength': 8, 'r.TAA.JitterScale': 0.0}, None),
]


def transfer_gain(alpha, n):
    """|H| of y[n] = (1-a)y[n-1] + a x[n] at the fundamental of a period-n input."""
    if not n:
        return 0.0
    w = 2.0 * math.pi / n
    denom = complex(1.0 - (1.0 - alpha) * math.cos(w), (1.0 - alpha) * math.sin(w))
    return alpha / abs(denom)


def luma(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '15-isolate-clamp-bounds'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_transfer')
    payload = {'roi': ROI, 'frames': FRAMES, 'alpha': ALPHA, 'conditions': {}}

    for label, cvars, period in CONDITIONS:
        print(f'  ... {label}', flush=True)
        res = runner.run_capture(os.path.join(work, label.replace(' ', '_')), PROJECT,
                                 LAST - SCENE_START + 8, regions=[ROI], region_fields='taa',
                                 region_first=FIRST, region_last=LAST,
                                 cvars={**BASE, **cvars}, timing_first=FIRST)
        if res['exit_code'] != 0:
            raise RuntimeError(f'{label}: engine exited {res["exit_code"]}')
        seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
        rect = [f.rect(ROI[0]) for f in seq]
        filtered = luma(np.stack([r.channel('taa_filtered')[..., :3] for r in rect]).astype(np.float64))
        output = luma(np.stack([r.channel('taa_output')[..., :3] for r in rect]).astype(np.float64))
        blend = np.stack([r.channel('taa_raw')[..., 3] for r in rect]).astype(np.float64)
        shutil.copy(res['manifest_path'],
                    os.path.join(args.out, 'manifests', label.replace(' ', '_') + '.json'))
        shutil.rmtree(res['region_dir'], ignore_errors=True)

        # The blend weight the shader actually used, rather than the CVar: the velocity lerp and
        # the anti-flicker guard modulate it, and on a static camera it should sit at the CVar.
        alpha_eff = float(np.median(blend))
        gain = transfer_gain(alpha_eff, period)
        in_ripple = float(filtered.std(axis=0).mean())
        out_ripple = float(output.std(axis=0).mean())
        predicted = in_ripple * gain
        entry = {
            'cvars': cvars, 'period': period,
            'alpha_effective': alpha_eff,
            'transfer_gain': gain,
            'input_ripple': in_ripple,
            'output_ripple_measured': out_ripple,
            'output_ripple_predicted': predicted,
            'ratio_measured_over_predicted': (out_ripple / predicted) if predicted else None,
        }
        payload['conditions'][label] = entry
        print(f"      alpha {alpha_eff:.4f}, gain {gain:.5f}; input ripple {in_ripple:.4e} -> "
              f"predicted {predicted:.4e}, measured {out_ripple:.4e}"
              + (f" (measured/predicted {entry['ratio_measured_over_predicted']:.2f}x)"
                 if predicted else ''), flush=True)

    ratios = [e['ratio_measured_over_predicted'] for e in payload['conditions'].values()
              if e['ratio_measured_over_predicted']]
    ordered = [payload['conditions'][k]['output_ripple_measured']
               for k in ('jitter 4', 'jitter 8 (production)', 'jitter 16')]
    payload['within_1_5x'] = bool(ratios and all(1 / 1.5 <= r <= 1.5 for r in ratios))
    payload['trend_reproduced'] = bool(ordered == sorted(ordered))
    payload['verdict'] = ('supported' if payload['within_1_5x'] and payload['trend_reproduced']
                          else 'rejected')
    print(f"\n  within 1.5x at every length: {payload['within_1_5x']}; "
          f"residual grows with the jitter period: {payload['trend_reproduced']} "
          f"({', '.join(f'{v:.3e}' for v in ordered)})")
    print(f"  verdict by the preregistered rule: {payload['verdict']}")

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'accumulation-transfer.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
