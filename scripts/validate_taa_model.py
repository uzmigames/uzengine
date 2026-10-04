"""Validates the offline TAA model (`scripts/flicker/taa_model.py`) against the GPU.

The model is only useful if it reproduces the shader. This captures a window region with the full
record set (`--region-fields taa+extra`) and checks, per pixel and per frame:

  1. reconstruction — the filtered luma predicted from the recorded plus-filter taps, the recorded
     jitter and the HDR weighting equals the filtered luma the shader wrote;
  2. clamp — the clamped history equals the recorded history clipped to the recorded clip box;
  3. blend — the output equals the HDR-weighted blend of the clamped history and the filtered
     colour with the recorded blend weight.

Every check is a relative error against the GPU value, over real scene content (a high-contrast
window edge), not a synthetic case.

The whole capture is repeated at `r.TAA.ReconstructionHdrWeightExponent = 0.5` (followup-14): a
candidate correction is only allowed to be studied offline if the model reproduces the shader with
the candidate applied, not just at the production default.

Usage: python scripts/validate_taa_model.py [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402
import taa_model as tm  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 120
FRAMES = 12
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = [('window', 800, 200, 96, 128)]


HDR_WEIGHT_CVAR = 'r.TAA.ReconstructionHdrWeightExponent'


def validate(work, exponent):
    """Captures the window with the full record set and checks the model against the GPU."""
    print(f'capturing the window region with the full record set '
          f'(HDR weight exponent {exponent:g})', flush=True)
    res = runner.run_capture(os.path.join(work, f'exp{exponent:g}'.replace('.', 'p')), PROJECT,
                             LAST - SCENE_START + 8, regions=ROI,
                             region_fields='taa+extra', region_first=FIRST, region_last=LAST,
                             cvars={'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0,
                                    'r.Exposure': 1.0, HDR_WEIGHT_CVAR: f'{exponent:g}'},
                             timing_first=FIRST)
    if res['exit_code'] != 0:
        raise SystemExit(f'engine exited {res["exit_code"]}, see {res["log"]}')
    got = float((res['manifest'] or {}).get('cvars', {}).get(HDR_WEIGHT_CVAR, float('nan')))
    if abs(got - exponent) > 1e-6:
        raise SystemExit(f'{HDR_WEIGHT_CVAR} is {got} in the manifest, {exponent} was requested')
    frames = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
    p = tm.TaaParams(hdr_weight_exponent=exponent)

    recon_err, clamp_err, blend_err = [], [], []
    for fr in frames:
        rect = fr.rect('window')
        jitter = np.array([fr.jitter_x, fr.jitter_y], dtype=np.float64)
        raw = rect.channel('taa_raw')[..., :3].astype(np.float64)
        filtered = rect.channel('taa_filtered')[..., :3].astype(np.float64)
        history = rect.channel('taa_history')[..., :3].astype(np.float64)
        clamped = rect.channel('taa_clamped')[..., :3].astype(np.float64)
        output = rect.channel('taa_output')[..., :3].astype(np.float64)
        blend = rect.channel('taa_raw')[..., 3].astype(np.float64)
        exposure = rect.channel('taa_output')[..., 3].astype(np.float64)
        clip_min = rect.channel('taa_clip_min')[..., :3].astype(np.float64)
        clip_max = rect.channel('taa_clip_max')[..., :3].astype(np.float64)
        taps = rect.channel('taa_plus_taps').astype(np.float64)   # up, left, right, down (YCoCg Y)

        # 1. reconstruction: the plus filter in luma. Tap order in the shader is
        # kPlusIndices = {1, 3, 4, 5, 7} with offsets (0,-1), (-1,0), (0,0), (1,0), (0,1).
        centre_y = tm.rgb_to_ycocg(raw)[..., 0]
        tap_y = np.stack([taps[..., 0], taps[..., 1], centre_y, taps[..., 2], taps[..., 3]], axis=-1)
        offsets = np.array([(0, -1), (-1, 0), (0, 0), (1, 0), (0, 1)], dtype=np.float64)
        d = offsets - jitter
        spatial = np.exp(-p.spatial_weight_mul * (d * d).sum(axis=1))
        hw = (1.0 / (tap_y * exposure[..., None] + 4.0)) ** p.hdr_weight_exponent
        w = spatial * hw
        pred_filtered_y = (tap_y * w).sum(axis=-1) / w.sum(axis=-1)
        got_filtered_y = tm.rgb_to_ycocg(filtered)[..., 0]
        scale = max(1e-6, float(np.abs(got_filtered_y).max()))
        recon_err.append(float(np.abs(pred_filtered_y - got_filtered_y).max() / scale))

        # 2. clamp: in YCoCg, the recorded bounds are the shader's clipMin/clipMax converted to RGB;
        # the conversion is linear and invertible, so convert them back and clip there.
        hist_y = tm.rgb_to_ycocg(history)
        lo, hi = tm.rgb_to_ycocg(clip_min), tm.rgb_to_ycocg(clip_max)
        pred_clamped = np.clip(hist_y, lo, hi)
        got_clamped = tm.rgb_to_ycocg(clamped)
        scale = max(1e-6, float(np.abs(got_clamped).max()))
        clamp_err.append(float(np.abs(pred_clamped - got_clamped).max() / scale))

        # 3. blend
        fy, cy = tm.rgb_to_ycocg(filtered)[..., 0], tm.rgb_to_ycocg(clamped)[..., 0]
        fw, hwt = 1.0 / (fy * exposure + 4.0), 1.0 / (cy * exposure + 4.0)
        a, b = (1.0 - blend) * hwt, blend * fw
        inv = 1.0 / (a + b)
        pred_out = clamped * (a * inv)[..., None] + filtered * (b * inv)[..., None]
        scale = max(1e-6, float(np.abs(output).max()))
        blend_err.append(float(np.abs(pred_out - output).max() / scale))

    checks = [
        ('reconstruction (plus filter, luma)', max(recon_err), 2e-3),
        ('clamp (history clipped to the recorded box)', max(clamp_err), 1e-5),
        ('blend (HDR-weighted lerp)', max(blend_err), 1e-5),
    ]
    ok = True
    for name, err, tol in checks:
        good = err <= tol
        ok &= good
        print(f'  {"ok  " if good else "FAIL"} {name}: max relative error {err:.2e} (tolerance {tol:.0e})')

    return ok, {'exponent': exponent, 'frames': [f.frame for f in frames], 'roi': ROI[0],
                'checks': [{'name': n, 'max_relative_error': e, 'tolerance': t, 'ok': e <= t}
                           for n, e, t in checks],
                'manifest': res['manifest']}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '10-isolate-taa-window'))
    ap.add_argument('--exponents', default='1,0.5',
                    help='HDR weight exponents to validate (1 is the production default)')
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_taa_model_validation')

    ok, runs = True, []
    for e in [float(v) for v in args.exponents.split(',')]:
        good, payload = validate(work, e)
        ok &= good
        runs.append(payload)

    out = {'runs': runs, 'source_fingerprint': runner.source_fingerprint()}
    path = os.path.join(args.out, 'taa-model-validation.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=2)
    print('wrote', path)
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
