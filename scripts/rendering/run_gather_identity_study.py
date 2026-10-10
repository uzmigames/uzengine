"""H-31 step 1: what the gather read and what it sampled, frame by frame.

The alternation is between odd and even frames. Before anything is frozen, this asks which of the
gather's inputs carries that parity — and specifically whether the *content* it reads alternates or
the *selection* of what it reads does.

Every quantity is judged by the same statistic the input was judged by, so they are comparable:
per pixel, are the 64 odd-frame values and the 64 even-frame values in disjoint ranges? That is a
threshold-free separation. Alongside it, the relative gap between the two group means says whether
a separation is also large enough to matter.

  selection   the resolved world position, the ray origin, the geometric normal, the hash seed, the
              first ray direction, the sample count
  resources   the clipmap, raw clipmap, probe-volume and sky SRV indices, and the accumulator's
              history SRV, history-geometry SRV and output UAV — the ping-pong index is not a
              shader input but it shows itself in those three
  content     a probe of the same scene data placed out of the jitter's reach: the world position
              snapped to a fixed grid, traced along a fixed world axis. This is only evidence for
              the pixels whose snapped origin is genuinely constant across the sequence, so those
              are counted and the rest excluded — a control that moved is not a control.

What it cannot settle: a resource whose SRV index is stable can still have had its contents
rewritten. The probe is what addresses that, and the phase test in step 2 is what settles it.

Usage: python scripts/rendering/run_gather_identity_study.py [--frames 128]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
WARMUP = 200
FIRST = SCENE_START + WARMUP
JITTER_PERIOD = 8
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': JITTER_PERIOD}
ROI = ('shadow_wall', 150, 260, 64, 64)
REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


def luma(rgb):
    return rgb @ REC709


def parity_split(values, frames):
    """Per-pixel separation of odd from even frames.

    `values` is (T, ...) float. Returns the fraction of pixels whose odd and even ranges do not
    overlap, and the relative gap between the group means over the whole region.
    """
    odd = values[frames % 2 == 1]
    even = values[frames % 2 == 0]
    if odd.size == 0 or even.size == 0:
        return 0.0, 0.0
    disjoint = (odd.min(axis=0) > even.max(axis=0)) | (even.min(axis=0) > odd.max(axis=0))
    scale = max(abs(float(odd.mean())), abs(float(even.mean())), 1e-12)
    gap = abs(float(odd.mean()) - float(even.mean())) / scale
    return float(disjoint.mean()), gap


def bits(channel_component):
    return channel_component.copy().view(np.uint32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '19-gather-identity'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    runner.require_exe()

    name, x0, y0, w, h = ROI
    last = FIRST + args.frames - 1
    work = os.path.join(args.out, 'capture')
    print(f'H-31 step 1 — resource and sample identity, frames {FIRST}..{last}, ROI {ROI}')
    if not args.reuse:
        res = runner.run_capture(work, os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject'),
                                 last - SCENE_START + 8, regions=[ROI], region_fields='gather',
                                 region_first=FIRST, region_last=last, cvars=BASE,
                                 timing_first=FIRST, clean=True)
        if res['exit_code'] != 0:
            raise RuntimeError(f'engine exited {res["exit_code"]}; log {res["log_path"]}')
    seq = regiondump.load_sequence(os.path.join(work, 'region'), FIRST, last)
    frames = np.array([f.frame for f in seq])

    # the gather lattice: one sample per gather pixel, taken at the even full pixel of its block
    ys = np.arange(0, h, 2)
    xs = np.arange(0, w, 2)

    def ch(f, cname):
        return f.rect(name).channel(cname)[np.ix_(ys, xs)]

    stack = {}
    for cname in ('gather_current', 'gather_world_pos', 'gather_origin', 'gather_geo_normal',
                  'gather_first_ray', 'gather_srv_a', 'gather_srv_b', 'gather_probe_origin',
                  'gather_probe', 'gather_accum_srv', 'gather_probe_volume'):
        stack[cname] = np.stack([ch(f, cname) for f in seq])

    print(f'\n  {"quantity":<34}{"disjoint by parity":>20}{"relative gap":>16}')
    rows = []

    def report(label, values, note=''):
        d, g = parity_split(np.asarray(values, dtype=np.float64), frames)
        print(f'  {label:<34}{d:>19.1%}{g:>16.3e}  {note}')
        rows.append({'quantity': label, 'disjoint_fraction': d, 'relative_gap': g, 'note': note})
        return d, g

    # ---- the thing being explained ----------------------------------------
    report('input C (what alternates)', luma(stack['gather_current'][..., :3]))

    # ---- selection ---------------------------------------------------------
    report('resolved world position x', stack['gather_world_pos'][..., 0])
    report('resolved world position y', stack['gather_world_pos'][..., 1])
    report('resolved world position z', stack['gather_world_pos'][..., 2])
    report('depth at that position', stack['gather_world_pos'][..., 3])
    report('ray origin x', stack['gather_origin'][..., 0])
    report('geometric normal x', stack['gather_geo_normal'][..., 0])
    report('first ray direction x', stack['gather_first_ray'][..., 0])
    report('first ray direction y', stack['gather_first_ray'][..., 1])
    report('first ray direction z', stack['gather_first_ray'][..., 2])

    seeds = bits(stack['gather_geo_normal'][..., 3])
    seed_changes = int(np.sum(np.any(np.diff(seeds.astype(np.int64), axis=0) != 0, axis=(1, 2))))
    print(f'\n  hash seed changes on {seed_changes} of {len(seq) - 1} frame steps '
          f'(it is pixel + frame * 747796405, so every frame is expected)')

    # ---- resources ---------------------------------------------------------
    print('\n  resource identity — distinct values seen, by frame parity:')
    odd_m, even_m = frames % 2 == 1, frames % 2 == 0
    resource_rows = {}
    for label, cname, comp in (
            ('clipmap SRV 0', 'gather_srv_a', 1), ('clipmap SRV 1', 'gather_srv_a', 2),
            ('clipmap SRV 2', 'gather_srv_a', 3), ('clipmap SRV 3', 'gather_srv_b', 0),
            ('probe volume SRV', 'gather_srv_b', 1), ('sky SRV', 'gather_srv_b', 2),
            ('raw clipmap SRV 0', 'gather_srv_b', 3),
            ('accum history SRV', 'gather_accum_srv', 0),
            ('accum history-geom SRV', 'gather_accum_srv', 1),
            ('accum output UAV', 'gather_accum_srv', 2),
            ('accum history valid', 'gather_accum_srv', 3)):
        v = bits(stack[cname][..., comp])
        o = sorted(set(v[odd_m].ravel().tolist()))
        e = sorted(set(v[even_m].ravel().tolist()))
        alternates = o != e
        resource_rows[label] = {'odd': o[:6], 'even': e[:6], 'alternates_by_parity': alternates}
        mark = '  <-- alternates' if alternates else ''
        print(f'    {label:<26} odd {str(o[:4]):<26} even {str(e[:4]):<26}{mark}')

    # ---- content -----------------------------------------------------------
    snapped = stack['gather_probe_origin'][..., :3]
    constant = np.all(snapped == snapped[0], axis=(0, 3))
    n_const = int(constant.sum())
    total = constant.size
    print(f'\n  jitter-independent probe: its snapped origin is constant across all {len(seq)} '
          f'frames for {n_const} of {total} gather pixels ({n_const / total:.1%})')
    if n_const:
        probe = luma(stack['gather_probe'][..., :3])[:, constant]
        d, g = parity_split(probe, frames)
        print(f'  {"probe radiance (content)":<34}{d:>19.1%}{g:>16.3e}  '
              f'over the {n_const} pixels whose origin held')
        rows.append({'quantity': 'probe radiance (content)', 'disjoint_fraction': d,
                     'relative_gap': g, 'note': f'{n_const} constant-origin pixels'})
        pm = float(probe.mean())
        hit = stack['gather_probe_volume'][..., 3][:, constant]
        print(f'    traced probe mean radiance {pm:.6e}, and it hit geometry on {float(hit.mean()):.1%} '
              f'of pixel-frames; a ray only reaches the probe volume on a hit')
        vol = luma(stack['gather_probe_volume'][..., :3])[:, constant]
        dv, gv = parity_split(vol, frames)
        print(f'  {"probe volume read directly":<34}{dv:>19.1%}{gv:>16.3e}  '
              f'mean {float(vol.mean()):.6e}')
        rows.append({'quantity': 'probe volume read directly', 'disjoint_fraction': dv,
                     'relative_gap': gv, 'note': f'mean {float(vol.mean()):.6e}'})
    else:
        print('  the snapped origin moved everywhere; the probe is not a usable control here')

    report_path = os.path.join(args.out, 'identity.json')
    os.makedirs(args.out, exist_ok=True)
    with open(report_path, 'w', encoding='utf-8') as fh:
        json.dump({'first_frame': FIRST, 'frames': args.frames, 'roi': ROI,
                   'parity': rows, 'resources': resource_rows,
                   'probe_constant_origin_pixels': n_const, 'gather_pixels': total},
                  fh, indent=2)
    print(f'\n  -> {report_path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
