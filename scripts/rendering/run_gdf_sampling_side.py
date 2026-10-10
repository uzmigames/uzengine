"""followup-13: the sampling-origin side of a recentre, measured per pixel.

H-49 showed a +-1 voxel recentre of clipmap 0 leaves the overlapping field bit-identical. The sampler
has no attenuation — `SampleGdfSurface` is first-containing-cascade selection — and a voxel-aligned
shift keeps the trilinear weights, so at a fixed world point the only sampling quantity a recentre can
change is which cascade serves a point within one voxel of the moved faces. The gather now records, per
pixel, where its rays ended with respect to clipmap 0 (GATHER +29) and clipmap 0 as it saw it (+30),
next to what it already recorded: the resolved world position (+9), the ray origin (+10), the snapped
probe origin (+15) and the raw trace output (+27).

Pre-registered before the captures (followup-13 notes, 2026-09-16):

  events    three clipmap-0 recentres on the boundary trajectory, one per axis, each a single voxel
            with no clipmap-1 recentre on the same frame: frame 238 (x-), 380 (y+), 668 (z+), read
            from the retained field-compare logs of the same deterministic trajectory. Windows of
            +-8 frames, `--region-fields gather`, ROIs `shadow_wall` and `lit_wall`, 2 repeats.
  G0  the log's clipmap-0 recentre frame equals the expected frame, in every run.
  G1  the recentre is visible in the records: at every pixel with gather data, +30's centre changes
      between the frame before and the event frame, and at no other consecutive pair in the window.
  pairing   frame f's pixels are carried to frame f-1 by reprojecting their recorded world position
            (+9) with frame f-1's view-projection; the pair is accepted when the world positions agree
            within 2 cm and the geometric normals (+11) within 0.99. The accepted fraction is reported.
  steps     the EVENT step (f_e vs f_e - 1) and the CONTROL steps (every other consecutive pair in the
            window). The camera moves through the whole window, so the controls carry the same motion
            and the event step differs from them only by the recentre.
  R1  per step: mean |delta| of the raw trace output luminance (+27) over paired pixels; the event
      step's value against the control steps' distribution (min, median, max).
  R2  constancy: the fraction of paired pixels whose snapped probe origin (+15) changed, and whose ray
      origin (+10) changed by more than 0.01 cm, at the event step and at the control steps.
  R3  where the change sits: at the event step, mean |delta output| for pixels whose rays ended within
      one voxel of a clipmap-0 face or changed cascade fraction (|delta +29.x| > 0), against the
      remaining pixels; and the same split at the control steps.

  What this can and cannot say, fixed now: it measures whether the recentre step differs from the
  motion around it in these records, and whether that difference sits where cascade selection can
  change. It cannot attribute the difference to a cause beyond what the records name, and it says
  nothing about any intervention.

Usage: python scripts/rendering/run_gdf_sampling_side.py [--repeats 2] [--out DIR] [--reuse]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
PROJECT = 'sponza-gdf-boundary.uzproject'
ROIS = [('shadow_wall', 150, 300, 128, 128), ('lit_wall', 1100, 300, 128, 128)]
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
EVENTS = {'x_minus_238': 238, 'y_plus_380': 380, 'z_plus_668': 668}
HALF_WINDOW = 8
TOTAL_FRAMES = 37 + 120 + 140 * 6 + 8
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
PAIR_CM = 2.0
PAIR_NORMAL = 0.99
RAY_ORIGIN_CM = 0.01


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def recentre_frames(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    out = []
    for m in re.finditer(r'GI recentre: frame=(\d+)[^\n]*gdfClipmap=(\d+)', text):
        if int(m.group(2)) == 0:
            out.append(int(m.group(1)))
    return out


def load(region_dir, first, last, roi):
    seq = regiondump.load_sequence(region_dir, first, last)
    frames = []
    for f in seq:
        r = f.rect(roi)
        world = r.channel('gather_world_position')[..., :3].astype(np.float64) if 'gather_world_position' in regiondump.CHANNELS else None
        frames.append({'frame': int(f.frame), 'view_proj': np.asarray(f.view_proj, dtype=np.float64),
                       'render': (f.render_width, f.render_height), 'rect': (r.x, r.y, r.w, r.h),
                       'world': _chan(r, 9)[..., :3], 'origin': _chan(r, 10)[..., :3],
                       'resolved': _chan(r, 10)[..., 3] > 0.5, 'normal': _chan(r, 11)[..., :3],
                       'probe_origin': _chan(r, 15)[..., :3], 'trace': _chan(r, 27)[..., :3] @ REC709,
                       'cascade': _chan(r, 29), 'clip0': _chan(r, 30)})
    return frames


def _chan(rect, slot):
    """GATHER record `slot` of this rectangle, by slot number, independent of the channel table."""
    return rect.records()[:, :, rect.frame.offsets[regiondump.FIELD_GATHER] + slot, :].astype(np.float64)


def pair(cur, prev):
    """Indices (yc, xc) -> (yp, xp) of accepted pairs, by reprojecting cur's world positions into prev."""
    h, w = cur['world'].shape[:2]
    P = cur['world'].reshape(-1, 3)
    clip = np.concatenate([P, np.ones((P.shape[0], 1))], axis=1) @ prev['view_proj']
    ok = clip[:, 3] > 1e-6
    ndc = np.full((P.shape[0], 2), np.nan)
    ndc[ok] = clip[ok, :2] / clip[ok, 3:4]
    rw, rh = prev['render']
    px = (ndc[:, 0] * 0.5 + 0.5) * rw - 0.5
    py = (0.5 - ndc[:, 1] * 0.5) * rh - 0.5
    rx, ry, _, _ = prev['rect']
    xi = np.rint(px - rx).astype(np.int64)
    yi = np.rint(py - ry).astype(np.int64)
    inside = ok & (xi >= 0) & (xi < w) & (yi >= 0) & (yi < h)
    yc, xc = np.divmod(np.flatnonzero(inside), w)
    yp, xp = yi[inside], xi[inside]
    close = np.linalg.norm(cur['world'][yc, xc] - prev['world'][yp, xp], axis=1) <= PAIR_CM
    n1, n2 = cur['normal'][yc, xc], prev['normal'][yp, xp]
    dots = np.einsum('ij,ij->i', n1, n2) / np.maximum(np.linalg.norm(n1, axis=1) * np.linalg.norm(n2, axis=1), 1e-9)
    geo = cur['resolved'][yc, xc] & prev['resolved'][yp, xp]
    keep = close & (dots >= PAIR_NORMAL) & geo
    return (yc[keep], xc[keep], yp[keep], xp[keep]), float(keep.sum() / max(1, h * w))


