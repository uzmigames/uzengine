"""followup-12, the door: what varies where the translucent pass draws - coverage, material, lighting
or the colour it produces - read only after the records are shown not to change the image.

History, kept because each version failed in a way the next had to answer (ledger H-43, H-44):
  * v1 (32-door-consumption) read coverage from records written by fragments the depth test later
    rejected (a UAV write disables the implicit early depth test); its own G1 caught it. Fixed with
    [earlydepthstencil] in translucent_ps.hlsl.
  * v2 (33-door-consumption-earlyz) compared its image with v1's capture, per pixel at 1e-6 and then
    on aggregates at 1e-6. Both failed; a same-configuration process floor (34-c0-process-floor)
    showed 1e-6 sits inside process noise, so that C0 could not discriminate. Its message "the
    instrumentation altered the image" over-claimed: the failure showed only that equivalence was
    not demonstrated at that tolerance. Review (2026-09-15) also found that C0 was optional (no
    --compare-with meant C0 = None and a report not marked incomplete), that a C0 failure exited
    before writing the report after two repeats' dumps had been deleted, that G1 compared luminance
    so channel errors could cancel, that identifiers were compared with a relative tolerance, and
    that one translucent record per pixel cannot show whether surfaces overlap.

This version, pre-registered before its captures (followup-12 notes, 2026-09-15):

  region   `rear_door`, the full followup-09 rectangle (960, 592, 224, 224); pose A; still camera;
           fixed exposure; jitter length 8; frames 237..364 after 200 warm-up frames
  captures two conditions, one binary, interleaved, N repeats each (default 3), EVERY dump retained:
           records_off  --region-fields taa         LIGHTING + TAA records
           records_on   --region-fields components  adds LIGHTING_EXTRA, including the translucent
                                                    records and the layer count

  C0  EQUIVALENCE of the image with and without the LIGHTING_EXTRA records. It validates the visual
      cost of the records this study reads. It does NOT isolate the effect of [earlydepthstencil],
      which both conditions carry, and is not presented as doing so.
      Quantities: `taa_raw` and `taa_output`, over the whole region and over the glass mask (pixels
      drawn at least once in any records_on repeat): mean energy per channel (R, G, B) and luminance
      oscillation (per-pixel temporal std, averaged over the mask).
      Margins, fixed before the captures and taken from rules the programme already uses:
        energy       |on - off| / |off| <= 1 %  (candidate criterion C5)
        oscillation  |on - off| <= max(3 x 95 % half-width, 5 % of the mean) of the records_off
                     repeats - followup-09's floor rule, which gives about rear_door's 8.9e-6 for
                     `taa_output` over the region. Here it is also applied to `taa_raw` and to the
                     glass mask, which it was not first measured on; that is stated, not hidden.
      Per quantity over every off x on pair: EQUIVALENT when the largest difference is within the
      margin; DIFFERENT when the smallest difference exceeds both the margin and the largest spread
      within either condition; otherwise INCONCLUSIVE.
      Status: PASSED (all EQUIVALENT) | NOT EQUIVALENT (any DIFFERENT) | INCONCLUSIVE | NOT RUN.
      Anything but PASSED stops the study with the report marked incomplete. NOT EQUIVALENT means the
      records moved the image beyond a margin; INCONCLUSIVE means only that equivalence was not shown.
  G0  the translucent pass drew at least one pixel of the region in every records_on repeat.
  O1  the layer record is consistent: drawn <=> layers >= 1, and layers are whole numbers.
  O2  relevant overlap = a pixel-frame with layers >= 2 whose recorded (last) layer has opacity < 1.
      Only there is a one-layer reconstruction invalid: at opacity 1 the blend (One/InvSrcAlpha)
      multiplies everything beneath by zero. Pixels with any relevant overlap are excluded from G1
      and from the reading, and listed. The count can undercount overlap inside one draw call.
  G1  the records reproduce the pixel PER RGB CHANNEL: src.rgb x opacity + composite x (1 - opacity)
      against `taa_raw`, 2e-3 relative (scale floor 1e-6), at every drawn pixel-frame outside the
      O2 exclusion, in every repeat.
  R   the reading, at pixels drawn in every frame of every repeat ("always") and at pixels drawn in
      only some frames ("some", over the frames they are drawn):
        coverage         the classes; whether coverage repeats with the jitter index; whether it is
                         the same in every repeat
        material         opacity, albedo (rgb), albedo SRV index, shading-model id, emission (rgb)
        lighting         IBL diffuse (rgb), irradiance SRV index, shading normal, N.L
        produced colour  source colour (rgb)
      Identifiers: EXACT equality of their bits. Real quantities: constant when the per-pixel range
      is <= 1e-6 of their magnitude (floor 1e-12). A varying quantity is also tested for being locked
      to the jitter index (constant, by the same rule, within every set of frames sharing an index).
      The light colour and the ambient term reach the colour through constant buffers and are not
      recorded: a produced colour that varies while every recorded input is constant points at them
      as a deduction, not a measurement.
  S5  oscillation before (`taa_raw`) and after (`taa_output`) the TAA and mean energy, per class,
      with the share of the summed per-pixel std each class holds - a distribution, not a cause.

Every gate runs in that order and the first failure stops the study. The report is written on EVERY
exit with `status` ("complete" or "incomplete") and `stopped_at`, and every dump stays on disk, so a
failure is audited from the data rather than from a log.

Usage: python scripts/rendering/run_door_consumption_study.py [--frames 128] [--repeats 3] [--out DIR] [--reuse]
  --reuse re-reads the captures in --out; it needs --repeats of each condition there.
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import sys
import traceback
import warnings

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import criteria as crit  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
ANALYSIS_LAST = FIRST + 127
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
PROJECT = 'sponza-flicker-a.uzproject'
ROI = ('rear_door', 960, 592, 224, 224)
NAME = ROI[0]
CONDITIONS = {'records_off': 'taa', 'records_on': 'components'}
IMAGES = ('taa_raw', 'taa_output')
TRANSLUCENT = ('translucent_source', 'translucent_albedo', 'translucent_ibl', 'translucent_normal',
               'translucent_emission')

C0_ENERGY_RELATIVE = 0.01          # candidate criterion C5
C0_FLOOR_FRACTION = 0.05           # followup-09 floor rule: 5 % of the mean ...
C0_FLOOR_HALF_WIDTHS = 3.0         # ... or 3 x the 95 % half-width, whichever is larger
T_975 = {2: 12.706, 3: 4.303, 4: 3.182, 5: 2.776, 6: 2.571, 7: 2.447, 8: 2.365, 9: 2.306, 10: 2.262}
G1_TOLERANCE = 2e-3
G1_SCALE_FLOOR = 1e-6
R_CONSTANT = 1e-6
R_MAGNITUDE_FLOOR = 1e-12

#: (label, factor, channel, components, kind); kind 'id' is compared as exact uint bits.
READ = [
    ('opacity', 'material', 'translucent_source', slice(3, 4), 'real'),
    ('albedo (rgb)', 'material', 'translucent_albedo', slice(0, 3), 'real'),
    ('albedo SRV index', 'material', 'translucent_albedo', slice(3, 4), 'id'),
    ('shading-model id', 'material', 'translucent_normal', slice(3, 4), 'id'),
    ('emission (rgb)', 'material', 'translucent_emission', slice(0, 3), 'real'),
    ('IBL diffuse (rgb)', 'lighting', 'translucent_ibl', slice(0, 3), 'real'),
    ('irradiance SRV index', 'lighting', 'translucent_ibl', slice(3, 4), 'id'),
    ('shading normal (xyz)', 'lighting', 'translucent_normal', slice(0, 3), 'real'),
    ('N.L', 'lighting', 'translucent_emission', slice(3, 4), 'real'),
    ('source colour (rgb)', 'produced colour', 'translucent_source', slice(0, 3), 'real'),
]
FACTORS = ('material', 'lighting', 'produced colour')


class Stop(Exception):
    """A gate failed: the study stops there and says so in the report."""

    def __init__(self, gate, reason):
        super().__init__(f'{gate}: {reason}')
        self.gate, self.reason = gate, reason


def finite(a, what):
    a = np.asarray(a)
    bad = int(np.count_nonzero(~np.isfinite(a)))
    if bad:
        raise Stop('finite', f'{what} holds {bad} non-finite values')
    return a


def composite_rgb(r):
    return (r.channel('direct_light')[..., :3].astype(np.float64)
            + r.channel('diffuse_indirect')[..., :3] + r.channel('specular_indirect')[..., :3]
            + r.channel('emissive_and_mis')[..., :3])


def load_capture(region_dir, records):
    """Everything the gates and the reading need from one capture, extracted in one pass."""
    seq = regiondump.load_sequence(region_dir, FIRST, ANALYSIS_LAST)
    T = len(seq)
    first = seq[0].rect(NAME)
    h, w = first.h, first.w
    cap = {'T': T, 'h': h, 'w': w, 'frames': [int(f.frame) for f in seq],
           'jitter': np.array([int(f.jitter_index) for f in seq], dtype=np.int64),
           'lum': {k: np.zeros((T, h, w)) for k in IMAGES},
           'mean_rgb': {k: np.zeros((h, w, 3)) for k in IMAGES}}
    if records:
        cap.update({'cov': np.zeros((T, h, w), dtype=bool), 'max_layers': np.zeros((h, w)),
                    'sparse': [],
                    'o1': {'drawn_without_layer': 0, 'layer_without_draw': 0, 'non_integral_layers': 0},
                    'overlap_pixel_frames': 0, 'relevant_overlap_pixel_frames': 0,
                    'relevant_overlap': np.zeros((h, w), dtype=bool),
                    'g1': {'evaluated_pixel_frames': 0, 'failing_pixel_frames': 0,
                           'worst_relative': 0.0, 'worst_channel': None, 'worst_frame': None}})
    for t, f in enumerate(seq):
        r = f.rect(NAME)
        raw = None
        for k in IMAGES:
            rgb = finite(r.channel(k)[..., :3].astype(np.float64), f'{k} at frame {f.frame}')
            cap['lum'][k][t] = rgb @ REC709
            cap['mean_rgb'][k] += rgb / T
            if k == 'taa_raw':
                raw = rgb
        if not records:
            continue
        src = finite(r.channel('translucent_source').astype(np.float64),
                     f'translucent_source at frame {f.frame}')
        op = src[..., 3]
        drawn = op >= 0.0
        lay = finite(r.channel('translucent_layers')[..., 0].astype(np.float64),
                     f'translucent_layers at frame {f.frame}')
        cap['o1']['drawn_without_layer'] += int(np.count_nonzero(drawn & (lay < 1.0)))
        cap['o1']['layer_without_draw'] += int(np.count_nonzero(~drawn & (lay != 0.0)))
        cap['o1']['non_integral_layers'] += int(np.count_nonzero(lay != np.round(lay)))
        cap['cov'][t] = drawn
        cap['max_layers'] = np.maximum(cap['max_layers'], lay)
        multi = drawn & (lay >= 2.0)
        relevant = multi & (op < 1.0)
        cap['overlap_pixel_frames'] += int(np.count_nonzero(multi))
        cap['relevant_overlap_pixel_frames'] += int(np.count_nonzero(relevant))
        cap['relevant_overlap'] |= relevant
        idx = np.flatnonzero(drawn)
        rows = {ch: r.channel(ch).reshape(-1, 4)[idx].copy() for ch in TRANSLUCENT}
        cap['sparse'].append({'t': t, 'idx': idx, 'rows': rows})
        if not idx.size:
            continue
        # Identifier components are uint bits and may legitimately be NaN as floats; only the real
        # components are required to be finite.
        for ch in ('translucent_albedo', 'translucent_ibl', 'translucent_normal'):
            finite(rows[ch][:, :3], f'{ch} rgb at frame {f.frame}')
        finite(rows['translucent_emission'], f'translucent_emission at frame {f.frame}')
        keep = ~relevant.reshape(-1)[idx]
        s = src.reshape(-1, 4)[idx]
        o = np.clip(s[:, 3:4], 0.0, 1.0)
        comp = finite(composite_rgb(r).reshape(-1, 3)[idx], f'deferred composite at frame {f.frame}')
        pred = s[:, :3] * o + comp * (1.0 - o)
        act = raw.reshape(-1, 3)[idx]
        scale = np.maximum(np.maximum(np.abs(pred), np.abs(act)), G1_SCALE_FLOOR)
        rel = (np.abs(pred - act) / scale)[keep]
        g = cap['g1']
        g['evaluated_pixel_frames'] += int(rel.shape[0])
        if rel.size:
            g['failing_pixel_frames'] += int(np.count_nonzero((rel > G1_TOLERANCE).any(axis=1)))
            worst = float(rel.max())
            if worst > g['worst_relative']:
                g['worst_relative'] = worst
                g['worst_channel'] = 'RGB'[int(np.unravel_index(int(rel.argmax()), rel.shape)[1])]
                g['worst_frame'] = int(f.frame)
    return cap


# ------------------------------------------------------------------------------------------ C0
def floor_rule(values):
    """followup-09's floor: max(3 x the 95 % half-width, 5 % of the mean) over the repeats given."""
    v = np.asarray(values, dtype=np.float64)
    n = len(v)
    mean = float(v.mean())
    half = float(T_975.get(n, 1.96) * v.std(ddof=1) / np.sqrt(n)) if n >= 2 else 0.0
    margin = max(C0_FLOOR_HALF_WIDTHS * half, C0_FLOOR_FRACTION * abs(mean))
    return margin, {'n': n, 'mean': mean, 'half_width_95': half, 'margin': margin}


