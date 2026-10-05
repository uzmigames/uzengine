"""Isolates the Camera C difference of the regional distance-field path (block B.2 phases 3-4).

Four captures of Sponza Camera C (project configuration, `diffuse_indirect` ROI means):
  whole      r.GI.GDF.Incremental 0                      whole-window recomposes, history discarded on change
  regional   Incremental 1, RegionalRetrace 1            regional recomposes, cache keeps history outside the boxes
  control    Incremental 1, RegionalRetrace 0            regional recomposes, cache discards history (old reaction)
  noregions  Incremental 1, MaxRegions 1                 almost every loading frame rewrites whole windows

If `control` == `whole`, the regional FIELD is right and the difference lives in the cache's history
handling; if `control` != `whole`, the regional recompose itself differs from a whole-window one.

Usage: python scripts/run_gdf_regional_ab.py [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import runner  # noqa: E402
import test_gdf_incremental as inc  # noqa: E402

VARIANTS = {
    'whole': (0, None),
    'regional': (1, {'r.GI.GDF.RegionalRetrace': 1}),
    'control': (1, {'r.GI.GDF.RegionalRetrace': 0}),
    'noregions': (1, {'r.GI.GDF.MaxRegions': 1}),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', 'test-gdf-regional-ab'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    if not args.reuse:
        runner.require_exe()
    rows = {}
    for name, (incremental, extra) in VARIANTS.items():
        print(f'  ... {name}', flush=True)
        rows[name] = inc.capture_rois(os.path.join(args.out, name), incremental, args.reuse, extra=extra)
    base = rows['whole']
    for name, r in rows.items():
        print(f'  {name:10} ' + '  '.join(f'{roi} {r[roi]["mean"]:.4e} ({r[roi]["mean"] / base[roi]["mean"]:.3f}x) unsup {r[roi]["unsupported"]:.4f}' for roi in base))
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'gdf_regional_ab.json'), 'w', encoding='utf-8') as fh:
        json.dump(rows, fh, indent=1)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
