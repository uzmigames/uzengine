"""Bounded Phase D stage 2 captures: local lights + emissive at the surface-cache cell (spec section 4,
"Stage 2", acceptance B1-B4). Run each case separately (180 seconds maximum per engine run); pass
`--exe` to capture with another binary (the before-change build for B1 / B3).

Cases (surface-cache-locallight.uzproject unless noted: a mirror wall reflecting an off-screen block
lit by a rect light paired with a coincident emissive fixture — the raster sees the rect light, the
path tracer the fixture; no sun, black sky, so a stage-1 cell holds only the probe term):
  default        reflection-reference, frame 95 of 100, no cache CVar (B1, byte-identical to `--exe` before)
  ll-off         the cache off, 100 frames, frames 95-99
  ll-on          the cache on (stage 2 defaults: LocalLights 4, Emissive 1), Stats 1
  ll-on-stage1   the cache on with LocalLights 0 + Emissive 0 (B3: the stage-1 estimator)
  ll-on-k1       the cache on with LocalLights 1 (one shadow ray per shaded hit)
  ll-pt          the converged DXR path tracer, frames 255 and 511 (the B2 reference; also the PT
                 byte-identity capture of B1 when run with both binaries)
  ll-perf-off / ll-perf-on   300 frames timed (GPU ms per pass from the manifest, B4)
  sponza-perf-off / sponza-perf-on   _sponza-tour 300 frames, every texel traced (B4 on Sponza)
  metrics        ROI means / RMSE vs the PT for every captured ll-* case -> metrics.json
  perf-metrics   the perf manifests -> perf.json
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
LL_PROJECT = HERE / "surface-cache-locallight.uzproject"
REFERENCE_PROJECT = HERE / "reflection-reference.uzproject"
SPONZA_PROJECT = HERE / "_sponza-tour.uzproject"

# Regions (x0, y0, x1, y1) of the 640 x 360 frame, measured on the captures: the reflected block (the
# ray pass puts its near face at x 58-129 / y 255-332, where the mirror geometry projects it for the
# vertical 40-degree fov; the path tracer draws it about 25 px to the right and larger - the PT's
# projection differs from the raster's, the Phase B follow-up (2) - so the box covers both), the
# reflected emissive fixture (a sliver at the top of the mirror; the PT's glossy lobe smears it into
# an ellipse), the lit floor pool under the lamp (the PT also lights the floor along the mirror's
# base through the mirror itself, a specular caustic no raster path has), and the mirror's lower
# half (everything lit).
ROI_BLOCK = (50, 238, 165, 335)
ROI_FIXTURE = (0, 165, 135, 180)
ROI_POOL = (0, 300, 320, 360)
ROI_MIRROR = (0, 160, 640, 360)
FIRST, LAST = 95, 99

BASE = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0}
OFF = {"r.GI.SurfaceCache": 0}
ON = {"r.GI.SurfaceCache": 1, "r.GI.SurfaceCache.Stats": 1}
STAGE1 = {**ON, "r.GI.SurfaceCache.LocalLights": 0, "r.GI.SurfaceCache.Emissive": 0}
SPONZA = {**BASE, "r.RayTracing.Reflections.MaxRoughness": 1.0}

# name: (project, cvars, frames, screenshot_frames, extra_args, timing_first)
CASES = {
    "default": (REFERENCE_PROJECT, {**BASE, "r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1}, 100, "95", [], 200),
    "ll-off": (LL_PROJECT, {**BASE, **OFF}, 100, f"{FIRST}-{LAST}", [], 200),
    "ll-on": (LL_PROJECT, {**BASE, **ON}, 100, f"{FIRST}-{LAST}", [], 200),
    "ll-on-stage1": (LL_PROJECT, {**BASE, **STAGE1}, 100, f"{FIRST}-{LAST}", [], 200),
    "ll-on-k1": (LL_PROJECT, {**BASE, **ON, "r.GI.SurfaceCache.LocalLights": 1}, 100, f"{FIRST}-{LAST}", [], 200),
    # The two stage 2 terms on their own (lights without the emitter, the emitter without lights).
    "ll-on-lights": (LL_PROJECT, {**BASE, **ON, "r.GI.SurfaceCache.Emissive": 0}, 100, f"{FIRST}-{LAST}", [], 200),
    "ll-on-emissive": (LL_PROJECT, {**BASE, **ON, "r.GI.SurfaceCache.LocalLights": 0}, 100, f"{FIRST}-{LAST}", [], 200),
    # `--screenshot-frames` takes N, A-B or A-B:step: 255-511:256 captures frames 255 and 511.
    "ll-pt": (LL_PROJECT, BASE, 512, "255-511:256", ["--path-trace"], 200),
    "ll-perf-off": (LL_PROJECT, {**BASE, **OFF}, 300, None, [], 150),
    "ll-perf-on": (LL_PROJECT, {**BASE, "r.GI.SurfaceCache": 1}, 300, None, [], 150),
    "ll-perf-on-stage1": (LL_PROJECT, {**BASE, "r.GI.SurfaceCache": 1, "r.GI.SurfaceCache.LocalLights": 0,
                                       "r.GI.SurfaceCache.Emissive": 0}, 300, None, [], 150),
    "sponza-perf-off": (SPONZA_PROJECT, {**SPONZA, **OFF}, 300, None, [], 150),
    "sponza-perf-on": (SPONZA_PROJECT, {**SPONZA, "r.GI.SurfaceCache": 1}, 300, None, [], 150),
    "sponza-perf-on-stage1": (SPONZA_PROJECT, {**SPONZA, "r.GI.SurfaceCache": 1, "r.GI.SurfaceCache.LocalLights": 0,
                                               "r.GI.SurfaceCache.Emissive": 0}, 300, None, [], 150),
}


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def crop(img, roi):
    """The ROI of one frame (H, W, 3) or of a frame stack (N, H, W, 3): the trailing axes are sliced."""
    x0, y0, x1, y1 = roi
    return img[..., y0:y1, x0:x1, :]


def rmse(a, b, roi):
    return float(np.sqrt(np.mean((crop(a, roi) - crop(b, roi)) ** 2)))


def identity(a: Path, b: Path):
    delta = np.abs(image(a) - image(b))
    return {"a": str(a), "b": str(b), "max_code_delta": int(delta.max()),
            "mean_abs_code_delta": float(delta.mean()), "px_differing": int((delta.max(axis=2) > 0).sum()),
            "png_bytes_identical": a.read_bytes() == b.read_bytes()}


def sequence(out: Path, name: str):
    return np.stack([image(out / name / "frames" / f"f_{f}.png") for f in range(FIRST, LAST + 1)])


def metrics(out: Path, before: Path | None):
    pt = image(out / "ll-pt/frames/f_511.png")
    pt_prev = image(out / "ll-pt/frames/f_255.png")
    rois = (("block", ROI_BLOCK), ("fixture", ROI_FIXTURE), ("pool", ROI_POOL), ("mirror", ROI_MIRROR))
    result = {"roi_block_xyxy": ROI_BLOCK, "roi_fixture_xyxy": ROI_FIXTURE, "roi_pool_xyxy": ROI_POOL,
              "roi_mirror_xyxy": ROI_MIRROR, "frames": [FIRST, LAST]}
    for roi_name, roi in rois:
        result[f"pt_mean_{roi_name}_rgb"] = [float(v) for v in crop(pt, roi).mean(axis=(0, 1))]
        result[f"pt_256_to_512_rmse_{roi_name}"] = rmse(pt_prev, pt, roi)
    sources = {n: out for n in ("ll-off", "ll-on", "ll-on-stage1", "ll-on-k1", "ll-on-lights", "ll-on-emissive")}
    if before is not None:
        sources.update({f"before-{n}": before for n in ("ll-off", "ll-on")})
    seqs = {}
    for name, root in sources.items():
        case = name.replace("before-", "")
        if (root / case / "frames" / f"f_{LAST}.png").exists():
            seqs[name] = np.stack([image(root / case / "frames" / f"f_{f}.png") for f in range(FIRST, LAST + 1)])
    for name, seq in seqs.items():
        mean = seq.mean(axis=0)
        for roi_name, roi in rois:
            result[f"{name}_mean_{roi_name}_rgb"] = [float(v) for v in crop(seq, roi).mean(axis=(0, 1, 2))]
            result[f"{name}_rmse_{roi_name}_vs_pt_mean95_99"] = rmse(mean, pt, roi)
            result[f"{name}_rmse_{roi_name}_vs_pt_f99"] = rmse(seq[-1], pt, roi)
            result[f"{name}_temporal_std_{roi_name}"] = float(crop(seq, roi).std(axis=0).mean())
    for name in seqs:
        if name in ("ll-off", "before-ll-off"):
            continue
        base = "before-ll-off" if name.startswith("before") else "ll-off"
        if base not in seqs:
            continue
        for roi_name, _ in rois:
            result[f"{name}_minus_off_rmse_{roi_name}_vs_pt"] = (result[f"{name}_rmse_{roi_name}_vs_pt_mean95_99"]
                                                               - result[f"{base}_rmse_{roi_name}_vs_pt_mean95_99"])
    if "ll-on" in seqs and "ll-on-stage1" in seqs:
        for roi_name, _ in rois:
            result[f"stage2_minus_stage1_rmse_{roi_name}_vs_pt"] = (result[f"ll-on_rmse_{roi_name}_vs_pt_mean95_99"]
                                                                  - result[f"ll-on-stage1_rmse_{roi_name}_vs_pt_mean95_99"])
    if "ll-on" in seqs and "before-ll-on" in seqs:
        for roi_name, _ in rois:
            result[f"stage2_minus_before_stage1_rmse_{roi_name}_vs_pt"] = (result[f"ll-on_rmse_{roi_name}_vs_pt_mean95_99"]
                                                                         - result[f"before-ll-on_rmse_{roi_name}_vs_pt_mean95_99"])
    if "ll-on-stage1" in seqs and "before-ll-on" in seqs:
        # B3: the stage-2 build with the stage-2 terms off against the stage-1 build's cache capture.
        delta = np.abs(seqs["ll-on-stage1"] - seqs["before-ll-on"])
        result["B3_stage1_equivalence_max_code_delta_f95_99"] = int(delta.max())
        result["B3_stage1_equivalence_mean_abs_code_delta"] = float(delta.mean())
    for name, root in sources.items():
        case = name.replace("before-", "")
        log_path = root / case / "run.log"
        if not log_path.exists():
            continue
        log = log_path.read_text(encoding="utf-8", errors="replace")
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
    for name in ("ll-perf-off", "ll-perf-on", "ll-perf-on-stage1", "sponza-perf-off", "sponza-perf-on", "sponza-perf-on-stage1"):
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
            entry["last_stats_line"] = stats[-1].strip()
        entry["compile_failed"] = "compile failed" in log
        entry["device_removed"] = "DEVICE_REMOVED" in log or "0x887A0005" in log
        result[name] = entry
    for off, on in (("ll-perf-off", "ll-perf-on"), ("ll-perf-on-stage1", "ll-perf-on"),
                    ("sponza-perf-off", "sponza-perf-on"), ("sponza-perf-on-stage1", "sponza-perf-on")):
        if off in result and on in result:
            for key in list(result[on].keys()):
                if "rt_reflection" in key.lower() and key in result[off] and result[off][key]:
                    result[f"{on}_vs_{off}_rt_reflections_delta_ms"] = result[on][key] - result[off][key]
                    result[f"{on}_vs_{off}_rt_reflections_growth_pct"] = (result[on][key] / result[off][key] - 1.0) * 100.0
    print(json.dumps(result, indent=2))
    (out / "perf.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=["default-metrics", "metrics", "perf-metrics", *CASES], required=True)
    parser.add_argument("--reference", type=Path, help="default-metrics: the before-change capture of frame 95")
    parser.add_argument("--before", type=Path, help="metrics: the before-change capture directory (ll-off / ll-on / ll-pt)")
    parser.add_argument("--exe", type=Path, help="the engine binary to capture with (default: the tree's)")
    args = parser.parse_args()
    out = args.out.resolve()
    if args.exe:
        runner.EXE = str(args.exe.resolve())
    if args.case == "default-metrics":
        result = {"vs_reference": identity(out / "default/frames/f_95.png", args.reference.resolve())}
        if args.before is not None:
            for frame in (255, 511):
                a = out / f"ll-pt/frames/f_{frame}.png"
                b = args.before.resolve() / f"ll-pt/frames/f_{frame}.png"
                if a.exists() and b.exists():
                    result[f"pt_f{frame}_vs_before"] = identity(a, b)
        print(json.dumps(result, indent=2))
        (out / "default_metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return
    if args.case == "metrics":
        metrics(out, args.before.resolve() if args.before else None)
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