def judge(off_vals, on_vals, margin, relative):
    for v in list(off_vals) + list(on_vals) + [margin]:
        if not np.isfinite(v):
            raise Stop('C0', f'a C0 quantity is not finite ({v!r})')
    if relative:
        cross = [abs(b - a) / abs(a) for a in off_vals for b in on_vals]
        within = [crit.relative_gap(x, y) for vals in (off_vals, on_vals)
                  for x, y in itertools.combinations(vals, 2)]
    else:
        cross = [abs(b - a) for a in off_vals for b in on_vals]
        within = [abs(x - y) for vals in (off_vals, on_vals) for x, y in itertools.combinations(vals, 2)]
    spread = max(within) if within else 0.0
    if max(cross) <= margin:
        verdict = 'EQUIVALENT'
    elif min(cross) > margin and min(cross) > spread:
        verdict = 'DIFFERENT'
    else:
        verdict = 'INCONCLUSIVE'
    return {'off': list(off_vals), 'on': list(on_vals), 'margin': margin, 'relative': relative,
            'largest_difference': max(cross), 'smallest_difference': min(cross),
            'largest_spread_within_a_condition': spread, 'verdict': verdict}


def c0_equivalence(off, on, glass):
    if not off or not on:
        missing = [c for c, caps in (('records_off', off), ('records_on', on)) if not caps]
        return {'status': 'NOT RUN', 'reason': f'no captures for {missing}'}
    masks = {'region': np.ones((on[0]['h'], on[0]['w']), dtype=bool)}
    if glass.any():
        masks['glass'] = glass
    res = {'status': None, 'mask_pixels': {k: int(m.sum()) for k, m in masks.items()},
           'what_it_validates': 'the visual cost of the LIGHTING_EXTRA records, with [earlydepthstencil] '
                                'in both conditions; it does not isolate the effect of the attribute',
           'quantities': {}}
    for image in IMAGES:
        for mname, m in masks.items():
            for c, cname in enumerate('RGB'):
                vo = [float(cap['mean_rgb'][image][m][:, c].mean()) for cap in off]
                vn = [float(cap['mean_rgb'][image][m][:, c].mean()) for cap in on]
                res['quantities'][f'{image} / {mname} / energy {cname}'] = judge(vo, vn, C0_ENERGY_RELATIVE, True)
            vo = [crit.oscillation(cap['lum'][image][:, m]) for cap in off]
            vn = [crit.oscillation(cap['lum'][image][:, m]) for cap in on]
            margin, rule = floor_rule(vo)
            q = judge(vo, vn, margin, False)
            q['margin_rule'] = rule
            res['quantities'][f'{image} / {mname} / oscillation'] = q
    verdicts = [q['verdict'] for q in res['quantities'].values()]
    if all(v == 'EQUIVALENT' for v in verdicts):
        res['status'] = 'PASSED'
    elif 'DIFFERENT' in verdicts:
        res['status'] = 'NOT EQUIVALENT'
    else:
        res['status'] = 'INCONCLUSIVE'
    return res


