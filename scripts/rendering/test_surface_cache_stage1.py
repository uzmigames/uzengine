"""Bounded Phase D stage 1 captures: the surface radiance cache (`r.GI.SurfaceCache`, opt-in) read and
written by the hardware ray-traced reflection pass. Run each case separately (180 seconds maximum per
engine run). Acceptance A1-A4 and A6 of docs/specs/rendering/surface-radiance-cache.md.

Cases:
  default          reflection-reference, frame 95 of 100, no cache CVar: must be byte-identical to a
                   fresh capture of the tree before this change (`--reference`, A1)
  default-metrics  the A1 comparison (default vs --reference, and vs E:/UzmiEngine/f005-after)
  off / on0 / on1  rt-reflections-glossy (640 x 360, the stage 2 scene: a rough metal wall reflecting
                   an off-screen block), the standalone RT route (r.SSR 0, every wall texel traced,
                   glossy + indirect, NO denoise), 100 frames, every frame from 60 captured: the cache
                   off, on with DirectMode 0 (cell sun + other), on with DirectMode 1 (inline sun)
  pt               the converged DXR path tracer, frames 255 and 511 (A3's reference)
  metrics          A2 (ROI mean within 2 codes OFF vs ON, lower temporal std ON) and A3 (RMSE of
                   frame 99 vs the PT frame 511: ON <= OFF + 1 code) -> metrics.json
  debug1/2/3       the three `.Debug` views on the glossy scene (frame 95), A6
  perf-off/perf-on the glossy scene timed (GPU ms per pass from the manifest, A4)
  sponza-off/on    _sponza-tour (every texel traced: MaxRoughness 1), 300 frames, `.Stats 1` on the
                   on case: failedInserts / inserts, live, the RT pass and the resolve in GPU ms (A4)
  perf-metrics     reads the four perf manifests + the Sponza stats log -> perf.json
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
import sys

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "flicker"))
import runner  # noqa: E402

REPO = Path(runner.REPO)
GLOSSY_PROJECT = HERE / "rt-reflections-glossy.uzproject"
REFERENCE_PROJECT = HERE / "reflection-reference.uzproject"
SPONZA_PROJECT = HERE / "_sponza-tour.uzproject"

# Regions (x0, y0, x1, y1) of the glossy scene (test_rt_reflections_stage2.py): the whole reflector,
# the off-screen block's reflection, its sun-shadowed face (lit by the probe term only).
ROI_WALL = (0, 0, 640, 300)
ROI_BLOCK = (35, 85, 205, 270)
ROI_SHADOWED_FACE = (55, 110, 150, 250)
FIRST, LAST = 60, 99

BASE = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0}
# The standalone RT route on the glossy scene: every wall texel traced, glossy + the probe term, the
# raw one-sample output (the denoiser would hide the signal the cache stabilises).
GLOSSY = {**BASE, "r.SSR": 0, "r.RayTracing.Reflections": 1, "r.RayTracing.Reflections.MaxRoughness": 1.0,
          "r.RayTracing.Reflections.Glossy": 1, "r.RayTracing.Reflections.Indirect": 1,
          "r.RayTracing.Reflections.Denoise": 0}
OFF = {"r.GI.SurfaceCache": 0}
ON0 = {"r.GI.SurfaceCache": 1, "r.GI.SurfaceCache.DirectMode": 0, "r.GI.SurfaceCache.Stats": 1}
ON1 = {"r.GI.SurfaceCache": 1, "r.GI.SurfaceCache.DirectMode": 1, "r.GI.SurfaceCache.Stats": 1}
SPONZA = {**BASE, "r.RayTracing.Reflections.MaxRoughness": 1.0}

# name: (project, cvars, frames, screenshot_frames, extra_args, timing_first)
CASES = {
    "default": (REFERENCE_PROJECT, {**BASE, "r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1}, 100, "95", [], 200),
    # The test_rt_reflections_stage1.py recipe (RT reflections pinned off, the f005-after frame).
    "default-rtoff": (REFERENCE_PROJECT, {**BASE, "r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1,
                                          "r.RayTracing.Reflections": 0}, 100, "95", [], 200),
    "off": (GLOSSY_PROJECT, {**GLOSSY, **OFF}, 100, f"{FIRST}-{LAST}", [], 200),
    "on0": (GLOSSY_PROJECT, {**GLOSSY, **ON0}, 100, f"{FIRST}-{LAST}", [], 200),
    "on1": (GLOSSY_PROJECT, {**GLOSSY, **ON1}, 100, f"{FIRST}-{LAST}", [], 200),
    # `--screenshot-frames` takes N, A-B or A-B:step: 255-511:256 captures frames 255 and 511.
    "pt": (GLOSSY_PROJECT, BASE, 512, "255-511:256", ["--path-trace"], 200),
    # Diagnostics of the temporal std (A2): the lookup jitter off, and the mirror lobe (deterministic
    # ray directions, so the only temporal variation left is the cache's own).
    "on0-nojitter": (GLOSSY_PROJECT, {**GLOSSY, **ON0, "r.GI.SurfaceCache.Jitter": 0}, 100, f"{FIRST}-{LAST}", [], 200),
    "mirror-off": (GLOSSY_PROJECT, {**GLOSSY, **OFF, "r.RayTracing.Reflections.Glossy": 0}, 100, f"{FIRST}-{LAST}", [], 200),
    "mirror-on0": (GLOSSY_PROJECT, {**GLOSSY, **ON0, "r.RayTracing.Reflections.Glossy": 0}, 100, f"{FIRST}-{LAST}", [], 200),
    "mirror-on0-nojitter": (GLOSSY_PROJECT, {**GLOSSY, **ON0, "r.RayTracing.Reflections.Glossy": 0, "r.GI.SurfaceCache.Jitter": 0}, 100, f"{FIRST}-{LAST}", [], 200),
    "debug1": (GLOSSY_PROJECT, {**GLOSSY, **ON0, "r.GI.SurfaceCache.Debug": 1}, 100, "95", [], 200),
    "debug2": (GLOSSY_PROJECT, {**GLOSSY, **ON0, "r.GI.SurfaceCache.Debug": 2}, 100, "95", [], 200),
    "debug3": (GLOSSY_PROJECT, {**GLOSSY, **ON0, "r.GI.SurfaceCache.Debug": 3}, 100, "95", [], 200),
    "perf-off": (GLOSSY_PROJECT, {**GLOSSY, **OFF}, 300, None, [], 150),
    "perf-on": (GLOSSY_PROJECT, {**GLOSSY, "r.GI.SurfaceCache": 1}, 300, None, [], 150),
    "sponza-off": (SPONZA_PROJECT, {**SPONZA, **OFF}, 300, None, [], 150),
    "sponza-on": (SPONZA_PROJECT, {**SPONZA, **ON0}, 300, None, [], 150),
    # The pure cost (no stats counters): the A4 growth number.
    "sponza-perf-off": (SPONZA_PROJECT, {**SPONZA, **OFF}, 300, None, [], 150),
    "sponza-perf-on": (SPONZA_PROJECT, {**SPONZA, "r.GI.SurfaceCache": 1}, 300, None, [], 150),
}


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def crop(img, roi):
    x0, y0, x1, y1 = roi
    return img[y0:y1, x0:x1]


def rmse(a, b, roi):
    return float(np.sqrt(np.mean((crop(a, roi) - crop(b, roi)) ** 2)))


def identity(a: Path, b: Path):
    delta = np.abs(image(a) - image(b))
    return {"a": str(a), "b": str(b), "max_code_delta": int(delta.max()),
            "mean_abs_code_delta": float(delta.mean()), "px_differing": int((delta.max(axis=2) > 0).sum()),
            "png_bytes_identical": a.read_bytes() == b.read_bytes()}


def sequence(out: Path, name: str):
    return np.stack([image(out / name / "frames" / f"f_{f}.png") for f in range(FIRST, LAST + 1)])


def set_window(first: int, last: int, frames: int):
    """`--long`: a later window (the radiance-cache probes keep brightening for ~150 frames on the
    glossy scene; the spec's frames 60-99 compare a lagging EMA against a still-rising inline
    estimate). Rewrites the capture cases and the metrics window in place."""
    global FIRST, LAST
    FIRST, LAST = first, last
    for name, (project, cvars, _frames, shots, extra, timing) in list(CASES.items()):
        if shots == f"60-99":
            CASES[name] = (project, cvars, frames, f"{first}-{last}", extra, timing)


def metrics(out: Path):
    pt = image(out / "pt/frames/f_511.png")
    pt_prev = image(out / "pt/frames/f_255.png")
    names = [n for n in ("off", "on0", "on1", "on0-nojitter", "mirror-off", "mirror-on0", "mirror-on0-nojitter")
             if (out / n / "frames" / f"f_{LAST}.png").exists()]
    seqs = {name: sequence(out, name) for name in names}
    result = {"roi_wall_xyxy": ROI_WALL, "roi_block_xyxy": ROI_BLOCK, "roi_shadowed_face_xyxy": ROI_SHADOWED_FACE,
              "frames": [FIRST, LAST], "pt_256_to_512_rmse_wall": rmse(pt_prev, pt, ROI_WALL)}
    means = {}
    stds = {}
    for name, seq in seqs.items():
        for roi_name, roi in (("wall", ROI_WALL), ("block", ROI_BLOCK), ("face", ROI_SHADOWED_FACE)):
            c = seq[:, roi[1]:roi[3], roi[0]:roi[2]]
            means[(name, roi_name)] = c.mean(axis=(0, 1, 2))                 # per channel over frames + pixels
            stds[(name, roi_name)] = float(c.std(axis=0).mean())             # per-pixel temporal std, ROI mean
            result[f"{name}_mean_{roi_name}_rgb"] = [float(v) for v in means[(name, roi_name)]]
            result[f"{name}_temporal_std_{roi_name}"] = stds[(name, roi_name)]
        last = seq[-1]
        result[f"{name}_rmse_wall_vs_pt_f99"] = rmse(last, pt, ROI_WALL)
        result[f"{name}_rmse_block_vs_pt_f99"] = rmse(last, pt, ROI_BLOCK)
        result[f"{name}_rmse_wall_vs_pt_mean60_99"] = rmse(seq.mean(axis=0), pt, ROI_WALL)
    for name in names:
        if name == "off":
            continue
        base = "mirror-off" if name.startswith("mirror") else "off"
        if base not in seqs:
            continue
        for roi_name in ("wall", "block", "face"):
            diff = means[(name, roi_name)] - means[(base, roi_name)]
            result[f"{name}_minus_off_mean_{roi_name}_rgb"] = [float(v) for v in diff]
            result[f"{name}_minus_off_mean_{roi_name}_max_abs"] = float(np.abs(diff).max())
            result[f"{name}_over_off_temporal_std_{roi_name}"] = (stds[(name, roi_name)] / stds[(base, roi_name)]
                                                                 if stds[(base, roi_name)] > 0 else None)
        result[f"{name}_minus_off_rmse_wall_vs_pt_f99"] = result[f"{name}_rmse_wall_vs_pt_f99"] - result[f"{base}_rmse_wall_vs_pt_f99"]
        result[f"{name}_minus_off_rmse_block_vs_pt_f99"] = result[f"{name}_rmse_block_vs_pt_f99"] - result[f"{base}_rmse_block_vs_pt_f99"]
        result[f"{name}_rmse_f99_vs_off_f99_wall"] = rmse(seqs[name][-1], seqs[base][-1], ROI_WALL)
    for name in ("on0", "on1"):
        result[f"{name}_A2_mean_within_2_codes"] = bool(result[f"{name}_minus_off_mean_wall_max_abs"] <= 2.0)
        result[f"{name}_A2_lower_temporal_std"] = bool((result[f"{name}_over_off_temporal_std_wall"] or 2.0) < 1.0)
        result[f"{name}_A3_rmse_vs_pt_within_1_code"] = bool(result[f"{name}_minus_off_rmse_wall_vs_pt_f99"] <= 1.0)
    for name in names:
        log = (out / name / "run.log").read_text(encoding="utf-8", errors="replace")
        result[f"{name}_compile_failed"] = "compile failed" in log
        result[f"{name}_device_removed"] = "DEVICE_REMOVED" in log or "0x887A0005" in log
        stats = re.findall(r"SurfaceCache stats: (.*)", log)
        if stats:
            result[f"{name}_last_stats_line"] = stats[-1].strip()
    print(json.dumps(result, indent=2))
    (out / "metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


def gpu_ms(path: Path):
    m = json.loads(path.read_text(encoding="utf-8"))
    return m.get("gpu_ms") or {}, m.get("frame_ms") or {}


def perf_metrics(out: Path):
    result = {}
    for name in ("perf-off", "perf-on", "sponza-off", "sponza-on", "sponza-perf-off", "sponza-perf-on"):
        manifest = out / name / "manifest.json"
        if not manifest.exists():
            continue
        gpu, frame = gpu_ms(manifest)
        entry = {"gpu_total_ms": gpu.get("_total"), "cpu_frame_ms": frame.get("mean")}
        for key, value in gpu.items():
            low = key.lower()
            if "rt_reflection" in low or "surfacecache" in low or "surface_cache" in low or "rtreflection" in low:
                entry[key] = value
        log = (out / name / "run.log").read_text(encoding="utf-8", errors="replace")
        stats = re.findall(r"SurfaceCache stats: (.*)", log)
        if stats:
            entry["stats_lines"] = [s.strip() for s in stats[-3:]]
            last = stats[-1]
            numbers = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", last)}
            inserts, failed = numbers.get("inserts", 0), numbers.get("failedInserts", 0)
            entry["failed_inserts_pct_last_frame"] = 100.0 * failed / (inserts + failed) if inserts + failed else 0.0
            total, total_failed = numbers.get("totalInserts", 0), numbers.get("totalFailedInserts", 0)
            entry["failed_inserts_pct_total"] = 100.0 * total_failed / (total + total_failed) if total + total_failed else 0.0
            entry["live"] = numbers.get("live")
            entry["tombstones"] = numbers.get("tombstones")
        created = re.findall(r"SurfaceCache: created (.*)", log)
        if created:
            entry["created"] = created[-1].strip()
        entry["compile_failed"] = "compile failed" in log
        entry["device_removed"] = "DEVICE_REMOVED" in log or "0x887A0005" in log
        result[name] = entry
    for off, on in (("perf-off", "perf-on"), ("sponza-off", "sponza-on"), ("sponza-perf-off", "sponza-perf-on")):
        if off in result and on in result:
            for key in list(result[on].keys()):
                if "rt_reflection" in key.lower() and key in result[off] and result[off][key]:
                    result[f"{on}_rt_reflections_growth_pct"] = (result[on][key] / result[off][key] - 1.0) * 100.0
    print(json.dumps(result, indent=2))
    (out / "perf.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=["default-metrics", "metrics", "perf-metrics", *CASES], required=True)
    parser.add_argument("--reference", type=Path, help="default-metrics: the fresh before-change capture of frame 95")
    parser.add_argument("--long", action="store_true", help="300 frames, window 260-299 (converged probes)")
    parser.add_argument("--exe", type=Path, help="the engine binary to capture with (default: the tree's)")
    args = parser.parse_args()
    out = args.out.resolve()
    if args.exe:
        runner.EXE = str(args.exe.resolve())
    if args.long:
        set_window(260, 299, 300)
    if args.case == "default-metrics":
        default_path = out / "default/frames/f_95.png"
        result = {"vs_reference": identity(default_path, args.reference.resolve()) if args.reference else None,
                  "vs_f005_after": identity(default_path, REPO.parent / "f005-after/frames/f_95.png")}
        print(json.dumps(result, indent=2))
        (out / "default_metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        assert result["vs_reference"] is None or result["vs_reference"]["max_code_delta"] == 0
        return
    if args.case == "metrics":
        metrics(out)
        return
    if args.case == "perf-metrics":
        perf_metrics(out)
        return

    project, cvars, frames, shots, extra, timing_first = CASES[args.case]
    result = runner.run_capture(str(out / args.case), str(project), frames, clean=False, timeout=180,
                                cvars=cvars, extra_args=extra, screenshot_frames=shots, timing_first=timing_first)
    assert result["exit_code"] == 0, result["exit_code"]
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log, result["log"]
    assert "DEVICE_REMOVED" not in log and "device removed" not in log.lower(), result["log"]
    print(f'{args.case}: exit=0; {result["frames_dir"]}', flush=True)


if __name__ == "__main__":
    main()
