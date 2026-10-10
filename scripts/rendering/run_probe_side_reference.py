"""followup-01, review of 2026-09-21, items 1-4 on the reference: where the radiance cache's probe
classification errs against geometry with known sides, and whether the candidate
(r.GI.RadianceCache.ProbeClassification = 1) recovers lighting without passing through walls.

Pre-registered in the followup-01 notes ("Probe-side reference and candidate — pre-registration",
2026-09-21, and "Candidate revised after its own smoke"). The geometry and the truth come from
scripts/rendering/probe_side_reference.py, the same module that generated the scenes.

  R1  where the current rule errs (reported): free probes discarded, probes inside solid accepted,
      probes behind the shell accepted, by configuration and true distance
  R2  the control H-64 lacked: median DXR back-face fraction >= 0.9 on probes inside solid and <= 0.1
      on free probes of SOLID; if it fails the face reading stays unvalidated
  R3  candidate classification: inside -> invalid on 100 %, free -> valid on >= 99 %, behind with the
      shell as the nearest surface -> invalid on 100 %
  P1  candidate, NEAR_OUT's lit face: unsupported <= 1 % and |mean / Lref - 1| <= 0.10
  P2  candidate, CORE's and NEAR_IN's lit faces within 5 % of the current rule
  P3  every sealed label: candidate mean <= current mean + max(3 x repeat spread, 1e-3 x Lref)
  P4  the dark means of both rules against Lref, and the valid probes from another region that reach
      them (reported)
  C   repeats agree on every label mean within 1 % (or 1e-3 x Lref where the mean is near zero)
Pass = R3, P1, P2, P3 in both configurations.

Usage: python scripts/rendering/run_probe_side_reference.py [--repeats 2] [--out DIR] [--reuse] [--configs QA,PROJECT]
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
import probe_side_reference as ref  # noqa: E402

BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.GI.FinalGather': 0, 'r.GI.Reflections': 0, 'r.SSGI': 0,
        'r.GTAO': 0, 'r.RadianceCache': 1, 'r.GI.RadianceCache.ProbesPerFrame': 4096, 'r.TAA': 1,
        'r.GI.RadianceCache.Diagnostic': 1, 'r.TechPanel.Show': 0}
DXR = {'r.RayTracing': 1, 'r.RayTracing.GI': 1, 'r.GI.DDGI.Diagnostic': 1}
RULES = (0, 1)
#: the rule compared with the current one (0); --candidate 2 = mode 2 (crossing count, 2026-09-23)
CAND = 1
SEEDS = False      # --visible-seeds: the candidate runs with r.GI.RadianceCache.ReachVisibleSeeds = 1
RELOCATE = False   # --relocate: the candidate runs with r.GI.RadianceCache.ReachRelocate = 1 (2026-09-25)
FRAMES = 210
FIRST, LAST = 200, 201
REC709 = np.array([0.2126, 0.7152, 0.0722])
INSIDE_BACK_MIN, FREE_BACK_MAX = 0.9, 0.1
FREE_VALID_MIN = 0.99
P1_UNSUPPORTED, P1_BAND, P2_BAND = 0.01, 0.10, 0.05
P3_SPREAD_K, P3_FLOOR = 3.0, 1e-3
C_REL, C_FLOOR = 0.01, 1e-3
ORACLE_CAMERAS = {'SOLID': 'EXT', 'SHELL': 'INT_E'}

NUM = r'(-?[\d.eE+-]+|nan|inf|-inf)'
RE_RC = re.compile(r'RC transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) fieldDistance=' + NUM + r' .*?hitCount=' + NUM +
                   r'(?: sideDistance=' + NUM + r' relocation=' + NUM + r' class=(\d+)(?: backFraction=' + NUM + r')?(?: crossingFraction=' + NUM + r')?(?: originOffset=\(' + NUM + ',' + NUM + ',' + NUM + r'\))?)?')
RE_ORIGIN = re.compile(r'queued SH snapshot .*origin=\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\)')
RE_DDGI = re.compile(r'DDGI transport: probe=(\d+) cell=\(\d+,\d+,\d+\) world=\(' + NUM + ',' + NUM + ',' + NUM +
                     r'\) .*?hitCount=' + NUM + r'(?: hitBackFraction=' + NUM + r')?')


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def work_dir(out, config, rule, scene, camera, k):
    return os.path.join(out, config, f'rule{rule}', f'{scene}_{camera}', f'r{k}')


def oracle_dir(out, config, scene):
    return os.path.join(out, config, 'dxr', f'{scene}_{ORACLE_CAMERAS[scene]}')


# ── per-probe tables ─────────────────────────────────────────────────────────────────────────────

def probe_table(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    o = RE_ORIGIN.search(text)
    if o is None:
        raise Stop('R0', f'{log_path}: no SH snapshot origin line')
    origin = np.array([float(o.group(i)) for i in (1, 2, 3)])
    rows = RE_RC.findall(text)
    if len(rows) != 4096:
        raise Stop('R0', f'{log_path}: {len(rows)} RC transport probe lines, expected 4096')
    f = lambda v: float(v) if v not in ('', None) else np.nan  # noqa: E731
    cell = np.array([[int(r[1]), int(r[2]), int(r[3])] for r in rows], float)
    return {'world': np.round(origin + cell * ref.S, 2),
            'field': np.array([f(r[4]) for r in rows]),
            'side': np.array([f(r[6]) for r in rows]),
            'reloc': np.array([f(r[7]) for r in rows]),
            'cls': np.array([int(r[8]) if r[8] else -1 for r in rows]),
            'back': np.array([f(r[9]) for r in rows]),
            'crossing': np.array([f(r[10]) for r in rows]),
            'offset': np.array([[f(r[11]), f(r[12]), f(r[13])] for r in rows])}


def invalid_of(cls):
    """Class codes: 0 valid / 1 inside (field rule); 2 valid / 3 inside by the rays / 4 relocated / 5 failed;
    6 inside by the crossings alone (mode 2); 7 not reachable from the camera (mode 3)."""
    return np.isin(cls, (1, 3, 5, 6, 7))


def reach_truth(scene, camera, world, cls):
    """Mode 3's truth: free probes in the air component the camera is in (2026-09-24).

    SOLID room cameras stand inside their sealed room; EXT stands outside every room; both SHELL
    cameras stand inside the shell box.
    """
    x, y, z = world[:, 0], world[:, 1], world[:, 2]
    free = cls != 'inside'
    if scene == 'SOLID':
        rooms = {name: (x > ref.WEST[1]) & (x < e0) & (np.abs(y - y0) < ref.HALF_IN) & (z > 0.0) & (z < ref.ROOM_TOP)
                 for name, y0, (e0, _) in ref.ROOMS}
        if camera == 'EXT':
            return free & ~np.logical_or.reduce(list(rooms.values()))
        return free & rooms[camera]
    (x0, y0, _), (x1, y1, z1) = ref.SHELL_BOX
    return free & (x > x0) & (x < x1) & (y > y0) & (y < y1) & (z > 0.0) & (z < z1)



def union_tables(tables):
    """Merge per-camera tables of one configuration/rule/scene by world position (probes are world-fixed)."""
    seen, out = {}, {k: [] for k in tables[0]}
    for t in tables:
        for i in range(len(t['world'])):
            key = tuple(t['world'][i])
            if key in seen:
                continue
            seen[key] = True
            for k in t:
                out[k].append(t[k][i])
    return {k: np.array(v) for k, v in out.items()}


def ddgi_table(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    rows = RE_DDGI.findall(text)
    if not rows:
        raise Stop('R2', f'{log_path}: no DDGI transport probe lines')
    world = np.round(np.array([[float(r[1]), float(r[2]), float(r[3])] for r in rows]), 2)
    back = np.array([float(r[5]) if r[5] else np.nan for r in rows])
    hits = np.array([float(r[4]) for r in rows])
    return {'world': world, 'back': back, 'hits': hits}


def region_of(scene, p):
    """Which enclosed region a point belongs to: a room / the shell interior / exterior / solid."""
    p = np.asarray(p, float)
    cls, _, _ = ref.truth(scene, p)
    reg = np.where(cls == 'inside', 'solid', 'exterior').astype(object)
    if scene == 'SOLID':
        for name, y0, (e0, e1) in ref.ROOMS:
            inside = (p[..., 0] > ref.WEST[1]) & (p[..., 0] < e0) & (np.abs(p[..., 1] - y0) < ref.HALF_IN) \
                & (p[..., 2] > 0.0) & (p[..., 2] < ref.ROOM_TOP)
            reg = np.where(inside, name, reg)
    else:
        (x0, y0, _), (x1, y1, z1) = ref.SHELL_BOX
        inside = (p[..., 0] > x0) & (p[..., 0] < x1) & (p[..., 1] > y0) & (p[..., 1] < y1) & (p[..., 2] > 0.0) & (p[..., 2] < z1)
        reg = np.where(inside & (cls != 'inside'), 'shell', reg)
    return reg


# ── pixels ───────────────────────────────────────────────────────────────────────────────────────

def label_summary(work, scene, camera):
    seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, LAST)
    acc, frames = {}, []
    for fr in seq:
        pos = fr.world_positions('FRAME')
        r = fr.rect('FRAME')
        di = r.channel('diffuse_indirect')[..., :3].astype(np.float64) @ REC709
        rc = r.channel('rc_support').astype(np.float64)
        frames.append({'pos': pos, 'di': di, 'rc': rc})
        for name, m in ref.label_pixels(scene, camera, pos).items():
            a = acc.setdefault(name, {'n': 0, 'sum': 0.0, 'unsup': 0, 'black': 0, 'max': 0.0})
            if not m.any():
                continue
            v = di[m]
            a['n'] += int(m.sum())
            a['sum'] += float(v.sum())
            a['unsup'] += int((rc[m][:, 3] == 0).sum())
            a['black'] += int((v == 0.0).sum())
            a['max'] = max(a['max'], float(v.max()))
    out = {}
    for name, a in acc.items():
        n = a['n']
        out[name] = {'n': n, 'mean': a['sum'] / n if n else None, 'unsupported': a['unsup'] / n if n else None,
                     'black': a['black'] / n if n else None, 'max': a['max']}
    return out, frames[0]


def crossing(scene, table, frame, labels_of_interest):
    """Valid probes from another region that reach the pixels of the dark labels (8 corners, facing > 0).

    The guard of relocated probes (class 4) is applied with the analytic nearest face, as the shader does.
    """
    pos = frame['pos']
    lookup = {tuple(w): i for i, w in enumerate(table['world'])}
    result = {}
    for name, mask in labels_of_interest.items():
        if not mask.any():
            continue
        p = pos[mask]
        # a surface pixel belongs to the region its normal faces: step 2 cm off the surface first
        reg_p = region_of(scene, p + 2.0 * LABEL_NORMALS[name])
        base = np.floor((p - ref.plane(0)) / ref.S)
        counts = {}
        reached = np.zeros(len(p), bool)
        for corner in range(8):
            off = np.array([(corner >> a) & 1 for a in range(3)], float)
            q = np.round(ref.plane(0) + (base + off) * ref.S, 2)
            to = q - p
            # the facing test against the label's surface normal, which the known geometry fixes
            n = LABEL_NORMALS[name]
            facing = (to @ n) > 0.0
            for j in np.where(facing)[0]:
                i = lookup.get(tuple(q[j]))
                if i is None or invalid_of(table['cls'][i:i + 1])[0]:
                    continue
                reg_q = region_of(scene, q[j])
                if reg_q == reg_p[j] or reg_q == 'solid':
                    continue
                if table['cls'][i] == 4:
                    nrm, dist = nearest_face(scene, q[j])
                    if np.dot(p[j] - (q[j] - nrm * dist), nrm) < -5.0:
                        continue
                reached[j] = True
                key = tuple(q[j])
                counts[key] = counts.get(key, 0) + 1
        result[name] = {'pixels': int(len(p)), 'reached_by_other_region': int(reached.sum()),
                        'probes': [{'world': list(k), 'pixels': v, 'class': int(table['cls'][lookup[k]]),
                                    'region': str(region_of(scene, np.array(k)))} for k, v in
                                   sorted(counts.items(), key=lambda kv: -kv[1])[:12]]}
    return result


LABEL_NORMALS = {}


def _init_label_normals():
    for name, y0, (e0, e1) in ref.ROOMS:
        LABEL_NORMALS[f'{name}:dark_face'] = np.array([-1.0, 0.0, 0.0])
        LABEL_NORMALS[f'{name}:dark_floor'] = np.array([0.0, 0.0, 1.0])
    LABEL_NORMALS['SHELL:east_face'] = np.array([-1.0, 0.0, 0.0])
    LABEL_NORMALS['SHELL:west_face'] = np.array([1.0, 0.0, 0.0])
    LABEL_NORMALS['SHELL:floor_east'] = np.array([0.0, 0.0, 1.0])
    LABEL_NORMALS['SHELL:floor_west'] = np.array([0.0, 0.0, 1.0])


_init_label_normals()


def nearest_face(scene, q):
    """Outward direction and distance from the nearest surface of the known geometry to a free point."""
    boxes = ref.solid_boxes() if scene == 'SOLID' else ref.shell_scene_boxes() + [('Shell',) + tuple(ref.SHELL_BOX)]
    best, bn = 1e30, np.array([0.0, 0.0, 1.0])
    for _, lo, hi in boxes:
        lo, hi = np.asarray(lo, float), np.asarray(hi, float)
        c = np.clip(q, lo, hi)
        if np.all(c == q):   # inside this box: nearest point on its boundary
            dists = np.concatenate([q - lo, hi - q])
            k = int(np.argmin(dists))
            d = dists[k]
            n = np.zeros(3)
            n[k % 3] = 1.0 if k >= 3 else -1.0
            n = -n
        else:
            d = float(np.linalg.norm(q - c))
            n = (q - c) / d
        if d < best:
            best, bn = d, n
    return bn, best


# ── study ────────────────────────────────────────────────────────────────────────────────────────

def capture(args, report):
    runner.require_exe()
    for config in args.configs:
        cfg = {**BASE, **ref.CONFIGS[config]}
        for k in range(args.repeats):
            for rule in RULES:
                for scene, camera in ref.CAMERAS:
                    work = work_dir(args.out, config, rule, scene, camera, k)
                    print(f'  ... {config} rule {rule} {scene}/{camera} repeat {k}', flush=True)
                    res = runner.run_capture(work, ref.project_path(scene, camera), FRAMES, regions=ref.regions((scene, camera)),
                                             region_fields='components', region_first=FIRST, region_last=LAST,
                                             cvars={**cfg, 'r.GI.RadianceCache.ProbeClassification': rule,
                                                    **({'r.GI.RadianceCache.ReachRelocate': 1} if RELOCATE and rule == CAND else {}),
                                                    **({'r.GI.RadianceCache.ReachVisibleSeeds': 1} if SEEDS and rule == CAND else {})},
                                             screenshot_frames='205-205', timing_first=200, clean=True)
                    if res['exit_code'] != 0:
                        raise Stop('capture', f'{work}: exit {res["exit_code"]}')
                    summary, _ = label_summary(work, scene, camera)
                    with open(os.path.join(work, 'labels.json'), 'w', encoding='utf-8') as fh:
                        json.dump(summary, fh, indent=1)
                    runner.release_repeat_dump(work, k)
        for scene in ORACLE_CAMERAS:
            work = oracle_dir(args.out, config, scene)
            print(f'  ... {config} DXR oracle {scene}', flush=True)
            res = runner.run_capture(work, ref.project_path(scene, ORACLE_CAMERAS[scene]), FRAMES,
                                     cvars={**cfg, **DXR, 'r.GI.RadianceCache.ProbeClassification': CAND},
                                     timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{work}: exit {res["exit_code"]}')


def labels_of(args, config, rule, scene, camera, k):
    path = os.path.join(work_dir(args.out, config, rule, scene, camera, k), 'labels.json')
    return json.load(open(path, encoding='utf-8'))


def analyse(args, report):
    for config in args.configs:
        rep = report.setdefault('configs', {}).setdefault(config, {})
        # ---- probe level --------------------------------------------------------------------
        for scene in ('SOLID', 'SHELL'):
            cams = [c for s, c in ref.CAMERAS if s == scene]
            tables = {rule: union_tables([probe_table(os.path.join(work_dir(args.out, config, rule, scene, c, 0), 'run.log'))
                                          for c in cams]) for rule in RULES}
            t0, t1 = tables[0], tables[CAND]
            if not np.array_equal(t0['world'], t1['world']):
                # align rule 1 on rule 0's order
                idx = {tuple(w): i for i, w in enumerate(t1['world'])}
                order = np.array([idx[tuple(w)] for w in t0['world']])
                t1 = {k: v[order] for k, v in t1.items()}
            cls, dist, shell_d = ref.truth(scene, t0['world'])
            rows = []
            for c in ('inside', 'free', 'behind'):
                for lo, hi in ((0, 25), (25, 50), (50, 100), (100, 150), (150, 1e9)):
                    m = (cls == c) & (dist >= lo) & (dist < hi)
                    if not m.any():
                        continue
                    rows.append({'class': c, 'dist': [lo, hi if hi < 1e9 else None], 'n': int(m.sum()),
                                 'invalid_current': int(invalid_of(t0['cls'][m]).sum()),
                                 'invalid_candidate': int(invalid_of(t1['cls'][m]).sum()),
                                 'candidate_codes': np.bincount(t1['cls'][m], minlength=7).tolist(),
                                 'field_median': float(np.median(t0['field'][m])),
                                 'relocation_max': float(np.nanmax(t1['reloc'][m]))})
            r1 = {'free_discarded': int(invalid_of(t0['cls'][cls == 'free']).sum()),
                  'inside_accepted': int((~invalid_of(t0['cls'][cls == 'inside'])).sum()),
                  'behind_accepted': int((~invalid_of(t0['cls'][cls == 'behind'])).sum()) if scene == 'SHELL' else None,
                  'rows': rows}
            m_in, m_free = cls == 'inside', cls == 'free'
            r3 = {'inside_invalid': float(invalid_of(t1['cls'][m_in]).mean()) if m_in.any() else None,
                  'inside_by_relocation_failure': int((t1['cls'][m_in] == 5).sum()),
                  'free_valid': float((~invalid_of(t1['cls'][m_free])).mean()) if m_free.any() else None,
                  'free_invalid_list': [{'world': t1['world'][i].tolist(), 'dist': float(dist[i]), 'field': float(t1['field'][i]),
                                         'class': int(t1['cls'][i]), 'relocation': float(t1['reloc'][i]),
                                         'back': float(t1['back'][i])}
                                        for i in np.where(m_free & invalid_of(t1['cls']))[0]]}
            if scene == 'SHELL':
                m_bs = (cls == 'behind') & (shell_d <= dist + 1e-6)
                r3['behind_shell_nearest_n'] = int(m_bs.sum())
                r3['behind_shell_nearest_invalid'] = float(invalid_of(t1['cls'][m_bs]).mean()) if m_bs.any() else None
                m_bg = (cls == 'behind') & ~m_bs
                r3['behind_ground_nearest_valid'] = float((~invalid_of(t1['cls'][m_bg])).mean()) if m_bg.any() else None
                # R3' (2026-09-23): the shell criterion the 2026-09-21 result said it should have been
                m_b100 = (cls == 'behind') & (shell_d <= 100.0)
                r3['behind_within_100_n'] = int(m_b100.sum())
                r3['behind_within_100_invalid'] = float(invalid_of(t1['cls'][m_b100]).mean()) if m_b100.any() else None
            ok3 = (r3['inside_invalid'] == 1.0) and (r3['free_valid'] is None or r3['free_valid'] >= FREE_VALID_MIN)
            if scene == 'SHELL':
                ok3 = ok3 and (r3['behind_shell_nearest_invalid'] in (None, 1.0))
            r3['pass'] = bool(ok3)
            ok3r = (r3['inside_invalid'] == 1.0) and (r3['free_valid'] is None or r3['free_valid'] >= FREE_VALID_MIN)
            if scene == 'SHELL':
                ok3r = ok3r and (r3['behind_within_100_invalid'] in (None, 1.0))
            r3['pass_revised'] = bool(ok3r)
            r3['class6'] = {'n': int((t1['cls'] == 6).sum()),
                            'by_truth': {c: int(((t1['cls'] == 6) & (cls == c)).sum()) for c in ('inside', 'free', 'behind')}}
            r3['inside_list'] = [{'world': t1['world'][i].tolist(), 'dist': float(dist[i]), 'class': int(t1['cls'][i]),
                                  'back': float(t1['back'][i]), 'crossing': float(t1['crossing'][i])}
                                 for i in np.where(m_in & ~invalid_of(t1['cls']))[0]]
            # R2: the DXR oracle
            dd = ddgi_table(os.path.join(oracle_dir(args.out, config, scene), 'run.log'))
            dcls, ddist, dshell = ref.truth(scene, dd['world'])
            hit = dd['hits'] > 0
            r2 = {c: {'n': int(((dcls == c) & hit).sum()),
                      'median_back': float(np.nanmedian(dd['back'][(dcls == c) & hit])) if ((dcls == c) & hit).any() else None,
                      'p10': float(np.nanpercentile(dd['back'][(dcls == c) & hit], 10)) if ((dcls == c) & hit).any() else None,
                      'p90': float(np.nanpercentile(dd['back'][(dcls == c) & hit], 90)) if ((dcls == c) & hit).any() else None}
                  for c in ('inside', 'free', 'behind')}
            # the oracle process ran the candidate: its classifier's fraction against the DXR one
            tt = probe_table(os.path.join(oracle_dir(args.out, config, scene), 'run.log'))
            idx = {tuple(w): i for i, w in enumerate(tt['world'])}
            pair = [(dd['back'][i], tt['back'][idx[tuple(w)]]) for i, w in enumerate(dd['world'])
                    if tuple(w) in idx and hit[i] and np.isfinite(tt['back'][idx[tuple(w)]])]
            pair = np.array(pair)
            r2['classifier_vs_dxr'] = {'n': int(len(pair)),
                                      'agree_on_threshold': float(((pair[:, 0] > 0.25) == (pair[:, 1] > 0.25)).mean()) if len(pair) else None,
                                      'median_abs_diff': float(np.median(np.abs(pair[:, 0] - pair[:, 1]))) if len(pair) else None}
            if scene == 'SOLID':
                r2['pass'] = bool(r2['inside']['median_back'] is not None and r2['inside']['median_back'] >= INSIDE_BACK_MIN
                                  and r2['free']['median_back'] is not None and r2['free']['median_back'] <= FREE_BACK_MAX)
            rep[scene] = {'R1': r1, 'R2': r2, 'R3': r3}
            rep[scene]['_tables'] = tables
            if CAND == 3:
                # Mode 3 depends on the camera: judge each camera's own table against its own truth.
                cam_tables = {rule: {c: probe_table(os.path.join(work_dir(args.out, config, rule, scene, c, 0), 'run.log'))
                                     for c in cams} for rule in RULES}
                rep[scene]['_cam_tables'] = cam_tables
                r3m = {}
                for c in cams:
                    t = cam_tables[CAND][c]
                    tcls, tdist, _ = ref.truth(scene, t['world'])
                    comp = reach_truth(scene, c, t['world'], tcls)
                    inv = invalid_of(t['cls'])
                    m_in, m_r, m_u = tcls == 'inside', comp, (tcls != 'inside') & ~comp
                    bad = np.where((m_in & ~inv) | (m_r & inv) | (m_u & ~inv))[0]
                    e = {'inside_invalid': float(inv[m_in].mean()) if m_in.any() else None,
                         'reachable_n': int(m_r.sum()),
                         'reachable_valid': float((~inv[m_r]).mean()) if m_r.any() else None,
                         'unreachable_n': int(m_u.sum()),
                         'unreachable_invalid': float(inv[m_u].mean()) if m_u.any() else None,
                         'mismatches': [{'world': t['world'][i].tolist(), 'truth': str(tcls[i]), 'component': bool(comp[i]),
                                         'dist': float(tdist[i]), 'field': float(t['field'][i]), 'class': int(t['cls'][i]),
                                         'climb': float(t['reloc'][i])} for i in bad[:40]],
                         'mismatch_n': int(len(bad))}
                    # R3r (relocation): every valid probe's TRACE ORIGIN lies in the camera's component.
                    origin = t['world'] + np.nan_to_num(t['offset'])
                    ocls, _, _ = ref.truth(scene, origin)
                    ocomp = reach_truth(scene, c, origin, ocls)
                    valid = ~inv
                    e['relocated_n'] = int((t['cls'] == 10).sum())
                    e['relocated_from_inside'] = int(((t['cls'] == 10) & m_in).sum())
                    e['valid_n'] = int(valid.sum())
                    e['valid_origin_in_component'] = float(ocomp[valid].mean()) if valid.any() else None
                    e['origin_mismatches'] = [{'world': t['world'][i].tolist(), 'origin': origin[i].tolist(),
                                               'class': int(t['cls'][i]), 'truth': str(tcls[i])}
                                              for i in np.where(valid & ~ocomp)[0][:40]]
                    e['pass_relocate'] = bool(e['valid_origin_in_component'] is None or e['valid_origin_in_component'] >= FREE_VALID_MIN)
                    e['pass'] = bool((e['inside_invalid'] in (None, 1.0))
                                     and (e['reachable_valid'] is None or e['reachable_valid'] >= FREE_VALID_MIN)
                                     and (e['unreachable_invalid'] is None or e['unreachable_invalid'] >= FREE_VALID_MIN))
                    r3m[c] = e
                r3['R3m'] = r3m
                r3['pass_reach'] = all(v['pass'] for v in r3m.values())
                r3['pass_relocate'] = all(v['pass_relocate'] for v in r3m.values())
        # ---- pixel level --------------------------------------------------------------------
        means = {}
        for rule in RULES:
            for scene, camera in ref.CAMERAS:
                for k in range(args.repeats):
                    for name, v in labels_of(args, config, rule, scene, camera, k).items():
                        means.setdefault((rule, name), []).append(v)
        def mean_of(rule, name):
            vals = [v['mean'] for v in means[(rule, name)] if v['mean'] is not None]
            return float(np.mean(vals)) if vals else None
        def spread_of(rule, name):
            vals = [v['mean'] for v in means[(rule, name)] if v['mean'] is not None]
            return float(np.max(vals) - np.min(vals)) if len(vals) > 1 else 0.0
        def unsup_of(rule, name):
            vals = [v['unsupported'] for v in means[(rule, name)] if v['unsupported'] is not None]
            return float(np.mean(vals)) if vals else None
        lref = {rule: 0.5 * (mean_of(rule, 'CORE:lit_face') + mean_of(rule, 'NEAR_IN:lit_face')) for rule in RULES}
        table = {name: {f'rule{rule}': {'mean': mean_of(rule, name), 'unsupported': unsup_of(rule, name),
                                        'spread': spread_of(rule, name),
                                        'over_Lref': (mean_of(rule, name) / lref[rule]) if mean_of(rule, name) is not None else None}
                        for rule in RULES}
                 for (_, name) in sorted({(0, n) for (_, n) in means})}
        p1v, p1u = mean_of(CAND, 'NEAR_OUT:lit_face'), unsup_of(CAND, 'NEAR_OUT:lit_face')
        p1 = {'mean': p1v, 'Lref': lref[CAND], 'ratio': p1v / lref[CAND], 'unsupported': p1u,
              'current_mean': mean_of(0, 'NEAR_OUT:lit_face'), 'current_unsupported': unsup_of(0, 'NEAR_OUT:lit_face'),
              'pass': bool(p1u <= P1_UNSUPPORTED and abs(p1v / lref[CAND] - 1.0) <= P1_BAND)}
        p2 = {}
        for name in ('CORE:lit_face', 'NEAR_IN:lit_face'):
            a, b = mean_of(0, name), mean_of(CAND, name)
            p2[name] = {'current': a, 'candidate': b, 'ratio': b / a, 'pass': bool(abs(b / a - 1.0) <= P2_BAND)}
        p2['pass'] = all(v['pass'] for v in p2.values() if isinstance(v, dict))
        sealed = [f'{n}:dark_all' for n, _, _ in ref.ROOMS] + ['SHELL:all_e', 'SHELL:all_w']
        p3 = {}
        for name in sealed:
            a, b = mean_of(0, name), mean_of(CAND, name)
            tol = max(P3_SPREAD_K * max(spread_of(0, name), spread_of(CAND, name)), P3_FLOOR * lref[0])
            p3[name] = {'current': a, 'candidate': b, 'tolerance': tol, 'current_over_Lref': a / lref[0],
                        'candidate_over_Lref': b / lref[CAND], 'pass': bool(b <= a + tol)}
        p3['pass'] = all(v['pass'] for v in p3.values() if isinstance(v, dict))
        cfail = []
        for (rule, name), vals in means.items():
            ms = [v['mean'] for v in vals if v['mean'] is not None]
            if len(ms) < 2:
                continue
            spread, m = max(ms) - min(ms), float(np.mean(ms))
            if spread > max(C_REL * abs(m), C_FLOOR * lref[rule]):
                cfail.append({'rule': rule, 'label': name, 'means': ms})
        rep['pixels'] = {'Lref': lref, 'labels': table, 'P1': p1, 'P2': p2, 'P3': p3,
                         'C': {'pass': not cfail, 'failures': cfail}}
        # ---- P4: who reaches the dark labels ----------------------------------------------------
        p4 = {}
        for rule in RULES:
            for scene, camera in ref.CAMERAS:
                if camera == 'EXT':
                    continue
                work = work_dir(args.out, config, rule, scene, camera, 0)
                seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, FIRST)
                fr = seq[0]
                pos = fr.world_positions('FRAME')
                frame = {'pos': pos, 'rc': fr.rect('FRAME').channel('rc_support')}
                labels = {n: m for n, m in ref.label_pixels(scene, camera, pos).items() if n in LABEL_NORMALS}
                tbl = rep[scene]['_cam_tables'][rule][camera] if '_cam_tables' in rep[scene] else rep[scene]['_tables'][rule]
                p4[f'rule{rule}:{scene}/{camera}'] = crossing(scene, tbl, frame, labels)
        rep['pixels']['P4'] = p4
        rep['pass'] = bool(rep['SOLID']['R3']['pass'] and rep['SHELL']['R3']['pass'] and p1['pass'] and p2['pass'] and p3['pass'])
        rep['pass_revised'] = bool(rep['SOLID']['R3']['pass_revised'] and rep['SHELL']['R3']['pass_revised']
                                   and p1['pass'] and p2['pass'] and p3['pass'])
        if CAND == 3:
            rep['pass_reach'] = bool(rep['SOLID']['R3']['pass_reach'] and rep['SHELL']['R3']['pass_reach']
                                     and p1['pass'] and p2['pass'] and p3['pass'])
            rep['pass_relocate'] = bool(rep['SOLID']['R3']['pass_relocate'] and rep['SHELL']['R3']['pass_relocate']
                                        and p1['pass'] and p2['pass'] and p3['pass'] and rep['pixels']['C']['pass'])
    for config in args.configs:
        for scene in ('SOLID', 'SHELL'):
            report['configs'][config][scene].pop('_tables', None)
            report['configs'][config][scene].pop('_cam_tables', None)
    report['pass'] = all(report['configs'][c]['pass'] for c in args.configs)
    report['pass_revised'] = all(report['configs'][c]['pass_revised'] for c in args.configs)
    if CAND == 3:
        report['pass_reach'] = all(report['configs'][c]['pass_reach'] for c in args.configs)
        report['pass_relocate'] = all(report['configs'][c]['pass_relocate'] for c in args.configs)


# ── montage for visual review ────────────────────────────────────────────────────────────────────

def montage(args):
    from PIL import Image, ImageDraw
    panels = []
    for config in args.configs:
        for scene, camera in ref.CAMERAS:
            row = []
            for rule in RULES:
                work = work_dir(args.out, config, rule, scene, camera, 0)
                fr = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, FIRST)[0]
                di = fr.rect('FRAME').channel('diffuse_indirect')[..., :3].astype(np.float64) @ REC709
                geo = fr.rect('FRAME').channel('lighting_normal')[..., 3] >= 0.0
                # log scale: 0.5 (the lit faces) -> white, 5e-5 -> black; zero and sky black
                v = np.clip((np.log10(np.maximum(di, 1e-12) / 0.5) + 4.0) / 4.0, 0.0, 1.0)
                v[~geo] = 0.0
                img = Image.fromarray((v * 255).astype(np.uint8)).convert('RGB')
                d = ImageDraw.Draw(img)
                d.rectangle([0, 0, 330, 16], fill=(0, 0, 0))
                d.text((4, 2), f'{config} {scene}/{camera} rule {rule} (log10 diffuse_indirect)', fill=(255, 255, 0))
                row.append(img)
            panels.append(row)
    w, h = ref.WIDTH, ref.HEIGHT
    sheet = Image.new('RGB', (2 * w, h * len(panels)))
    for i, row in enumerate(panels):
        for j, img in enumerate(row):
            sheet.paste(img, (j * w, i * h))
    path = os.path.join(args.out, 'probe_side_reference_montage.jpg')
    sheet.save(path, quality=85)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '62-probe-side-reference', 'study'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--configs', default='QA,PROJECT')
    ap.add_argument('--candidate', type=int, default=1, choices=(1, 2, 3))
    ap.add_argument('--relocate', action='store_true')
    ap.add_argument('--visible-seeds', action='store_true')
    args = ap.parse_args()
    global RULES, CAND, RELOCATE, SEEDS
    RELOCATE = args.relocate
    SEEDS = args.visible_seeds
    CAND = args.candidate
    RULES = (0, CAND)
    args.configs = args.configs.split(',')
    os.makedirs(args.out, exist_ok=True)
    report = {'status': 'incomplete', 'stopped_at': None, 'base': BASE, 'configs': {},
              'source': runner.source_fingerprint() if hasattr(runner, 'source_fingerprint') else None}
    try:
        if not args.reuse:
            capture(args, report)
        report['identity'] = runner.capture_identity(work_dir(args.out, args.configs[0], CAND, 'SOLID', 'EXT', 0))
        analyse(args, report)
        report['montage'] = montage(args)
        report['status'] = 'complete'
    except Stop as e:
        report['status'] = 'stopped'
        report['stopped_at'] = {'gate': e.gate, 'reason': e.reason}
        print(f'STOPPED at {e.gate}: {e.reason}')
    except Exception:                                   # noqa: BLE001
        report['status'] = 'error'
        report['error'] = traceback.format_exc()
        print(report['error'])
    path = runner.report_path(args.out, 'probe_side_reference.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=1, default=lambda o: o.tolist() if hasattr(o, 'tolist') else str(o))
    print(f'report: {path}  status: {report["status"]}  pass: {report.get("pass")}  pass_revised: {report.get("pass_revised")}  pass_reach: {report.get("pass_reach")}  pass_relocate: {report.get("pass_relocate")}')


if __name__ == '__main__':
    main()
