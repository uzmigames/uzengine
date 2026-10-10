"""H-30: which stage inside the final gather carries the variation.

The review's instruction was to measure inside the gather without altering the accumulator, and to
treat "two temporal filters exist" as a structure, not a cause: rejection, reprojection and varying
weights can dominate the response just as easily. So this changes nothing in the engine. It reads
the GATHER records of one ordinary capture and separates the mechanisms arithmetically, which is
possible because the records carry every input each stage used.

  The output the lighting consumes is  U(t) = sum_i w_i(t) A_i(t) / sum_i w_i(t)
  where A_i is the accumulator's output at gather pixel i and w_i the reconstruction weight.
  Two things vary: the accumulator and the weights. Holding one at its temporal mean and letting
  the other move gives a series whose variation is what that factor alone reproduces.

    S_A   weights held at their per-pixel temporal mean, accumulator free
    S_W   accumulator held at its per-pixel temporal mean, weights free

  These are NOT additive shares — the user's standing objection to reading ratios as parcels
  applies here too, and the interaction term is reported precisely so the reader can see how far
  from additive the pair is. `additive_residual` is std(U) compared with the variation of
  S_A + S_W - mean, i.e. what the additive reading would predict.

  Inside the accumulator, A(t) = (1-alpha) H(t) + alpha C(t) with C the spatially denoised trace
  and H the reprojected history. Whether the accumulator *originates* variation or merely passes
  its input's is decided by the same first-order model H-27 used: a coefficient-a IIR driven at
  period N has gain a / |1 - (1-a) e^{-j 2pi/N}|. The measured ratio std(A)/std(C) is compared
  with it. The reprojection is checked separately: if H(t) equals A(t-1) at the same gather pixel,
  the recurrence is a plain per-pixel IIR and that model applies; if it does not, reprojection is
  mixing pixels and the model does not.

  Rejection is read directly from the recorded codes, which is why they exist: a count of 1 cannot
  tell a rejection from an initialisation, a code can.

  Reprojection is bounded rather than assumed away. The accumulator reads its history through a
  bilinear reprojection, so H(t) need not equal A(t-1) even with a static camera: the projection is
  jittered, and the jitter moves every frame. `reprojection_share` is the oscillation of
  (1-alpha)(H(t) - A(t-1)) — the part of the accumulator's step that reprojection, rather than the
  history itself, put there — against the oscillation of A. It is a magnitude placed on a scale,
  NOT a causal share: these terms are not additive parcels, and a small ratio does not retire the
  history as a participant any more than a large one would convict it.

  The spectrum is reported as variance shares of named periods, not as an argmax: which bin happens
  to be largest says little on its own. Note that period 2 does not exclude the jitter or the
  sampling — a length-8 sequence can carry a period-2 component, and this one does: Halton base 2
  places its values >= 0.5 on even indices and < 0.5 on odd, so the sign of jitterX is locked to
  frame parity. Both period-2 shares and the jitter period are reported for that reason.

Every number below is gated on the replay reproducing the recorded output first: if
`reconstruct` does not return what the GPU recorded, the separation built on it means nothing
and the study stops. Only interior pixels are analysed - the two rows and columns at the far
edge of the rectangle have taps outside it, so their replay would read a clamped neighbour.

Nothing here selects a correction. It says which stage the variation is in.

Usage: python scripts/rendering/run_gather_stage_study.py [--frames 128]
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

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
JITTER_PERIOD = 8
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': JITTER_PERIOD}
ROI = ('shadow_wall', 150, 260, 64, 64)
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


def luma(rgb):
    return rgb @ REC709


def truncate_half(x):
    """This hardware's RGBA16Float store truncates toward zero; see the validation script."""
    x = np.asarray(x, dtype=np.float32)
    n = x.astype(np.float16)
    over = np.abs(n.astype(np.float32)) > np.abs(x)
    return np.where(over, np.nextafter(n, np.float16(0.0)).astype(np.float32), n.astype(np.float32))


def iir_gain(a, period):
    """Steady-state gain of a first-order IIR at one period (H-27's model)."""
    w = 2.0 * np.pi / period
    return a / abs(1.0 - (1.0 - a) * np.exp(-1j * w))


