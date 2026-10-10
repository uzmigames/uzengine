"""followup-06 4.1: do the isolated pixels left in `rear_door`'s OUTPUT come from the SDF reflections?

H-41 (`run_firefly_reassessment.py`) left one open question: `taa_output` at `rear_door` still carries
~199 isolated pixels per frame (14.7 % of the region's energy), in the region where H-38 localised the
oscillation to the translucent pass and H-45/H-47 to glass coverage. A reflection filter is worth
building only if those output pixels are reflection fireflies that survive the TAA.

Two independent readings, both pre-registered:

  switch    each condition switches ONE producer off with an existing CVar — `r.GI.Reflections=0`,
            `r.Show.Translucency=0` — and the output isolated count is compared with the baseline's.
            Switching reflections off changes brightness (and the IBL fallback), so this reading is
            confounded by the isolation ratio; the mean is printed beside it.
  overlap   within the BASELINE runs only (nothing changed): the share of the output's isolated
            pixel-frames that are also isolated in `lighting_reflection` in the same frame, and the
            reflection term's share of the output luminance at those pixels. No confound.

Decision (fixed before the captures):
  JUSTIFIED      reflections off removes >= 50 % of the door's isolated output pixels (worst repeat
                 against best baseline repeat) AND the overlap share is >= 50 %
  NOT JUSTIFIED  reflections off removes < 20 % OR the overlap share is < 20 %
  otherwise      INCONCLUSIVE

`right_shadow` (15/frame in H-41) is carried in the same captures and read with the same rule.
Isolation rule and masks are imported unchanged from `run_firefly_reassessment.py`.

Usage: python scripts/rendering/run_door_firefly_attribution.py [--frames 48] [--repeats 3] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
from run_firefly_reassessment import (BASE, FIRST, POSES, SCENE_START, isolated_mask,  # noqa: E402
                                      luma)

PROJECT, ROIS = POSES['A']
CONDITIONS = {
    'baseline': {},
    'no_reflections': {'r.GI.Reflections': 0},
    'no_translucency': {'r.Show.Translucency': 0},
}
JUSTIFIED_CUT, JUSTIFIED_OVERLAP = 0.50, 0.50
NOT_JUSTIFIED_BELOW = 0.20


def measure(seq, name):
    counts, shares, means = [], [], []
    overlap_hits = overlap_total = 0
    refl_share_num = refl_share_den = 0.0
    for f in seq:
        out = luma(f.rect(name).channel('taa_output')[..., :3]).astype(np.float64)
        refl = luma(f.rect(name).channel('lighting_reflection')[..., :3]).astype(np.float64)
        m_out = isolated_mask(out)
        m_refl = isolated_mask(refl)
        counts.append(int(m_out.sum()))
        total = float(out.sum())
        shares.append(float(out[m_out].sum() / total) if total > 0 else 0.0)
        means.append(float(out.mean()))
        overlap_total += int(m_out.sum())
        overlap_hits += int((m_out & m_refl).sum())
        refl_share_num += float(refl[m_out].sum())
        refl_share_den += float(out[m_out].sum())
    return {'mean_count': float(np.mean(counts)), 'max_count': int(np.max(counts)),
            'energy_share': float(np.mean(shares)), 'mean_luma': float(np.mean(means)),
            'overlap_share': overlap_hits / overlap_total if overlap_total else None,
            'reflection_luma_share': refl_share_num / refl_share_den if refl_share_den > 0 else None}


def verdict(cut, overlap):
    if overlap is None:
        return 'NOTHING TO ATTRIBUTE'
    if cut < NOT_JUSTIFIED_BELOW or overlap < NOT_JUSTIFIED_BELOW:
        return 'NOT JUSTIFIED'
    if cut >= JUSTIFIED_CUT and overlap >= JUSTIFIED_OVERLAP:
        return 'JUSTIFIED'
    return 'INCONCLUSIVE'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=48)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '53-door-firefly-attribution'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()
    last = FIRST + args.frames - 1
    print(f'followup-06 4.1 — door output fireflies attributed, frames {FIRST}..{last}')

    rows = {}
    for cond, cvars in CONDITIONS.items():
        for k in runner.reuse_repeat_range(args.repeats, args.reuse):
            work = os.path.join(args.out, cond, f'r{k}')
            if not args.reuse:
                print(f'  ... {cond} repeat {k}', flush=True)
                r = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', PROJECT),
                                       last - SCENE_START + 8, regions=ROIS,
                                       region_fields='components', region_first=FIRST,
                                       region_last=last, cvars={**BASE, **cvars},
                                       timing_first=FIRST, clean=True)
                if r['exit_code'] != 0:
                    raise RuntimeError(f'{cond} r{k}: exit {r["exit_code"]}')
            runner.require_region_dir(os.path.join(work, 'region'), f'{cond} repeat {k}')
            seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, last)
            for name, *_ in ROIS:
                rows.setdefault((name, cond), []).append(measure(seq, name))
            runner.release_repeat_dump(work, k)

    summary = {}
    for name, *_ in ROIS:
        base = rows[(name, 'baseline')]
        base_best = min(r['mean_count'] for r in base)
        overlaps = [r['overlap_share'] for r in base if r['overlap_share'] is not None]
        overlap = min(overlaps) if overlaps else None
        refl_shares = [r['reflection_luma_share'] for r in base if r['reflection_luma_share'] is not None]
        print(f'\n  {name}: baseline isolated/frame {[round(r["mean_count"], 1) for r in base]}, '
              f'mean luma {base[0]["mean_luma"]:.4e}')
        print(f'    overlap with isolated reflection pixels (worst repeat): '
              f'{"n/a" if overlap is None else f"{overlap:.1%}"}; reflection share of output luma at '
              f'those pixels: {"n/a" if not refl_shares else f"{max(refl_shares):.2%}"}')
        cuts = {}
        for cond in CONDITIONS:
            if cond == 'baseline':
                continue
            reps = rows[(name, cond)]
            worst = max(r['mean_count'] for r in reps)
            cut = 1.0 - worst / base_best if base_best > 0 else 0.0
            cuts[cond] = cut
            print(f'    {cond:>16}: isolated/frame {[round(r["mean_count"], 1) for r in reps]} '
                  f'-> removes {cut:+.1%} (worst vs best baseline); mean luma {reps[0]["mean_luma"]:.4e}')
        v = verdict(cuts['no_reflections'], overlap)
        print(f'    verdict: {v}')
        summary[name] = {'baseline_best': base_best, 'cuts': cuts, 'overlap_share': overlap,
                         'reflection_luma_share': max(refl_shares) if refl_shares else None,
                         'verdict': v}

    report = {'first_frame': FIRST, 'frames': args.frames, 'repeats': args.repeats,
              'capture_identity': runner.capture_identity(os.path.join(args.out, 'baseline', 'r0')),
              'rule': {'justified_cut': JUSTIFIED_CUT, 'justified_overlap': JUSTIFIED_OVERLAP,
                       'not_justified_below': NOT_JUSTIFIED_BELOW},
              'conditions': CONDITIONS, 'summary': summary,
              'per_repeat': {f'{n}|{c}': r for (n, c), r in rows.items()}}
    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, 'door_fireflies.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2, default=str)
    print(f'\n  -> {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
