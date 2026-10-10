"""followup-12, the door: at the 125 partly covered glass pixels, is the glass fragment absent (not
rasterised) or present and hidden by opaque geometry (rejected by the depth test)?

Pre-registered before its captures (followup-12 notes, 2026-09-15). A short diagnostic, not a
correction: the depth-off condition changes the image, and none of its image data is read.

  region   `rear_door` (960, 592, 224, 224), pose A, still camera, fixed exposure, jitter length 8
  frames   237..268: 32 frames = four jitter cycles, after the same 200 warm-up frames as H-45
  captures `components` fields, interleaved, 2 repeats each, one binary, every dump retained:
           depth_on   the default r.Translucency.DepthTest (1)
           depth_off  r.Translucency.DepthTest=0: the translucent pipeline has no depth test, so every
                      rasterised translucent fragment reaches the pixel shader and is recorded
  target   the 125 pixels H-45 found drawn in some frames only, recomputed from
           35-door-reading/records_on_r0 over its 128 frames

Gates, in order; the first failure stops the study and the report says where:
  D0  one binary for all four captures, and each manifest's CVar registry shows
      r.Translucency.DepthTest = 1 (depth_on) or 0 (depth_off).
  D1  the default is unchanged by the new build: depth_on coverage over frames 237..268 equals
      35-door-reading/records_on_r0 coverage at the same frames, at every pixel; and in both conditions
      the layer record agrees with coverage (drawn <=> layers >= 1, whole numbers, depth 0 where not drawn).
  D2  the switch acted: depth_off draws at least one pixel-frame that depth_on does not. (v1, which ran
      without early depth testing, recorded 2 640 pixels with translucent fragments against 137.)
  D3  superset: every pixel-frame depth_on draws, depth_off draws too. A fragment that passes the depth
      test must exist; a violation means the conditions differ in more than the depth test.
  D4  coverage is identical between the two repeats of each condition.
  D5  the depth record is trustworthy: where both conditions draw with exactly one layer, the recorded
      depth agrees to 1e-6 relative.

Classification, at each target pixel and each frame depth_on does NOT draw it (repeat 0; repeat 1 must
give the same classification, which is reported):
  ABSENT   depth_off draws nothing there: no translucent fragment was rasterised at the pixel.
  HIDDEN   depth_off draws exactly one layer whose depth lies in the pixel's glass band: the glass
           fragment exists and the depth test rejected it.
  OTHER    depth_off draws one layer outside the band, or two or more layers: another translucent
           surface, or ambiguous. Counted separately, never folded into the other two.
  Glass band of a pixel: the min..max depth depth_on records there over the frames it draws it,
  widened by the largest depth step between that pixel and a 4-adjacent pixel drawn in the same
  depth_on frame (a sub-pixel jitter move cannot change a plane's depth by more than a pixel's step).
  A pixel with no drawn neighbour in any frame keeps its own range only, and is counted.
Per pixel: raster edge (ABSENT only), depth edge (HIDDEN only), mixed (both); OTHER is flagged.

What it can and cannot conclude, stated before the numbers: it says which of the two mechanisms makes
the glass disappear at each pixel-frame. It says nothing about the TAA and nothing about a correction.
A fixed translucent projection could act only on ABSENT pixel-frames, and would still change the
alignment with the jittered depth and the position the TAA reconstructs; that is a later, separately
pre-registered step, taken only if the rasterisation turns out to matter.

Usage: python scripts/rendering/run_door_coverage_depth.py [--repeats 2] [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
FIRST = 237
LAST = FIRST + 31
TARGET_FIRST, TARGET_LAST = 237, 364
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
PROJECT = 'sponza-flicker-a.uzproject'
ROI = ('rear_door', 960, 592, 224, 224)
NAME = ROI[0]
CVAR = 'r.Translucency.DepthTest'
CONDITIONS = {'depth_on': ({}, 1), 'depth_off': ({CVAR: 0}, 0)}
H45 = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '35-door-reading', 'records_on_r0')
D5_RELATIVE = 1e-6


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def finite(a, what):
    bad = int(np.count_nonzero(~np.isfinite(np.asarray(a, dtype=np.float64))))
    if bad:
        raise Stop('finite', f'{what} holds {bad} non-finite values')
    return a


def load(region_dir, first, last):
    seq = regiondump.load_sequence(region_dir, first, last)
    T = len(seq)
    r0 = seq[0].rect(NAME)
    h, w = r0.h, r0.w
    cap = {'T': T, 'h': h, 'w': w, 'frames': [int(f.frame) for f in seq],
           'jitter': np.array([int(f.jitter_index) for f in seq], dtype=np.int64),
           'cov': np.zeros((T, h, w), dtype=bool), 'layers': np.zeros((T, h, w)),
           'depth': np.zeros((T, h, w))}
    for t, f in enumerate(seq):
        r = f.rect(NAME)
        op = finite(r.channel('translucent_source')[..., 3], f'opacity at frame {f.frame}')
        lay = finite(r.channel('translucent_layers')[..., :2].astype(np.float64), f'layer record at frame {f.frame}')
        cap['cov'][t] = op >= 0.0
        cap['layers'][t] = lay[..., 0]
        cap['depth'][t] = lay[..., 1]
    return cap


def record_consistency(cap, check_depth=True):
    cov, lay, dep = cap['cov'], cap['layers'], cap['depth']
    res = {'drawn_without_layer': int(np.count_nonzero(cov & (lay < 1.0))),
           'layer_without_draw': int(np.count_nonzero(~cov & (lay != 0.0))),
           'non_integral_layers': int(np.count_nonzero(lay != np.round(lay)))}
    if check_depth:
        res['depth_without_draw'] = int(np.count_nonzero(~cov & (dep != 0.0)))
    return res


def manifest_cvar(work):
    with open(os.path.join(work, 'manifest.json'), encoding='utf-8') as fh:
        m = json.load(fh)
    return (m.get('cvars') or {}).get(CVAR)


def glass_band(on):
    cov = np.concatenate([c['cov'] for c in on])
    dep = np.concatenate([c['depth'] for c in on])
    lo = np.where(cov, dep, np.inf).min(axis=0)
    hi = np.where(cov, dep, -np.inf).max(axis=0)
    h, w = lo.shape
    step = np.zeros((h, w))
    pairs = np.zeros((h, w), dtype=np.int64)
    both_x = cov[:, :, 1:] & cov[:, :, :-1]
    dx = np.where(both_x, np.abs(dep[:, :, 1:] - dep[:, :, :-1]), 0.0).max(axis=0)
    nx = both_x.sum(axis=0)
    step[:, 1:] = np.maximum(step[:, 1:], dx)
    step[:, :-1] = np.maximum(step[:, :-1], dx)
    pairs[:, 1:] += nx
    pairs[:, :-1] += nx
    both_y = cov[:, 1:, :] & cov[:, :-1, :]
    dy = np.where(both_y, np.abs(dep[:, 1:, :] - dep[:, :-1, :]), 0.0).max(axis=0)
    ny = both_y.sum(axis=0)
    step[1:, :] = np.maximum(step[1:, :], dy)
    step[:-1, :] = np.maximum(step[:-1, :], dy)
    pairs[1:, :] += ny
    pairs[:-1, :] += ny
    return lo, hi, step, pairs > 0


def classify(on_cap, off_cap, target, lo, hi, step):
    not_drawn = ~on_cap['cov'] & target[None]
    off_drawn = off_cap['cov']
    one = off_cap['layers'] == 1.0
    in_band = (off_cap['depth'] >= (lo - step)[None]) & (off_cap['depth'] <= (hi + step)[None])
    return {'ABSENT': not_drawn & ~off_drawn,
            'HIDDEN': not_drawn & off_drawn & one & in_band,
            'OTHER (one layer outside the band)': not_drawn & off_drawn & one & ~in_band,
            'OTHER (two or more layers)': not_drawn & off_drawn & ~one}


def run(args, report):
    runner.require_exe()
    works = {c: [] for c in CONDITIONS}
    if args.reuse:
        for c in CONDITIONS:
            k = 0
            while os.path.isdir(os.path.join(args.out, f'{c}_r{k}', 'region')):
                works[c].append(os.path.join(args.out, f'{c}_r{k}'))
                k += 1
    else:
        for k in range(args.repeats):
            for c, (extra, _) in CONDITIONS.items():
                work = os.path.join(args.out, f'{c}_r{k}')
                print(f'  ... {c} repeat {k}', flush=True)
                res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', PROJECT),
                                         LAST - SCENE_START + 8, regions=[ROI], region_fields='components',
                                         region_first=FIRST, region_last=LAST, cvars={**BASE, **extra},
                                         timing_first=FIRST, clean=True)
                if res['exit_code'] != 0:
                    raise Stop('capture', f'{c} repeat {k}: exit {res["exit_code"]}')
                works[c].append(work)
    counts = {c: len(ws) for c, ws in works.items()}
    if any(n != args.repeats for n in counts.values()):
        raise Stop('repeats', f'expected {args.repeats} captures per condition, found {counts}')

    # ---- D0 ----------------------------------------------------------------------------------
    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w), CVAR: manifest_cvar(w)}
                              for w in ws] for c, ws in works.items()}
    hashes = {str(e['identity'].get('executable', {}).get('hash'))
              for ws in report['captures'].values() for e in ws}
    wrong = [(c, e['dir'], e[CVAR]) for c, ws in report['captures'].items() for e in ws
             if e[CVAR] is None or int(e[CVAR]) != CONDITIONS[c][1]]
    report['D0'] = {'executable_hashes': sorted(hashes), 'cvar_mismatches': wrong}
    print(f'  D0 binaries {sorted(hashes)}; {CVAR} mismatches {wrong}')
    if len(hashes) != 1:
        raise Stop('D0', f'the captures do not share one binary: {sorted(hashes)}')
    if wrong:
        raise Stop('D0', f'{CVAR} is missing or wrong in the manifest: {wrong}')

    print('  loading captures', flush=True)
    on = [load(os.path.join(w, 'region'), FIRST, LAST) for w in works['depth_on']]
    off = [load(os.path.join(w, 'region'), FIRST, LAST) for w in works['depth_off']]
    h45 = load(runner.require_region_dir(os.path.join(H45, 'region'), 'H-45 records_on_r0'),
               TARGET_FIRST, TARGET_LAST)
    T = on[0]['T']
    for cap in on + off:
        if cap['frames'] != on[0]['frames'] or not np.array_equal(cap['jitter'], on[0]['jitter']):
            raise Stop('alignment', 'the captures differ in frames or jitter sequence')
    if h45['frames'][:T] != on[0]['frames'] or not np.array_equal(h45['jitter'][:T], on[0]['jitter']):
        raise Stop('alignment', 'H-45 frames or jitter sequence do not match this capture')

    # ---- D1 ----------------------------------------------------------------------------------
    differing = int(np.count_nonzero(on[0]['cov'] != h45['cov'][:T]))
    consistency = {'depth_on': [record_consistency(c) for c in on],
                   'depth_off': [record_consistency(c) for c in off]}
    report['D1'] = {'pixel_frames_differing_from_h45': differing, 'record_consistency': consistency}
    print(f'  D1 depth_on vs H-45 coverage, pixel-frames differing: {differing}; record consistency {consistency}')
    if differing:
        raise Stop('D1', 'the default coverage differs from H-45 at the same frames; the new build changed more than intended')
    if any(v for runs in consistency.values() for r in runs for v in r.values()):
        raise Stop('D1', 'the layer or depth record disagrees with coverage')

    # ---- D2 / D3 / D4 ------------------------------------------------------------------------
    extra = off[0]['cov'] & ~on[0]['cov']
    report['D2'] = {'pixel_frames_only_without_depth_test': int(extra.sum()),
                    'pixels_only_without_depth_test': int(extra.any(axis=0).sum()),
                    'pixels_drawn_without_depth_test': int(off[0]['cov'].any(axis=0).sum()),
                    'pixels_drawn_with_depth_test': int(on[0]['cov'].any(axis=0).sum())}
    print(f'  D2 {report["D2"]}')
    if not extra.any():
        raise Stop('D2', f'{CVAR}=0 drew nothing the default did not; the switch did not act')
    violations = [int(np.count_nonzero(a['cov'] & ~b['cov'])) for a, b in zip(on, off)]
    report['D3'] = {'depth_on_drawn_but_depth_off_not': violations}
    print(f'  D3 superset violations per repeat: {violations}')
    if any(violations):
        raise Stop('D3', 'a fragment passed the depth test where no fragment was rasterised without it')
    rep = {'depth_on': int(np.count_nonzero(on[0]['cov'] != on[1]['cov'])) if len(on) > 1 else None,
           'depth_off': int(np.count_nonzero(off[0]['cov'] != off[1]['cov'])) if len(off) > 1 else None}
    report['D4'] = {'pixel_frames_differing_between_repeats': rep}
    print(f'  D4 repeat differences: {rep}')
    if any(v for v in rep.values()):
        raise Stop('D4', 'coverage differs between repeats of one condition')

    # ---- D5 ----------------------------------------------------------------------------------
    worst, n = 0.0, 0
    for a, b in zip(on, off):
        both = a['cov'] & b['cov'] & (a['layers'] == 1.0) & (b['layers'] == 1.0)
        n += int(both.sum())
        if both.any():
            da, db = a['depth'][both], b['depth'][both]
            rel = np.abs(da - db) / np.maximum(np.maximum(np.abs(da), np.abs(db)), 1e-12)
            worst = max(worst, float(rel.max()))
    report['D5'] = {'pixel_frames_compared': n, 'worst_relative': worst}
    print(f'  D5 depth agreement: worst relative {worst:.2e} over {n} pixel-frames')
    if n == 0:
        raise Stop('D5', 'no pixel-frame drawn with one layer in both conditions; the depth record cannot be checked')
    if worst > D5_RELATIVE:
        raise Stop('D5', f'the recorded depth differs between conditions ({worst:.2e} > {D5_RELATIVE:g})')

    # ---- classification ----------------------------------------------------------------------
    target = h45['cov'].any(axis=0) & ~h45['cov'].all(axis=0)
    always = h45['cov'].all(axis=0)
    in_window = on[0]['cov'][:, target]
    lo, hi, step, has_neighbour = glass_band(on)
    classes = classify(on[0], off[0], target, lo, hi, step)
    same_r1 = None
    if len(on) > 1:
        c1 = classify(on[1], off[1], target, lo, hi, step)
        same_r1 = all(np.array_equal(classes[k], c1[k]) for k in classes)
    totals = {k: int(v.sum()) for k, v in classes.items()}
    not_drawn_total = sum(totals.values())
    per_pixel = {k: v.sum(axis=0) for k, v in classes.items()}
    other = per_pixel['OTHER (one layer outside the band)'] + per_pixel['OTHER (two or more layers)']
    absent, hidden = per_pixel['ABSENT'], per_pixel['HIDDEN']
    cats = {'raster edge (ABSENT only)': target & (absent > 0) & (hidden == 0),
            'depth edge (HIDDEN only)': target & (hidden > 0) & (absent == 0),
            'mixed (ABSENT and HIDDEN)': target & (absent > 0) & (hidden > 0),
            'neither (OTHER only, or never undrawn here)': target & (absent == 0) & (hidden == 0)}

    def coords(mask):
        ys, xs = np.nonzero(mask)
        return [[int(x), int(y)] for x, y in zip(xs, ys)]

    report['classification'] = {
        'target_pixels': int(target.sum()), 'always_drawn_pixels_h45': int(always.sum()),
        'target_pixels_drawn_every_frame_here': int(in_window.all(axis=0).sum()),
        'target_pixels_never_drawn_here': int((~in_window.any(axis=0)).sum()),
        'target_pixels_without_a_drawn_neighbour': int((target & ~has_neighbour).sum()),
        'largest_step_used': float(step[target].max()) if target.any() else None,
        'band_width_median': float(np.median((hi - lo)[target & np.isfinite(lo)])) if target.any() else None,
        'undrawn_pixel_frames': not_drawn_total,
        'pixel_frames': totals,
        'shares': {k: (v / not_drawn_total if not_drawn_total else None) for k, v in totals.items()},
        'repeat_1_gives_the_same_classification': same_r1,
        'pixels': {k: int(m.sum()) for k, m in cats.items()},
        'pixels_flagged_with_other': int((target & (other > 0)).sum()),
        'coordinates': {k: coords(m) for k, m in cats.items()},
    }
    cl = report['classification']
    print(f'\n  target pixels {cl["target_pixels"]} (H-45 always-drawn {cl["always_drawn_pixels_h45"]}); '
          f'undrawn pixel-frames here {not_drawn_total}; repeat 1 identical: {same_r1}')
    for k, v in totals.items():
        share = v / not_drawn_total if not_drawn_total else float('nan')
        print(f'    {k:<38} {v:>6} pixel-frames ({share:.1%})')
    for k, m in cats.items():
        print(f'    {k:<46} {int(m.sum()):>4} pixels')
    print(f'    pixels flagged with OTHER: {cl["pixels_flagged_with_other"]}; without a drawn neighbour: '
          f'{cl["target_pixels_without_a_drawn_neighbour"]}; drawn every frame here: '
          f'{cl["target_pixels_drawn_every_frame_here"]}; never drawn here: {cl["target_pixels_never_drawn_here"]}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '36-door-coverage-depth'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'rear_door coverage: rasterisation or depth test — frames {FIRST}..{LAST}, {args.repeats} repeats')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'roi': ROI, 'frames': [FIRST, LAST],
              'repeats': args.repeats, 'conditions': {c: v[0] for c, v in CONDITIONS.items()},
              'reuse': args.reuse, 'harness': runner.harness_stamp(),
              'criteria': {'D5_relative': D5_RELATIVE}}
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
        path = runner.report_path(args.out, 'coverage_depth.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        stopped = f', stopped at {report["stopped_at"]}' if report['stopped_at'] else ''
        print(f'\n  -> {path}  (status: {report["status"]}{stopped})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
