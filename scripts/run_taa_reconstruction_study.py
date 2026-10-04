"""followup-12: the TAA reconstruction, recomputed from what it consumed, and taken apart.

The review asked for this, and asked that it be a mathematical diagnostic and nothing more: no
intervention is chosen from it, and no claim that a visual change would work.

The plus-filter reconstruction (r.TAA.ReconstructionMode 0) takes five taps in YCoCg — up, left,
centre, right, down — and forms

    filtered = sum_j c_j * w_j / sum_j w_j,
    w_j = exp(-k * |offset_j - jitter|^2) * HdrWeightY(Y_j)^e,   HdrWeightY(Y) = 1 / (Y * exposure + 4).

The TAA_EXTRA records now carry, per pixel and per frame: the five tap colours and the weight the
shader ACTUALLY applied to each, the jitter it consumed, the sum of the weights, the spatial
multiplier k, the exposure, the exponent e and the mode. `taa_filtered` is the shader's own result.

A constant centre is not a constant input to the filter: the four neighbours can change. That is
the distinction this study resolves, and it does so on the captured numbers, not on the image.

Pre-registered before the captures (followup-12 notes, 2026-09-16):

  capture   pose A, still camera, `rear_door` (960, 592, 224, 224), frames 237..268 (four jitter
            cycles), `--region-fields full`, 2 repeats, dumps retained. Fixed exposure, jitter
            length 8. The classes come from the same capture's coverage record (LIGHTING_EXTRA):
            `always` = glass drawn in every frame (the 12 of H-45), `some` = drawn in some frames
            (the 125), `ring1` = 8-neighbours of any glass pixel that are not glass, `rest`.

  T0  the records describe the plus filter: mode is 0 in every frame, every recorded sum of weights
      is positive, and the five weights sum to the recorded sum within 1e-5 relative. A capture in
      another mode is refused, not approximated.
  T1  the reconstruction recomputed from the records matches the shader's `taa_filtered` at every
      pixel-frame within 1e-4 relative (float32 in the shader, float64 here, one YCoCg round trip).
      This can fail — if the records are not what the shader used, it will — and if it does, nothing
      below is reported.
  T2  the weights recomputed from (jitter, k, Y_j, exposure, e) match the recorded weights within
      1e-4 relative. This can fail on a sign or offset convention, and pins the formula above.

  Recombinations, per pixel over the 32 frames, all evaluated with the SAME formula that passed T1:
    A  actual taps, actual weights            (must equal the record: the control)
    B  taps frozen at their per-pixel temporal mean, actual weights   -> what the weights alone do
    C  actual taps, weights frozen at their per-pixel temporal mean   -> what the values alone do
    D  both frozen                              (must have zero oscillation: the sanity control)
    E  centre tap frozen at its mean, neighbours and weights actual  -> the neighbours' part, with the
                                                                        centre held still
  The oscillation of each is the programme's metric: per-pixel temporal std of luminance, averaged
  over the class; energy is the class mean. B and C are NOT additive shares of A — the filter is a
  ratio of sums — and are reported as what they are: the variation left when one factor is held.

  Also reported, per class: how often each neighbour tap is "glass" (brighter than 10x the class's
  never-drawn median), which is the coverage of the neighbourhood, not of the centre.

What this cannot say: whether any change to the filter, the weights or the coverage would look
better; it decomposes the recorded arithmetic of one pose, one rectangle, 32 frames, 2 repeats.

Usage: python scripts/run_taa_reconstruction_study.py [--frames 32] [--repeats 2] [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import criteria as crit  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
FIRST = 237
NAME = 'rear_door'
ROI = (NAME, 960, 592, 224, 224)
PROJECT = 'sponza-flicker-a.uzproject'
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
TAPS = ('taa_tap_up', 'taa_tap_left', 'taa_tap_centre', 'taa_tap_right', 'taa_tap_down')
OFFSETS = np.array([[0, -1], [-1, 0], [0, 0], [1, 0], [0, 1]], dtype=np.float64)
T0_SUM_TOL = 1e-5
T1_TOL = 1e-4
T2_TOL = 1e-4
GLASS_FACTOR = 10.0


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def rgb_to_ycocg(rgb):
    y = rgb[..., 0] + 2.0 * rgb[..., 1] + rgb[..., 2]
    co = 2.0 * rgb[..., 0] - 2.0 * rgb[..., 2]
    cg = -rgb[..., 0] + 2.0 * rgb[..., 1] - rgb[..., 2]
    return np.stack([y, co, cg], axis=-1)


def ycocg_to_rgb(ycc):
    y, co, cg = ycc[..., 0] * 0.25, ycc[..., 1] * 0.25, ycc[..., 2] * 0.25
    return np.stack([y + co - cg, y + cg, y - co - cg], axis=-1)


def load(region_dir, last):
    seq = regiondump.load_sequence(region_dir, FIRST, last)
    r0 = seq[0].rect(NAME)
    h, w, T = r0.h, r0.w, len(seq)
    cap = {'T': T, 'h': h, 'w': w, 'frames': [int(f.frame) for f in seq],
           'jitter_index': np.array([int(f.jitter_index) for f in seq]),
           'taps': np.zeros((T, 5, h, w, 3)), 'weights': np.zeros((T, 5, h, w)),
           'jitter': np.zeros((T, h, w, 2)), 'wsum': np.zeros((T, h, w)), 'k': np.zeros((T, h, w)),
           'exposure': np.zeros((T, h, w)), 'exponent': np.zeros((T, h, w)), 'mode': np.zeros((T, h, w)),
           'filtered': np.zeros((T, h, w, 3)), 'cov': np.zeros((T, h, w), dtype=bool)}
    for t, f in enumerate(seq):
        r = f.rect(NAME)
        for j, ch in enumerate(TAPS):
            v = r.channel(ch).astype(np.float64)
            cap['taps'][t, j] = v[..., :3]
            cap['weights'][t, j] = v[..., 3]
        fp = r.channel('taa_filter_params').astype(np.float64)
        hp = r.channel('taa_hdr_params').astype(np.float64)
        cap['jitter'][t] = fp[..., :2]
        cap['wsum'][t] = fp[..., 2]
        cap['k'][t] = fp[..., 3]
        cap['exposure'][t] = hp[..., 0]
        cap['exponent'][t] = hp[..., 1]
        cap['mode'][t] = hp[..., 2]
        cap['filtered'][t] = r.channel('taa_filtered')[..., :3].astype(np.float64)
        cap['cov'][t] = r.channel('translucent_source')[..., 3] >= 0.0
    for key in ('taps', 'weights', 'jitter', 'wsum', 'k', 'exposure', 'exponent', 'mode', 'filtered'):
        if not np.all(np.isfinite(cap[key])):
            raise Stop('finite', f'{key} holds non-finite values in {region_dir}')
    return cap


def reconstruct(taps_rgb, weights):
    """taps (T,5,h,w,3) linear RGB, weights (T,5,h,w) -> (T,h,w,3) linear RGB, the shader's formula."""
    ycc = rgb_to_ycocg(taps_rgb)
    num = (ycc * weights[..., None]).sum(axis=1)
    den = weights.sum(axis=1)[..., None]
    return ycocg_to_rgb(num / den)


