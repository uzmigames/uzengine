"""followup-12, the door: where does the energy the TAA takes from the bright glass go? Measured over
the glass and its neighbourhood against the cycle-averaged raw input, on the captures of H-45
(35-door-reading). No new capture.

Pre-registered before it was run (followup-12 notes, 2026-09-15).

  data       35-door-reading: records_on r0..r2 give the glass mask and the TAA stages; records_off
             r0..r2 repeat the energy verdicts without the records (C0 showed the two equivalent), as a
             check, not as extra samples
  reference  the cycle-averaged raw input: the per-pixel mean of `taa_raw` over the 128 frames, sixteen
             whole jitter cycles - the reference followup-14 used. (Its 42 % "bright-side energy" at
             `rear_door` was measured on a different mask, the top decile of that reference, so it is
             related, not directly comparable.)
  masks      G    the glass: pixels drawn at least once in records_on r0 (r1 and r2 must match it)
             R_k  ring k: pixels at Chebyshev distance exactly k from G, k = 1..8 (the TAA's 3x3
                  reconstruction reaches one pixel, its Catmull-Rom history sample two)
             N_k  G plus rings 1..k, for k = 1, 2, 4, 8, 16
             F    far background: rectangle pixels at Chebyshev distance > 16 from G
             the whole rectangle
  energy     sum over a mask of the per-pixel temporal mean luminance, for the reference and for
             `taa_output`; ratio = output / reference, per repeat
  rule       candidate criterion C5's 1 %, as the programme already applies it to energy: over a mask,
             CONSERVED when |ratio - 1| <= 1 % in every repeat, NOT CONSERVED when > 1 % in every
             repeat, otherwise INCONCLUSIVE. Evaluated for G, N_1, N_2, N_4, N_8, N_16, F and the rectangle.
  reading    fixed now: if G is NOT CONSERVED and some N_k with k <= 8 is CONSERVED, the energy that
             left the glass is found within k pixels (redistribution). If G and every N_k up to N_16 are
             NOT CONSERVED with the same sign, the drop is not recovered within 16 pixels. Anything else is
             inconclusive. F is the TAA's effect on energy away from the glass: if F is itself NOT
             CONSERVED, ring sums mix the two effects, and the background-corrected recovery is
             descriptive only.
  recovery   descriptive: glass deficit D = reference_G - output_G; ring excess
             E_k = output_Rk - ratio_F x reference_Rk; cumulative recovery = sum_{j <= k} E_j / D.
  stages     descriptive, the reconstruction at the edges: for the glass drawn in every frame, the glass
             drawn in some frames, rings 1..4 and F, the energy and oscillation of `taa_raw`,
             `taa_filtered` (the reconstruction), `taa_history` (reprojected), `taa_clamped` and
             `taa_output`.

It cannot say why energy moves or is lost, and compares no correction. The distance from the glass to
the rectangle's border is reported, and masks the border clips are flagged.

Usage: python scripts/run_door_energy_neighbourhood.py
"""
from __future__ import annotations

import json
import os
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import criteria as crit  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

FIRST, LAST = 237, 364
NAME = 'rear_door'
DATA = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '35-door-reading')
STAGES = ('taa_raw', 'taa_filtered', 'taa_history', 'taa_clamped', 'taa_output')
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
RINGS = tuple(range(1, 9))
N_EVAL = (1, 2, 4, 8, 16)
FAR = 16
C5 = 0.01


class Stop(Exception):
    pass


def chebyshev_distance(mask, limit):
    """Distance in pixels (square neighbourhood) from `mask`, exact up to `limit`, inf beyond."""
    dist = np.full(mask.shape, np.inf)
    dist[mask] = 0.0
    cur = mask.copy()
    for k in range(1, limit + 2):
        grown = cur.copy()
        grown[1:, :] |= cur[:-1, :]
        grown[:-1, :] |= cur[1:, :]
        square = grown.copy()
        square[:, 1:] |= grown[:, :-1]
        square[:, :-1] |= grown[:, 1:]
        dist[square & ~cur] = k
        cur = square
    return dist


