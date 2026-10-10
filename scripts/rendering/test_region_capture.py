"""Behavioural harness for the region-dump instrumentation (`--region-dump`, `--regions`,
`--run-manifest`). It runs the engine and checks what the GPU actually wrote, not the source text.

Checks, in the order the followup-09 checklist asks for them:

1. Frame integrity — every file's GPU stamps equal the frame the CPU read back, the requested
   range is complete (including the frames still in flight at shutdown), with no duplicates.
2. Multi-ROI layout — each rectangle lands at its own offset with its own stamps and geometry.
3. Field semantics — the records hold linear HDR: changing `r.Exposure` must not move them. Judged
   on the run aggregate (mean |delta| against the records' own mean, `UNCHANGED_FRACTION`); the
   per-value maximum and the floor of a repeated identical run are reported next to it (negative
   control).
4. Jitter determinism — the raw centre sample repeats with the jitter period and differs between
   neighbouring frames (so "the same jitter index gives the same image" is measured, not assumed).
5. Instrumentation neutrality — with and without the dump the presented image is indistinguishable
   from process-to-process variation, over the whole frame and over each dumped rectangle, judged on
   NEUTRALITY_REPEATS interleaved processes per condition (since 2026-10-09; one pair on a fixed
   bound before, see NEUTRALITY_REPEATS); the frame-time cost is reported from both manifests.
6. Missing-source guard — with `r.TAA=0` the resolve never writes its records, the files are
   flagged, and the reader refuses them instead of returning last frame's values.

Checks 3 and 5 compare separate processes, which are not bit-reproducible (ledger, "The renderer is
not bit-reproducible across processes"), so neither asserts per-value identity (2026-10-08).

Usage: python scripts/rendering/test_region_capture.py [--keep] [--quick]
Each check prints `ok`/`FAIL`; the exit code is non-zero if any failed.
"""
from __future__ import annotations

import itertools
import os
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import criteria as crit  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w-seq.uzproject')
# Pose W holds still until frame 337; measure inside that window.
FIRST, LAST = 60, 75
RECTS = [('window', 770, 150, 96, 128), ('door', 300, 500, 48, 48), ('sky', 900, 40, 32, 32)]

#: A run aggregate (mean |delta| over every value compared) below this fraction of the signal's own
#: mean counts as unchanged. Two processes of one configuration can land in states whose records
#: differ by up to ~1e-2 (docs/analysis/rendering-arch/flicker/flicker-hypothesis-ledger.md, "The renderer is not
#: bit-reproducible across processes"), so a per-value maximum judged against ONE repeat pair
#: failed when that pair happened to agree exactly and the compared pair did not (22/23 on 2026-10-05,
#: docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-a-2026-10-05.md). On the window rectangle the cross-process
#: aggregate measured at most 1.6e-6 of the mean, a pre-exposed record would move by 0.94 of it, and
#: the 2026-10-05 merge and A.10 builds differed by 1.5e-3 on the presented frame: numbers in the
#: followup-09 notes, "Closing review (2026-10-08)".
UNCHANGED_FRACTION = 1e-3

#: Check 5 is judged against repeats since 2026-10-09 (followup-09 close-out, amendment registered
#: before its run): NEUTRALITY_REPEATS processes with the dump and as many without, interleaved, by the
#: rule the programme adopted on 2026-10-08 for its other equivalence claims (`indistinguishable()` in
#: scripts/rendering/run_display_study.py): per region, every cross pair's max |delta| inside the repeats' own
#: spread (INDISTINGUISHABLE), the mean |delta| not SEPARATED, and a repeat spread under
#: NEUTRALITY_POWER of the level (else the check could not have detected anything). The single-pair
#: aggregate against UNCHANGED_FRACTION, the rule before, failed on 2026-10-09 (Debug build of 2cc2e85c:
#: frame 0.013 code values on 5.14, 2.5e-3 of the level) with no floor measured in this setting to
#: judge it against; it is still printed, not judged.
NEUTRALITY_REPEATS = 3
NEUTRALITY_POWER = 0.02

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)
    return ok


