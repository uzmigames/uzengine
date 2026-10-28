"""Bounded Phase D stage 3 captures: the SDF consumers and the probes read exact-material radiance
(spec section 4, "Stage 3", acceptance C1-C5). Run each case separately (180 seconds maximum per
engine run); pass `--exe` to capture with another binary (the before-change build for C1).

Cases:
  default         reflection-reference, frame 95 of 100, no cache CVar (C1 / A1: byte-identical to `--exe` before)
  ll-pt           surface-cache-locallight --path-trace frames 255 + 511 (the PT byte-identity capture: the
                  stage 3 shader changes touch headers the path tracer includes)
  sponza-off      surface-cache-sponza (Camera A, static, fixed exposure, 960x540): the cache off — the
                  current GDF + SH shading of the final gather, the SDF reflections and the SH writer
  sponza-on       the cache on with the stage 3 defaults (SdfReads 1, SdfLodBias 2, FeedRays 16, DDGI 1, SdfWrites 0), Stats 1
  sponza-on-s2    the cache on with every stage 3 switch at its OFF value (SdfReads 0, FeedRays 0, DDGI 0):
                  the stage 2 estimator on this build (C1: reproduces the before binary's cache-on capture)
  sponza-on-lod0  the cache on with SdfLodBias 0 (C3: the TLAS-written-cell fraction falls without the bias)
  sponza-on-feed0 the cache on without the feed (FeedRays 0): the reflection rays alone fill the cells
  sponza-pt       the converged DXR path tracer, 4 bounces, frames 255 and 511 (the C4 reference)
  sponza-perf-off / sponza-perf-on   1920x1080, 300 frames timed (GPU ms per pass from the manifest, C5)
  ll-fg-off / ll-fg-on / ll-fg-s2   surface-cache-locallight with the final gather + SDF reflections ON
                  (r.GI.FinalGather 1, r.GI.Reflections 1): the diffuse consumers' hits see the rect
                  light and the emitter only through the cache (stage 2 terms reach the SDF consumers
                  in stage 3); the PT reference is ll-pt. off = cache off, on = stage 3 defaults,
                  s2 = the cache on with the stage 3 switches off
  llgi-off / llgi-on / llgi-s2 / llgi-pt   surface-cache-locallight-gi: the same scene seen from the floor (the
                  block's shadowed front face and the floor beyond the lamp's pool are DIFFUSE receivers of the
                  lamp's bounce); off = cache off, on = stage 3 defaults, s2 = the stage 3 switches off, pt =
                  the 4-bounce DXR path tracer (frames 255 + 511)
  sponza-ptlegacy the legacy reference path tracer (r.PathTracer.Enable 1, 300 accumulated frames) on the
                  Sponza scene: the DXR pipeline path tracer treats every material-graph mesh as a white
                  emitter (the (1,1,1) x 1 graph marker, pre-existing), so it cannot reference Sponza
  metrics         ROI means / RMSE vs the PT for every captured sponza-* case -> metrics.json
  ll-metrics      ROI means / RMSE vs the PT for the ll-fg-* cases -> ll_metrics.json
  llgi-metrics    ROI means / RMSE vs the PT for the llgi-* cases -> llgi_metrics.json
  perf-metrics    the perf manifests -> perf.json
  default-metrics frame-95 and PT byte identity vs `--reference` / `--before`
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
SPONZA_PROJECT = HERE / "surface-cache-sponza.uzproject"
LL_PROJECT = HERE / "surface-cache-locallight.uzproject"
LLGI_PROJECT = HERE / "surface-cache-locallight-gi.uzproject"
REFERENCE_PROJECT = HERE / "reflection-reference.uzproject"

# Regions (x0, y0, x1, y1) of the 960 x 540 Camera A frame, chosen on the captures (see the stage 3
# section of the task file): surfaces lit by bounce only, where the off-screen hit material decides
# the colour the real-time gather reads.
ROIS = {
    "full": (0, 0, 960, 540),
    "left_wall_shadow": (0, 60, 150, 330),       # the shadowed left-hand wall and its arches
    "right_wall_shadow": (810, 60, 960, 330),    # the shadowed right-hand wall
    "ceiling_vault": (300, 0, 660, 90),          # the vault above the atrium: one bounce only
    "far_arcade": (330, 150, 630, 330),          # the far upper arcade in shadow
    "floor_shadow": (300, 420, 660, 540),        # the floor under the camera, out of the sun
}
FIRST, LAST = 280, 299

BASE = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0}
# The Sponza comparisons: a linear, fixed exposure chain pinned on the command line (the scene's own
# cvars would otherwise override the project's), so the raster and the path tracer share one curve.
SPONZA = {**BASE, "r.AutoExposure": 0, "r.Exposure": 1, "r.ColorContrast": 1, "r.ColorSaturation": 1,
          "r.TonemapOperator": 0, "r.Bloom": 0, "r.TAA": 0, "r.Fog": 0, "r.VolumetricFog": 0, "r.GTAO": 0}
OFF = {"r.GI.SurfaceCache": 0}
ON = {"r.GI.SurfaceCache": 1, "r.GI.SurfaceCache.Stats": 1}
STAGE2 = {**ON, "r.GI.SurfaceCache.SdfReads": 0, "r.GI.SurfaceCache.FeedRays": 0, "r.GI.SurfaceCache.DDGI": 0}
PERF = {**BASE, "r.GI.SurfaceCache.Stats": 0}
# The stage 2 scene with the diffuse SDF consumers on (its project pins the exposure chain itself).
LL_FG = {**BASE, "r.GI.FinalGather": 1, "r.GI.Reflections": 1}
LL_FIRST, LL_LAST = 95, 99
# Regions of the 640 x 360 locallight frame lit by the lamp's BOUNCE only (the lamp itself shades
# the visible surfaces directly in the raster): the block's faces away from the lamp, the wall
# behind the block, the floor beyond the lamp's pool, and the mirror's lower half.
LL_ROIS = {
    "full": (0, 0, 640, 360),
    "block": (50, 238, 165, 335),
    "pool": (0, 300, 320, 360),
    "mirror": (0, 160, 640, 360),
    "floor_far": (320, 300, 640, 360),
    "wall_upper": (0, 60, 640, 160),
}

# name: (project, cvars, frames, screenshot_frames, extra_args, timing_first, window)
CASES = {
    "default": (REFERENCE_PROJECT, {**BASE, "r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1}, 100, "95", [], 200, None),
    "ll-pt": (LL_PROJECT, BASE, 512, "255-511:256", ["--path-trace"], 200, None),
    "sponza-off": (SPONZA_PROJECT, {**SPONZA, **OFF}, 300, f"{FIRST}-{LAST}", [], 200, None),
    "sponza-on": (SPONZA_PROJECT, {**SPONZA, **ON}, 300, f"{FIRST}-{LAST}", [], 200, None),
    "sponza-on-s2": (SPONZA_PROJECT, {**SPONZA, **STAGE2}, 300, f"{FIRST}-{LAST}", [], 200, None),
    "sponza-on-lod0": (SPONZA_PROJECT, {**SPONZA, **ON, "r.GI.SurfaceCache.SdfLodBias": 0}, 300, f"{FIRST}-{LAST}", [], 200, None),
    "sponza-on-feed0": (SPONZA_PROJECT, {**SPONZA, **ON, "r.GI.SurfaceCache.FeedRays": 0}, 300, f"{FIRST}-{LAST}", [], 200, None),
    # The DDGI hook (r.RayTracing + r.RayTracing.GI: the DXR probe writer reads and writes the cells).
    "sponza-on-ddgi": (SPONZA_PROJECT, {**SPONZA, **ON, "r.RayTracing": 1, "r.RayTracing.GI": 1}, 300, f"{FIRST}-{LAST}", [], 200, None),
    "sponza-off-ddgi": (SPONZA_PROJECT, {**SPONZA, **OFF, "r.RayTracing": 1, "r.RayTracing.GI": 1}, 300, f"{FIRST}-{LAST}", [], 200, None),
    # `--screenshot-frames` takes N, A-B or A-B:step: 255-511:256 captures frames 255 and 511.
    "sponza-pt": (SPONZA_PROJECT, SPONZA, 512, "255-511:256", ["--path-trace"], 200, None),
    "sponza-perf-off": (SPONZA_PROJECT, {**SPONZA, **PERF, "r.GI.SurfaceCache": 0}, 300, None, ["--width", "1920", "--height", "1080"], 150, None),
    "sponza-perf-on": (SPONZA_PROJECT, {**SPONZA, **PERF, "r.GI.SurfaceCache": 1}, 300, None, ["--width", "1920", "--height", "1080"], 150, None),
    "ll-fg-off": (LL_PROJECT, {**LL_FG, **OFF}, 100, f"{LL_FIRST}-{LL_LAST}", [], 200, None),
    "ll-fg-on": (LL_PROJECT, {**LL_FG, **ON}, 100, f"{LL_FIRST}-{LL_LAST}", [], 200, None),
    "ll-fg-s2": (LL_PROJECT, {**LL_FG, **STAGE2}, 100, f"{LL_FIRST}-{LL_LAST}", [], 200, None),
    "llgi-off": (LLGI_PROJECT, {**BASE, **OFF}, 100, f"{LL_FIRST}-{LL_LAST}", [], 200, None),
    "llgi-on": (LLGI_PROJECT, {**BASE, **ON}, 100, f"{LL_FIRST}-{LL_LAST}", [], 200, None),
    "llgi-s2": (LLGI_PROJECT, {**BASE, **STAGE2}, 100, f"{LL_FIRST}-{LL_LAST}", [], 200, None),
    "llgi-pt": (LLGI_PROJECT, BASE, 512, "255-511:256", ["--path-trace"], 200, None),
    "sponza-ptlegacy": (SPONZA_PROJECT, {**SPONZA, "r.PathTracer.Enable": 1}, 300, "299", [], 200, None),
}
# Regions of the 640 x 360 locallight-gi frame (chosen on the captures): the block's front face (in the
# lamp's shadow: bounce only), the floor beyond the lamp's pool, and the full frame.
LLGI_ROIS = {
    "full": (0, 0, 640, 360),
    "block_front": (245, 150, 360, 268),      # the block's camera-facing face: in the lamp's shadow, bounce only
    "block_top": (240, 140, 365, 160),        # the lamp-lit top edge (direct in the raster, a sanity row)
    "floor_right": (380, 200, 640, 360),      # the floor beside the block, toward the lamp's pool
    "floor_left": (0, 230, 230, 360),         # the floor away from the lamp: bounce only
    "mirror_block": (370, 90, 470, 175),      # the block's reflection in the mirror wall (the RT writer's cells)
}


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def crop(img, roi):
    x0, y0, x1, y1 = roi
    return img[..., y0:y1, x0:x1, :]


def rmse(a, b, roi):
    return float(np.sqrt(np.mean((crop(a, roi) - crop(b, roi)) ** 2)))


def identity(a: Path, b: Path):
    delta = np.abs(image(a) - image(b))
    return {"a": str(a), "b": str(b), "max_code_delta": int(delta.max()),
            "mean_abs_code_delta": float(delta.mean()), "px_differing": int((delta.max(axis=2) > 0).sum()),
            "png_bytes_identical": a.read_bytes() == b.read_bytes()}


def sequence(root: Path, name: str):
    return np.stack([image(root / name / "frames" / f"f_{f}.png") for f in range(FIRST, LAST + 1)])


def log_facts(root: Path, name: str, result: dict):
    log_path = root / name / "run.log"
    if not log_path.exists():
        return
    log = log_path.read_text(encoding="utf-8", errors="replace")
    result[f"{name}_compile_failed"] = "compile failed" in log
    result[f"{name}_device_removed"] = "DEVICE_REMOVED" in log or "0x887A0005" in log
    stats = re.findall(r"SurfaceCache stats: (.*)", log)
    if stats:
        result[f"{name}_last_stats_line"] = stats[-1].strip()
        m = re.search(r"sdfQueries=(\d+) sdfHits=(\d+) \(([\d.]+) %\) feedUpdates=(\d+)", stats[-1])
        if m:
            result[f"{name}_sdf_queries"] = int(m.group(1))
            result[f"{name}_sdf_hits"] = int(m.group(2))
            result[f"{name}_sdf_hit_pct"] = float(m.group(3))
            result[f"{name}_feed_updates"] = int(m.group(4))


def metrics(out: Path, before: Path | None):
    pt = image(out / "sponza-pt/frames/f_511.png")
    pt_prev = image(out / "sponza-pt/frames/f_255.png")
    result = {"rois_xyxy": ROIS, "frames": [FIRST, LAST]}
    for roi_name, roi in ROIS.items():
        result[f"pt_mean_{roi_name}_rgb"] = [float(v) for v in crop(pt, roi).mean(axis=(0, 1))]
        result[f"pt_256_to_512_rmse_{roi_name}"] = rmse(pt_prev, pt, roi)
    names = ["sponza-off", "sponza-on", "sponza-on-s2", "sponza-on-lod0", "sponza-on-feed0"]
    seqs = {}
    for name in names:
        if (out / name / "frames" / f"f_{LAST}.png").exists():
            seqs[name] = sequence(out, name)
    if before is not None:
        for name in ("sponza-off", "sponza-on"):
            if (before / name / "frames" / f"f_{LAST}.png").exists():
                seqs[f"before-{name}"] = sequence(before, name)
    for name, seq in seqs.items():
        mean = seq.mean(axis=0)
        for roi_name, roi in ROIS.items():
            result[f"{name}_mean_{roi_name}_rgb"] = [float(v) for v in crop(seq, roi).mean(axis=(0, 1, 2))]
            result[f"{name}_rmse_{roi_name}_vs_pt_mean"] = rmse(mean, pt, roi)
            result[f"{name}_rmse_{roi_name}_vs_pt_f299"] = rmse(seq[-1], pt, roi)
            result[f"{name}_mean_error_{roi_name}_vs_pt"] = float((crop(mean, roi) - crop(pt, roi)).mean())
            result[f"{name}_temporal_std_{roi_name}"] = float(crop(seq, roi).std(axis=0).mean())
    for name in seqs:
        if name in ("sponza-off", "before-sponza-off"):
            continue
        base = "before-sponza-off" if name.startswith("before") else "sponza-off"
        if base not in seqs:
            continue
        for roi_name in ROIS:
            result[f"{name}_minus_off_rmse_{roi_name}_vs_pt"] = (result[f"{name}_rmse_{roi_name}_vs_pt_mean"]
                                                               - result[f"{base}_rmse_{roi_name}_vs_pt_mean"])
    if "sponza-on" in seqs and "sponza-on-s2" in seqs:
        for roi_name in ROIS:
            result[f"stage3_minus_stage2_rmse_{roi_name}_vs_pt"] = (result[f"sponza-on_rmse_{roi_name}_vs_pt_mean"]
                                                                  - result[f"sponza-on-s2_rmse_{roi_name}_vs_pt_mean"])
    if "sponza-on-s2" in seqs and "before-sponza-on" in seqs:
        # C1: the stage 3 build with the stage 3 switches off against the before binary's cache-on capture.
        delta = np.abs(seqs["sponza-on-s2"] - seqs["before-sponza-on"])
        result["C1_stage2_equivalence_max_code_delta"] = int(delta.max())
        result["C1_stage2_equivalence_mean_abs_code_delta"] = float(delta.mean())
    if "sponza-off" in seqs and "before-sponza-off" in seqs:
        delta = np.abs(seqs["sponza-off"] - seqs["before-sponza-off"])
        result["off_vs_before_off_max_code_delta"] = int(delta.max())
        result["off_vs_before_off_mean_abs_code_delta"] = float(delta.mean())
    for name in names + ["sponza-pt"]:
        log_facts(out, name, result)
    print(json.dumps(result, indent=2))
    (out / "metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


def ll_metrics(out: Path, before: Path | None):
    pt = image(out / "ll-pt/frames/f_511.png")
    pt_prev = image(out / "ll-pt/frames/f_255.png")
    result = {"rois_xyxy": LL_ROIS, "frames": [LL_FIRST, LL_LAST]}
    for roi_name, roi in LL_ROIS.items():
        result[f"pt_mean_{roi_name}_rgb"] = [float(v) for v in crop(pt, roi).mean(axis=(0, 1))]
        result[f"pt_256_to_512_rmse_{roi_name}"] = rmse(pt_prev, pt, roi)
    seqs = {}
    for name in ("ll-fg-off", "ll-fg-on", "ll-fg-s2"):
        if (out / name / "frames" / f"f_{LL_LAST}.png").exists():
            seqs[name] = np.stack([image(out / name / "frames" / f"f_{f}.png") for f in range(LL_FIRST, LL_LAST + 1)])
    if before is not None:
        for name in ("ll-fg-off", "ll-fg-on"):
            if (before / name / "frames" / f"f_{LL_LAST}.png").exists():
                seqs[f"before-{name}"] = np.stack([image(before / name / "frames" / f"f_{f}.png") for f in range(LL_FIRST, LL_LAST + 1)])
    for name, seq in seqs.items():
        mean = seq.mean(axis=0)
        for roi_name, roi in LL_ROIS.items():
            result[f"{name}_mean_{roi_name}_rgb"] = [float(v) for v in crop(seq, roi).mean(axis=(0, 1, 2))]
            result[f"{name}_rmse_{roi_name}_vs_pt_mean"] = rmse(mean, pt, roi)
            result[f"{name}_mean_error_{roi_name}_vs_pt"] = float((crop(mean, roi) - crop(pt, roi)).mean())
    for name in seqs:
        if name.endswith("ll-fg-off"):
            continue
        base = "before-ll-fg-off" if name.startswith("before") else "ll-fg-off"
        if base in seqs:
            for roi_name in LL_ROIS:
                result[f"{name}_minus_off_rmse_{roi_name}_vs_pt"] = (result[f"{name}_rmse_{roi_name}_vs_pt_mean"]
                                                                   - result[f"{base}_rmse_{roi_name}_vs_pt_mean"])
    if "ll-fg-s2" in seqs and "before-ll-fg-on" in seqs:
        delta = np.abs(seqs["ll-fg-s2"] - seqs["before-ll-fg-on"])
        result["C1_stage2_equivalence_max_code_delta"] = int(delta.max())
        result["C1_stage2_equivalence_mean_abs_code_delta"] = float(delta.mean())
    if "ll-fg-off" in seqs and "before-ll-fg-off" in seqs:
        delta = np.abs(seqs["ll-fg-off"] - seqs["before-ll-fg-off"])
        result["off_vs_before_off_max_code_delta"] = int(delta.max())
    for name in ("ll-fg-off", "ll-fg-on", "ll-fg-s2", "ll-pt"):
        log_facts(out, name, result)
    print(json.dumps(result, indent=2))
    (out / "ll_metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


def llgi_metrics(out: Path, before: Path | None):
    pt = image(out / "llgi-pt/frames/f_511.png")
    pt_prev = image(out / "llgi-pt/frames/f_255.png")
    result = {"rois_xyxy": LLGI_ROIS, "frames": [LL_FIRST, LL_LAST]}
    for roi_name, roi in LLGI_ROIS.items():
        result[f"pt_mean_{roi_name}_rgb"] = [float(v) for v in crop(pt, roi).mean(axis=(0, 1))]
        result[f"pt_256_to_512_rmse_{roi_name}"] = rmse(pt_prev, pt, roi)
    seqs = {}
    for name in ("llgi-off", "llgi-on", "llgi-s2"):
        if (out / name / "frames" / f"f_{LL_LAST}.png").exists():
            seqs[name] = np.stack([image(out / name / "frames" / f"f_{f}.png") for f in range(LL_FIRST, LL_LAST + 1)])
    if before is not None:
        for name in ("llgi-off", "llgi-on"):
            if (before / name / "frames" / f"f_{LL_LAST}.png").exists():
                seqs[f"before-{name}"] = np.stack([image(before / name / "frames" / f"f_{f}.png") for f in range(LL_FIRST, LL_LAST + 1)])
    for name, seq in seqs.items():
        mean = seq.mean(axis=0)
        for roi_name, roi in LLGI_ROIS.items():
            result[f"{name}_mean_{roi_name}_rgb"] = [float(v) for v in crop(seq, roi).mean(axis=(0, 1, 2))]
            result[f"{name}_rmse_{roi_name}_vs_pt_mean"] = rmse(mean, pt, roi)
            result[f"{name}_mean_error_{roi_name}_vs_pt"] = float((crop(mean, roi) - crop(pt, roi)).mean())
    for name in seqs:
        if name.endswith("llgi-off"):
            continue
        base = "before-llgi-off" if name.startswith("before") else "llgi-off"
        if base in seqs:
            for roi_name in LLGI_ROIS:
                result[f"{name}_minus_off_rmse_{roi_name}_vs_pt"] = (result[f"{name}_rmse_{roi_name}_vs_pt_mean"]
                                                                   - result[f"{base}_rmse_{roi_name}_vs_pt_mean"])
    if "llgi-on" in seqs and "llgi-s2" in seqs:
        for roi_name in LLGI_ROIS:
            result[f"stage3_minus_stage2_rmse_{roi_name}_vs_pt"] = (result[f"llgi-on_rmse_{roi_name}_vs_pt_mean"]
                                                                  - result[f"llgi-s2_rmse_{roi_name}_vs_pt_mean"])
    if "llgi-s2" in seqs and "before-llgi-on" in seqs:
        delta = np.abs(seqs["llgi-s2"] - seqs["before-llgi-on"])
        result["C1_stage2_equivalence_max_code_delta"] = int(delta.max())
        result["C1_stage2_equivalence_mean_abs_code_delta"] = float(delta.mean())
    if "llgi-off" in seqs and "before-llgi-off" in seqs:
        delta = np.abs(seqs["llgi-off"] - seqs["before-llgi-off"])
        result["off_vs_before_off_max_code_delta"] = int(delta.max())
    for name in ("llgi-off", "llgi-on", "llgi-s2", "llgi-pt"):
        log_facts(out, name, result)
    print(json.dumps(result, indent=2))
    (out / "llgi_metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


def gpu_ms(path: Path):
    m = json.loads(path.read_text(encoding="utf-8"))
    return m.get("gpu_ms") or {}, m.get("frame_ms") or {}


def perf_metrics(out: Path):
    result = {}
    keys = ("surfacecache", "surface_cache", "ssgi", "radiancecache", "radiance_cache", "rt_reflection", "rtreflection",
            "deferredlighting", "deferred_lighting", "lighting", "ddgi", "gdf")
    for name in ("sponza-perf-off", "sponza-perf-on"):
        manifest = out / name / "manifest.json"
        if not manifest.exists():
            continue
        gpu, frame = gpu_ms(manifest)
        entry = {"gpu_total_ms": gpu.get("_total"), "cpu_frame_ms": frame.get("mean")}
        for key, value in gpu.items():
            low = key.lower()
            if any(k in low for k in keys):
                entry[key] = value
        log = (out / name / "run.log").read_text(encoding="utf-8", errors="replace")
        stats = re.findall(r"SurfaceCache stats: (.*)", log)
        if stats:
            entry["last_stats_line"] = stats[-1].strip()
        entry["compile_failed"] = "compile failed" in log
        entry["device_removed"] = "DEVICE_REMOVED" in log or "0x887A0005" in log
        result[name] = entry
    if "sponza-perf-off" in result and "sponza-perf-on" in result:
        off, on = result["sponza-perf-off"], result["sponza-perf-on"]
        for key in list(on.keys()):
            if isinstance(on[key], (int, float)) and isinstance(off.get(key), (int, float)):
                result[f"delta_{key}_ms"] = on[key] - off[key]
    print(json.dumps(result, indent=2))
    (out / "perf.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=["default-metrics", "metrics", "ll-metrics", "llgi-metrics", "perf-metrics", *CASES], required=True)
    parser.add_argument("--reference", type=Path, help="default-metrics: the before-change capture of frame 95")
    parser.add_argument("--before", type=Path, help="metrics / default-metrics: the before-change capture directory")
    parser.add_argument("--exe", type=Path, help="the engine binary to capture with (default: the tree's)")
    args = parser.parse_args()
    out = args.out.resolve()
    if args.exe:
        runner.EXE = str(args.exe.resolve())
    if args.case == "default-metrics":
        result = {}
        if args.reference is not None:
            result["vs_reference"] = identity(out / "default/frames/f_95.png", args.reference.resolve())
        if args.before is not None:
            for case in ("ll-pt", "sponza-pt"):
                for frame in (255, 511):
                    a = out / f"{case}/frames/f_{frame}.png"
                    b = args.before.resolve() / f"{case}/frames/f_{frame}.png"
                    if a.exists() and b.exists():
                        result[f"{case}_f{frame}_vs_before"] = identity(a, b)
        print(json.dumps(result, indent=2))
        (out / "default_metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return
    if args.case == "metrics":
        metrics(out, args.before.resolve() if args.before else None)
        return
    if args.case == "ll-metrics":
        ll_metrics(out, args.before.resolve() if args.before else None)
        return
    if args.case == "llgi-metrics":
        llgi_metrics(out, args.before.resolve() if args.before else None)
        return
    if args.case == "perf-metrics":
        perf_metrics(out)
        return

    project, cvars, frames, shots, extra, timing_first, _ = CASES[args.case]
    result = runner.run_capture(str(out / args.case), str(project), frames, clean=False, timeout=180,
                                cvars=cvars, extra_args=extra, screenshot_frames=shots, timing_first=timing_first)
    assert result["exit_code"] == 0, result["exit_code"]
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log, result["log"]
    assert "DEVICE_REMOVED" not in log and "device removed" not in log.lower(), result["log"]
    print(f'{args.case}: exit=0; {result["frames_dir"]}', flush=True)


if __name__ == "__main__":
    main()