def load(region_dir, with_coverage):
    seq = regiondump.load_sequence(region_dir, FIRST, LAST)
    T = len(seq)
    r0 = seq[0].rect(NAME)
    h, w = r0.h, r0.w
    if T % 8:
        raise Stop(f'{region_dir}: {T} frames is not a whole number of jitter cycles')
    stacks = {s: np.zeros((T, h, w)) for s in STAGES}
    cov = np.zeros((T, h, w), dtype=bool) if with_coverage else None
    for t, f in enumerate(seq):
        r = f.rect(NAME)
        for s in STAGES:
            rgb = r.channel(s)[..., :3].astype(np.float64)
            if not np.all(np.isfinite(rgb)):
                raise Stop(f'{s} at frame {f.frame} holds non-finite values')
            stacks[s][t] = rgb @ REC709
        if with_coverage:
            cov[t] = r.channel('translucent_source')[..., 3] >= 0.0
    del seq
    return stacks, cov


def mask_stats(stacks, masks):
    means = {s: stacks[s].mean(axis=0) for s in STAGES}
    out = {}
    for name, m in masks.items():
        if not m.any():
            continue
        row = {'pixels': int(m.sum())}
        for s in STAGES:
            row[f'{s}_energy_sum'] = float(means[s][m].sum())
            row[f'{s}_energy_mean'] = float(means[s][m].mean())
            row[f'{s}_oscillation'] = crit.oscillation(stacks[s][:, m])
        out[name] = row
    return out


def verdict(ratios):
    dev = [abs(r - 1.0) for r in ratios]
    if all(d <= C5 for d in dev):
        return 'CONSERVED'
    if all(d > C5 for d in dev):
        return 'NOT CONSERVED'
    return 'INCONCLUSIVE'


def energy_verdicts(stats_per_repeat, names):
    res = {}
    for name in names:
        ratios = [st[name]['taa_output_energy_sum'] / st[name]['taa_raw_energy_sum'] for st in stats_per_repeat]
        for r in ratios:
            if not np.isfinite(r):
                raise Stop(f'energy ratio for {name} is not finite')
        res[name] = {'ratios': ratios, 'verdict': verdict(ratios),
                     'sign': 'below' if all(r < 1.0 for r in ratios) else ('above' if all(r > 1.0 for r in ratios) else 'mixed')}
    return res