def capture(work, name, **kw):
    kw.setdefault('regions', RECTS)
    kw.setdefault('region_first', FIRST)
    kw.setdefault('region_last', LAST)
    kw.setdefault('frames', LAST - 30)
    kw.setdefault('timing_first', 45)
    print(f'  … capturing {name}', flush=True)
    res = runner.run_capture(os.path.join(work, name), PROJECT, **kw)
    if res['exit_code'] != 0:
        check(False, f'{name}: engine exited {res["exit_code"]}', res['log'])
    return res


def output_stack(region_dir, rect='window', channel='taa_output', first=FIRST, last=LAST):
    frames = regiondump.load_sequence(region_dir, first, last)
    return frames, np.stack([f.rect(rect).channel(channel)[..., :3] for f in frames])


def moved(delta_mean, level, fraction=UNCHANGED_FRACTION):
    """True when a run-aggregate |delta| counts as a change: at least `fraction` of the level it is
    judged against. Any change of a region whose level is 0 counts, and a non-finite value counts as
    moved, so an unmeasurable comparison never passes as "unchanged"."""
    if not crit.is_measurable(delta_mean, level):
        return True
    if level <= 0.0:
        return delta_mean > 0.0
    return crit.exceeds_scale(delta_mean, level, fraction)


def exposure_control(base, repeat, dark, bright):
    """Check 3 on four captures (run_capture results, or retained work dirs re-read without the
    engine): returns (unchanged, detail, energy ratio bright/dark)."""
    _, s_dark = output_stack(dark['region_dir'])
    _, s_bright = output_stack(bright['region_dir'])
    _, s_base = output_stack(base['region_dir'])
    _, s_repeat = output_stack(repeat['region_dir'])
    floor = np.abs(s_base - s_repeat)
    delta = np.abs(s_dark - s_bright)
    scale = max(float(np.abs(s_dark).mean()), float(np.abs(s_bright).mean()))
    delta_mean = float(delta.mean())
    ratio = float(np.abs(s_bright).mean() / max(1e-9, np.abs(s_dark).mean()))
    share = delta_mean / scale if scale > 0.0 else float('inf')
    detail = (f'mean |delta| {delta_mean:.3g} = {share:.2g} of the mean record {scale:.4g} '
              f'(bound {UNCHANGED_FRACTION:g}); max |delta| {float(delta.max()):.6g}; repeat-run floor '
              f'max {float(floor.max()):.6g}, mean {float(floor.mean()):.3g} (reported, not judged)')
    return not moved(delta_mean, scale), detail, ratio


def png_pair(run_a, run_b):
    """The screenshot frames both runs wrote, as two (frames, h, w, 3) arrays of code values."""
    a_all, b_all = [], []
    for n in range(FIRST + 2, FIRST + 5):
        a = os.path.join(run_a['frames_dir'], f'f_{n}.png')
        b = os.path.join(run_b['frames_dir'], f'f_{n}.png')
        if os.path.exists(a) and os.path.exists(b):
            a_all.append(runner.png_array(a).astype(np.int16))
            b_all.append(runner.png_array(b).astype(np.int16))
    if not a_all:
        return None, None
    return np.stack(a_all), np.stack(b_all)


def png_stack(run):
    """The screenshot frames FIRST+2..FIRST+4 of one run as (frames, h, w, 3) code values, or None."""
    frames = []
    for n in range(FIRST + 2, FIRST + 5):
        p = os.path.join(run['frames_dir'], f'f_{n}.png')
        if not os.path.exists(p):
            return None
        frames.append(runner.png_array(p).astype(np.int16))
    return np.stack(frames)