def rel_err(a, b):
    return np.abs(a - b) / np.maximum(np.maximum(np.abs(a), np.abs(b)), 1e-6)


def run(args, report):
    last = FIRST + args.frames - 1
    works = []
    for k in range(args.repeats):
        work = os.path.join(args.out, f'r{k}')
        works.append(work)
        if args.reuse:
            continue
        print(f'  ... repeat {k}', flush=True)
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', PROJECT),
                                 last - SCENE_START + 8, regions=[ROI], region_fields='full',
                                 region_first=FIRST, region_last=last, cvars=BASE, timing_first=FIRST,
                                 clean=True)
        if res['exit_code'] != 0:
            raise Stop('capture', f'repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = [{'dir': w, 'identity': runner.capture_identity(w)} for w in works]
    caps = [load(runner.require_region_dir(os.path.join(w, 'region'), w), last) for w in works]
    report['frames'] = [caps[0]['frames'][0], caps[0]['frames'][-1]]

    # ---- T0 ----------------------------------------------------------------------------------
    modes = {float(m) for c in caps for m in np.unique(c['mode'])}
    wsum_ok = all(np.all(c['wsum'] > 0) for c in caps)
    sum_rel = max(float(rel_err(c['weights'].sum(axis=1), c['wsum']).max()) for c in caps)
    report['T0'] = {'modes': sorted(modes), 'all_sums_positive': wsum_ok, 'worst_sum_relative': sum_rel,
                    'pass': bool(modes == {0.0} and wsum_ok and sum_rel <= T0_SUM_TOL)}
    print(f'  T0 modes {sorted(modes)}, sums positive {wsum_ok}, five weights vs recorded sum '
          f'worst {sum_rel:.2e}')
    if not report['T0']['pass']:
        raise Stop('T0', 'the records do not describe the plus filter (mode, sums or weights)')

    # ---- T1 ----------------------------------------------------------------------------------
    worst = 0.0
    for c in caps:
        recon = reconstruct(c['taps'], c['weights'])
        worst = max(worst, float(rel_err(recon, c['filtered']).max()))
    report['T1'] = {'worst_relative': worst, 'tolerance': T1_TOL, 'pass': bool(worst <= T1_TOL)}
    print(f'  T1 reconstruction recomputed from the records vs the shader: worst relative {worst:.2e}')
    if not report['T1']['pass']:
        raise Stop('T1', 'the records do not reproduce the shader output; nothing below is reported')

    # ---- T2 ----------------------------------------------------------------------------------
    worst = 0.0
    for c in caps:
        y = rgb_to_ycocg(c['taps'])[..., 0]                                  # (T,5,h,w)
        d = OFFSETS[None, :, None, None, :] - c['jitter'][:, None, :, :, :]   # (T,5,h,w,2)
        spatial = np.exp(-c['k'][:, None] * (d ** 2).sum(axis=-1))
        hdr = (1.0 / (y * c['exposure'][:, None] + 4.0)) ** c['exponent'][:, None]
        worst = max(worst, float(rel_err(spatial * hdr, c['weights']).max()))
    report['T2'] = {'worst_relative': worst, 'tolerance': T2_TOL, 'pass': bool(worst <= T2_TOL)}
    print(f'  T2 weights recomputed from jitter, k, Y, exposure, exponent vs recorded: worst relative {worst:.2e}')
    if not report['T2']['pass']:
        raise Stop('T2', 'the recorded weights are not the formula assumed; the decomposition would be '
                         'of the wrong function')

    # ---- classes -----------------------------------------------------------------------------
    c0 = caps[0]
    glass = np.logical_or.reduce([c['cov'].any(axis=0) for c in caps])
    always = np.logical_and.reduce([c['cov'].all(axis=0) for c in caps])
    some = glass & ~always
    grown = glass.copy()
    grown[1:, :] |= glass[:-1, :]; grown[:-1, :] |= glass[1:, :]
    grown[:, 1:] |= grown[:, :-1].copy(); grown[:, :-1] |= grown[:, 1:].copy()
    ring1 = grown & ~glass
    rest = ~grown
    classes = {'always': always, 'some': some, 'ring1': ring1, 'rest': rest}
    report['classes'] = {k: int(v.sum()) for k, v in classes.items()}
    print(f'  classes: ' + ', '.join(f'{k} {int(v.sum())}' for k, v in classes.items()))

    # ---- recombinations ----------------------------------------------------------------------
    def lum(rgb):
        return rgb @ REC709

    results = {}
    print('\n  oscillation of the reconstruction (per-pixel temporal std of luminance, class mean; worst repeat)')
    print(f'    {"class":<8}{"A actual":>12}{"B weights":>12}{"C values":>12}{"D fixed":>12}'
          f'{"E centre fixed":>16}{"energy A":>12}')
    for cname, mask in classes.items():
        if not mask.any():
            continue
        rows = {'A': [], 'B': [], 'C': [], 'D': [], 'E': [], 'energy': []}
        for c in caps:
            taps, w = c['taps'], c['weights']
            taps_mean = np.broadcast_to(taps.mean(axis=0, keepdims=True), taps.shape)
            w_mean = np.broadcast_to(w.mean(axis=0, keepdims=True), w.shape)
            taps_e = taps.copy()
            taps_e[:, 2] = taps_mean[:, 2]
            variants = {'A': (taps, w), 'B': (taps_mean, w), 'C': (taps, w_mean),
                        'D': (taps_mean, w_mean), 'E': (taps_e, w)}
            for key, (tp, ww) in variants.items():
                out = lum(reconstruct(tp, ww))[:, mask]
                rows[key].append(crit.oscillation(out))
                if key == 'A':
                    rows['energy'].append(float(out.mean()))
        results[cname] = {k: (max(v) if k != 'energy' else float(np.mean(v))) for k, v in rows.items()}
        results[cname]['per_repeat'] = rows
        r = results[cname]
        print(f'    {cname:<8}{r["A"]:>12.3e}{r["B"]:>12.3e}{r["C"]:>12.3e}{r["D"]:>12.3e}'
              f'{r["E"]:>16.3e}{r["energy"]:>12.3e}')
    report['recombinations'] = results

    # ---- the neighbourhood's coverage --------------------------------------------------------
    never_med = float(np.median(lum(c0['taps'][:, 2])[:, rest])) if rest.any() else 0.0
    thr = GLASS_FACTOR * max(never_med, 1e-9)
    print(f'\n  neighbour taps brighter than {thr:.3e} (10x the never-drawn median), fraction of frames, repeat 0:')
    neigh = {}
    for cname, mask in classes.items():
        if not mask.any():
            continue
        row = {}
        for j, label in enumerate(('up', 'left', 'centre', 'right', 'down')):
            row[label] = float((lum(c0['taps'][:, j])[:, mask] > thr).mean())
        neigh[cname] = row
        print(f'    {cname:<8}' + ''.join(f'{label} {row[label]:6.1%}  ' for label in row))
    report['neighbour_glass_fraction'] = {'threshold': thr, 'classes': neigh}

    # ---- the control that must hold ----------------------------------------------------------
    d_max = max(results[c]['D'] for c in results)
    report['D_control'] = {'worst_oscillation': d_max, 'pass': bool(d_max <= 1e-12)}
    if not report['D_control']['pass']:
        raise Stop('D', f'a reconstruction with both factors frozen still oscillates ({d_max:.2e}); the '
                        'decomposition is not trustworthy')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=32)
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '42-taa-reconstruction'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'TAA reconstruction from its own records — rear_door, frames {FIRST}..{FIRST + args.frames - 1}, '
          f'{args.repeats} repeats')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'roi': ROI,
              'harness': runner.harness_stamp(),
              'criteria': {'T0_sum_tol': T0_SUM_TOL, 'T1_tol': T1_TOL, 'T2_tol': T2_TOL,
                           'glass_factor': GLASS_FACTOR}}
    code = 1
    try:
        run(args, report)
        report['status'] = 'complete'
        code = 0
    except Stop as s:
        report['stopped_at'] = s.gate
        report['reasons'].append(s.reason)
        print(f'\n  STOPPED at {s.gate}: {s.reason}')
        code = 2
    except Exception:  # noqa: BLE001 - every exit writes the report
        report['stopped_at'] = 'error'
        report['reasons'].append(traceback.format_exc())
        traceback.print_exc()
    finally:
        os.makedirs(args.out, exist_ok=True)
        path = os.path.join(args.out, 'taa_reconstruction.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
