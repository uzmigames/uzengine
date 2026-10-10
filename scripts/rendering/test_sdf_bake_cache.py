"""Asynchronous, disk-cached mesh SDF bakes — gates of lighting block B, item 3
(`.rulebook/archive/2026-10-10-phase0_lighting-block-b-03-async-sdf-bake`).

Runs Sponza (project configuration, 640x360 unless noted) through `scripts/rendering/flicker/runner.py`
with the cache directory `cache/meshsdf` (relative to the repo root, where the engine runs):

  cold     cache deleted, then the project: every mesh SDF is baked and stored
  warm     the project again: every mesh SDF comes from the cache
  verify   warm + `r.GI.MeshSdf.Diagnostic.CacheVerify 1`: every hit is baked again and compared
  poisoned one cache entry truncated, then the project: that mesh is baked and the entry rewritten
  camera-c cold and warm captures of the Camera C ROIs (`run_probe_side_sponza`, 1920x1080)
  enable   `scripts/rendering/sponza-gi-runtime-enable.uzproject`: loads with `r.RadianceCache` 0, a Lua
           script switches it on at frame 60; the view's `diffuse_indirect` and the frame times
           are recorded while the fields are baked in the background

Checks (pre-registered in tasks.md 1.2; C4' registered 2026-10-05 before the measurement):
  C1  a cache hit is the same bits as a fresh bake: every `MeshSDF cache verify` line of the
      verify run says IDENTICAL (volume and records), none DIFFERENT
  C2  warm preload <= 50 % of the cold preload (`Pre-loading` .. `Pre-load complete`)
  C3  no `WaitIdle` in the bake path (source check: FlushSdfBakes gone, no WaitIdle in
      mesh_sdf_bake_queue.cpp) and, in the enable run, no GPU frame above 2x the mean of the
      timing window while the fields are baked (`frame_ms` max / p50 of the manifest)
  C4  Camera C ROI means cold vs warm bit-identical (the letter of the pre-registration), and
  C4' within 1 % relative — registered before measuring: cold and warm receive their fields on
      different frames, so the temporal GI state is not expected to be the same bits
  C2' (registered after the first measurement, 2026-10-05) the SDF-attributable warm preload — warm
      preload minus the preload of the same scene with SDF GI off (the enable run's) — is <= 50 % of
      cold's; C2 as registered compares whole preloads, which the asynchronous bake made mostly
      texture time (cold 20.3 s against a 10.1 s floor)
  C5  enabling `r.RadianceCache` at runtime bakes what is missing and the GI appears: the enable
      run logs the re-trigger, completes as many fields as the warm run, and the view's
      `diffuse_indirect` mean over the last 50 frames is at least 1.5x its value before frame 60
  C5' (registered after the first measurement) with SDF GI off `diffuse_indirect` holds the sky
      ambient fallback (0.26 against 1.4e-4 of radiance-cache GI), so the gain criterion is replaced by
      agreement: the enable run's last-50-frame mean is within 5 % of a reference run of the same
      project with `r.RadianceCache` 1 from the start, dumped on the same frames
  C3b  and C3b' are read on an enable run without the region dump (its per-frame file writes stall
      the main thread for seconds in every dumped run, the reference included); C5/C5' use a
      second, dumped enable run and the dumped reference
  C3b' per-frame attribution from `r.GpuProfiler.Log`: the frame TOTAL max over the frames after
      the enable frame plus two (the global distance field's first whole-window fill when SDF GI
      turns on, which is not the bake path) <= 2x the median of those frames, and the `MeshSdfBake`
      scope's max is reported
  N   negative control: the poisoned run bakes exactly one mesh, fetches the rest from the cache,
      and leaves the entry rewritten (size restored)

Usage: python scripts/rendering/test_sdf_bake_cache.py [--out DIR] [--reuse] [--skip-camera-c]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
import run_probe_side_sponza as sponza  # noqa: E402
import sdf_bake_timeline as timeline  # noqa: E402

PROJECT = os.path.join(runner.REPO, 'sample', 'projects', 'sponza.uzproject')
ENABLE_PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-gi-runtime-enable.uzproject')
CACHE_DIR = os.path.join(runner.REPO, 'cache', 'meshsdf')
PRELOAD_FRAMES = 150
ENABLE_FRAMES, ENABLE_AT, ENABLE_DUMP_FIRST, ENABLE_DUMP_LAST = 720, 60, 40, 700   # one start per frame: the last field arrives ~140 frames after the enable, the GI settles ~200 frames later
VIEW = ('VIEW', 160, 90, 320, 180)          # centre of the 640x360 view
REC709 = np.array([0.2126, 0.7152, 0.0722])
C2_MAX_RATIO, C3_MAX_RATIO, C4_REL_TOL, C5_MIN_GAIN = 0.5, 2.0, 1e-2, 1.5
BASE_CVARS = {'r.TechPanel.Show': 0}


def clear_cache():
    shutil.rmtree(CACHE_DIR, ignore_errors=True)


def run(work, project, frames, reuse, cvars=None, **kw):
    if not reuse or not os.path.isfile(os.path.join(work, 'manifest.json')):   # --reuse still captures what is missing
        res = runner.run_capture(work, project, frames, cvars={**BASE_CVARS, **(cvars or {})}, clean=True, timeout=900, **kw)
        if res['exit_code'] != 0:
            raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    with open(os.path.join(work, 'manifest.json'), encoding='utf-8') as fh:
        manifest = json.load(fh)
    return timeline.parse(os.path.join(work, 'run.log')), manifest


def parse_profile_log(path, passes=None):
    """`r.GpuProfiler.Log` lines: frame -> TOTAL ms, and frame -> MeshSdfBake ms (0 when absent).
    With `passes` (a dict) every pass is summed into it per name over the frames of the log."""
    totals, bakes = {}, {}
    frame = None
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = re.search(r'GPU Profile \(frame (\d+)\)', line)
            if m:
                frame = int(m.group(1))
                bakes[frame] = 0.0
                continue
            if frame is None:
                continue
            m = re.search(r'\]\s+(\S+)\s+([0-9.]+) ms\s*$', line)
            if not m:
                continue
            name, ms = m.group(1), float(m.group(2))
            if passes is not None and name != 'TOTAL':
                passes[name] = passes.get(name, 0.0) + ms
            if name == 'TOTAL':
                totals[frame] = ms
            elif name == 'MeshSdfBake':
                bakes[frame] = ms
    return totals, bakes


def preload_frames(work):
    with open(os.path.join(work, 'run.log'), encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = re.search(r'Pre-load complete: assets ready and exposure seeded after (\d+) frames', line)
            if m:
                return int(m.group(1))
    return None


def count_lines(work, *needles):
    counts = {n: 0 for n in needles}
    with open(os.path.join(work, 'run.log'), encoding='utf-8', errors='replace') as fh:
        for line in fh:
            for n in needles:
                if n in line:
                    counts[n] += 1
    return counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', 'test-sdf-bake-cache'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--skip-camera-c', action='store_true')
    args = ap.parse_args()
    if not args.reuse:
        runner.require_exe()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    work = lambda name: os.path.join(args.out, name)   # noqa: E731

    # ── cold / warm ──────────────────────────────────────────────────────────────────────────
    print('cold run (cache cleared)', flush=True)
    if not args.reuse:
        clear_cache()
    cold, _ = run(work('cold'), PROJECT, PRELOAD_FRAMES, args.reuse, timing_first=100)
    print(f'  cold: {cold}')
    print('warm run', flush=True)
    warm, _ = run(work('warm'), PROJECT, PRELOAD_FRAMES, args.reuse, timing_first=100)
    print(f'  warm: {warm}')
    report['cold'], report['warm'] = cold, warm
    eligible = cold['meshes_baked']
    check('C2 warm preload <= 50 % of cold', cold['preload_s'] and warm['preload_s'] and warm['preload_s'] <= C2_MAX_RATIO * cold['preload_s']
          and cold['meshes_cached'] == 0 and warm['meshes_baked'] == 0 and warm['meshes_cached'] == eligible and eligible > 0,
          f'cold {cold["preload_s"]} s ({cold["meshes_baked"]} baked), warm {warm["preload_s"]} s ({warm["meshes_cached"]} cached)')

    # ── attribution: where the warm preload's GPU time goes (informational) ─────────────────
    print('attribution run (warm preload with the GPU profile log)', flush=True)
    attr_reuse = args.reuse and os.path.isfile(os.path.join(work('attribution'), 'manifest.json'))
    attr_tl, _ = run(work('attribution'), PROJECT, PRELOAD_FRAMES, attr_reuse, cvars={'r.GpuProfiler.Log': 1}, timing_first=100)
    passes = {}
    n_pre = preload_frames(work('attribution'))
    totals, bakes = parse_profile_log(os.path.join(work('attribution'), 'run.log'), passes)
    pre_total = sum(v for f, v in totals.items() if n_pre is None or f <= n_pre)
    pre_bake = sum(v for f, v in bakes.items() if n_pre is None or f <= n_pre)
    # per-pass sums over the preload frames only
    passes_pre = {}
    parse_frames = {}
    frame = None
    with open(os.path.join(work('attribution'), 'run.log'), encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = re.search(r'GPU Profile \(frame (\d+)\)', line)
            if m:
                frame = int(m.group(1))
                continue
            m = re.search(r'\]\s+(\S+)\s+([0-9.]+) ms\s*$', line)
            if m and frame is not None and (n_pre is None or frame <= n_pre) and m.group(1) != 'TOTAL':
                passes_pre[m.group(1)] = passes_pre.get(m.group(1), 0.0) + float(m.group(2))
    top = sorted(passes_pre.items(), key=lambda kv: -kv[1])[:6]
    report['attribution'] = {'timeline': attr_tl, 'preload_frames': n_pre, 'gpu_total_preload_ms': pre_total,
                             'mesh_sdf_bake_preload_ms': pre_bake, 'passes_preload_ms': passes_pre}
    print(f'  attribution: preload {attr_tl["preload_s"]} s over {n_pre} frames; GPU total {pre_total:.0f} ms, '
          f'MeshSdfBake {pre_bake:.0f} ms; top passes ' + ', '.join(f'{k} {v:.0f}' for k, v in top), flush=True)

    # ── verify (C1) ──────────────────────────────────────────────────────────────────────────
    print('verify run (every hit baked again and compared)', flush=True)
    verify, _ = run(work('verify'), PROJECT, PRELOAD_FRAMES, args.reuse, cvars={'r.GI.MeshSdf.Diagnostic.CacheVerify': 1}, timing_first=100)
    v = count_lines(work('verify'), 'result=IDENTICAL', 'result=DIFFERENT')
    report['verify'] = {'timeline': verify, **v}
    check('C1 cache hits are the same bits as a fresh bake (volume and records)',
          v['result=IDENTICAL'] == eligible and v['result=DIFFERENT'] == 0,
          f'{v["result=IDENTICAL"]} IDENTICAL, {v["result=DIFFERENT"]} DIFFERENT of {eligible}')

    # ── poisoned entry (negative control) ───────────────────────────────────────────────────
    print('poisoned run (one entry truncated)', flush=True)
    entries = sorted(glob.glob(os.path.join(CACHE_DIR, 'imported', '*.bin')))
    poisoned_path, original_size = (entries[0], os.path.getsize(entries[0])) if entries else (None, 0)
    if not args.reuse and poisoned_path:
        with open(poisoned_path, 'r+b') as fh:
            fh.truncate(original_size // 2)
    poisoned, _ = run(work('poisoned'), PROJECT, PRELOAD_FRAMES, args.reuse, timing_first=100)
    p = count_lines(work('poisoned'), 'is stale or corrupt', 'fails its checksum')
    rewritten = os.path.getsize(poisoned_path) if poisoned_path and os.path.exists(poisoned_path) else 0
    report['poisoned'] = {'timeline': poisoned, 'entry': poisoned_path, 'original_size': original_size, 'rewritten_size': rewritten, **p}
    check('N poisoned entry: one mesh baked, the rest cached, the entry rewritten',
          poisoned_path is not None and poisoned['meshes_baked'] == 1 and poisoned['meshes_cached'] == eligible - 1
          and (p['is stale or corrupt'] + p['fails its checksum']) == 1 and rewritten == original_size,
          f'{poisoned["meshes_baked"]} baked, {poisoned["meshes_cached"]} cached, warnings {p}, size {original_size} -> {rewritten}')

    # ── C3 source check ──────────────────────────────────────────────────────────────────────
    with open(os.path.join(runner.REPO, 'runtime', 'engine', 'renderer', 'scene', 'scene_mesh_assets.cpp'), encoding='utf-8') as fh:
        assets_src = fh.read()
    with open(os.path.join(runner.REPO, 'runtime', 'engine', 'renderer', 'gi', 'mesh_sdf_bake_queue.cpp'), encoding='utf-8') as fh:
        queue_src = fh.read()
    no_wait = 'FlushSdfBakes' not in assets_src and '->WaitIdle(' not in queue_src and '->WaitIdle(' not in assets_src   # calls, not comments
    check('C3a no WaitIdle in the bake path (source)', no_wait, 'scene_mesh_assets.cpp / mesh_sdf_bake_queue.cpp')

    # ── enable run (C3b, C5) ────────────────────────────────────────────────────────────────
    print('runtime-enable run (r.RadianceCache 0 -> 1 at frame 60)', flush=True)
    # Frame times come from a run without the region dump: the dump's per-frame file writes stall the
    # main thread for seconds (seen in every dumped run, the reference included).
    enable_tl, man = run(work('enable'), ENABLE_PROJECT, ENABLE_FRAMES, args.reuse, cvars={'r.GpuProfiler.Log': 1}, timing_first=ENABLE_AT)
    view_tl, _ = run(work('enable-view'), ENABLE_PROJECT, ENABLE_FRAMES, args.reuse, regions=[VIEW], region_fields='components',
                     region_first=ENABLE_DUMP_FIRST, region_last=ENABLE_DUMP_LAST, timing_first=ENABLE_AT)
    ref_tl, _ = run(work('enable-reference'), ENABLE_PROJECT, ENABLE_FRAMES, args.reuse, cvars={'r.RadianceCache': 1}, regions=[VIEW],
                    region_fields='components', region_first=ENABLE_DUMP_FIRST, region_last=ENABLE_DUMP_LAST, timing_first=ENABLE_AT)
    ref_seq = regiondump.load_sequence(os.path.join(work('enable-reference'), 'region'), ENABLE_DUMP_FIRST, ENABLE_DUMP_LAST)
    ref_series = [float(np.nanmean(fr.rect('VIEW').channel('diffuse_indirect')[..., :3].astype(np.float64) @ REC709)) for fr in ref_seq]
    ref_after = float(np.mean(ref_series[-50:]))
    frames_total, frames_bake = parse_profile_log(os.path.join(work('enable'), 'run.log'))
    e = count_lines(work('enable'), 'SDF GI enabled after load', 'gi_enable_at_frame: r.RadianceCache=1')
    seq = regiondump.load_sequence(os.path.join(work('enable-view'), 'region'), ENABLE_DUMP_FIRST, ENABLE_DUMP_LAST)
    series = []
    for fr in seq:
        di = fr.rect('VIEW').channel('diffuse_indirect')[..., :3].astype(np.float64) @ REC709
        series.append(float(np.nanmean(di)))
    before = float(np.mean(series[:ENABLE_AT - ENABLE_DUMP_FIRST - 5]))
    after = float(np.mean(series[-50:]))
    gpu = man.get('gpu_ms') or {}
    frame_ms = man.get('frame_ms')
    frame_mean, frame_max = gpu.get('_frame'), None
    if isinstance(frame_ms, dict):          # CPU frame times: mean/min/p50/p95/max
        frame_mean = frame_ms.get('p50', frame_ms.get('mean', frame_mean))
        frame_max = frame_ms.get('max')
    ratio = (frame_max / frame_mean) if frame_mean and frame_max else None
    completed = enable_tl['meshes_baked'] + enable_tl['meshes_cached']
    report['enable'] = {'timeline': enable_tl, 'series_first': ENABLE_DUMP_FIRST, 'series': series, 'before': before, 'after': after,
                        'frame_mean_ms': frame_mean, 'frame_max_ms': frame_max, 'ratio': ratio, **e}
    print(f'  enable: {enable_tl}; indirect before {before:.4e} after {after:.4e}; frame mean {frame_mean} max {frame_max}')
    check('C5 enabling r.RadianceCache at runtime bakes the missing fields and the GI appears',
          e['SDF GI enabled after load'] >= 1 and completed == eligible and after >= C5_MIN_GAIN * max(before, 1e-12),
          f'retrigger lines {e["SDF GI enabled after load"]}, {completed} fields of {eligible}, indirect {before:.3e} -> {after:.3e}')
    check('C3b no frame above 2x the median while the fields are baked', ratio is not None and ratio <= C3_MAX_RATIO,
          f'max/median {ratio} ({frame_max} / {frame_mean} ms)' if ratio else 'no per-frame max in the manifest')
    c5_rel = abs(after - ref_after) / max(abs(ref_after), 1e-12)
    report['enable']['reference_after'] = ref_after
    report['enable']['reference_timeline'] = ref_tl
    check("C5' after enabling, the view agrees with the reference run (SDF GI on from the start) within 5 %",
          c5_rel <= 0.05 and completed == eligible, f'enable {after:.4e} vs reference {ref_after:.4e} ({100 * c5_rel:.2f} %)')
    late = {f: v for f, v in frames_total.items() if f >= ENABLE_AT + 3}
    if late:
        vals = np.array(list(late.values()))
        tot_max, tot_med = float(vals.max()), float(np.median(vals))
        worst_frame = max(late, key=late.get)
        bake_max = max(frames_bake.values()) if frames_bake else 0.0
        spike = {f: v for f, v in frames_total.items() if ENABLE_AT <= f < ENABLE_AT + 3}
        report['enable']['profile'] = {'total_max_ms': tot_max, 'total_median_ms': tot_med, 'worst_frame': worst_frame,
                                       'bake_scope_max_ms': bake_max, 'enable_frames_ms': spike}
        check("C3b' frames after the enable frame + 2: TOTAL max <= 2x median (the first fill excluded); MeshSdfBake scope max reported",
              tot_max <= C3_MAX_RATIO * tot_med,
              f'TOTAL max {tot_max:.1f} ms at frame {worst_frame}, median {tot_med:.1f} ms; MeshSdfBake max {bake_max:.2f} ms; '
              'enable frames {' + ', '.join(f'{f}: {v:.0f}' for f, v in sorted(spike.items())) + '}')
    else:
        check("C3b' per-frame GPU profile available", False, 'no GPU profile lines in the enable log')
    floor = enable_tl['preload_s']
    if floor and cold['preload_s'] and warm['preload_s']:
        cold_sdf, warm_sdf = cold['preload_s'] - floor, warm['preload_s'] - floor
        report['c2_prime'] = {'floor_s': floor, 'cold_sdf_s': cold_sdf, 'warm_sdf_s': warm_sdf}
        check("C2' SDF-attributable warm preload <= 50 % of cold (floor = preload with SDF GI off)",
              cold_sdf > 0 and warm_sdf <= C2_MAX_RATIO * cold_sdf,
              f'floor {floor} s; cold {cold_sdf:.1f} s -> warm {warm_sdf:.1f} s ({100 * warm_sdf / max(cold_sdf, 1e-9):.0f} %)')

    # ── Camera C cold vs warm (C4) ──────────────────────────────────────────────────────────
    if not args.skip_camera_c:
        print('Camera C cold vs warm', flush=True)
        cvars = {**sponza.PROJECT_CVARS, 'r.GI.RadianceCache.ProbeClassification': 3, 'r.GI.RadianceCache.ReachRelocate': 1,
                 'r.GI.RadianceCache.ReachVisibleSeeds': 1}

        def camera_c(name):
            w = work(name)
            if not args.reuse:
                res = runner.run_capture(w, sponza.QA_PROJECT, sponza.uniform.FRAMES, regions=sponza.ROIS, region_fields='components',
                                         region_first=sponza.uniform.FIRST, region_last=sponza.uniform.LAST, cvars=cvars,
                                         timing_first=200, clean=True)
                if res['exit_code'] != 0:
                    raise SystemExit(f'{w}: engine exited {res["exit_code"]}')
            return sponza.roi_support(w, sponza.uniform.FIRST, sponza.uniform.LAST), timeline.parse(os.path.join(w, 'run.log'))

        if not args.reuse:
            clear_cache()
        rois_cold, tl_cold = camera_c('camera-c-cold')
        rois_warm, tl_warm = camera_c('camera-c-warm')
        rel = {n: abs(rois_warm[n]['mean'] - rois_cold[n]['mean']) / max(abs(rois_cold[n]['mean']), 1e-12) for n in rois_cold}
        identical = all(rois_warm[n]['mean'] == rois_cold[n]['mean'] for n in rois_cold)
        report['camera_c'] = {'cold': rois_cold, 'warm': rois_warm, 'relative': rel, 'cold_timeline': tl_cold, 'warm_timeline': tl_warm}
        for n in rois_cold:
            print(f'  {n:12} cold {rois_cold[n]["mean"]:.6e}  warm {rois_warm[n]["mean"]:.6e}  rel {rel[n]:.2e}')
        check('C4 Camera C ROI means bit-identical cold vs warm', identical and tl_warm['meshes_cached'] == tl_cold['meshes_baked'] > 0,
              {n: f'{v:.2e}' for n, v in rel.items()})
        check("C4' Camera C ROI means within 1 % relative cold vs warm", all(v <= C4_REL_TOL for v in rel.values())
              and tl_warm['meshes_cached'] == tl_cold['meshes_baked'] > 0, {n: f'{v:.2e}' for n, v in rel.items()})

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'sdf_bake_cache.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