# ------------------------------------------------------------------------------------- reading
def per_pixel_values(cap, pixels, channel, comp, kind):
    """(T, P, C) float64 with NaN where the pass did not draw, or for identifiers (T, P) uint32
    bits; plus the (T, P) drawn mask."""
    T, P = cap['T'], len(pixels)
    pos = np.full(cap['h'] * cap['w'], -1, dtype=np.int64)
    pos[pixels] = np.arange(P)
    if kind == 'id':
        vals = np.zeros((T, P), dtype=np.uint32)
    else:
        vals = np.full((T, P, comp.stop - comp.start), np.nan)
    drawn = np.zeros((T, P), dtype=bool)
    for fr in cap['sparse']:
        col = pos[fr['idx']]
        sel = col >= 0
        if not sel.any():
            continue
        rows = fr['rows'][channel][sel]
        drawn[fr['t'], col[sel]] = True
        if kind == 'id':
            vals[fr['t'], col[sel]] = np.ascontiguousarray(rows[:, comp.start]).view(np.uint32)
        else:
            vals[fr['t'], col[sel]] = rows[:, comp].astype(np.float64)
    return vals, drawn


def varies(vals, drawn, kind, frame_sel=None):
    """Per pixel: does it vary over its drawn frames (in `frame_sel`), and its relative range."""
    if frame_sel is not None:
        vals, drawn = vals[frame_sel], drawn[frame_sel]
    any_drawn = drawn.any(axis=0)
    if kind == 'id':
        lo = np.where(drawn, vals, np.iinfo(np.uint32).max).min(axis=0)
        hi = np.where(drawn, vals, 0).max(axis=0)
        return any_drawn & (lo != hi), None
    with warnings.catch_warnings(), np.errstate(invalid='ignore'):
        warnings.simplefilter('ignore', RuntimeWarning)
        lo = np.nanmin(vals, axis=0)
        hi = np.nanmax(vals, axis=0)
        mag = np.maximum(np.maximum(np.abs(lo), np.abs(hi)), R_MAGNITUDE_FLOOR)
        rel = np.where(any_drawn[:, None], (hi - lo) / mag, 0.0).max(axis=1)
    return any_drawn & (rel > R_CONSTANT), rel


