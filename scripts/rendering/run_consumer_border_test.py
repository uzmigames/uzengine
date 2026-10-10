"""followup-01, consumer stage: post-hoc test of the unwritten-border candidate on the known-radiance
captures (52-consumer-known-radiance). Prediction pre-registered in the followup-01 notes, 2026-09-18,
before this ran.

The DDGI atlas tiles carry a 1-texel border the writer never fills and nothing clears. The consumer
samples the 6x6 tile bilinearly at `origin + oct * 6`, so for octahedral coordinates near 0 or 1 part
of the bilinear weight lands on a border texel. If that texel holds 0, the sampled irradiance is the
interior value times the weight that stayed inside:

    predicted(pixel) = (1 - bx) * (1 - by)

with bx, by the border weights per axis from the pixel's normal through the consumer's own encode.

  G   the recorded normal is unit length on >= 99 % of geometry pixels (else the channel is not N).
  P1  median of measured / predicted within 1 +- 0.05 on every ROI.
  P2  fraction of pixels with |measured - predicted| <= 0.05: >= 90 % on every ROI.
  Reading: BORDER if P1 and P2 hold on every ROI; NOT BORDER otherwise (reported with the numbers).

Usage: python scripts/rendering/run_consumer_border_test.py [--data DIR]
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
import run_consumer_known_radiance as stage  # noqa: E402

TILE = 6
BAND = 0.05
FRACTION = 0.90
UNIT_TOL = 1e-2


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def oct_encode(n):
    """Mirrors DDGIOctEncode in deferred_lighting_ps.hlsl, vectorised over (..., 3)."""
    s = np.abs(n[..., 0]) + np.abs(n[..., 1]) + np.abs(n[..., 2])
    n = n / np.maximum(s, 1e-12)[..., None]
    e = n[..., :2].copy()
    neg = n[..., 2] < 0.0
    sx = np.where(e[..., 0] >= 0.0, 1.0, -1.0)
    sy = np.where(e[..., 1] >= 0.0, 1.0, -1.0)
    wrapped = (1.0 - np.abs(e[..., ::-1])) * np.stack([sx, sy], axis=-1)
    e = np.where(neg[..., None], wrapped, e)
    return e * 0.5 + 0.5


def border_weight(oct1d):
    """Bilinear weight falling on a border texel along one axis, for the consumer's sampling position
    p = 1 + oct * TILE in texel units (texel i covers [i, i+1), centre i + 0.5; texels 0 and TILE+1 are
    the border)."""
    p = 1.0 + oct1d * TILE
    q = p - 0.5
    t0 = np.floor(q)
    f = q - t0
    on_low = t0 <= 0.0            # tap t0 is texel 0 (the border)
    on_high = (t0 + 1.0) >= TILE + 1.0  # tap t0+1 is texel TILE+1 (the border)
    return np.where(on_low, 1.0 - f, np.where(on_high, f, 0.0))


def load(region_dir):
    seq = regiondump.load_sequence(region_dir, stage.FIRST, stage.LAST)
    out = {}
    for name, *_ in stage.ROIS:
        lum, nrm, geo = [], [], []
        for fr in seq:
            r = fr.rect(name)
            lum.append(r.channel('diffuse_indirect')[..., :3].astype(np.float64) @ stage.REC709)
            nrm.append(r.channel('lighting_normal')[..., :3].astype(np.float64))
            geo.append(r.channel('lighting_normal')[..., 3].astype(np.float64) >= 0.0)
        out[name] = {'lum': np.stack(lum), 'n': np.stack(nrm), 'geo': np.stack(geo)}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                   '52-consumer-known-radiance'))
    args = ap.parse_args()
    print('unwritten-border prediction against the known-radiance captures')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'data': args.data,
              'criteria': {'band': BAND, 'fraction': FRACTION, 'unit_tol': UNIT_TOL, 'tile': TILE}, 'rows': []}
    code = 1
    try:
        src = json.load(open(os.path.join(args.data, 'consumer_known_radiance.json'), encoding='utf-8'))
        if src.get('status') != 'complete':
            raise Stop('input', 'the known-radiance study is not complete')
        verdict_ok = True
        for k in range(2):
            rc = load(runner.require_region_dir(os.path.join(args.data, f'RC_r{k}', 'region'), f'RC r{k}'))
            dd = load(runner.require_region_dir(os.path.join(args.data, f'DDGI_r{k}', 'region'), f'DDGI r{k}'))
            for name, *_ in stage.ROIS:
                a, b = dd[name], rc[name]
                mask = a['geo'] & b['geo'] & (b['lum'] > stage.MIN_DENOM)
                n = a['n'][mask]
                length = np.linalg.norm(n, axis=-1)
                unit = np.mean(np.abs(length - 1.0) <= UNIT_TOL)
                if unit < 0.99:
                    raise Stop('G', f'{name} r{k}: the recorded normal is unit length on only {unit:.1%} of pixels')
                oct = oct_encode(n)
                bx, by = border_weight(oct[:, 0]), border_weight(oct[:, 1])
                predicted = (1.0 - bx) * (1.0 - by)
                measured = a['lum'][mask] / b['lum'][mask]
                q = measured / np.maximum(predicted, 1e-6)
                within = np.mean(np.abs(measured - predicted) <= BAND)
                row = {'roi': name, 'repeat': k, 'pixels': int(mask.sum()), 'unit_normal_fraction': float(unit),
                       'measured_median': float(np.median(measured)), 'predicted_median': float(np.median(predicted)),
                       'ratio_measured_over_predicted': {'median': float(np.median(q)), 'p10': float(np.percentile(q, 10)),
                                                         'p90': float(np.percentile(q, 90))},
                       'within_band_fraction': float(within),
                       'border_touching_fraction': float(np.mean((bx > 0) | (by > 0))),
                       'P1': bool(abs(np.median(q) - 1.0) <= BAND), 'P2': bool(within >= FRACTION)}
                report['rows'].append(row)
                verdict_ok &= row['P1'] and row['P2']
                print(f'  {name:<12} r{k}: measured median {row["measured_median"]:.4f}  predicted median '
                      f'{row["predicted_median"]:.4f}  measured/predicted median {row["ratio_measured_over_predicted"]["median"]:.4f} '
                      f'(p10 {row["ratio_measured_over_predicted"]["p10"]:.4f}, p90 {row["ratio_measured_over_predicted"]["p90"]:.4f})  '
                      f'within +-{BAND}: {within:.1%}  touching border: {row["border_touching_fraction"]:.1%}  '
                      f'P1 {row["P1"]} P2 {row["P2"]}')
        report['verdict'] = 'BORDER' if verdict_ok else 'NOT BORDER'
        print(f'\n  -> {report["verdict"]}')
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
        path = os.path.join(args.data, 'consumer_border_test.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