def gather_arrays(seq, name, x0, y0, w, h):
    """Per-frame accumulator records indexed by gather pixel, and the full-res upsample records."""
    gx0, gy0 = x0 >> 1, y0 >> 1
    gw = ((x0 + w - 1) >> 1) - gx0 + 1
    gh = ((y0 + h - 1) >> 1) - gy0 + 1
    # a gather pixel's records are duplicated over its 2x2 block; take the even full pixel
    ys = np.arange(0, h, 2)
    xs = np.arange(0, w, 2)
    assert len(ys) == gh and len(xs) == gw, (len(ys), gh, len(xs), gw)
    out = {k: [] for k in ('current', 'history', 'accum', 'alpha', 'count', 'reject',
                           'weights', 'base', 'upsampled', 'upstate', 'taa')}
    for f in seq:
        r = f.rect(name)
        cur = r.channel('gather_current')[np.ix_(ys, xs)]
        his = r.channel('gather_history')[np.ix_(ys, xs)]
        acc = r.channel('gather_output')[np.ix_(ys, xs)]
        st = r.channel('gather_state')[np.ix_(ys, xs)]
        out['current'].append(cur[..., :3])
        out['history'].append(his[..., :3])
        out['accum'].append(acc[..., :3])
        out['alpha'].append(acc[..., 3])
        out['count'].append(st[..., 3])
        out['reject'].append(st[..., 0].copy().view(np.uint32))
        out['weights'].append(r.channel('gather_upsample_w'))
        out['base'].append(r.channel('gather_upsample_map')[..., :2].astype(np.int64))
        out['upsampled'].append(r.channel('gather_upsampled')[..., :3])
        out['upstate'].append(r.channel('gather_upsampled')[..., 3].copy().view(np.uint32))
        out['taa'].append(r.channel('taa_output')[..., :3])
    arrays = {k: np.stack(v) for k, v in out.items()}
    arrays['gather_origin'] = (gx0, gy0)
    return arrays