def read_quantity(on, pixels, channel, comp, kind):
    n = len(pixels)
    var_any = np.zeros(n, dtype=bool)
    locked = np.ones(n, dtype=bool)
    worst, distinct = 0.0, set()
    for cap in on:
        vals, drawn = per_pixel_values(cap, pixels, channel, comp, kind)
        v, rel = varies(vals, drawn, kind)
        var_any |= v
        for j in np.unique(cap['jitter']):
            vj, _ = varies(vals, drawn, kind, frame_sel=(cap['jitter'] == j))
            locked &= ~vj
        if kind == 'id':
            distinct.update(int(x) for x in np.unique(vals[drawn]))
        elif drawn.any():
            worst = max(worst, float(rel[drawn.any(axis=0)].max()))
    res = {'pixels': n, 'constant': bool(not var_any.any()), 'varying_pixels': int(var_any.sum()),
           'varying_pixels_locked_to_jitter_index': int((var_any & locked).sum())}
    if kind == 'id':
        res['distinct_values'] = sorted(distinct)[:16]
        res['distinct_count'] = len(distinct)
    else:
        res['worst_relative_range'] = worst
    return res


def read_coverage(on, always, never, some):
    res = {'always': int(always.sum()), 'never': int(never.sum()), 'some': int(some.sum())}
    if some.any():
        locked = []
        for cap in on:
            cov = cap['cov'][:, some]
            ok = np.ones(cov.shape[1], dtype=bool)
            for j in np.unique(cap['jitter']):
                sel = cov[cap['jitter'] == j]
                ok &= (sel == sel[0]).all(axis=0)
            locked.append(int(ok.sum()))
        res['some_locked_to_jitter_index_per_repeat'] = locked
        ref = on[0]['cov'][:, some]
        res['some_pixel_frames_differing_from_repeat_0'] = [
            float(np.mean(cap['cov'][:, some] != ref)) for cap in on[1:]]
    return res


