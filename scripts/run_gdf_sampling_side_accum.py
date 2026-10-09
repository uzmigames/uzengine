"""followup-13: at a recentre, where does the change first appear — the trace, the accumulator, or the TAA?

`run_gdf_sampling_side.py` found the recentre step indistinguishable from the surrounding motion steps
in the gather's RAW trace output (+27), in all six event/ROI cases. The excess of H-40 was measured on
the TAA output. Between the two sit the gather's temporal accumulator — which reprojects a history and
can reject it — and the TAA. The same captures hold all three stages per pixel: +27 the trace, +2 the
accumulated output with +3 its rejection code and sample count, and `taa_output`.

Pre-registered before the numbers were read (followup-13 notes, 2026-09-16). Same events, windows,
pairing and steps as the sampling-side study (the pairing is by recorded world position, so the same
pairs serve every stage):

  A1  per stage — trace (+27), accumulated (+2), TAA output — the mean |delta| of luminance over the
      paired pixels at the EVENT step against the CONTROL steps (min, median, max).
  A2  the accumulator's state at the event step against the controls: the fraction of paired pixels
      whose rejection code (+3.x) is non-zero, the mean sample count (+3.w) and the fraction whose
      sample count fell to 1, the mean effective alpha (+2.w).

  The reading, fixed now, per event/ROI:
    TRACE         the event step exceeds the control maximum already at +27  -> the change is in the
                  per-frame trace (contradicts the sampling-side study; both are reported).
    ACCUMULATOR   +27 inside the controls, +2 above the control maximum, and A2 shows a history reset
                  at the event (rejection or sample count drop beyond the controls) -> the recentre
                  acts through the accumulator's history handling.
    TAA ONLY      +27 and +2 inside the controls, `taa_output` above the control maximum -> the
                  change enters after the gather.
    NONE          every stage inside its controls at these events -> no excess is visible in this
                  window at these ROIs; H-40's excess is not reproduced here and that is reported.
    MIXED         anything else.

What this cannot say: why the history is rejected, if it is; whether the same holds at other events
or poses; anything about an intervention.

Usage: python scripts/run_gdf_sampling_side_accum.py [--data DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
import run_gdf_sampling_side as side  # noqa: E402

REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def load(region_dir, first, last, roi):
    seq = regiondump.load_sequence(region_dir, first, last)
    frames = []
    for f in seq:
        r = f.rect(roi)
        g = lambda slot: r.records()[:, :, r.frame.offsets[regiondump.FIELD_GATHER] + slot, :].astype(np.float64)
        frames.append({'frame': int(f.frame), 'view_proj': np.asarray(f.view_proj, dtype=np.float64),
                       'render': (f.render_width, f.render_height), 'rect': (r.x, r.y, r.w, r.h),
                       'world': g(9)[..., :3], 'resolved': g(10)[..., 3] > 0.5, 'normal': g(11)[..., :3],
                       'trace': g(27)[..., :3] @ REC709, 'accum': g(2)[..., :3] @ REC709, 'alpha': g(2)[..., 3],
                       'reject': g(3)[..., 0].copy().view(np.float64) if False else g(3)[..., 0],
                       'count': g(3)[..., 3], 'taa': r.channel('taa_output')[..., :3].astype(np.float64) @ REC709})
    return frames


def step(cur, prev):
    (yc, xc, yp, xp), coverage = side.pair(cur, prev)
    if yc.size == 0:
        return None
    out = {'pairs': int(yc.size), 'coverage': coverage}
    for key in ('trace', 'accum', 'taa'):
        out[f'delta_{key}'] = float(np.abs(cur[key][yc, xc] - prev[key][yp, xp]).mean())
    rej = cur['reject'][yc, xc]
    # The rejection code is asfloat(uint); a non-zero float here means a non-zero code.
    out['rejected_fraction'] = float(np.mean(rej != 0.0))
    out['sample_count_mean'] = float(cur['count'][yc, xc].mean())
    out['sample_count_reset_fraction'] = float(np.mean(cur['count'][yc, xc] <= 1.0))
    out['alpha_mean'] = float(cur['alpha'][yc, xc].mean())
    return out


def rng(vals):
    return {'min': float(min(vals)), 'median': float(np.median(vals)), 'max': float(max(vals))}


def run(args, report):
    rep = json.load(open(os.path.join(args.data, 'gdf_sampling_side.json'), encoding='utf-8'))
    if rep.get('status') != 'complete':
        raise Stop('input', 'the sampling-side study is not complete; nothing to extend')
    # The captures' own identity, not the binary on disk at analysis time (the 'harness' stamp).
    report['captures'] = [{'name': c['name'], 'repeat': c['repeat'],
                           'identity': c.get('identity') or runner.capture_identity(c['dir'])}
                          for c in rep['captures']]
    rows = []
    for cap in rep['captures']:
        region = runner.require_region_dir(os.path.join(cap['dir'], 'region'), cap['name'])
        for roi_name, *_ in side.ROIS:
            frames = load(region, cap['first'], cap['last'], roi_name)
            by = {f['frame']: f for f in frames}
            steps = {}
            for f in range(cap['first'] + 1, cap['last'] + 1):
                a, b = by.get(f), by.get(f - 1)
                if a is None or b is None:
                    continue
                st = step(a, b)
                if st is not None:
                    steps[f] = st
            ev = steps.get(cap['event'])
            if ev is None:
                raise Stop('pairing', f'{cap["name"]} r{cap["repeat"]} {roi_name}: no pairs at the event')
            ctrl = [s for f, s in steps.items() if f != cap['event']]
            summary = {}
            for key in ('delta_trace', 'delta_accum', 'delta_taa', 'rejected_fraction', 'sample_count_mean',
                        'sample_count_reset_fraction', 'alpha_mean'):
                summary[key] = {'event': ev[key], 'controls': rng([s[key] for s in ctrl])}
            above = {k: summary[f'delta_{k}']['event'] > summary[f'delta_{k}']['controls']['max']
                     for k in ('trace', 'accum', 'taa')}
            reset = (summary['rejected_fraction']['event'] > summary['rejected_fraction']['controls']['max']
                     or summary['sample_count_reset_fraction']['event'] > summary['sample_count_reset_fraction']['controls']['max'])
            if above['trace']:
                verdict = 'TRACE'
            elif above['accum'] and reset:
                verdict = 'ACCUMULATOR'
            elif not above['accum'] and above['taa']:
                verdict = 'TAA ONLY'
            elif not any(above.values()):
                verdict = 'NONE'
            else:
                verdict = 'MIXED'
            rows.append({'event': cap['name'], 'repeat': cap['repeat'], 'roi': roi_name, 'summary': summary,
                         'above_controls': above, 'history_reset_at_event': reset, 'verdict': verdict})
            print(f'  {cap["name"]:<12} r{cap["repeat"]} {roi_name:<12} '
                  f'trace {ev["delta_trace"]:.3e} [{summary["delta_trace"]["controls"]["min"]:.2e}..{summary["delta_trace"]["controls"]["max"]:.2e}]  '
                  f'accum {ev["delta_accum"]:.3e} [{summary["delta_accum"]["controls"]["min"]:.2e}..{summary["delta_accum"]["controls"]["max"]:.2e}]  '
                  f'taa {ev["delta_taa"]:.3e} [{summary["delta_taa"]["controls"]["min"]:.2e}..{summary["delta_taa"]["controls"]["max"]:.2e}]  '
                  f'rejected {ev["rejected_fraction"]:.1%} (ctrl max {summary["rejected_fraction"]["controls"]["max"]:.1%})  '
                  f'count {ev["sample_count_mean"]:.1f} reset {ev["sample_count_reset_fraction"]:.1%} '
                  f'(ctrl max {summary["sample_count_reset_fraction"]["controls"]["max"]:.1%})  -> {verdict}')
    report['rows'] = rows
    report['verdicts'] = sorted({r['verdict'] for r in rows})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                   '46-gdf-sampling-side'))
    args = ap.parse_args()
    print('GDF recentre: trace vs accumulator vs TAA, on the sampling-side captures')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'data': args.data,
              'harness': runner.harness_stamp()}
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
        path = os.path.join(args.data, 'gdf_sampling_side_accum.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