def reconstruct(weights, accum_by_gather, base, gx0, gy0):
    """U(t) from a weight series and an accumulator series, exactly as the shader combines them.

    `weights` is (T, h, w, 4), `accum_by_gather` is (T, gh, gw, 3), `base` is (T, h, w, 2).
    The accumulator is put through the store the upsample reads it through, so the arithmetic is
    the shader's and not an idealisation of it.
    """
    T, h, w, _ = weights.shape
    acc = np.zeros((T, h, w, 3), dtype=np.float64)
    wsum = np.zeros((T, h, w), dtype=np.float64)
    by = base[..., 1] - gy0
    bx = base[..., 0] - gx0
    gh, gw = accum_by_gather.shape[1:3]
    ti = np.arange(T)[:, None, None]
    for q, (dx, dy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
        tap = truncate_half(accum_by_gather[ti, np.clip(by + dy, 0, gh - 1),
                                            np.clip(bx + dx, 0, gw - 1)]).astype(np.float64)
        wq = weights[..., q].astype(np.float64)
        acc += tap * wq[..., None]
        wsum += wq
    nz = wsum > 0
    out = np.zeros_like(acc)
    out[nz] = acc[nz] / wsum[nz][:, None]
    return out, nz


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '18-gather-stages'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()

    name, x0, y0, w, h = ROI
    last = FIRST + args.frames - 1
    work = os.path.join(args.out, 'capture')
    print(f'H-30 gather stage study — frames {FIRST}..{last}, ROI {ROI}, jitter period {JITTER_PERIOD}')
    if not args.reuse:
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject'),
                                 last - SCENE_START + 8, regions=[ROI], region_fields='gather',
                                 region_first=FIRST, region_last=last, cvars=BASE,
                                 timing_first=FIRST, clean=True)
        if res['exit_code'] != 0:
            raise RuntimeError(f'engine exited {res["exit_code"]}; log {res["log_path"]}')
    seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, last)
    a = gather_arrays(seq, name, x0, y0, w, h)
    gx0, gy0 = a['gather_origin']

    # ---- rejection, read from the codes -----------------------------------
    rej = a['reject']
    ups = a['upstate']
    rej_hist = {regiondump.GATHER_REJECT.get(int(c), str(c)): float((rej == c).mean())
                for c in np.unique(rej)}
    ups_hist = {regiondump.GATHER_UPSAMPLE.get(int(c), str(c)): float((ups == c).mean())
                for c in np.unique(ups)}

    # ---- the two-factor separation ----------------------------------------
    W = a['weights'].astype(np.float64)
    A = a['accum'].astype(np.float64)
    base = a['base']
    Wbar = np.repeat(W.mean(axis=0, keepdims=True), W.shape[0], axis=0)
    Abar = np.repeat(A.mean(axis=0, keepdims=True), A.shape[0], axis=0)
    U_actual, nz = reconstruct(W, A, base, gx0, gy0)
    U_A, _ = reconstruct(Wbar, A, base, gx0, gy0)       # only the accumulator moves
    U_W, _ = reconstruct(W, Abar, base, gx0, gy0)       # only the weights move

    recorded = a['upsampled'].astype(np.float64)
    # Interior only: at the far edge of the rectangle a pixel's +1 taps lie outside the capture and
    # `reconstruct` clamps to the edge, which the GPU did not do.
    fh, fw = recorded.shape[1:3]
    inner = (slice(None), slice(0, fh - 2), slice(0, fw - 2))
    gh_a, gw_a = A.shape[1:3]
    ginner = (slice(None), slice(0, gh_a - 1), slice(0, gw_a - 1))
    fidelity = float(np.max(np.abs(luma(U_actual[inner]) - luma(recorded[inner]))
                            / np.maximum(np.abs(luma(recorded[inner])), 1e-6)))
    if fidelity > 1e-6:
        raise SystemExit(
            f'replay does not reproduce the recorded reconstruction ({fidelity:.2e} relative); '
            'the separation below would be meaningless, so nothing is reported')

    def osc(x):
        return float(luma(x).std(axis=0).mean())

    o_u = osc(U_actual[inner])
    o_a, o_w = osc(U_A[inner]), osc(U_W[inner])
    # what an additive reading of the two factors would predict for U
    additive = (U_A - U_A.mean(axis=0, keepdims=True)) + (U_W - U_W.mean(axis=0, keepdims=True))
    o_add = osc(additive[inner])

    # ---- inside the accumulator -------------------------------------------
    C = a['current'].astype(np.float64)
    H = a['history'].astype(np.float64)
    alpha = a['alpha'].astype(np.float64)
    count = a['count'].astype(np.float64)
    o_c, o_h, o_acc = osc(C[ginner]), osc(H[ginner]), osc(A[ginner])
    # is the reprojection the identity here? H(t) against A(t-1) at the same gather pixel
    Hi, Ai = H[ginner], A[ginner]
    repro = float(np.max(np.abs(luma(Hi[1:]) - luma(Ai[:-1]))
                         / np.maximum(np.abs(luma(Ai[:-1])), 1e-6)))
    repro_rms = float(np.sqrt(np.mean((luma(Hi[1:]) - luma(Ai[:-1])) ** 2))
                      / max(np.sqrt(np.mean(luma(Ai[:-1]) ** 2)), 1e-12))
    a_mean = float(alpha.mean())
    predicted = iir_gain(a_mean, JITTER_PERIOD)
    measured = o_acc / o_c if o_c else float('nan')

    taa = a['taa'].astype(np.float64)
    o_taa = osc(taa[inner])

    # Spectrum of each stage's ROI mean, as variance shares of named periods. Per-pixel series
    # are averaged first, so this describes the whole-region flicker rather than per-pixel noise.
    def spectrum(x):
        m = luma(x).mean(axis=(1, 2))
        m = m - m.mean()
        power = np.abs(np.fft.rfft(m)) ** 2
        # Weight conjugate pairs: the one-sided spectrum is not a variance decomposition, and
        # leaving it unweighted inflated the Nyquist bin — which is period 2, the bin being read.
        weight = np.full(power.shape, 2.0)
        weight[0] = 1.0
        if len(m) % 2 == 0:
            weight[-1] = 1.0
        power = power * weight
        power[0] = 0.0
        total = power.sum()
        if total <= 0:
            return {'period_2': 0.0, f'period_{JITTER_PERIOD}': 0.0, 'peak_period': float('nan'),
                    'peak_share': 0.0}
        n = len(m)
        share = lambda period: float(power[int(round(n / period))] / total)  # noqa: E731
        k = int(np.argmax(power))
        return {'period_2': share(2), f'period_{JITTER_PERIOD}': share(JITTER_PERIOD),
                'peak_period': float(n / k) if k else float('inf'),
                'peak_share': float(power[k] / total)}

    spectra = {'current': spectrum(C[ginner]), 'accumulator': spectrum(A[ginner]),
               'upsampled': spectrum(U_actual[inner]), 'taa_output': spectrum(taa[inner])}

    # How much of the accumulator's step is reprojection rather than the history itself.
    reproj_step = (1.0 - alpha[ginner][1:, ..., None]) * (Hi[1:] - Ai[:-1])
    o_reproj = osc(reproj_step)

    print(f'\n  gate - the replay reproduces the recorded reconstruction to {fidelity:.2e} '
          f'relative (interior {fh - 2}x{fw - 2} of {fh}x{fw})')
    print(f'\n  rejection codes over {args.frames} frames: '
          + ', '.join(f'{k} {v:.3%}' for k, v in rej_hist.items()))
    print(f'  upsample states: ' + ', '.join(f'{k} {v:.3%}' for k, v in ups_hist.items()))
    print(f'  alpha: mean {a_mean:.4f}, min {alpha.min():.4f}, max {alpha.max():.4f}; '
          f'count mean {count.mean():.1f}, min {count.min():.0f}, max {count.max():.0f}')

    print('\n  temporal std of luminance, per pixel then averaged (the programme\'s oscillation metric)')
    print(f'    current input C (denoised trace)     {o_c:.4e}')
    print(f'    reprojected history H                {o_h:.4e}')
    print(f'    accumulator output A                 {o_acc:.4e}')
    print(f'    upsampled U (what lighting consumes) {o_u:.4e}')
    print(f'    TAA output                           {o_taa:.4e}')
    print('\n  separation of U (not additive shares — the interaction is reported):')
    print(f'    S_A accumulator free, weights held   {o_a:.4e}  ({o_a / o_u:6.1%} of U)')
    print(f'    S_W weights free, accumulator held   {o_w:.4e}  ({o_w / o_u:6.1%} of U)')
    print(f'    additive reading would give          {o_add:.4e}  ({o_add / o_u:6.1%} of U)')
    print('\n  the accumulator as a first-order filter:')
    print(f'    reprojection vs A(t-1): rms {repro_rms:.3e} relative, worst {repro:.3e}')
    print(f'    std(A)/std(C) measured {measured:.4f}, H-27 model at alpha={a_mean:.4f}, '
          f'period {JITTER_PERIOD}: {predicted:.4f}  (ratio {measured / predicted:.2f}x)')
    print(f'    reprojection\'s own contribution (1-alpha)(H(t) - A(t-1)): {o_reproj:.4e}'
          f'  ({o_reproj / o_acc:.1%} of the accumulator\'s oscillation)')
    print('\n  spectrum of each stage\'s region mean, variance share by period:')
    print(f'    {"stage":<14}{"period 2":>10}{f"period {JITTER_PERIOD}":>10}{"peak":>10}{"at":>8}')
    for k, v in spectra.items():
        print(f'    {k:<14}{v["period_2"]:>10.1%}{v[f"period_{JITTER_PERIOD}"]:>10.1%}'
              f'{v["peak_share"]:>10.1%}{v["peak_period"]:>8.1f}')

    report = {
        'first_frame': FIRST, 'frames': args.frames, 'roi': ROI, 'jitter_period': JITTER_PERIOD,
        'replay_fidelity_relative': fidelity,
        'reject_histogram': rej_hist, 'upsample_histogram': ups_hist,
        'alpha': {'mean': a_mean, 'min': float(alpha.min()), 'max': float(alpha.max())},
        'count': {'mean': float(count.mean()), 'min': float(count.min()), 'max': float(count.max())},
        'oscillation': {'current': o_c, 'history': o_h, 'accumulator': o_acc,
                        'upsampled': o_u, 'taa_output': o_taa},
        'separation': {'accumulator_only': o_a, 'weights_only': o_w, 'additive_reading': o_add,
                       'accumulator_fraction': o_a / o_u, 'weights_fraction': o_w / o_u},
        'reprojection': {'rms_relative_vs_previous_accum': repro_rms, 'worst_relative': repro},
        'iir_model': {'alpha': a_mean, 'predicted_gain': predicted, 'measured_ratio': measured},
        'spectrum': spectra,
        'reprojection_share': {'oscillation': o_reproj, 'fraction_of_accumulator': o_reproj / o_acc},
    }
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, 'stage_study.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n  -> {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