# ------------------------------------------------------------------------------------------ run
def run(args, report):
    runner.require_exe()
    works = {c: [] for c in CONDITIONS}
    if args.reuse:
        for c in CONDITIONS:
            k = 0
            while os.path.isdir(os.path.join(args.out, f'{c}_r{k}', 'region')):
                works[c].append(os.path.join(args.out, f'{c}_r{k}'))
                k += 1
    else:
        for k in range(args.repeats):
            for c, fields in CONDITIONS.items():
                work = os.path.join(args.out, f'{c}_r{k}')
                print(f'  ... {c} repeat {k}', flush=True)
                res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', PROJECT),
                                         ANALYSIS_LAST - SCENE_START + 8, regions=[ROI],
                                         region_fields=fields, region_first=FIRST,
                                         region_last=ANALYSIS_LAST, cvars=BASE, timing_first=FIRST,
                                         clean=True)
                if res['exit_code'] != 0:
                    raise Stop('capture', f'{c} repeat {k}: exit {res["exit_code"]}')
                works[c].append(work)
    report['captures'] = {c: [{'dir': w, 'identity': runner.capture_identity(w)} for w in ws]
                          for c, ws in works.items()}
    counts = {c: len(ws) for c, ws in works.items()}
    if any(n != args.repeats for n in counts.values()):
        raise Stop('repeats', f'expected {args.repeats} captures per condition, found {counts}')
    hashes = {str(e['identity'].get('executable', {}).get('hash'))
              for ws in report['captures'].values() for e in ws}
    report['executable_hashes'] = sorted(hashes)
    if len(hashes) != 1:
        raise Stop('identity', f'the captures do not share one binary: {sorted(hashes)}')

    print('  loading captures', flush=True)
    off = [load_capture(os.path.join(w, 'region'), records=False) for w in works['records_off']]
    on = [load_capture(os.path.join(w, 'region'), records=True) for w in works['records_on']]
    ref = on[0]
    for cap in off + on:
        if cap['frames'] != ref['frames'] or not np.array_equal(cap['jitter'], ref['jitter']) \
                or (cap['h'], cap['w']) != (ref['h'], ref['w']):
            raise Stop('alignment', 'captures differ in frames, jitter sequence or rectangle size')

    glass = np.logical_or.reduce([cap['cov'].any(axis=0) for cap in on])

    # ---- C0 ----------------------------------------------------------------------------------
    c0 = c0_equivalence(off, on, glass)
    report['C0'] = c0
    print(f'\n  C0 equivalence, records off vs on — {c0["status"]}')
    for key, q in c0.get('quantities', {}).items():
        unit = 'rel' if q['relative'] else 'abs'
        print(f'    {key:<34} largest diff {q["largest_difference"]:.2e} ({unit}) margin {q["margin"]:.2e} '
              f'spread within {q["largest_spread_within_a_condition"]:.2e}  {q["verdict"]}')
    if c0['status'] != 'PASSED':
        meaning = {
            'NOT EQUIVALENT': 'the image with the records differs from the image without them beyond a '
                              'pre-set margin',
            'INCONCLUSIVE': 'equivalence was not shown within the pre-set margins; this does not say '
                            'the records changed the image',
            'NOT RUN': 'a condition is missing'}[c0['status']]
        raise Stop('C0', f'{c0["status"]}: {meaning}. The reading is not computed.')

    # ---- G0 ----------------------------------------------------------------------------------
    drawn_any = [int(cap['cov'].any(axis=0).sum()) for cap in on]
    report['G0'] = {'pixels_drawn_at_least_once_per_repeat': drawn_any}
    print(f'\n  G0 pixels drawn at least once, per repeat: {drawn_any}')
    if min(drawn_any) == 0:
        raise Stop('G0', 'the translucent pass drew no pixel of the region in at least one repeat')

    # ---- O1 / O2 -----------------------------------------------------------------------------
    report['O1'] = [cap['o1'] for cap in on]
    print(f'  O1 layer record consistency, per repeat: {report["O1"]}')
    if any(v for o in report['O1'] for v in o.values()):
        raise Stop('O1', 'the layer record disagrees with the coverage record; the records are not trusted')
    excluded = np.logical_or.reduce([cap['relevant_overlap'] for cap in on])
    ys, xs = np.nonzero(excluded)
    report['O2'] = {'pixel_frames_with_two_or_more_layers': [cap['overlap_pixel_frames'] for cap in on],
                    'relevant_overlap_pixel_frames': [cap['relevant_overlap_pixel_frames'] for cap in on],
                    'max_layers_seen': float(max(cap['max_layers'].max() for cap in on)),
                    'excluded_pixels': int(excluded.sum()),
                    'excluded_pixel_coordinates': [[int(x), int(y)] for x, y in zip(xs[:64], ys[:64])]}
    print(f'  O2 overlap: pixel-frames with >= 2 layers {report["O2"]["pixel_frames_with_two_or_more_layers"]}, '
          f'relevant (last layer opacity < 1) {report["O2"]["relevant_overlap_pixel_frames"]}, '
          f'max layers {report["O2"]["max_layers_seen"]:g}, excluded pixels {int(excluded.sum())}')
    if excluded.any():
        report['restrictions'].append(f'{int(excluded.sum())} pixels with relevant overlap are excluded '
                                      'from G1 and from the reading')

    # ---- G1 ----------------------------------------------------------------------------------
    report['G1'] = [cap['g1'] for cap in on]
    worst = max(g['worst_relative'] for g in report['G1'])
    failing = sum(g['failing_pixel_frames'] for g in report['G1'])
    print(f'  G1 per RGB channel: worst relative {worst:.2e}, failing pixel-frames {failing} of '
          f'{sum(g["evaluated_pixel_frames"] for g in report["G1"])}')
    if failing or worst > G1_TOLERANCE:
        raise Stop('G1', f'the records do not reproduce taa_raw per channel (worst {worst:.2e} > '
                         f'{G1_TOLERANCE:g}); they are not what produced the image, so the reading is '
                         'not computed')

    # ---- R: the reading ----------------------------------------------------------------------
    drawn_every = np.logical_and.reduce([cap['cov'].all(axis=0) for cap in on])
    always = drawn_every & ~excluded
    never = ~glass
    some = glass & ~drawn_every & ~excluded
    reading = {'coverage': read_coverage(on, always, never, some), 'quantities': {}, 'factors': {}}
    print(f'\n  R coverage: always {int(always.sum())}, never {int(never.sum())}, some {int(some.sum())}')
    if some.any():
        cv = reading['coverage']
        print(f'    "some" pixels whose coverage repeats with the jitter index, per repeat: '
              f'{cv["some_locked_to_jitter_index_per_repeat"]} of {int(some.sum())}; pixel-frames '
              f'differing from repeat 0: {cv["some_pixel_frames_differing_from_repeat_0"]}')
    for cname, mask in (('always', always), ('some', some)):
        pixels = np.flatnonzero(mask)
        if not pixels.size:
            continue
        rows = {}
        print(f'    {cname} ({pixels.size} px):')
        for label, factor, channel, comp, kind in READ:
            q = read_quantity(on, pixels, channel, comp, kind)
            q['factor'] = factor
            rows[label] = q
            detail = (f'distinct {q["distinct_count"]}' if kind == 'id'
                      else f'worst relative range {q["worst_relative_range"]:.2e}')
            print(f'      {factor:<16} {label:<22} {"constant" if q["constant"] else "VARIES":<9} '
                  f'varying px {q["varying_pixels"]:>4} (jitter-locked '
                  f'{q["varying_pixels_locked_to_jitter_index"]:>4})  {detail}')
        reading['quantities'][cname] = rows
        verdicts = {'coverage': 'varies' if some.any() else 'constant'}
        for factor in FACTORS:
            entries = [q for q in rows.values() if q['factor'] == factor]
            verdicts[factor] = 'constant' if all(q['constant'] for q in entries) else 'varies'
        if verdicts['produced colour'] == 'varies' and verdicts['material'] == 'constant' \
                and verdicts['lighting'] == 'constant':
            verdicts['deduction'] = ('the produced colour varies while every recorded input is constant: '
                                     'the unrecorded constant-buffer terms (light colour, ambient) are '
                                     'implicated, by deduction, not measurement')
        reading['factors'][cname] = verdicts
    report['reading'] = reading

    # ---- S5 ----------------------------------------------------------------------------------
    classes = {'region': np.ones_like(never), 'always drawn': always, 'never drawn': never,
               'some frames drawn': some}
    if excluded.any():
        classes['excluded (relevant overlap)'] = excluded
    stds = {k: [cap['lum'][k].std(axis=0) for cap in on] for k in IMAGES}
    s5 = {'records_off_region': {k: max(crit.oscillation(cap['lum'][k]) for cap in off) for k in IMAGES}}
    print('\n  S5 oscillation (worst repeat) and energy, per class:')
    for label, m in classes.items():
        if not m.any():
            continue
        row = {'pixels': int(m.sum())}
        for k in IMAGES:
            row[f'{k}_oscillation'] = max(crit.oscillation(cap['lum'][k][:, m]) for cap in on)
            row[f'{k}_energy'] = float(np.mean([cap['lum'][k][:, m].mean() for cap in on]))
            row[f'{k}_share_of_summed_std'] = float(np.mean([s[m].sum() / s.sum() for s in stds[k]]))
        s5[label] = row
        print(f'    {label:<28} {row["pixels"]:>6} px  before TAA {row["taa_raw_oscillation"]:.3e} '
              f'(share {row["taa_raw_share_of_summed_std"]:.1%})  after TAA {row["taa_output_oscillation"]:.3e} '
              f'(share {row["taa_output_share_of_summed_std"]:.1%})  energy {row["taa_output_energy"]:.3e}')
    if some.any():
        on_l = np.concatenate([cap['lum']['taa_raw'][:, some][cap['cov'][:, some]] for cap in on])
        off_l = np.concatenate([cap['lum']['taa_raw'][:, some][~cap['cov'][:, some]] for cap in on])
        s5['some frames drawn']['taa_raw_mean_when_drawn'] = float(on_l.mean())
        s5['some frames drawn']['taa_raw_mean_when_not_drawn'] = float(off_l.mean())
        print(f'    at "some" pixels, taa_raw luminance averages {on_l.mean():.3e} when the glass is drawn '
              f'and {off_l.mean():.3e} when it is not')
    report['S5'] = s5


