"""Validates the GATHER instrumentation before any of it is interpreted.

The review set four requirements for measuring inside the gather, and this checks each of them
against a real capture rather than against the shader source. The criteria and the tolerances below
are fixed here, before the capture runs; nothing is relaxed afterwards.

  V1  mapping        The effective resolutions, the gather pixel, the reconstruction base and the
                     fractions are recorded, not derived, so they are checked against each other
                     and against the file header. Exact: they are integers and exact halves.
                     V1b also checks that the four full pixels one gather pixel writes carry
                     identical accumulator records — they are one invocation's fan-out.
  V2  one-step blend Reproduce the accumulator from the recorded values and compare with the GPU:
                     count == min(historyCount + 1, 64), alpha == max(1/count, temporalAlpha),
                     output == lerp(history, current, alpha); and on every rejection code the
                     output is the current sample with count 1 and alpha 1.
                     Tolerance 1e-6 relative. All of these values come from one shader invocation
                     and are recorded before they are stored, so only float32 rounding separates
                     the replay from the GPU (eps = 1.2e-7; a handful of operations).
  V3  reconstruction Reproduce the bilateral upsample from the four recorded weights and the four
                     taps' own accumulator records.
                     Tolerance 1e-3 relative. The upsample reads the accumulator through an
                     RGBA16Float texture, so its inputs are the recorded values rounded to half
                     precision (eps = 4.9e-4); this tolerance is that rounding, not slack.
  V4  consumption    `ssgi_consumed` in the lighting group must equal the upsampled value, which is
                     what ties a gather output to the frame that consumed it. Same 1e-3 relative
                     for the same reason: RGBA16Float again.
  V3b V4b            The same two comparisons with the half-precision store modelled explicitly.
                     These were first written assuming the RGBA16Float store rounds to nearest even
                     and V4b failed: the consumed value was consistently one half-ulp BELOW the
                     recorded one. The rounding mode was then measured rather than guessed — over
                     393216 components, round-toward-zero reproduces the store bit for bit 100.000%
                     of the time and round-to-nearest-even only 62.6% — so the store truncates on
                     the path observed here, and these checks now model that. V4b is exact and V3b
                     is `(truncate(tap) * w) / sum(w)` in float32, worst case one float32 ulp.
                     Scope: this establishes the behaviour of this store on this path, not a
                     property of the hardware in general. Magnitude: truncation is not bounded by
                     half an ulp. On a positive value it can lose almost a whole ulp (up to 9.8e-4
                     relative for float16), and on a negative one it moves the value toward zero
                     instead. Gather radiance is non-negative, so what is seen here is the downward
                     case.
                     Note what did and did not move: V3 and V4, pre-registered at 1e-3 before the
                     capture, passed as written and are unchanged. What changed is a factual
                     assumption about the store, and the checks that depended on it became
                     stricter, not looser.
  V5  correspondence Every frame is consecutive and its gather, lighting and TAA stamps all equal
                     the frame the CPU read back. The reader already refuses a file where they do
                     not, so this reports what it accepted.

  Negative controls  Two, because a check that cannot fail is worse than no check. N1 replays the
                     blend with alpha displaced by 1e-3 and requires V2 to fail. N2 replays the
                     reconstruction with the naive mapping — the nearest gather pixel, i.e. halving
                     the coordinate and ignoring the recorded weights — and requires V3 to fail.
                     If either control passes, the corresponding check proves nothing and this
                     script reports failure regardless of the checks themselves.

Usage: python scripts/rendering/validate_gather_instrumentation.py [--frames 32]
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
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}

#: A window of the followup-09 `shadow_wall` rectangle. 21 records/pixel is 336 bytes, so the full
#: 192x384 ROI would be 25 MB per frame; this is the same wall, small enough to keep every frame.
ROI = ('shadow_wall', 150, 260, 64, 64)

TOL_BLEND = 1e-6      # one invocation, float32
TOL_HALF = 1e-3       # across an RGBA16Float texture
CONTROL_ALPHA_SHIFT = 1e-3

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def truncate_half(x):
    """The largest half-precision value with |h| <= |x| — what this hardware's RGBA16Float store
    does, measured rather than assumed (see the module docstring)."""
    x = np.asarray(x, dtype=np.float32)
    n = x.astype(np.float16)
    over = np.abs(n.astype(np.float32)) > np.abs(x)
    return np.where(over, np.nextafter(n, np.float16(0.0)).astype(np.float32), n.astype(np.float32))


def relative(a, b):
    """Max relative difference, with an absolute floor so near-zero radiance is not amplified."""
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    scale = np.maximum(np.maximum(np.abs(a), np.abs(b)), 1e-6)
    return float(np.max(np.abs(a - b) / scale)) if a.size else 0.0


def gather_grid(rect, x0, y0):
    """Accumulator records indexed by gather pixel, plus the fan-out consistency check.

    Records 0-4 describe gather pixel `floor(p/2)`, and the four full pixels of one gather pixel
    receive identical copies. Returns (grid, gx0, gy0, consistent) where grid is
    (gh, gw, 5, 4) float32.
    """
    h, w = rect.h, rect.w
    recs = rect.records()[:, :, rect.frame.offsets[regiondump.FIELD_GATHER]:
                          rect.frame.offsets[regiondump.FIELD_GATHER] + 5, :]
    gx0, gy0 = x0 >> 1, y0 >> 1
    gw = ((x0 + w - 1) >> 1) - gx0 + 1
    gh = ((y0 + h - 1) >> 1) - gy0 + 1
    grid = np.full((gh, gw, 5, 4), np.nan, dtype=np.float32)
    consistent = True
    for py in range(h):
        gy = ((y0 + py) >> 1) - gy0
        for px in range(w):
            gx = ((x0 + px) >> 1) - gx0
            cell = grid[gy, gx]
            if np.isnan(cell[0, 0]):
                grid[gy, gx] = recs[py, px]
            elif not np.array_equal(cell, recs[py, px]):
                consistent = False
    return grid, gx0, gy0, consistent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=32)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '17-gather-instrumentation'))
    ap.add_argument('--reuse', action='store_true', help='analyse the last capture instead of rerunning')
    args = ap.parse_args()
    runner.require_exe()

    last = FIRST + args.frames - 1
    work = os.path.join(args.out, 'capture')
    print(f'gather instrumentation validation — frames {FIRST}..{last}, ROI {ROI}')
    if not args.reuse:
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject'),
                                 last - SCENE_START + 8, regions=[ROI], region_fields='gather',
                                 region_first=FIRST, region_last=last,
                                 cvars=BASE, timing_first=FIRST, clean=True)
        if res['exit_code'] != 0:
            raise RuntimeError(f'engine exited {res["exit_code"]}; log {res["log_path"]}')
        region_dir = res['region_dir']
    else:
        region_dir = os.path.join(work, 'region')

    # ---- V5: the sequence the reader accepts ------------------------------
    seq = regiondump.load_sequence(region_dir, FIRST, last)
    name, x0, y0, w, h = ROI
    stamps_ok = all(f.rect(name).gather_stamp == f.frame
                    and f.rect(name).lighting_stamp == f.frame
                    and f.rect(name).taa_stamp == f.frame for f in seq)
    consecutive = [f.frame for f in seq] == list(range(FIRST, last + 1))
    check(stamps_ok and consecutive,
          'V5 every frame is consecutive and its gather, lighting and TAA stamps are that frame',
          f'{len(seq)} frames, gather stamps {seq[0].rect(name).gather_stamp}..'
          f'{seq[-1].rect(name).gather_stamp}')

    temporal_alpha = None
    v1, v1b, v2, v3, v4, v3b, v4b = True, True, True, True, True, True, True
    n1_failed_as_required, n2_failed_as_required = True, True
    worst = {'v2': 0.0, 'v3': 0.0, 'v4': 0.0, 'v3b': 0.0, 'n1': 0.0, 'n2': 0.0}
    v4b_exact = 0
    reject_hist = {}
    upsample_hist = {}
    v1_detail = v2_detail = v3_detail = v4_detail = v3b_detail = ''

    X = x0 + np.arange(w)[None, :]
    Y = y0 + np.arange(h)[:, None]
    want_base_x = np.broadcast_to(X >> 1, (h, w))
    want_base_y = np.broadcast_to(Y >> 1, (h, w))
    want_frac_x = np.broadcast_to((X & 1) * 0.5, (h, w))
    want_frac_y = np.broadcast_to((Y & 1) * 0.5, (h, w))

    for f in seq:
        r = f.rect(name)
        state = r.channel('gather_state')
        weights4 = r.channel('gather_weights')
        upw = r.channel('gather_upsample_w')
        upmap = r.channel('gather_upsample_map')
        resolution = r.channel('gather_resolution')
        upsampled = r.channel('gather_upsampled')
        current = r.channel('gather_current')
        history = r.channel('gather_history')
        output = r.channel('gather_output')

        # ---- V1 mapping ---------------------------------------------------
        gw, gh, fw, fh = (resolution[..., i] for i in range(4))
        res_ok = (np.all(fw == f.render_width) and np.all(fh == f.render_height)
                  and np.all(gw == (f.render_width + 1) // 2) and np.all(gh == (f.render_height + 1) // 2))
        map_ok = (np.array_equal(upmap[..., 0], want_base_x.astype(np.float32))
                  and np.array_equal(upmap[..., 1], want_base_y.astype(np.float32))
                  and np.array_equal(upmap[..., 2], want_frac_x.astype(np.float32))
                  and np.array_equal(upmap[..., 3], want_frac_y.astype(np.float32)))
        tap0_ok = (np.array_equal(state[..., 1], want_base_x.astype(np.float32))
                   and np.array_equal(state[..., 2], want_base_y.astype(np.float32)))
        if not (res_ok and map_ok and tap0_ok):
            v1 = False
            v1_detail = v1_detail or (f'frame {f.frame}: resolutions {res_ok}, base/fraction {map_ok}, '
                                      f'gather pixel is tap (0,0) {tap0_ok}')

        grid, gx0, gy0, consistent = gather_grid(r, x0, y0)
        if not consistent:
            v1b = False

        # ---- V2 the one-step blend ----------------------------------------
        reject = state[..., 0].copy().view(np.uint32)
        count = state[..., 3].astype(np.float64)
        alpha = output[..., 3].astype(np.float64)
        hist_count = history[..., 3].astype(np.float64)
        ta = float(weights4[..., 3].reshape(-1)[0])
        temporal_alpha = ta if temporal_alpha is None else temporal_alpha
        for code in np.unique(reject):
            reject_hist[int(code)] = reject_hist.get(int(code), 0) + int((reject == code).sum())

        accepted = reject == 0
        if accepted.any():
            want_count = np.minimum(hist_count[accepted] + 1.0, 64.0)
            want_alpha = np.maximum(1.0 / want_count, ta)
            blend = (history[..., :3][accepted].astype(np.float64) * (1.0 - want_alpha[:, None])
                     + current[..., :3][accepted].astype(np.float64) * want_alpha[:, None])
            e = max(relative(count[accepted], want_count), relative(alpha[accepted], want_alpha),
                    relative(output[..., :3][accepted], blend))
            worst['v2'] = max(worst['v2'], e)
            if e > TOL_BLEND:
                v2 = False
                v2_detail = v2_detail or f'frame {f.frame}: relative {e:.3e} > {TOL_BLEND:.0e}'
            # N1: the same replay with a displaced alpha must not pass
            bad_alpha = want_alpha + CONTROL_ALPHA_SHIFT
            bad = (history[..., :3][accepted].astype(np.float64) * (1.0 - bad_alpha[:, None])
                   + current[..., :3][accepted].astype(np.float64) * bad_alpha[:, None])
            en = relative(output[..., :3][accepted], bad)
            worst['n1'] = max(worst['n1'], en)
            if en <= TOL_BLEND:
                n1_failed_as_required = False
        rejected = ~accepted
        if rejected.any():
            e = max(relative(output[..., :3][rejected], current[..., :3][rejected]),
                    relative(count[rejected], 1.0), relative(alpha[rejected], 1.0))
            worst['v2'] = max(worst['v2'], e)
            if e > TOL_BLEND:
                v2 = False
                v2_detail = v2_detail or f'frame {f.frame}: rejected pixels differ by {e:.3e}'

        # ---- V3 the reconstruction ----------------------------------------
        st = upsampled[..., 3].copy().view(np.uint32)
        for code in np.unique(st):
            upsample_hist[int(code)] = upsample_hist.get(int(code), 0) + int((st == code).sum())
        # interior pixels: both taps of both axes are inside the rectangle
        interior = np.zeros((h, w), dtype=bool)
        interior[:max(h - 2, 0), :max(w - 2, 0)] = True
        sel = interior & (st == 0)
        if sel.any():
            bx = upmap[..., 0].astype(np.int64) - gx0
            by = upmap[..., 1].astype(np.int64) - gy0
            acc = np.zeros((h, w, 3), dtype=np.float64)
            acc16 = np.zeros((h, w, 3), dtype=np.float64)
            naive = np.zeros((h, w, 3), dtype=np.float64)
            wsum = np.zeros((h, w), dtype=np.float64)
            for q, (dx, dy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
                tap = grid[np.clip(by + dy, 0, grid.shape[0] - 1),
                           np.clip(bx + dx, 0, grid.shape[1] - 1), 2, :3].astype(np.float64)
                wq = upw[..., q].astype(np.float64)
                acc += tap * wq[..., None]
                # what the upsample actually loads: the accumulator through RGBA16Float
                acc16 += truncate_half(tap).astype(np.float64) * wq[..., None]
                wsum += wq
                if q == 0:
                    naive = tap        # "halve the coordinate": the nearest gather pixel, unweighted
            recon = np.zeros_like(acc)
            recon16 = np.zeros_like(acc)
            nz = wsum > 0
            recon[nz] = acc[nz] / wsum[nz, None]
            recon16[nz] = acc16[nz] / wsum[nz, None]
            e = relative(upsampled[..., :3][sel & nz], recon[sel & nz])
            worst['v3'] = max(worst['v3'], e)
            if e > TOL_HALF:
                v3 = False
                v3_detail = v3_detail or f'frame {f.frame}: relative {e:.3e} > {TOL_HALF:.0e}'
            eb = relative(upsampled[..., :3][sel & nz], recon16[sel & nz])
            worst['v3b'] = max(worst['v3b'], eb)
            if eb > TOL_BLEND:
                v3b = False
                v3b_detail = v3b_detail or f'frame {f.frame}: relative {eb:.3e} > {TOL_BLEND:.0e}'
            # N2: the naive mapping must not reproduce it
            en = relative(upsampled[..., :3][sel & nz], naive[sel & nz])
            worst['n2'] = max(worst['n2'], en)
            if en <= TOL_HALF:
                n2_failed_as_required = False

        # ---- V4 what the lighting consumed --------------------------------
        consumed = r.channel('ssgi_consumed')
        mode = consumed[..., 3].copy().view(np.uint32)
        sdf = mode == 1
        if sdf.any():
            e = relative(consumed[..., :3][sdf], upsampled[..., :3][sdf])
            worst['v4'] = max(worst['v4'], e)
            if e > TOL_HALF:
                v4 = False
                v4_detail = v4_detail or f'frame {f.frame}: relative {e:.3e} > {TOL_HALF:.0e}'
            # V4b: the lighting loads a texel, so putting the recorded value through the store
            # must give back exactly what it read — no tolerance at all.
            stored = truncate_half(upsampled[..., :3][sdf])
            if np.array_equal(stored, consumed[..., :3][sdf].astype(np.float32)):
                v4b_exact += int(sdf.sum())
            else:
                v4b = False
                v4_detail = v4_detail or (f'frame {f.frame}: {int((stored != consumed[..., :3][sdf]).sum())}'
                                          ' components differ after the half store')

    check(v1, 'V1 the recorded mapping agrees with the recorded coordinates and the file header',
          v1_detail or f'render {seq[0].render_width}x{seq[0].render_height}, '
                       f'gather {seq[0].render_width // 2}x{seq[0].render_height // 2}')
    check(v1b, 'V1b the four full pixels of one gather pixel carry identical accumulator records')
    check(v2, 'V2 the one-step blend replays the GPU output from the recorded values',
          v2_detail or f'worst relative {worst["v2"]:.2e} <= {TOL_BLEND:.0e}, '
                       f'temporalAlpha {temporal_alpha:.4f}')
    check(v3, 'V3 the reconstruction replays from the four recorded weights and the taps records',
          v3_detail or f'worst relative {worst["v3"]:.2e} <= {TOL_HALF:.0e}')
    check(v4, 'V4 the value the lighting consumed is the upsampled value of the same frame',
          v4_detail or f'worst relative {worst["v4"]:.2e} <= {TOL_HALF:.0e}')
    check(v3b, 'V3b modelling the truncating RGBA16Float store leaves only float32 rounding in V3',
          v3b_detail or f'worst relative {worst["v3b"]:.2e} <= {TOL_BLEND:.0e}')
    check(v4b, 'V4b the consumed value is the upsampled value through the store, bit for bit',
          v4_detail or f'{v4b_exact} pixel-frames, exact')
    check(n1_failed_as_required,
          'N1 negative control: a blend replayed with a displaced alpha does NOT pass V2',
          f'displaced by {CONTROL_ALPHA_SHIFT}, worst relative {worst["n1"]:.2e}')
    check(n2_failed_as_required,
          'N2 negative control: halving the coordinate does NOT reproduce the reconstruction',
          f'worst relative {worst["n2"]:.2e}')

    total = sum(reject_hist.values())
    print('\n  rejection codes (accumulator), share of pixel-frames:')
    for code, n in sorted(reject_hist.items()):
        print(f'    {code} {regiondump.GATHER_REJECT.get(code, "?"):<22} {n / total:7.3%}')
    print('  upsample states:')
    tot2 = sum(upsample_hist.values())
    for code, n in sorted(upsample_hist.items()):
        print(f'    {code} {regiondump.GATHER_UPSAMPLE.get(code, "?"):<22} {n / tot2:7.3%}')

    report = {
        'first_frame': FIRST, 'frames': args.frames, 'roi': ROI,
        'tolerances': {'blend': TOL_BLEND, 'half_texture': TOL_HALF},
        'worst_relative': worst, 'temporal_alpha': temporal_alpha,
        'reject_histogram': {regiondump.GATHER_REJECT.get(k, str(k)): v / total
                             for k, v in sorted(reject_hist.items())},
        'upsample_histogram': {regiondump.GATHER_UPSAMPLE.get(k, str(k)): v / tot2
                               for k, v in sorted(upsample_hist.items())},
        'passed': PASS, 'failed': FAIL,
    }
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'validation.json'), 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2)
    print(f'\n{len(PASS)} passed, {len(FAIL)} failed  ->  {os.path.join(args.out, "validation.json")}')
    for f in FAIL:
        print(f'  FAILED: {f}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
