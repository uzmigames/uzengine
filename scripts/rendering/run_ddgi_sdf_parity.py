"""followup-01: the DXR (DDGI) and SDF probe paths compared in HDR-linear radiance, at the consumer.

The record's open item says the 1.21 ratio between the two paths is display-referred (PNG luminance)
and that no HDR-linear or per-probe measurement exists. This is the HDR-linear one, taken where the
deferred pass consumes the probes, from the region dump's `diffuse_indirect` record.

Pre-registered before the captures (followup-01 notes, 2026-09-16):

  pose      Camera C (`sponza-lighting-camera-c.uzproject`) — the QA view the record used, because
            the default pose runs the final gather, which replaces the probe irradiance for both
            paths and hides the difference. `r.GI.FinalGather=0` throughout for the same reason.
  regions   a fixed rectangle, identical in every condition.
  conditions, one binary, interleaved, N repeats each, every dump retained:
    sdf               r.RayTracing=0, r.RadianceCache=1 — the SDF radiance cache feeds the consumer
    ddgi_flat         r.RayTracing=1, r.RadianceCache=1, r.GI.DDGI.DirectionalSky=0 — DDGI takes
                      priority in the consumer; escaping rays keep the flat sky colour (the
                      behaviour before followup-01's change)
    ddgi_directional  as above with DirectionalSky=1 (the default): escaping rays sample the sky
                      cubemap in the ray direction, as the SDF writer does

  metric    mean HDR-linear luminance of `diffuse_indirect` over the rectangle and the sequence.

  C1  control that must NOT move: the recorded VIEW DISTANCE (`lighting_normal`.w, centimetres, -1
      where there is no geometry). It is geometry, so no probe path can change it, and it varies
      across the rectangle, so it can fail if the conditions differ in pose, scene or frames. It must
      agree across the three conditions within 1 % (candidate criterion C5).
      The first version used `direct_light`, which is identically ZERO over the whole view at this
      pose (probed: max 0.0 over 1280x720, 0 % non-zero), so it could not have failed. A control that
      cannot move is not a control; the study now reports such a control UNUSABLE instead of passing
      on it. `direct_light` is still reported, as context.
      Note what this control does and does not show: it establishes the same view of the same
      geometry. At a pose with no direct light there is no non-probe LIGHTING term to hold fixed.
  C2  the switch acted: `diffuse_indirect` must differ between `sdf` and at least one DDGI condition
      by more than the repeat spread; otherwise the conditions are not exercising different paths and
      no ratio is reported.
  P1  parity, the task's own +-10 % criterion: ratio = ddgi_directional / sdf.
  P2  what the sky change did: ddgi_directional / ddgi_flat, with the same +-10 % band reported for
      scale. This isolates the change on ONE binary, which is why the CVar exists.

  Reported, not judged: the same ratios for the whole rectangle's `diffuse_indirect` energy and the
  per-repeat spread. A ratio is not a causal share, and this measures the consumer, not the probes:
  a per-probe comparison is still missing and stays open.

Usage: python scripts/rendering/run_ddgi_sdf_parity.py [--frames 32] [--repeats 2] [--out DIR]
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import sys
import traceback

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
PROJECT = 'sponza-lighting-camera-c.uzproject'
#: Moved from (640, 360) after a probe of the whole view (39-camera-c-probe): the indirect light is
#: strongest here (9.3e-04 against 4.5e-04), which is the quantity being compared.
ROI = ('camera_c', 320, 180, 256, 256)
NAME = ROI[0]
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
#: r.TAA is ON only so the region dump is readable: the TAA resolve writes the stamp the reader
#: validates every frame against, and this project leaves TAA off, which invalidated all 32 frames
#: of the first attempt. It does not touch what is measured - `diffuse_indirect` and `direct_light`
#: are written by the deferred pass BEFORE the resolve - and it is identical in all three
#: conditions, which is what a ratio between them requires. No preset offers LIGHTING_EXTRA
#: without TAA (uzengine.cpp ParseRegionFields), so the records cannot be captured another way.
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.GI.FinalGather': 0, 'r.RadianceCache': 1,
        'r.TAA': 1}
#: The DDGI writer runs only when the r.RayTracing master switch AND r.RayTracing.GI are on and the
#: TLAS holds instances (frame_gi_passes.cpp). With either off the atlases are never written, the
#: consumer never binds them, and the deferred pass stays on the SDF radiance cache — which is what
#: the `sdf` condition wants, and what the DDGI conditions must avoid.
CONDITIONS = {
    'sdf':              {'r.RayTracing': 0, 'r.RayTracing.GI': 0},
    'ddgi_flat':        {'r.RayTracing': 1, 'r.RayTracing.GI': 1, 'r.GI.DDGI.DirectionalSky': 0},
    'ddgi_directional': {'r.RayTracing': 1, 'r.RayTracing.GI': 1, 'r.GI.DDGI.DirectionalSky': 1},
}
CONTROL_TOLERANCE = 0.01   # C5, applied to the direct-light control
CONTROL_FLOOR = 1e-9       # below this the control carries no signal and cannot fail
PARITY_BAND = 0.10         # the task's +-10 % criterion


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def load(region_dir, last):
    seq = regiondump.load_sequence(region_dir, FIRST, last)
    out = {}
    for channel in ('diffuse_indirect', 'direct_light'):
        stack = np.stack([f.rect(NAME).channel(channel)[..., :3].astype(np.float64) @ REC709
                          for f in seq])
        if not np.all(np.isfinite(stack)):
            raise Stop('finite', f'{channel} holds non-finite values in {region_dir}')
        out[channel] = float(stack.mean())
    # The control: view distance in cm, -1 where the pixel has no geometry.
    dist = np.stack([f.rect(NAME).channel('lighting_normal')[..., 3].astype(np.float64) for f in seq])
    if not np.all(np.isfinite(dist)):
        raise Stop('finite', f'lighting_normal holds non-finite values in {region_dir}')
    valid = dist >= 0.0
    out['view_distance'] = float(dist[valid].mean()) if valid.any() else 0.0
    out['view_distance_valid_fraction'] = float(valid.mean())
    out['frames'] = [int(f.frame) for f in seq]
    return out


def spread(values):
    return max(abs(a - b) for a, b in itertools.combinations(values, 2)) if len(values) > 1 else 0.0


def run(args, report):
    runner.require_exe()
    last = FIRST + args.frames - 1
    works = {c: [] for c in CONDITIONS}
    for k in range(args.repeats):
        for cond, cvars in CONDITIONS.items():
            work = os.path.join(args.out, f'{cond}_r{k}')
            print(f'  ... {cond} repeat {k}', flush=True)
            res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', PROJECT),
                                     last - SCENE_START + 8, regions=[ROI], region_fields='components',
                                     region_first=FIRST, region_last=last, cvars={**BASE, **cvars},
                                     timing_first=FIRST, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{cond} repeat {k}: exit {res["exit_code"]}')
            works[cond].append(work)

    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w)} for w in ws]
                          for c, ws in works.items()}
    hashes = {str(e['identity'].get('executable', {}).get('hash'))
              for ws in report['captures'].values() for e in ws}
    if len(hashes) != 1:
        raise Stop('identity', f'the captures do not share one binary: {sorted(hashes)}')

    print('  loading captures', flush=True)
    data = {c: [load(os.path.join(w, 'region'), last) for w in ws] for c, ws in works.items()}
    channels = ('diffuse_indirect', 'direct_light', 'view_distance', 'view_distance_valid_fraction')
    means = {c: {ch: float(np.mean([r[ch] for r in runs])) for ch in channels}
             for c, runs in data.items()}
    spreads = {c: {ch: spread([r[ch] for r in runs]) for ch in channels}
               for c, runs in data.items()}
    report['means'] = means
    report['repeat_spreads'] = spreads
    print('\n  HDR-linear luminance, mean over the rectangle and the sequence:')
    for c in CONDITIONS:
        print(f'    {c:<18} diffuse_indirect {means[c]["diffuse_indirect"]:.6e}   '
              f'direct_light {means[c]["direct_light"]:.6e}   '
              f'view distance {means[c]["view_distance"]:.4f} cm '
              f'({means[c]["view_distance_valid_fraction"]:.1%} of pixels have geometry)')

    # ---- C1: the control that must not move -------------------------------------------------
    # A control that is identically zero cannot fail, and a gate that cannot fail is not a control.
    # On the first run of this study the rectangle held no direct light at all (0.0 in all three
    # conditions), so C1 would have "passed" on nothing. It is reported as UNUSABLE instead.
    direct = [means[c]['view_distance'] for c in CONDITIONS]
    scale = max(abs(v) for v in direct)
    if scale <= CONTROL_FLOOR:
        report['C1'] = {'values': direct, 'scale': scale, 'floor': CONTROL_FLOOR, 'usable': False,
                        'pass': None}
        print(f'  C1 UNUSABLE — the control is {scale:.2e} over this rectangle, at or below the '
              f'floor {CONTROL_FLOOR:.0e}: a control that cannot move cannot fail')
        raise Stop('C1', 'the control is degenerate over this rectangle, so it cannot detect a '
                         'condition difference; no ratio is reported')
    worst = max(abs(a - b) / scale for a, b in itertools.combinations(direct, 2))
    report['C1'] = {'values': direct, 'scale': scale, 'worst_relative_difference': worst,
                    'tolerance': CONTROL_TOLERANCE, 'usable': True,
                    'pass': bool(worst <= CONTROL_TOLERANCE)}
    print(f'  C1 view distance across conditions: worst relative {worst:.2e} '
          f'(tolerance {CONTROL_TOLERANCE:.0%}) — {"ok" if report["C1"]["pass"] else "FAILS"}')
    if not report['C1']['pass']:
        raise Stop('C1', 'the view distance moved between conditions, so they are not the same view of '
                         'the same geometry; no ratio is reported')

    # ---- C2: the switch acted ----------------------------------------------------------------
    sdf_mean = means['sdf']['diffuse_indirect']
    biggest = max(abs(means[c]['diffuse_indirect'] - sdf_mean) for c in ('ddgi_flat', 'ddgi_directional'))
    worst_spread = max(s['diffuse_indirect'] for s in spreads.values())
    report['C2'] = {'largest_difference_from_sdf': biggest, 'worst_repeat_spread': worst_spread,
                    'pass': bool(biggest > worst_spread)}
    print(f'  C2 switch acted: largest difference from sdf {biggest:.3e} against a repeat spread of '
          f'{worst_spread:.3e} — {"ok" if report["C2"]["pass"] else "FAILS"}')
    if not report['C2']['pass']:
        raise Stop('C2', 'the conditions do not differ by more than their own repeat spread; the probe '
                         'path may not have switched')

    # ---- P1 / P2 ------------------------------------------------------------------------------
    ratio_parity = means['ddgi_directional']['diffuse_indirect'] / sdf_mean
    ratio_sky = means['ddgi_directional']['diffuse_indirect'] / means['ddgi_flat']['diffuse_indirect']
    report['P1_parity_ddgi_over_sdf'] = {'ratio': ratio_parity, 'band': PARITY_BAND,
                                         'within_band': bool(abs(ratio_parity - 1.0) <= PARITY_BAND)}
    report['P2_directional_over_flat'] = {'ratio': ratio_sky, 'band': PARITY_BAND,
                                          'within_band': bool(abs(ratio_sky - 1.0) <= PARITY_BAND)}
    print(f'\n  P1 parity, ddgi_directional / sdf = {ratio_parity:.4f} '
          f'({"within" if report["P1_parity_ddgi_over_sdf"]["within_band"] else "outside"} +-{PARITY_BAND:.0%})')
    print(f'  P2 the sky change, ddgi_directional / ddgi_flat = {ratio_sky:.4f} '
          f'({"within" if report["P2_directional_over_flat"]["within_band"] else "outside"} +-{PARITY_BAND:.0%})')
    print('  (measured at the consumer, over one rectangle; a per-probe comparison is still missing)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=32)
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '38-ddgi-sdf-parity'))
    args = ap.parse_args()
    print(f'DDGI vs SDF probe parity in HDR-linear — Camera C, frames {FIRST}..{FIRST + args.frames - 1}, '
          f'{args.repeats} repeats per condition')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'roi': ROI,
              'frames': [FIRST, FIRST + args.frames - 1], 'conditions': CONDITIONS,
              'harness': runner.harness_stamp(),
              'criteria': {'control_tolerance': CONTROL_TOLERANCE, 'parity_band': PARITY_BAND}}
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
        path = os.path.join(args.out, 'ddgi_sdf_parity.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