def _json_default(o):
    if isinstance(o, np.bool_):
        return bool(o)
    if isinstance(o, np.integer):
        return int(o)
    if isinstance(o, np.floating):
        return float(o)
    if isinstance(o, np.ndarray):
        return o.tolist()
    return str(o)


def _write(args, report):
    os.makedirs(args.out, exist_ok=True)
    path = runner.report_path(args.out, 'door_reading.json', args.reuse)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2, default=_json_default)
    stopped = f', stopped at {report["stopped_at"]}' if report['stopped_at'] else ''
    print(f'\n  -> {path}  (status: {report["status"]}{stopped})')


def main():
    global ANALYSIS_LAST
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '35-door-reading'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    ANALYSIS_LAST = FIRST + args.frames - 1
    print(f'rear_door reading — frames {FIRST}..{ANALYSIS_LAST}, {args.repeats} repeats per condition, '
          f'conditions {CONDITIONS}')
    report = {'status': 'incomplete', 'stopped_at': None, 'reasons': [], 'restrictions': [],
              'roi': ROI, 'frames': [FIRST, ANALYSIS_LAST], 'repeats': args.repeats,
              'conditions': CONDITIONS, 'reuse': args.reuse, 'harness': runner.harness_stamp(),
              'criteria': {'C0_energy_relative': C0_ENERGY_RELATIVE, 'C0_floor_fraction': C0_FLOOR_FRACTION,
                           'C0_floor_half_widths': C0_FLOOR_HALF_WIDTHS, 'G1_tolerance': G1_TOLERANCE,
                           'G1_scale_floor': G1_SCALE_FLOOR, 'R_constant': R_CONSTANT,
                           'R_magnitude_floor': R_MAGNITUDE_FLOOR},
              'C0': {'status': 'NOT RUN'}}
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
        if report['C0'].get('status') == 'NOT RUN' and 'reason' not in report['C0']:
            report['C0']['reason'] = f'the study stopped before C0, at {report["stopped_at"]}'
        _write(args, report)
    return code


if __name__ == '__main__':
    raise SystemExit(main())