def display_neutrality_repeats(withs, withouts):
    """Check 5 on repeated captures (NEUTRALITY_REPEATS): returns (unchanged, detail). Per region, the
    max |delta| of every cross pair must sit inside the repeats' own spread, the mean |delta| must not
    be SEPARATED, and the repeats' mean spread must stay under NEUTRALITY_POWER of the level (a region
    is judged against the larger of its own level and the frame's, as in `display_neutrality`)."""
    a = [png_stack(r) for r in withs]
    b = [png_stack(r) for r in withouts]
    if any(x is None for x in a + b):
        return False, 'a run wrote no screenshot for one of the compared frames'
    frame_level = max(float(x.mean()) for x in a + b)

    def dist(p, q):
        d = np.abs(p - q)
        return float(d.max()), float(d.mean())

    unchanged, parts = True, []
    for name, x, y, w, h in [('frame', 0, 0, a[0].shape[2], a[0].shape[1])] + RECTS:
        ra = [s[:, y:y + h, x:x + w] for s in a]
        rb = [s[:, y:y + h, x:x + w] for s in b]
        within = [dist(r[i], r[j]) for r in (ra, rb) for i, j in itertools.combinations(range(len(r)), 2)]
        between = [dist(p, q) for p in ra for q in rb]
        level = max(max(float(s.mean()) for s in ra + rb), frame_level)
        v_max = crit.spread_verdict([d[0] for d in between], [d[0] for d in within])
        v_mean = crit.spread_verdict([d[1] for d in between], [d[1] for d in within])
        powered = max(d[1] for d in within) < NEUTRALITY_POWER * level
        unchanged = unchanged and v_max == 'INDISTINGUISHABLE' and v_mean != 'SEPARATED' and powered
        parts.append(f'{name}: max {max(d[0] for d in between):g} vs repeats {max(d[0] for d in within):g} '
                     f'-> {v_max}; mean {min(d[1] for d in between):.2g}..{max(d[1] for d in between):.2g} vs '
                     f'repeats ..{max(d[1] for d in within):.2g} on {level:.3g} -> {v_mean}'
                     + ('' if powered else ' (no power)'))
    return unchanged, '; '.join(parts)


def png_delta(run_a, run_b):
    """Max channel delta of each screenshot frame the two runs share."""
    a, b = png_pair(run_a, run_b)
    return [] if a is None else [int(np.abs(x - y).max()) for x, y in zip(a, b)]


def display_neutrality(with_dump, without):
    """Check 5 on two captures: returns (unchanged, detail). Judged over the whole frame and over each
    dumped rectangle, since a side effect of the dump would show first where it writes. A rectangle
    is judged against the larger of its own level and the frame's: at exposure 1.0 the door and sky
    rectangles are black, and one flipped code value there is not a 100 % change."""
    a, b = png_pair(with_dump, without)
    if a is None:
        return False, 'no screenshot frame written by both runs'
    frame_level = max(float(a.mean()), float(b.mean()))
    unchanged, parts = True, []
    for name, x, y, w, h in [('frame', 0, 0, a.shape[2], a.shape[1])] + RECTS:
        ra, rb = a[:, y:y + h, x:x + w], b[:, y:y + h, x:x + w]
        delta_mean = float(np.abs(ra - rb).mean())
        level = max(float(ra.mean()), float(rb.mean()), frame_level)
        unchanged = unchanged and not moved(delta_mean, level)
        parts.append(f'{name} {delta_mean:.2g} on {level:.3g}')
    per_frame = [int(np.abs(x - y).max()) for x, y in zip(a, b)]
    identical = ' (bit-identical)' if not any(per_frame) else ''
    return unchanged, (f'mean |delta| on the mean level, code values: {", ".join(parts)} '
                       f'(bound {UNCHANGED_FRACTION:g} of the level); max channel delta per frame '
                       f'{per_frame}{identical}')


