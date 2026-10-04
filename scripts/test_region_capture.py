"""Behavioural harness for the region-dump instrumentation (`--region-dump`, `--regions`,
`--run-manifest`). It runs the engine and checks what the GPU actually wrote, not the source text.

Checks, in the order the followup-09 checklist asks for them:

1. Frame integrity — every file's GPU stamps equal the frame the CPU read back, the requested
   range is complete (including the frames still in flight at shutdown), with no duplicates.
2. Multi-ROI layout — each rectangle lands at its own offset with its own stamps and geometry.
3. Field semantics — the records hold linear HDR: changing `r.Exposure` must not move them by
   more than the run-to-run noise floor measured from a repeated identical run (negative control).
4. Jitter determinism — the raw centre sample repeats with the jitter period and differs between
   neighbouring frames (so "the same jitter index gives the same image" is measured, not assumed).
5. Instrumentation neutrality — with and without the dump the presented image is identical, and
   the frame-time cost is reported from both manifests.
6. Missing-source guard — with `r.TAA=0` the resolve never writes its records, the files are
   flagged, and the reader refuses them instead of returning last frame's values.

Usage: python scripts/test_region_capture.py [--keep] [--quick]
Each check prints `ok`/`FAIL`; the exit code is non-zero if any failed.
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w-seq.uzproject')
# Pose W holds still until frame 337; measure inside that window.
FIRST, LAST = 60, 75
RECTS = [('window', 770, 150, 96, 128), ('door', 300, 500, 48, 48), ('sky', 900, 40, 32, 32)]

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
        _, s_dark = output_stack(dark['region_dir'])
        _, s_bright = output_stack(bright['region_dir'])
        _, s_base = output_stack(base['region_dir'])
        _, s_repeat = output_stack(repeat['region_dir'])
        noise = float(np.abs(s_base - s_repeat).max())
        exposure_delta = float(np.abs(s_dark - s_bright).max())
        ratio = float(np.abs(s_bright).mean() / max(1e-9, np.abs(s_dark).mean()))
        check(exposure_delta <= max(noise, 1e-4) * 4.0 + 1e-6,
              'a 16x exposure change does not move the linear records beyond run-to-run noise',
              f'exposure delta {exposure_delta:.6g} vs repeat-run noise {noise:.6g}')
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
        # every display-referred comparison in this programme has to control for.
        print('5. instrumentation neutrality')
        shots = f'{FIRST + 2}-{FIRST + 4}'
        fixed = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}
        with_dump = capture(work, 'shot_with', cvars=fixed, screenshot_frames=shots)
        without = capture(work, 'shot_without', regions=None, region_first=None, region_last=None,
                          cvars=fixed, screenshot_frames=shots)
        auto_with = capture(work, 'shot_auto_with', cvars={'r.TAA.JitterSequenceLength': 8},
                            screenshot_frames=shots)
        auto_without = capture(work, 'shot_auto_without', regions=None, region_first=None, region_last=None,
                               cvars={'r.TAA.JitterSequenceLength': 8}, screenshot_frames=shots)

        def png_delta(run_a, run_b):
            out = []
            for n in range(FIRST + 2, FIRST + 5):
                a = os.path.join(run_a['frames_dir'], f'f_{n}.png')
                b = os.path.join(run_b['frames_dir'], f'f_{n}.png')
                if os.path.exists(a) and os.path.exists(b):
                    out.append(int(np.abs(runner.png_array(a).astype(int) - runner.png_array(b).astype(int)).max()))
            return out

        diffs = png_delta(with_dump, without)
        check(bool(diffs) and max(diffs) == 0,
              'at fixed exposure the presented image is identical with and without the dump',
              f'max channel delta {diffs}')
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
