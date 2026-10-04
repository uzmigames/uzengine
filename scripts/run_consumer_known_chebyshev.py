"""followup-01, review step 4: the consumer's Chebyshev visibility under KNOWN distances and a
per-probe known radiance, against an offline model of the consumer's own query.

Pre-registered in the followup-01 notes, 2026-09-18, before this ran. Conditions (DDGI path):
  FAR   KeepDepth 0, KnownDistance 0     visibility 1 everywhere (reference)
  D300  KnownDistance 300 cm, mode 0     some of a pixel's 8 probes fail the test, some pass
  D400  KnownDistance 400 cm, mode 0     a second visibility situation
  DDIR  KnownDistance 350 cm, mode 1     D (0.75 + 0.25 dot(dir, d0)): moments vary across each tile

  K0  identity lines echo the values; per probe outside geometry the writers' means are L on odd
      cells and L/4 on even cells (+-1e-3), both writers.
  K4  constant D: per pixel R_m = diffuseIndirect(D)/diffuseIndirect(FAR) against the model's R_p;
      excluded: |dist - D| < 1 cm at any corner, or no passing probe (the consumer falls to the cache).
      Band: median R_m/R_p within 1 +- 0.05 per ROI on D300 and D400, >= 90 % within +-0.05.
  K5  directional D: moments as the writer's cos^2-weighted hemisphere expectations; band 1 +- 0.10,
      >= 80 % within +-0.10.
  C   repeats agree on the K4/K5 medians to 1e-3.
  Reading: CHEBYSHEV OK/DIFFERS (K4), DEPTH BORDER OK/DIFFERS (K5), MIXED.

Usage: python scripts/run_consumer_known_chebyshev.py [--repeats 2] [--out DIR] [--reuse]
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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
import run_probe_sky_parity as stage1  # noqa: E402
import run_probe_hit_identity as stage3  # noqa: E402
import run_consumer_known_radiance as uniform  # noqa: E402

KNOWN = 1.0
PARITY_LOW = 0.25
PROBES = 16
BASE = {**uniform.BASE, 'r.GI.Diagnostic.KnownRadianceMode': 2, 'r.RayTracing': 1, 'r.RayTracing.GI': 1}
CONDITIONS = {
    'FAR':  {'r.GI.Diagnostic.KnownRadianceKeepDepth': 0, 'r.GI.Diagnostic.KnownDistance': 0,   'r.GI.Diagnostic.KnownDistanceMode': 0},
    'D300': {'r.GI.Diagnostic.KnownRadianceKeepDepth': 0, 'r.GI.Diagnostic.KnownDistance': 300, 'r.GI.Diagnostic.KnownDistanceMode': 0},
    'D400': {'r.GI.Diagnostic.KnownRadianceKeepDepth': 0, 'r.GI.Diagnostic.KnownDistance': 400, 'r.GI.Diagnostic.KnownDistanceMode': 0},
    'DDIR': {'r.GI.Diagnostic.KnownRadianceKeepDepth': 0, 'r.GI.Diagnostic.KnownDistance': 350, 'r.GI.Diagnostic.KnownDistanceMode': 1},
}
ROIS = uniform.ROIS
K0_TOL = 1e-3
BAND_K4, FRAC_K4 = 0.05, 0.90
BAND_K5, FRAC_K5 = 0.10, 0.80
REPEAT_TOL = 1e-3
BOUNDARY_CM = 1.0
VAR_FLOOR = 1e-4
D0 = np.array([0.30305, 0.50508, 0.80812]); D0 /= np.linalg.norm(D0)
NUM = stage1.NUM
RE_DDGI_ID = re.compile(r'DDGI transport: identity [^\n]* knownRadiance=' + NUM + r' keepDepth=(\d) knownMode=(\d) '
                        r'knownDistance=' + NUM + r' knownDistanceMode=(\d)')
RE_RC_ID = re.compile(r'RC transport: identity [^\n]* knownRadiance=' + NUM + r' knownMode=(\d)')


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def cell_of(index):
    return np.array([index % PROBES, (index // PROBES) % PROBES, index // (PROBES * PROBES)])


def parity_L(ix, iy, iz):
    return np.where(((ix + iy + iz) & 1) != 0, KNOWN, PARITY_LOW * KNOWN)


def parse_log(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    dd, rc = RE_DDGI_ID.search(text), RE_RC_ID.search(text)
    q = stage1.RE_QUEUE_D.search(text)
    out = {'ddgi': None if not dd else {'known': float(dd.group(1)), 'keep': int(dd.group(2)), 'mode': int(dd.group(3)),
                                        'distance': float(dd.group(4)), 'distance_mode': int(dd.group(5))},
           'rc': None if not rc else {'known': float(rc.group(1)), 'mode': int(rc.group(2))},
           'grid': None if not q else {'origin': np.array([float(q.group(k)) for k in (2, 3, 4)]), 'spacing': float(q.group(5))}}
    r = np.full((stage1.TOTAL, 10), np.nan)
    for m in stage3.RE_RC.finditer(text):
        g = m.groups()
        r[int(g[0])] = [float(g[k]) for k in (4, 5, 8, 9, 10, 11, 12, 13, 14, 15)]
    d = np.full((stage1.TOTAL, 7), np.nan)
    for m in stage3.RE_DDGI.finditer(text):
        g = m.groups()
        d[int(g[0])] = [float(g[k]) for k in (7, 8, 9, 10, 11, 12, 13)]
    ok = np.isfinite(r).all(1) & np.isfinite(d).all(1) & (r[:, 0] >= 5.0)
    idx = np.arange(stage1.TOTAL)
    expect = parity_L(idx % PROBES, (idx // PROBES) % PROBES, idx // (PROBES * PROBES))
    out['rc_parity_err'] = float(np.abs(r[ok, 3] - expect[ok]).max()) if ok.any() else None   # skyLum per probe (all rays sky)
    out['ddgi_parity_err'] = float(np.abs(d[ok, 1] - expect[ok]).max()) if ok.any() else None  # mean radiance
    out['probes'] = int(ok.sum())
    return out


# ── the writer's cos^2-weighted hemisphere expectations of the directional distance ─────────────
_MU = np.linspace(-1.0, 1.0, 201)
_E1 = None
_E2 = None


def _hemisphere_tables():
    """E[D] and E[D^2] as functions of mu = dot(d, d0), for D = D0 (0.75 + 0.25 dot(w, d0)) with weight
    cos^2 over the hemisphere about d (the writer's `dw = w*w`)."""
    global _E1, _E2
    if _E1 is not None:
        return
    rng = np.random.default_rng(3)
    w = rng.normal(size=(400000, 3)); w /= np.linalg.norm(w, axis=1, keepdims=True)
    e1, e2 = [], []
    for mu in _MU:
        # frame: d = (0,0,1); d0 in the x-z plane with dot(d, d0) = mu
        d = np.array([0.0, 0.0, 1.0]); d0 = np.array([np.sqrt(max(0.0, 1 - mu * mu)), 0.0, mu])
        c = w @ d; keep = c > 0
        wt = c[keep] ** 2
        u = w[keep] @ d0
        f = 0.75 + 0.25 * u
        e1.append((wt * f).sum() / wt.sum()); e2.append((wt * f * f).sum() / wt.sum())
    _E1, _E2 = np.array(e1), np.array(e2)


def predict_ratio(world, grid, D, dmode):
    """R_p per pixel: [sum w vis L / sum w vis] / [sum w L / sum w], plus exclusion masks."""
    origin, spacing = grid['origin'], grid['spacing']
    cell = (world - origin) / spacing
    i0 = np.clip(np.floor(cell).astype(int), 0, PROBES - 1)
    i1 = np.minimum(i0 + 1, PROBES - 1)
    frac = np.clip(cell - i0, 0.0, 1.0)
    num_v = np.zeros(world.shape[:-1]); den_v = np.zeros_like(num_v)
    num_f = np.zeros_like(num_v); den_f = np.zeros_like(num_v)
    near_boundary = np.zeros(world.shape[:-1], dtype=bool)
    if dmode == 1:
        _hemisphere_tables()
    for corner in range(8):
        ix = np.where(corner & 1, i1[..., 0], i0[..., 0]); iy = np.where(corner & 2, i1[..., 1], i0[..., 1])
        iz = np.where(corner & 4, i1[..., 2], i0[..., 2])
        wtri = (np.where(corner & 1, frac[..., 0], 1 - frac[..., 0]) * np.where(corner & 2, frac[..., 1], 1 - frac[..., 1])
                * np.where(corner & 4, frac[..., 2], 1 - frac[..., 2]))
        L = parity_L(ix, iy, iz)
        probe = origin + np.stack([ix, iy, iz], -1) * spacing
        to_probe = probe - world
        dist = np.linalg.norm(to_probe, axis=-1)
        if dmode == 0:
            meanD = np.full_like(dist, D); var = np.full_like(dist, VAR_FLOOR)
        else:
            d = -to_probe / np.maximum(dist, 1e-6)[..., None]        # probe -> surface, the consumer's sampling direction
            mu = d @ D0
            e1 = np.interp(mu, _MU, _E1); e2 = np.interp(mu, _MU, _E2)
            meanD = D * e1; var = np.maximum(D * D * e2 - meanD * meanD, VAR_FLOOR)
        diff = dist - meanD
        vis = np.where(diff > 0, (var / (var + diff * diff)) ** 3, 1.0)
        near_boundary |= np.abs(diff) < BOUNDARY_CM
        num_v += wtri * vis * L; den_v += wtri * vis
        num_f += wtri * L; den_f += wtri
    no_probe = den_v <= 1e-6
    with np.errstate(divide='ignore', invalid='ignore'):
        rp = (num_v / den_v) / (num_f / den_f)
    return rp, near_boundary, no_probe


def load(region_dir):
    seq = regiondump.load_sequence(region_dir, uniform.FIRST, uniform.LAST)
    out = {}
    for name, *_ in ROIS:
        lum, geo, world = [], [], []
        for fr in seq:
            r = fr.rect(name)
            lum.append(r.channel('diffuse_indirect')[..., :3].astype(np.float64) @ uniform.REC709)
            geo.append(r.channel('lighting_normal')[..., 3].astype(np.float64) >= 0.0)
            world.append(np.asarray(fr.world_positions(name), dtype=np.float64))
        out[name] = {'lum': np.stack(lum), 'geo': np.stack(geo), 'world': np.stack(world)}
    return out


def run(args, report):
    works = {c: [] for c in CONDITIONS}
    for k in range(args.repeats):
        for cond, extra in CONDITIONS.items():
            work = os.path.join(args.out, f'{cond}_r{k}')
            works[cond].append(work)
            if args.reuse:
                continue
            print(f'  ... {cond} repeat {k}', flush=True)
            res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', uniform.PROJECT), uniform.FRAMES,
                                     regions=ROIS, region_fields='components', region_first=uniform.FIRST,
                                     region_last=uniform.LAST, cvars={**BASE, **extra}, timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{cond} repeat {k}: exit {res["exit_code"]}')
    if not args.reuse:
        runner.require_exe()
    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w)} for w in ws] for c, ws in works.items()}

    # ---- K0 --------------------------------------------------------------------------------
    logs = {}
    for cond, ws in works.items():
        logs[cond] = []
        for k, w in enumerate(ws):
            p = parse_log(os.path.join(w, 'run.log'))
            logs[cond].append(p)
            want = CONDITIONS[cond]
            if p['ddgi'] is None or p['rc'] is None or p['grid'] is None:
                raise Stop('K0', f'{cond} r{k}: identity or grid line missing (ddgi {p["ddgi"] is not None}, rc {p["rc"] is not None}, grid {p["grid"] is not None})')
            if p['ddgi']['known'] != KNOWN or p['ddgi']['mode'] != 2 or p['rc']['known'] != KNOWN or p['rc']['mode'] != 2:
                raise Stop('K0', f'{cond} r{k}: known radiance/mode not as asked: ddgi {p["ddgi"]}, rc {p["rc"]}')
            if p['ddgi']['distance'] != float(want['r.GI.Diagnostic.KnownDistance']) or p['ddgi']['distance_mode'] != want['r.GI.Diagnostic.KnownDistanceMode'] \
                    or p['ddgi']['keep'] != want['r.GI.Diagnostic.KnownRadianceKeepDepth']:
                raise Stop('K0', f'{cond} r{k}: DDGI distance/mode/keep not as asked: {p["ddgi"]}')
            if p['probes'] == 0 or p['rc_parity_err'] > K0_TOL or p['ddgi_parity_err'] > K0_TOL:
                raise Stop('K0', f'{cond} r{k}: parity field not stored (rc err {p["rc_parity_err"]}, ddgi err {p["ddgi_parity_err"]}, probes {p["probes"]})')
            print(f'  K0 {cond} r{k}: parity stored on {p["probes"]} probes (rc err {p["rc_parity_err"]:.1e}, ddgi err {p["ddgi_parity_err"]:.1e}); '
                  f'distance {p["ddgi"]["distance"]} mode {p["ddgi"]["distance_mode"]}; grid origin {p["grid"]["origin"].round(1).tolist()} spacing {p["grid"]["spacing"]:.2f}')

    # ---- K4 / K5 ---------------------------------------------------------------------------
    rows = []
    for k in range(args.repeats):
        far = load(runner.require_region_dir(os.path.join(works['FAR'][k], 'region'), f'FAR r{k}'))
        for cond in ('D300', 'D400', 'DDIR'):
            cap = load(runner.require_region_dir(os.path.join(works[cond][k], 'region'), f'{cond} r{k}'))
            grid = logs[cond][k]['grid']
            D = float(CONDITIONS[cond]['r.GI.Diagnostic.KnownDistance']); dmode = CONDITIONS[cond]['r.GI.Diagnostic.KnownDistanceMode']
            band, frac_req = (BAND_K5, FRAC_K5) if dmode == 1 else (BAND_K4, FRAC_K4)
            for name, *_ in ROIS:
                a, b = cap[name], far[name]
                rp, near, none = predict_ratio(a['world'], grid, D, dmode)
                mask = a['geo'] & b['geo'] & (b['lum'] > uniform.MIN_DENOM) & ~near & ~none & np.isfinite(rp)
                if mask.sum() < 100:
                    raise Stop('K4' if dmode == 0 else 'K5', f'{cond} {name} r{k}: only {int(mask.sum())} usable pixels')
                rm = a['lum'][mask] / b['lum'][mask]
                q = rm / rp[mask]
                within = float(np.mean(np.abs(rm - rp[mask]) <= band))
                row = {'condition': cond, 'roi': name, 'repeat': k, 'n': int(mask.sum()),
                       'excluded_boundary': float(np.mean(near & a['geo'])), 'excluded_no_probe': float(np.mean(none & a['geo'])),
                       'measured_median': float(np.median(rm)), 'predicted_median': float(np.median(rp[mask])),
                       'ratio': {'median': float(np.median(q)), 'p10': float(np.percentile(q, 10)), 'p90': float(np.percentile(q, 90))},
                       'within_band': within, 'pass': bool(abs(np.median(q) - 1.0) <= band and within >= frac_req)}
                rows.append(row)
                print(f'  {cond:<5} {name:<12} r{k}: measured {row["measured_median"]:.4f} predicted {row["predicted_median"]:.4f} '
                      f'm/p median {row["ratio"]["median"]:.4f} (p10 {row["ratio"]["p10"]:.4f}, p90 {row["ratio"]["p90"]:.4f}) '
                      f'within +-{band}: {within:.1%}  n={row["n"]} (boundary {row["excluded_boundary"]:.1%}, no-probe {row["excluded_no_probe"]:.1%})  pass {row["pass"]}')
    report['rows'] = rows

    # ---- C ---------------------------------------------------------------------------------
    for cond in ('D300', 'D400', 'DDIR'):
        for name, *_ in ROIS:
            meds = [r['ratio']['median'] for r in rows if r['condition'] == cond and r['roi'] == name]
            if len(meds) >= 2 and max(abs(x - y) for x in meds for y in meds) > REPEAT_TOL:
                raise Stop('C', f'{cond} {name}: repeats disagree: {meds}')

    k4 = all(r['pass'] for r in rows if r['condition'] in ('D300', 'D400'))
    k5 = all(r['pass'] for r in rows if r['condition'] == 'DDIR')
    report['verdict'] = {'chebyshev': 'CHEBYSHEV OK' if k4 else 'CHEBYSHEV DIFFERS',
                         'depth_border': 'DEPTH BORDER OK' if k5 else 'DEPTH BORDER DIFFERS'}
    print(f'\n  -> {report["verdict"]["chebyshev"]}; {report["verdict"]["depth_border"]}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '56-consumer-known-chebyshev'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    print(f'consumer Chebyshev under known distances and a per-probe field — Camera C, {list(CONDITIONS)} x {args.repeats}')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'base': BASE, 'conditions': CONDITIONS,
              'rois': ROIS, 'harness': runner.harness_stamp(),
              'criteria': {'k4_band': BAND_K4, 'k4_fraction': FRAC_K4, 'k5_band': BAND_K5, 'k5_fraction': FRAC_K5,
                           'boundary_cm': BOUNDARY_CM, 'var_floor': VAR_FLOOR, 'repeat_tol': REPEAT_TOL}}
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
        # A --reuse run re-reads the retained repeat only and must not overwrite a full run's report
        # (runner.report_path writes <stem>.reuse.json). Bypassed until 2026-09-18; fixed on review.
        path = runner.report_path(args.out, 'consumer_known_chebyshev.json', args.reuse)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2, default=str)
        print(f'\n  -> {path}  (status: {report["status"]})')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