def main():
    runner.require_exe()
    keep = '--keep' in sys.argv
    work = tempfile.mkdtemp(prefix='uz_region_capture_')
    print(f'work dir: {work}')
    try:
        base = capture(work, 'base', cvars={'r.TAA.JitterSequenceLength': 8})
        repeat = capture(work, 'repeat', cvars={'r.TAA.JitterSequenceLength': 8})

        # 1 — frame integrity
        print('1. frame integrity')
        report = regiondump.check_sequence(base['region_dir'], FIRST, LAST)
        check(report['ok'], 'every requested frame present, unique and stamp-matched',
              f'count={report["count"]} missing={report["missing"]} invalid={report["invalid"][:3]}')
        check(report['count'] == LAST - FIRST + 1,
              f'file count equals the requested range ({LAST - FIRST + 1})', str(report['count']))
        frames = regiondump.load_sequence(base['region_dir'], FIRST, LAST)
        check(all(f.rect('window').lighting_stamp == f.frame and f.rect('window').taa_stamp == f.frame
                  for f in frames), 'lighting and TAA stamps equal the file frame')
        check(base['manifest']['region_dump']['invalid'] == 0,
              'engine reported no invalid readbacks', str(base['manifest']['region_dump']))
        check(all(f.render_width == f.output_width and f.render_height == f.output_height for f in frames),
              'lighting (render) and TAA (output) domains coincide at 100 % screen percentage',
              f'{frames[0].render_width}x{frames[0].render_height}')

        # 2 — multi-ROI layout
        print('2. multi-ROI layout')
        f0 = frames[0]
        check([r.name for r in f0.rects] == [r[0] for r in RECTS], 'all three rectangles present')
        check(all(f0.rect(n).w == w and f0.rect(n).h == h for n, _, _, w, h in RECTS),
              'each rectangle has its requested size')
        bases = [f0.rect(n).base for n, *_ in RECTS]
        check(len(set(bases)) == 3 and bases == sorted(bases), 'rectangles occupy distinct increasing offsets',
              str(bases))
        win, door = f0.rect('window'), f0.rect('door')
        check(not np.allclose(win.channel('taa_output')[..., :3].mean(),
                              door.channel('taa_output')[..., :3].mean()),
              'different rectangles hold different image content')
        geo = f0.rect('window').geometry_mask()
        check(geo.any(), 'the window rectangle has geometry records', f'{int(geo.sum())}/{geo.size} px')

        # 3 — field semantics: linear HDR, no pre-exposure
        print('3. field semantics (negative control: exposure must not touch the records)')
        dark = capture(work, 'exposure_dark', cvars={'r.TAA.JitterSequenceLength': 8,
                                                     'r.AutoExposure': 0, 'r.Exposure': 0.25})
        bright = capture(work, 'exposure_bright', cvars={'r.TAA.JitterSequenceLength': 8,
                                                         'r.AutoExposure': 0, 'r.Exposure': 4.0})
        unchanged, detail, ratio = exposure_control(base, repeat, dark, bright)
        check(unchanged, 'a 16x exposure change does not move the linear records (run aggregate)', detail)
        check(abs(ratio - 1.0) < 0.05, 'mean record energy is unchanged by exposure', f'ratio {ratio:.4f}')
        check(abs(dark['manifest']['exposure'] - 0.25) < 1e-6 and abs(bright['manifest']['exposure'] - 4.0) < 1e-6,
              'the manifest reports the exposure each run actually used',
              f'{dark["manifest"]["exposure"]} vs {bright["manifest"]["exposure"]}')

        # 4 — jitter determinism
        print('4. jitter determinism')
        raws = np.stack([f.rect('window').channel('taa_raw')[..., :3] for f in frames])
        idx = [f.jitter_index for f in frames]
        same = [(i, j) for i in range(len(frames)) for j in range(i + 1, len(frames)) if idx[i] == idx[j]]
        adjacent = float(np.abs(raws[1:] - raws[:-1]).mean())
        same_jitter = float(np.mean([np.abs(raws[i] - raws[j]).mean() for i, j in same])) if same else float('nan')
        check(bool(same), 'the capture covers at least one full jitter cycle', f'{len(same)} matching pairs')
        check(adjacent > 1e-5, 'neighbouring frames differ in the raw centre sample', f'{adjacent:.6g}')
        check(same_jitter < 0.05 * adjacent,
              'frames sharing a jitter index hold the same raw centre sample',
              f'{same_jitter:.6g} vs {adjacent:.6g}')
        check(sorted(set(idx)) == list(range(1, 9)), 'jitter indices cover the 8-frame sequence', str(sorted(set(idx))))

        # 5 — instrumentation neutrality
        # Auto-exposure adapts with the frame time (auto_exposure.cpp uses dt), so ANY change of
        # cost moves the displayed image slightly. The neutrality claim is therefore made with a
        # fixed exposure; the auto-exposure case is measured too and reported as a confound that
        # every display-referred comparison in this programme has to control for. Two processes are
        # not bit-reproducible either, so the fixed-exposure pair is judged on the run aggregate and
        # bit-identity is reported rather than required (see UNCHANGED_FRACTION).
        print('5. instrumentation neutrality')
        shots = f'{FIRST + 2}-{FIRST + 4}'
        fixed = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}
        withs, withouts = [], []
        for r in range(NEUTRALITY_REPEATS):  # interleaved: a drift over the session cannot line up with one condition
            withs.append(capture(work, f'shot_with_{r}', cvars=fixed, screenshot_frames=shots))
            withouts.append(capture(work, f'shot_without_{r}', regions=None, region_first=None,
                                    region_last=None, cvars=fixed, screenshot_frames=shots))
        with_dump, without = withs[0], withouts[0]
        auto_with = capture(work, 'shot_auto_with', cvars={'r.TAA.JitterSequenceLength': 8},
                            screenshot_frames=shots)
        auto_without = capture(work, 'shot_auto_without', regions=None, region_first=None, region_last=None,
                               cvars={'r.TAA.JitterSequenceLength': 8}, screenshot_frames=shots)

        unchanged, detail = display_neutrality_repeats(withs, withouts)
        check(unchanged, 'at fixed exposure the dump leaves the presented image indistinguishable from '
              f'process-to-process variation ({NEUTRALITY_REPEATS} processes each)', detail)
        _, pair_detail = display_neutrality(with_dump, without)
        print(f'       one pair on the fixed bound, the rule before 2026-10-09 (reported, not judged): {pair_detail}')
        auto_diffs = png_delta(auto_with, auto_without)
        check(bool(auto_diffs), 'auto-exposure confound measured',
              f'max channel delta with auto-exposure {auto_diffs} (dt-driven adaptation, '
              f'auto_exposure.cpp; display-referred runs must fix the exposure to be comparable)')
        on_ms = with_dump['manifest']['frame_ms']['mean']
        off_ms = without['manifest']['frame_ms']['mean']
        check(True, 'frame-time cost of the instrumentation recorded',
              f'{on_ms:.2f} ms with dump vs {off_ms:.2f} ms without '
              f'({100.0 * (on_ms - off_ms) / max(1e-6, off_ms):+.1f} %, screenshots active in both)')

        # 6 — missing-source guard
        print('6. missing-source guard')
        no_taa = capture(work, 'taa_off', cvars={'r.TAA': 0})
        paths = regiondump.sequence_paths(no_taa['region_dir'], FIRST, LAST)
        if not paths:
            check(False, 'TAA-off run produced files to check', 'no files written')
        else:
            fr = regiondump.load_frame(paths[0][1])
            check(not fr.valid, 'a frame whose TAA records were never written is flagged invalid',
                  str(regiondump.flag_names(fr.flags)))
            try:
                regiondump.load_sequence(no_taa['region_dir'], FIRST, LAST)
                check(False, 'the reader refuses a run with missing TAA records')
            except ValueError:
                check(True, 'the reader refuses a run with missing TAA records')
            check(no_taa['manifest']['region_dump']['invalid'] > 0,
                  'the engine counted the invalid readbacks', str(no_taa['manifest']['region_dump']))

    finally:
        print(f'\n{len(PASS)} passed, {len(FAIL)} failed')
        for f in FAIL:
            print(f'  FAILED: {f}')
        if keep:
            print(f'kept: {work}')
        else:
            shutil.rmtree(work, ignore_errors=True)
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
