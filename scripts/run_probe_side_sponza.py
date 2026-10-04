"""followup-01, review of 2026-09-21, item 4 on Sponza: the probe-side candidate
(r.GI.RadianceCache.ProbeClassification = 1) against the current rule, still and in motion. Runs only
after the reference (run_probe_side_reference.py) passed. Pre-registered in the followup-01 notes
("Probe-side reference and candidate — pre-registration", 2026-09-21).

  S1  still, the H-63 setup (Camera C, the QA project, final gather OFF, the three H-62 ROIs), 2 repeats:
      unsupported fraction per ROI with the candidate <= 5 % (current: 20.6 / 96.0 / 1.5 % in H-63)
  S1o the DXR oracle in the same setup, candidate on: per probe, the classifier's back-face fraction
      against the DXR writer's (reported: agreement on the 0.25 threshold)
  S2  still, the project configuration (GDF 256, mesh SDF 128, final gather ON, reflections ON) at
      Camera C: the three ROIs' diffuse_indirect and support (reported) and a full-frame linear output
      for visual review
  S3  motion, the project configuration, the long sweep (Camera A, 900 cm along +x, ~3 grid cells):
      per-frame linear output means on four screen ROIs; the candidate must not add steps —
      p99 |frame-to-frame change| per ROI <= 1.1 x the current rule's, and no larger step at the
      logged radiance-cache recentres

Usage: python scripts/run_probe_side_sponza.py [--out DIR] [--reuse] [--only S1,S1o,S2,S3]
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
import run_consumer_support as support  # noqa: E402
import run_consumer_known_radiance as uniform  # noqa: E402
import run_probe_side_reference as refstudy  # noqa: E402

QA_PROJECT = os.path.join(runner.REPO, 'scripts', uniform.PROJECT)            # Camera C, QA configuration
SWEEP_PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-longsweep-camera.uzproject')
S1_BASE = dict(support.BASE)                                                   # study 60 / H-63: FG off, AE off
PROJECT_CVARS = {'r.RadianceCache': 1, 'r.GI.Reflections': 1, 'r.GI.FinalGather': 1, 'r.GI.GDF.Resolution': 256,
                 'r.GI.MeshSdf.Resolution': 128, 'r.GI.MeshSdf.TwoSided': 1, 'r.GI.RadianceCache.GridExtent': 2000,
                 'r.TAA': 1, 'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TechPanel.Show': 0,
                 'r.GI.RadianceCache.Diagnostic': 1}
ROIS = uniform.ROIS                                                            # shadow_wall, lit_wall, centre
S1_UNSUPPORTED_MAX = 0.05
S3_ROIS = [('rear_door', 960, 592, 224, 224), ('right_shadow', 1472, 464, 256, 256),
           ('lit_floor', 832, 816, 256, 64), ('left_wall', 160, 400, 256, 256)]
S3_FRAMES, S3_FIRST, S3_LAST = 300, 90, 290                                    # hold 60 from ~37, sweep 150, hold
S3_P99_RATIO = 1.1
REC709 = np.array([0.2126, 0.7152, 0.0722])
RULES = (0, 1)
CAND = 1        # --candidate: the rule compared with the current one (0)
SEEDS = False      # --visible-seeds: the candidate runs with r.GI.RadianceCache.ReachVisibleSeeds = 1
RELOCATE = False  # --relocate: the candidate runs with r.GI.RadianceCache.ReachRelocate = 1


class Stop(Exception):
    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def rule_cvar(rule):
    extra = {'r.GI.RadianceCache.ReachRelocate': 1} if RELOCATE and rule == CAND else {}
    if SEEDS and rule == CAND:
        extra['r.GI.RadianceCache.ReachVisibleSeeds'] = 1
    return {'r.GI.RadianceCache.ProbeClassification': rule, **extra}


def roi_support(work, first, last):
    seq = regiondump.load_sequence(os.path.join(work, 'region'), first, last)
    out = {}
    for name, *_ in ROIS:
        di, geo, rc = [], [], []
        for fr in seq:
            r = fr.rect(name)
            di.append(r.channel('diffuse_indirect')[..., :3].astype(np.float64) @ REC709)
            geo.append(r.channel('lighting_normal')[..., 3] >= 0.0)
            rc.append(r.channel('rc_support').astype(np.float64))
        di, geo, rc = np.stack(di), np.stack(geo), np.stack(rc)
        out[name] = {'geometry': int(geo.sum()), 'mean': float(di[geo].mean()),
                     'black': float((di[geo] == 0.0).mean()),
                     'unsupported': float((rc[..., 3][geo] == 0.0).mean()),
                     'surviving_median': float(np.median(rc[..., 3][geo])),
                     'weight_median': float(np.median(rc[..., 0][geo]))}
    return out


def s1(args, report):
    rows = {}
    for rule in RULES:
        for k in range(args.repeats):
            work = os.path.join(args.out, 'S1', f'rule{rule}_r{k}')
            if not args.reuse:
                print(f'  ... S1 rule {rule} repeat {k}', flush=True)
                res = runner.run_capture(work, QA_PROJECT, uniform.FRAMES, regions=ROIS, region_fields='components',
                                         region_first=uniform.FIRST, region_last=uniform.LAST,
                                         cvars={**S1_BASE, **support.CONDITIONS['SDF'], **rule_cvar(rule)}, timing_first=200, clean=True)
                if res['exit_code'] != 0:
                    raise Stop('capture', f'{work}: exit {res["exit_code"]}')
            rows.setdefault(f'rule{rule}', []).append(roi_support(work, uniform.FIRST, uniform.LAST))
    s = {'rows': rows}
    cand = rows[f'rule{CAND}']
    s['pass'] = bool(all(r[name]['unsupported'] <= S1_UNSUPPORTED_MAX for r in cand for name, *_ in ROIS))
    report['S1'] = s


def s1_oracle(args, report):
    work = os.path.join(args.out, 'S1o', 'dxr_rule1')
    if not args.reuse:
        print('  ... S1o DXR oracle, candidate on', flush=True)
        res = runner.run_capture(work, QA_PROJECT, uniform.FRAMES,
                                 cvars={**S1_BASE, **support.CONDITIONS['DDGI'], **refstudy.DXR, **rule_cvar(1)},
                                 timing_first=200, clean=True)
        if res['exit_code'] != 0:
            raise Stop('capture', f'{work}: exit {res["exit_code"]}')
    rc = refstudy.probe_table(os.path.join(work, 'run.log'))
    dd = refstudy.ddgi_table(os.path.join(work, 'run.log'))
    idx = {tuple(w): i for i, w in enumerate(rc['world'])}
    pairs = []
    for i, w in enumerate(dd['world']):
        j = idx.get(tuple(w))
        if j is None or dd['hits'][i] <= 0 or not np.isfinite(rc['back'][j]):
            continue
        pairs.append((dd['back'][i], rc['back'][j], rc['field'][j], rc['cls'][j]))
    p = np.array(pairs)
    inband = p[:, 2] < 5.0
    agree = (p[:, 0] > 0.25) == (p[:, 1] > 0.25)
    report['S1o'] = {'n': int(len(p)), 'agree_on_threshold': float(agree.mean()),
                     'agree_in_band': float(agree[inband].mean()) if inband.any() else None,
                     'n_in_band': int(inband.sum()),
                     'median_abs_diff': float(np.median(np.abs(p[:, 0] - p[:, 1]))),
                     'candidate_codes': np.bincount(rc['cls'][rc['cls'] >= 0], minlength=6).tolist(),
                     'current_invalid_in_same_process': int((rc['field'] < 5.0).sum()),
                     'in_band_by_candidate_code': np.bincount(p[inband, 3].astype(int), minlength=6).tolist()}


def s2(args, report):
    rows, frames = {}, {}
    for rule in RULES:
        work = os.path.join(args.out, 'S2', f'rule{rule}_rois')
        if not args.reuse:
            print(f'  ... S2 rule {rule} ROIs', flush=True)
            res = runner.run_capture(work, QA_PROJECT, uniform.FRAMES, regions=ROIS, region_fields='components',
                                     region_first=uniform.FIRST, region_last=uniform.LAST,
                                     cvars={**PROJECT_CVARS, **rule_cvar(rule)}, timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{work}: exit {res["exit_code"]}')
        rows[f'rule{rule}'] = roi_support(work, uniform.FIRST, uniform.LAST)
        work = os.path.join(args.out, 'S2', f'rule{rule}_frame')
        if not args.reuse:
            print(f'  ... S2 rule {rule} full frame', flush=True)
            res = runner.run_capture(work, QA_PROJECT, uniform.FRAMES, regions=[('FRAME', 0, 0, 1920, 1080)],
                                     region_fields='output', region_first=uniform.FIRST, region_last=uniform.FIRST,
                                     cvars={**PROJECT_CVARS, **rule_cvar(rule)}, timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{work}: exit {res["exit_code"]}')
        fr = regiondump.load_sequence(os.path.join(work, 'region'), uniform.FIRST, uniform.FIRST)[0]
        frames[rule] = fr.rect('FRAME').channel('output')[..., :3].astype(np.float64)
    report['S2'] = {'rows': rows}
    report['S2']['image'] = image_pair(args, frames, 'S2_cameraC_project', 'Sponza Camera C, project config, gather on')


def image_pair(args, frames, name, title):
    """Current | candidate | difference, tonemapped identically (Reinhard on the pair's common exposure)."""
    from PIL import Image, ImageDraw
    lum = np.stack([frames[r] @ REC709 for r in RULES])
    key = 0.18 / max(float(np.exp(np.mean(np.log(np.maximum(lum[0], 1e-6))))), 1e-6)
    tiles = []
    for r in RULES:
        v = frames[r] * key
        v = v / (1.0 + v)
        v = np.power(np.clip(v, 0, 1), 1 / 2.2)
        tiles.append(Image.fromarray((v * 255).astype(np.uint8)))
    diff = (lum[1] - lum[0]) / np.maximum(lum[0], 1e-4)
    d = np.zeros(diff.shape + (3,), np.uint8)
    d[..., 0] = (np.clip(diff, 0, 1) * 255).astype(np.uint8)          # red: brighter with the candidate
    d[..., 2] = (np.clip(-diff, 0, 1) * 255).astype(np.uint8)         # blue: darker
    tiles.append(Image.fromarray(d))
    w, h = tiles[0].size
    sheet = Image.new('RGB', (3 * w // 2, h // 2))
    for i, t in enumerate(tiles):
        t = t.resize((w // 2, h // 2))
        dr = ImageDraw.Draw(t)
        dr.rectangle([0, 0, 470, 16], fill=(0, 0, 0))
        dr.text((4, 2), [f'{title}: current rule', f'{title}: candidate', 'relative change (red up, blue down, 100 %)'][i],
                fill=(255, 255, 0))
        sheet.paste(t, (i * w // 2, 0))
    path = os.path.join(args.out, f'{name}.jpg')
    sheet.save(path, quality=85)
    return path


def recentre_frames(log_path):
    text = open(log_path, encoding='utf-8', errors='replace').read()
    return sorted({int(m.group(1)) for m in re.finditer(r'GI recentre: frame=(\d+) radianceCache reason=recentre', text)})


def s3(args, report):
    series, recentres = {}, {}
    for rule in RULES:
        work = os.path.join(args.out, 'S3', f'rule{rule}')
        if not args.reuse:
            print(f'  ... S3 rule {rule} sweep', flush=True)
            res = runner.run_capture(work, SWEEP_PROJECT, S3_FRAMES, regions=S3_ROIS, region_fields='output',
                                     region_first=S3_FIRST, region_last=S3_LAST,
                                     cvars={**PROJECT_CVARS, **rule_cvar(rule)}, timing_first=200, clean=True)
            if res['exit_code'] != 0:
                raise Stop('capture', f'{work}: exit {res["exit_code"]}')
        seq = regiondump.load_sequence(os.path.join(work, 'region'), S3_FIRST, S3_LAST)
        series[rule] = {name: np.array([float((fr.rect(name).channel('output')[..., :3].astype(np.float64) @ REC709).mean())
                                        for fr in seq]) for name, *_ in S3_ROIS}
        series[rule]['_frames'] = np.array([fr.frame for fr in seq]) if hasattr(seq[0], 'frame') else np.arange(S3_FIRST, S3_LAST + 1)
        recentres[rule] = recentre_frames(os.path.join(work, 'run.log'))
    out = {'recentres': {f'rule{r}': recentres[r] for r in RULES}, 'rois': {}}
    ok = True
    for name, *_ in S3_ROIS:
        d0, d1 = np.abs(np.diff(series[0][name])), np.abs(np.diff(series[CAND][name]))
        frames = series[0]['_frames'][1:]
        p0, p1 = float(np.percentile(d0, 99)), float(np.percentile(d1, 99))
        at = [int(np.where(frames == f)[0][0]) for f in recentres[0] if f in frames]
        step0 = float(max((d0[i] for i in at), default=0.0))
        step1 = float(max((d1[i] for i in at), default=0.0))
        roi_ok = p1 <= S3_P99_RATIO * p0 and step1 <= S3_P99_RATIO * max(step0, p0)
        ok &= roi_ok
        diff = series[CAND][name] - series[0][name]
        out['rois'][name] = {'mean_current': float(series[0][name].mean()), 'mean_candidate': float(series[CAND][name].mean()),
                             'p99_step_current': p0, 'p99_step_candidate': p1,
                             'max_step_at_recentre_current': step0, 'max_step_at_recentre_candidate': step1,
                             'candidate_minus_current': {'min': float(diff.min()), 'max': float(diff.max()),
                                                         'max_abs_jump': float(np.abs(np.diff(diff)).max())},
                             'pass': bool(roi_ok)}
    out['pass'] = bool(ok and recentres[0] == recentres[CAND])
    out['same_recentre_frames'] = recentres[0] == recentres[CAND]
    # Frame cost, reported not gated: the manifests time frames 200.. (the recentre near 216 included).
    def timing(r):
        m = json.load(open(os.path.join(args.out, 'S3', f'rule{r}', 'manifest.json'), encoding='utf-8'))
        return {'frame_ms': m.get('frame_ms'), 'gpu_ms': m.get('gpu_ms')}
    out['manifest_timing'] = {f'rule{r}': timing(r) for r in RULES}
    report['S3'] = out
    np.savez(os.path.join(args.out, 'S3_series.npz'), **{f'r{r}_{k}': v for r in RULES for k, v in series[r].items()})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '63-probe-side-sponza'))
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--only', default='S1,S1o,S2,S3')
    ap.add_argument('--candidate', type=int, default=1, choices=(1, 2, 3))
    ap.add_argument('--relocate', action='store_true')
    ap.add_argument('--visible-seeds', action='store_true')
    args = ap.parse_args()
    global RULES, CAND, RELOCATE, SEEDS
    RELOCATE = args.relocate
    SEEDS = args.visible_seeds
    CAND = args.candidate
    RULES = (0, CAND)
    os.makedirs(args.out, exist_ok=True)
    # A partial run (--only) updates the existing report instead of replacing the other steps.
    path = runner.report_path(args.out, 'probe_side_sponza.json', args.reuse)
    report = json.load(open(path, encoding='utf-8')) if os.path.exists(path) else {}
    report.update({'status': 'incomplete', 'stopped_at': None})
    steps = {'S1': s1, 'S1o': s1_oracle, 'S2': s2, 'S3': s3}
    try:
        if not args.reuse:
            runner.require_exe()
        for name in args.only.split(','):
            steps[name](args, report)
        report['status'] = 'complete'
    except Stop as e:
        report['status'] = 'stopped'
        report['stopped_at'] = {'gate': e.gate, 'reason': e.reason}
        print(f'STOPPED at {e.gate}: {e.reason}')
    except Exception:                                   # noqa: BLE001
        report['status'] = 'error'
        report['error'] = traceback.format_exc()
        print(report['error'])
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=1, default=lambda o: o.tolist() if hasattr(o, 'tolist') else str(o))
    print(f'report: {path}  status: {report["status"]}')


if __name__ == '__main__':
    main()