def run(report):
    works_on = [os.path.join(DATA, f'records_on_r{k}', 'region') for k in range(3)]
    works_off = [os.path.join(DATA, f'records_off_r{k}', 'region') for k in range(3)]
    for p in works_on + works_off:
        runner.require_region_dir(p, p)
    report['captures'] = {'records_on': works_on, 'records_off': works_off,
                          'identity': runner.capture_identity(os.path.dirname(works_on[0]))}

    print('  loading records_on r0', flush=True)
    stacks, cov = load(works_on[0], True)
    glass = cov.any(axis=0)
    always = cov.all(axis=0)
    some = glass & ~always
    h, w = glass.shape
    dist = chebyshev_distance(glass, FAR)
    masks = {'glass': glass, 'glass drawn every frame': always, 'glass drawn in some frames': some}
    for k in RINGS:
        masks[f'ring {k}'] = dist == k
    for k in N_EVAL:
        masks[f'N_{k}'] = dist <= k
    masks['far (> 16 px)'] = ~np.isfinite(dist)
    masks['rectangle'] = np.ones_like(glass)
    ys, xs = np.nonzero(glass)
    border = int(min(ys.min(), xs.min(), h - 1 - ys.max(), w - 1 - xs.max()))
    report['masks'] = {name: int(m.sum()) for name, m in masks.items()}
    report['glass_distance_to_border'] = border
    report['clipped_by_border'] = [f'N_{k}' for k in N_EVAL if k > border] + ([f'far (> 16 px)'] if border <= FAR else [])
    print(f'  glass {int(glass.sum())} px (every frame {int(always.sum())}, some {int(some.sum())}); '
          f'distance to the border {border} px; clipped: {report["clipped_by_border"]}')

    on_stats = [mask_stats(stacks, masks)]
    del stacks
    for k in (1, 2):
        print(f'  loading records_on r{k}', flush=True)
        stacks, cov_k = load(works_on[k], True)
        if not np.array_equal(cov_k.any(axis=0), glass):
            raise Stop(f'records_on r{k} draws a different glass mask from r0')
        on_stats.append(mask_stats(stacks, masks))
        del stacks
    off_stats = []
    for k in range(3):
        print(f'  loading records_off r{k}', flush=True)
        stacks, _ = load(works_off[k], False)
        off_stats.append(mask_stats(stacks, masks))
        del stacks

    names = ['glass'] + [f'N_{k}' for k in N_EVAL] + ['far (> 16 px)', 'rectangle']
    ev_on = energy_verdicts(on_stats, names)
    ev_off = energy_verdicts(off_stats, names)
    report['energy'] = {'records_on': ev_on, 'records_off': ev_off,
                        'verdicts_agree': all(ev_on[n]['verdict'] == ev_off[n]['verdict'] for n in names)}
    print('\n  energy over the mask, output / cycle-averaged raw input (C5: within 1 %):')
    for n in names:
        print(f'    {n:<16} {report["masks"][n]:>6} px  ratios {["%.4f" % r for r in ev_on[n]["ratios"]]}  '
              f'{ev_on[n]["verdict"]:<13} (records off: {ev_off[n]["verdict"]})')

    g = ev_on['glass']
    conserved_k = [k for k in N_EVAL if k <= 8 and ev_on[f'N_{k}']['verdict'] == 'CONSERVED']
    if g['verdict'] == 'NOT CONSERVED' and conserved_k:
        reading = f'redistribution: the energy that left the glass is found within {min(conserved_k)} px'
    elif g['verdict'] == 'NOT CONSERVED' and all(ev_on[f'N_{k}']['verdict'] == 'NOT CONSERVED'
                                                 and ev_on[f'N_{k}']['sign'] == g['sign'] for k in N_EVAL):
        reading = 'not recovered within 16 px: every neighbourhood up to N_16 departs from the reference with the glass\'s sign'
    else:
        reading = 'inconclusive under the pre-registered rule'
    if ev_on['far (> 16 px)']['verdict'] != 'CONSERVED':
        reading += '; the far background is not conserved either, so ring sums mix the two effects'
    report['reading'] = reading
    print(f'  reading: {reading}')

    def avg(stats, name, key):
        return float(np.mean([st[name][key] for st in stats]))

    ratio_far = avg(on_stats, 'far (> 16 px)', 'taa_output_energy_sum') / avg(on_stats, 'far (> 16 px)', 'taa_raw_energy_sum')
    deficit = avg(on_stats, 'glass', 'taa_raw_energy_sum') - avg(on_stats, 'glass', 'taa_output_energy_sum')
    cum, recovery = 0.0, []
    for k in RINGS:
        name = f'ring {k}'
        excess = avg(on_stats, name, 'taa_output_energy_sum') - ratio_far * avg(on_stats, name, 'taa_raw_energy_sum')
        cum += excess
        recovery.append({'ring': k, 'excess': excess, 'cumulative_fraction_of_glass_deficit': cum / deficit})
    report['recovery'] = {'glass_deficit': deficit, 'far_ratio': ratio_far, 'rings': recovery,
                          'note': 'descriptive only; the far ratio corrects ring sums for the TAA\'s effect away from the glass'}
    print(f'\n  recovery (descriptive): glass deficit {deficit:.4e}, far-background ratio {ratio_far:.4f}')
    for r in recovery:
        print(f'    ring {r["ring"]}: excess {r["excess"]:+.4e}, cumulative {r["cumulative_fraction_of_glass_deficit"]:+.1%} of the deficit')

    stage_masks = ['glass drawn every frame', 'glass drawn in some frames', 'ring 1', 'ring 2', 'ring 3',
                   'ring 4', 'far (> 16 px)']
    report['stages'] = {n: {k: avg(on_stats, n, k) for k in on_stats[0][n]} for n in stage_masks if n in on_stats[0]}
    print('\n  TAA stages (mean energy per pixel / oscillation, mean of records_on repeats):')
    print('    ' + ' ' * 28 + ''.join(f'{s:>26}' for s in STAGES))
    for n, row in report['stages'].items():
        cells = ''.join(f'{row[f"{s}_energy_mean"]:>12.4e} / {row[f"{s}_oscillation"]:<11.3e}' for s in STAGES)
        print(f'    {n:<28}{cells}')
    report['per_repeat'] = {'records_on': on_stats, 'records_off': off_stats}


def main():
    report = {'status': 'incomplete', 'reasons': [], 'data': DATA, 'frames': [FIRST, LAST],
              'harness': runner.harness_stamp(), 'criteria': {'C5_energy': C5, 'far_px': FAR, 'rings': list(RINGS)}}
    code = 1
    try:
        run(report)
        report['status'] = 'complete'
        code = 0
    except Stop as s:
        report['reasons'].append(str(s))
        print(f'\n  STOPPED: {s}')
        code = 2
    except Exception:  # noqa: BLE001 - every exit writes the report
        report['reasons'].append(traceback.format_exc())
        traceback.print_exc()
    finally:
        path = os.path.join(DATA, 'door_energy_neighbourhood.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