def step_stats(cur, prev):
    (yc, xc, yp, xp), coverage = pair(cur, prev)
    if yc.size == 0:
        return None
    d_out = np.abs(cur['trace'][yc, xc] - prev['trace'][yp, xp])
    d_probe = np.linalg.norm(cur['probe_origin'][yc, xc] - prev['probe_origin'][yp, xp], axis=1)
    d_origin = np.linalg.norm(cur['origin'][yc, xc] - prev['origin'][yp, xp], axis=1)
    c_cur, c_prev = cur['cascade'][yc, xc], prev['cascade'][yp, xp]
    near_face = (np.minimum(c_cur[:, 2], c_prev[:, 2]) < 1.0) | (np.abs(c_cur[:, 0] - c_prev[:, 0]) > 0.0)
    clip_changed = np.any(np.abs(cur['clip0'][yc, xc] - prev['clip0'][yp, xp]) > 1e-3, axis=1)
    return {'pairs': int(yc.size), 'coverage': coverage, 'mean_abs_delta_out': float(d_out.mean()),
            'probe_origin_changed_fraction': float(np.mean(d_probe > 1e-3)),
            'ray_origin_changed_fraction': float(np.mean(d_origin > RAY_ORIGIN_CM)),
            'clip0_changed_fraction': float(clip_changed.mean()),
            'near_face_fraction': float(near_face.mean()),
            'delta_out_near_face': float(d_out[near_face].mean()) if near_face.any() else None,
            'delta_out_elsewhere': float(d_out[~near_face].mean()) if (~near_face).any() else None}


