"""Regional global distance field updates — gates G2 / G3 of lighting block B, item 2 (phases 3-4).

Scene `scenes/gdf-incremental-reference.uzscene` (`scripts/gdf_incremental_reference.py`): a ground
slab, two walls and a 100 cm box authored at x -250..-150 that `gdf_box_move.lua` moves 400 cm along
+x (to x 150..250) between Lua frames 100 and 160 — render frames ~103-163, the scene preloads in
about three frames. Camera above the path, sun from +x. Project configuration GI (radiance cache,
final gather, reflections, GDF 256, mesh SDF 128, TAA), fixed exposure.

The ground under the box is hidden from the camera, so the measured areas are the 50 cm strips just
west of the box's start and end footprints (START x -300..-250, END x 100..150, both y -50..50) and
a control area away from the path (x -250..250, y 250..350); the strip beside the box reads brighter
than open ground (the box bounces sky light onto it).

Captures (640x360, `diffuse_indirect` of the ground pixels, Rec.709 luminance):
  A  mover scene, frames 0-100, frame-wide `components` dump at frame 95: the world positions give
     the screen rectangles of the three areas, and their values before the move.
  C  static variant `gdf-incremental-reference-end` (the box at its end from the first frame, no
     mover), frames 0-266, the three rectangles dumped on frames 164-262: the reference.
  B  mover scene, same frames and dumps, the regional path (defaults).
  D  mover scene, same frames and dumps, the whole-window path (`r.GI.GDF.Incremental 0`: every
     change rewrites the clipmaps and discards the GI history — the behaviour before this item).

Why the gates read as they do (measured 2026-10-05): the probes are updated in budgeted round-robin
batches (`r.GI.RadianceCache.ProbesPerFrame`) and blended at 0.12, and the multi-bounce feedback
runs through that loop, so the GI of a freshly built scene keeps converging for ~200 frames (the
END strip of C: 1.335e-1 at frame 95, 1.398e-1 at 164, 1.411e-1 over 230-262). The reference is
therefore read at the same frames as the candidate, and convergence is judged ~100 frames after
the arrival. A first version of G3 compared frame 170 with C at frame 95 and failed for that reason.

Checks (pre-registered):
  G2a the move is regional: capture B's log has `GDF regional recompose` lines during the move, no
      whole-window scene recompose after loading (`reason=revoxelise` lines after frame 60 == 0)
      and no cache invalidation after loading (`reason=invalidate` lines == 0; `retrace` is fine)
  G2b cost: the manifest's `GDF_Composite` mean GPU time over frames 100-266 of B is below 0.5 ms
      (D's mean over the same window is reported beside it)
  G3a the END strip (beside the arrival) agrees with C within 3 % over frames 230-262
  G3b the START strip (beside the departure) agrees with C within 3 % over frames 230-262
  G3c the control area stays within 3 % of C on every frame 164-262: a regional change does not
      disturb the lighting away from it (D is reported beside it)
  G3d away from the arrival the regional path re-converges at least as well as the whole-window
      path: mean |deviation from C| over 164-262 of B <= that of D, START strip and control area
  G3e the transient beside the arrival: the END strip's peak |deviation from C| over 164-262 <= 5 %.
      Registered before the `r.GI.RadianceCache.RegionBlendAlpha` candidate was measured (the
      replace, alpha 1, gives 8.8 %); the candidate becomes the default only if it passes G3e while
      G3a-d and the Sponza gates hold.

Writes gdf_regional.json. Usage: python scripts/test_gdf_regional.py [--out DIR] [--reuse] [--reuse-reference] [--alpha A]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

PROJECT = os.path.join(runner.REPO, 'scripts', 'gdf-incremental-reference.uzproject')
PROJECT_END = os.path.join(runner.REPO, 'scripts', 'gdf-incremental-reference-end.uzproject')   # the box at its end, static
W, H = 640, 360
# The scene preloads in ~3 frames, so Lua frame 100 is render frame ~103 and the move ends at ~163.
BEFORE = 95
AFTER_FIRST, AFTER_LAST = 164, 262
STEADY_FIRST = 230
FRAMES = AFTER_LAST + 4
MOVE_TIMING_FIRST = 100
AREAS = {'start': ((-300, -250), (-50, 50)), 'end': ((100, 150), (-50, 50)), 'control': ((-250, 250), (250, 350))}
REC709 = np.array([0.2126, 0.7152, 0.0722])
LOADING_END = 60
G2B_MAX_MS, G3_STEADY_TOL, G3C_TOL, G3E_PEAK_TOL = 0.5, 0.03, 0.03, 0.05


def area_mask(pos, area):
    (x0, x1), (y0, y1) = area
    ground = np.isfinite(pos).all(-1) & (np.abs(pos[..., 2]) < 3.0)
    return ground & (pos[..., 0] >= x0) & (pos[..., 0] <= x1) & (pos[..., 1] >= y0) & (pos[..., 1] <= y1)


def screen_rect(mask, margin=4):
    ys, xs = np.nonzero(mask)
    x0, x1 = max(0, xs.min() - margin), min(W, xs.max() + margin + 1)
    y0, y1 = max(0, ys.min() - margin), min(H, ys.max() + margin + 1)
    return int(x0), int(y0), int(x1 - x0), int(y1 - y0)


def luminance(rect):
    return rect.channel('diffuse_indirect')[..., :3].astype(np.float64) @ REC709


def frame_areas(work):
    """Capture A/C at frame BEFORE: per-area ground pixel count and mean, and the screen rectangles."""
    fr = regiondump.load_sequence(os.path.join(work, 'region'), BEFORE, BEFORE)[0]
    pos = fr.world_positions('FRAME')
    indirect = luminance(fr.rect('FRAME'))
    out, rects = {}, []
    for name, area in AREAS.items():
        m = area_mask(pos, area)
        if m.sum() < 50:
            raise SystemExit(f'area {name} has only {int(m.sum())} ground pixels in {work}')
        out[name] = {'pixels': int(m.sum()), 'mean': float(indirect[m].mean())}
        rects.append((name, *screen_rect(m)))
    return out, rects


def strip_series(work):
    seq = regiondump.load_sequence(os.path.join(work, 'region'), AFTER_FIRST, AFTER_LAST)
    series = {name: [] for name in AREAS}
    for frame in seq:
        for name, area in AREAS.items():
            r = frame.rect(name)
            p = r.world_positions() if hasattr(r, 'world_positions') else frame.world_positions(name)
            m = area_mask(p, area)
            di = luminance(r)
            series[name].append(float(di[m].mean()) if m.any() else float('nan'))
    return series


def log_counts(work):
    counts = {'regional': 0, 'revoxelise': 0, 'invalidate': 0, 'retrace': 0}
    with open(os.path.join(work, 'run.log'), encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = re.search(r'frame=(\d+)', line)
            frame = int(m.group(1)) if m else -1
            if 'GDF regional recompose' in line:
                counts['regional'] += 1
            elif 'GI recentre:' in line and 'reason=revoxelise' in line and frame > LOADING_END:
                counts['revoxelise'] += 1
            elif 'reason=invalidate' in line and frame > LOADING_END:
                counts['invalidate'] += 1
            elif 'reason=retrace' in line and frame > LOADING_END:
                counts['retrace'] += 1
    return counts


def composite_ms(work):
    with open(os.path.join(work, 'manifest.json'), encoding='utf-8') as fh:
        man = json.load(fh)
    return (man.get('gpu_ms') or {}).get('GDF_Composite')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', 'test-gdf-regional'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--alpha', type=float, default=None, help='r.GI.RadianceCache.RegionBlendAlpha for capture B (default: the engine default)')
    ap.add_argument('--reuse-reference', action='store_true', help='keep captures A, C and D from a previous run; recapture B only')
    args = ap.parse_args()
    works = {k: os.path.join(args.out, k) for k in ('a-before', 'c-end-static', 'b-regional', 'd-whole')}
    if not args.reuse:
        runner.require_exe()

    def capture(work, project, frames, **kw):
        if args.reuse or (args.reuse_reference and work != works['b-regional'] and os.path.isdir(os.path.join(work, 'region'))):
            return
        res = runner.run_capture(work, project, frames, region_fields='components', clean=True, **kw)
        if res['exit_code'] != 0:
            raise SystemExit(f'{work}: engine exited {res["exit_code"]}')

    capture(works['a-before'], PROJECT, BEFORE + 6, regions=[('FRAME', 0, 0, W, H)], region_first=BEFORE, region_last=BEFORE,
            timing_first=BEFORE)
    before, rects = frame_areas(works['a-before'])
    print('  areas (capture A, frame %d): ' % BEFORE + ', '.join(f'{n} {b["pixels"]} px mean {b["mean"]:.4e} rect {r[1:]}'
                                                                  for (n, b), r in zip(before.items(), rects)))
    strips = dict(regions=rects, region_first=AFTER_FIRST, region_last=AFTER_LAST, timing_first=MOVE_TIMING_FIRST)
    capture(works['c-end-static'], PROJECT_END, FRAMES, **strips)
    b_cvars = {'r.GI.RadianceCache.RegionBlendAlpha': args.alpha} if args.alpha is not None else None
    capture(works['b-regional'], PROJECT, FRAMES, cvars=b_cvars, **strips)
    capture(works['d-whole'], PROJECT, FRAMES, cvars={'r.GI.GDF.Incremental': 0}, **strips)

    series = {k: strip_series(works[k]) for k in ('c-end-static', 'b-regional', 'd-whole')}
    counts = {k: log_counts(works[k]) for k in ('b-regional', 'd-whole')}
    ms = {k: composite_ms(works[k]) for k in ('b-regional', 'd-whole')}
    steady = STEADY_FIRST - AFTER_FIRST
    ref = {n: np.array(series['c-end-static'][n]) for n in AREAS}
    refc = {n: float(np.nanmean(ref[n][steady:])) for n in AREAS}
    stats = {}
    for k in ('b-regional', 'd-whole'):
        stats[k] = {}
        for n in AREAS:
            s = np.array(series[k][n])
            dev = (s - ref[n]) / refc[n]
            stats[k][n] = {'mean_abs_dev': float(np.nanmean(np.abs(dev))), 'peak_abs_dev': float(np.nanmax(np.abs(dev))),
                           'dev_first': float(dev[0]), 'steady': float(np.nanmean(s[steady:])),
                           'steady_dev': float((np.nanmean(s[steady:]) - refc[n]) / refc[n])}
    print(f'  counts: B {counts["b-regional"]}  D {counts["d-whole"]}; GDF_Composite mean ms over {MOVE_TIMING_FIRST}-{FRAMES}: '
          f'B {ms["b-regional"]}  D {ms["d-whole"]}')
    for n in AREAS:
        print(f'  {n:8} C steady {refc[n]:.4e} (sd {100 * np.nanstd(ref[n][steady:]) / refc[n]:.2f} %, before-move A {before[n]["mean"]:.4e})')
        for k in ('b-regional', 'd-whole'):
            st = stats[k][n]
            print(f'    {k:12} steady {st["steady"]:.4e} ({100 * st["steady_dev"]:+.2f} %)  mean|dev| {100 * st["mean_abs_dev"]:.2f} %  '
                  f'peak|dev| {100 * st["peak_abs_dev"]:.2f} %  dev@{AFTER_FIRST} {100 * st["dev_first"]:+.2f} %')
    results = []

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}')

    cb = counts['b-regional']
    check('G2a the move is regional (no whole-window recompose, no cache invalidation after loading)',
          cb['regional'] > 0 and cb['revoxelise'] == 0 and cb['invalidate'] == 0,
          f'regional {cb["regional"]}, revoxelise {cb["revoxelise"]}, invalidate {cb["invalidate"]}, retrace {cb["retrace"]}')
    check(f'G2b GDF_Composite mean over frames {MOVE_TIMING_FIRST}-{FRAMES} < {G2B_MAX_MS} ms',
          ms['b-regional'] is not None and ms['b-regional'] < G2B_MAX_MS, f'B {ms["b-regional"]} ms (whole-window D {ms["d-whole"]} ms)')
    b, d = stats['b-regional'], stats['d-whole']
    check(f'G3a END strip within {100 * G3_STEADY_TOL:.0f} % of the static reference over frames {STEADY_FIRST}-{AFTER_LAST}',
          abs(b['end']['steady_dev']) <= G3_STEADY_TOL, f'{100 * b["end"]["steady_dev"]:+.2f} % (D {100 * d["end"]["steady_dev"]:+.2f} %)')
    check(f'G3b START strip within {100 * G3_STEADY_TOL:.0f} % of the static reference over frames {STEADY_FIRST}-{AFTER_LAST}',
          abs(b['start']['steady_dev']) <= G3_STEADY_TOL, f'{100 * b["start"]["steady_dev"]:+.2f} % (D {100 * d["start"]["steady_dev"]:+.2f} %)')
    check(f'G3c control area within {100 * G3C_TOL:.0f} % of the static reference on every frame {AFTER_FIRST}-{AFTER_LAST}',
          b['control']['peak_abs_dev'] <= G3C_TOL, f'peak {100 * b["control"]["peak_abs_dev"]:.2f} % (D {100 * d["control"]["peak_abs_dev"]:.2f} %)')
    check('G3d regional re-converges at least as well as whole-window away from the arrival (mean |dev|, START and control)',
          b['start']['mean_abs_dev'] <= d['start']['mean_abs_dev'] and b['control']['mean_abs_dev'] <= d['control']['mean_abs_dev'],
          f'START {100 * b["start"]["mean_abs_dev"]:.2f} vs {100 * d["start"]["mean_abs_dev"]:.2f} %, '
          f'control {100 * b["control"]["mean_abs_dev"]:.2f} vs {100 * d["control"]["mean_abs_dev"]:.2f} %')
    check(f'G3e END strip peak |dev| over frames {AFTER_FIRST}-{AFTER_LAST} <= {100 * G3E_PEAK_TOL:.0f} %',
          b['end']['peak_abs_dev'] <= G3E_PEAK_TOL, f'{100 * b["end"]["peak_abs_dev"]:.2f} % (D {100 * d["end"]["peak_abs_dev"]:.2f} %)')
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'gdf_regional.json'), 'w', encoding='utf-8') as fh:
        json.dump({'alpha': args.alpha, 'before': before, 'reference_steady': refc, 'stats': stats, 'series': series, 'counts': counts,
                   'composite_ms': ms, 'first_frame': AFTER_FIRST,
                   'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