def run(args, report):
    runs = []
    for k in range(args.repeats):
        for name, ev in EVENTS.items():
            work = os.path.join(args.out, f'{name}_r{k}')
            first, last = ev - HALF_WINDOW, ev + HALF_WINDOW
            if not args.reuse:
                print(f'  ... {name} repeat {k} (frames {first}..{last})', flush=True)
                res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', PROJECT), TOTAL_FRAMES,
                                         regions=ROIS, region_fields='gather', region_first=first,
                                         region_last=last, cvars=BASE, timing_first=first, clean=True)
                if res['exit_code'] != 0:
                    raise Stop('capture', f'{name} r{k}: exit {res["exit_code"]}')
            runs.append({'name': name, 'event': ev, 'repeat': k, 'dir': work, 'first': first, 'last': last})
    if not args.reuse:
        runner.require_exe()
    report['captures'] = [{**r, 'identity': runner.capture_identity(r['dir'])} for r in runs]

    out = []
    for r in runs:
        frames_log = recentre_frames(os.path.join(r['dir'], 'run.log'))
        # ---- G0 ------------------------------------------------------------------------------
        if r['event'] not in frames_log:
            raise Stop('G0', f'{r["name"]} r{r["repeat"]}: no clipmap-0 recentre at frame {r["event"]} '
                             f'(log has {frames_log[:12]}...)')
        region = runner.require_region_dir(os.path.join(r['dir'], 'region'), r['name'])
        for roi_name, *_ in ROIS:
            frames = load(region, r['first'], r['last'], roi_name)
            by_frame = {f['frame']: f for f in frames}
            # ---- G1: clip0 changes exactly at the event ---------------------------------------
            changes = []
            for f in range(r['first'] + 1, r['last'] + 1):
                a, b = by_frame.get(f), by_frame.get(f - 1)
                if a is None or b is None:
                    continue
                changed = float(np.mean(np.any(np.abs(a['clip0'] - b['clip0']) > 1e-3, axis=-1)))
                changes.append((f, changed))
            at_event = [c for f, c in changes if f == r['event']]
            elsewhere = [c for f, c in changes if f != r['event']]
            g1 = bool(at_event and at_event[0] > 0.99 and (not elsewhere or max(elsewhere) == 0.0))
            if not g1:
                raise Stop('G1', f'{r["name"]} r{r["repeat"]} {roi_name}: clip0 changed {at_event} at the event '
                                 f'and up to {max(elsewhere) if elsewhere else 0} elsewhere')
            # ---- steps ----------------------------------------------------------------------
            steps = {}
            for f in range(r['first'] + 1, r['last'] + 1):
                a, b = by_frame.get(f), by_frame.get(f - 1)
                if a is None or b is None:
                    continue
                st = step_stats(a, b)
                if st is not None:
                    steps[f] = st
            if r['event'] not in steps:
                raise Stop('pairing', f'{r["name"]} r{r["repeat"]} {roi_name}: no paired pixels at the event step')
            ev = steps[r['event']]
            ctrl = [s for f, s in steps.items() if f != r['event']]
            ctrl_out = [s['mean_abs_delta_out'] for s in ctrl]
            row = {'event': r['name'], 'repeat': r['repeat'], 'roi': roi_name,
                   'event_step': ev,
                   'control_steps': {'n': len(ctrl), 'mean_abs_delta_out': {'min': float(min(ctrl_out)),
                                                                            'median': float(np.median(ctrl_out)),
                                                                            'max': float(max(ctrl_out))},
                                     'probe_origin_changed_fraction_max': float(max(s['probe_origin_changed_fraction'] for s in ctrl)),
                                     'ray_origin_changed_fraction_max': float(max(s['ray_origin_changed_fraction'] for s in ctrl)),
                                     'near_face_fraction_median': float(np.median([s['near_face_fraction'] for s in ctrl])),
                                     'delta_out_near_face_median': float(np.median([s['delta_out_near_face'] for s in ctrl if s['delta_out_near_face'] is not None] or [float('nan')])),
                                     'delta_out_elsewhere_median': float(np.median([s['delta_out_elsewhere'] for s in ctrl if s['delta_out_elsewhere'] is not None] or [float('nan')]))},
                   'all_steps': steps}
            out.append(row)
            print(f'  {r["name"]:<12} r{r["repeat"]} {roi_name:<12} pairs {ev["pairs"]:>5} ({ev["coverage"]:.0%})  '
                  f'|d out| event {ev["mean_abs_delta_out"]:.4e} vs controls '
                  f'{row["control_steps"]["mean_abs_delta_out"]["min"]:.2e}..{row["control_steps"]["mean_abs_delta_out"]["max"]:.2e}  '
                  f'probe-origin moved {ev["probe_origin_changed_fraction"]:.1%} (ctrl max {row["control_steps"]["probe_origin_changed_fraction_max"]:.1%})  '
                  f'ray-origin moved {ev["ray_origin_changed_fraction"]:.1%}  near-face {ev["near_face_fraction"]:.1%}: '
                  f'|d out| near {ev["delta_out_near_face"]}, elsewhere {ev["delta_out_elsewhere"]}')
    report['results'] = out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '46-gdf-sampling-side'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'GDF recentre, the sampling side — events {EVENTS}, windows +-{HALF_WINDOW}, {args.repeats} repeats')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'events': EVENTS, 'rois': ROIS,
              'harness': runner.harness_stamp(),
              'criteria': {'pair_cm': PAIR_CM, 'pair_normal': PAIR_NORMAL, 'ray_origin_cm': RAY_ORIGIN_CM}}
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
        path = os.path.join(args.out, 'gdf_sampling_side.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
