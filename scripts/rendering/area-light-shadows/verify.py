"""Bounded LTC-shadow captures, HDR ROI metrics, temporal check and montage.

prepare --out <directory>
capture --out <directory> --kind rect|disk|tube --mode off|on|pt|default [--frame 383]
  (default = the ON fixture with no r.Lighting.AreaLightShadows override)
capture --out <directory> --kind reference --mode off --exe <fresh-baseline.exe>
analyze --out <directory>
All runs default to a 180-second timeout. No repository asset is generated.
"""
from __future__ import annotations

import argparse
import copy
import importlib.util
import json
import math
import os
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from flicker import runner, regiondump  # noqa: E402

# Measured on this fixture (occluder 30x24 px raster vs 15x12 px PT, offset from the image
# centre 49 vs 25 px, same recorded camera and resolution): the --path-trace camera spans
# 2x tan(fov/2) of the raster one (pre-existing, outside this feature). The PT captures
# get the FOV whose half-angle tangent is halved so both project identically.
RASTER_FOV = 70.0
PT_FOV = 2.0 * math.degrees(math.atan(math.tan(math.radians(RASTER_FOV / 2.0)) / 2.0))

spec = importlib.util.spec_from_file_location("shape_reference", HERE.parent / "area-light-shapes" / "verify.py")
shapes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shapes)


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def prepare(root):
    fixtures = root / "fixtures"
    (fixtures / "meshes").mkdir(parents=True, exist_ok=True)
    base = json.loads((HERE / "shadow.uzscene").read_text(encoding="utf-8"))
    project = json.loads((HERE.parent / "area-light-shapes" / "disk.uzproject").read_text(encoding="utf-8"))
    project["cvars"]["r.RayTracing.Reflections"] = 0
    for kind in ("rect", "disk", "tube"):
        if kind != "rect":
            (fixtures / "meshes" / f"{kind}.uzmesh").write_bytes(shapes.emitter_mesh(kind))
        for mode in ("off", "on", "pt"):
            scene = copy.deepcopy(base)
            scene["entities"] = [e for e in scene["entities"] if e["name"] !=
                                 ("Analytic emitter" if mode == "pt" else "PT emitter")]
            for entity in scene["entities"]:
                c = entity["components"]
                if entity["name"] == "Analytic emitter":
                    c["light"]["type"] = kind
                    c["light"]["height"] = {"rect": 80, "disk": 120, "tube": 30}[kind]
                if entity["name"] == "Camera" and mode == "pt":
                    c["camera"]["fov"] = PT_FOV
                if entity["name"] == "PT emitter" and kind != "rect":
                    c["transform"] = {"position": [0, 0, 0]}
                    c["mesh"]["asset"] = f"meshes/{kind}.uzmesh"
                mesh = c.get("mesh")
                if mesh and mesh["asset"].startswith("sample/"):
                    mesh["asset"] = Path(os.path.relpath(REPO / mesh["asset"], fixtures)).as_posix()
            name = f"{kind}-{mode}"
            save_json(fixtures / f"{name}.uzscene", scene)
            save_json(fixtures / f"{name}.uzproject",
                      {**project, "name": name, "content_root": ".", "default_scene": f"{name}.uzscene"})
    print(fixtures)


def capture(args):
    runner.EXE = str(args.exe.resolve())
    reference = args.kind == "reference"
    name = f"{args.kind}-{args.mode}" + (f"-f{args.frame}" if args.frame != 95 else "") + args.suffix
    dest = args.out / name
    if dest.exists():
        raise SystemExit(f"Refusing overwrite: {dest}")
    fixture_mode = "on" if args.mode == "default" else args.mode
    project = REPO / "scripts/rendering/reflection-reference.uzproject" if reference else (
        args.out / "fixtures" / f"{args.kind}-{fixture_mode}.uzproject")
    old = Path.cwd()
    os.chdir(args.cwd)
    try:
        result = runner.run_capture(
            str(dest), str(project), args.frame + 5,
            regions=None if reference else [("shadow", 270, 155, 100, 80)],
            region_fields="output", region_first=max(0, args.frame - 15), region_last=args.frame,
            screenshot_frames=str(args.frame), timing_first=32, timeout=args.timeout, clean=False,
            cvars={**({} if args.mode == "default" else
                      {"r.Lighting.AreaLightShadows": int(args.mode == "on")}),
                   **({"r.RayTracing.SimulateUnsupported": 1} if args.simulate_unsupported else {})},
            extra_args=["--hidden"] + (["--path-trace", "--pt-bounces", "2"] if args.mode == "pt" else []))
        if result["exit_code"] != 0:
            raise SystemExit(f"Capture failed: {result['exit_code']} {result['log']}")
        if not reference:
            regiondump.load_sequence(str(dest / "region"), max(0, args.frame - 15), args.frame)
        print(dest)
    finally:
        os.chdir(old)


def analyze(args):
    report = {"runs": {}, "notes": [
        "Separate diffuse/specular LTC density-weighted self-normalized visibility, 16 Hammersley source "
        "samples toroidally shifted per pixel (IGN) and per frame (R2); the existing TAA accumulates them.",
        "Ratios are bounded in [0,1] (no fireflies); finite-sample bias remains. No dedicated shadow history.",
        "PT uses matching emissive geometry, two bounces, black environment.",
        f"PT fixtures use fov {PT_FOV:.3f} instead of {RASTER_FOV}: the --path-trace camera spans 2x "
        "tan(fov/2) of the raster one (pre-existing); with it the occluder projects on identical pixels.",
        "Base-BRDF raster/PT energy partition residual is unchanged; ROI error is not pure shadow error."
    ]}
    montage = Image.new("RGB", (640 * 3, 390 * 3), "#181818")
    draw = ImageDraw.Draw(montage)
    penumbra = Image.new("RGB", (400 * 3, 350 * 3), "#181818")
    penumbra_draw = ImageDraw.Draw(penumbra)
    for row, kind in enumerate(("rect", "disk", "tube")):
        hdr = {}
        for col, mode in enumerate(("off", "on", "pt")):
            frame = args.pt_frame if mode == "pt" else 95
            name = f"{kind}-{mode}" + (f"-f{frame}" if frame != 95 else "")
            dest = args.out / name
            seq = regiondump.load_sequence(str(dest / "region"), frame - 15, frame)
            values = np.stack([f.rect("shadow").channel("output")[..., :3] for f in seq])
            if not np.isfinite(values).all():
                raise ValueError(f"{name}: non-finite HDR pixels")
            hdr[mode] = values[-1]
            lum = values.mean(-1)
            manifest = json.loads((dest / "manifest.json").read_text(encoding="utf-8"))
            report["runs"][name] = {
                "mean_hdr": float(values[-1].mean()), "max_hdr": float(values.max()),
                "temporal_pixel_std_mean": float(lum.std(0).mean()),
                "temporal_pixel_std_max": float(lum.std(0).max()),
                "max_frame_delta": float(np.abs(np.diff(lum, axis=0)).max()),
                "gpu_ms": manifest.get("gpu_ms"), "frame_ms": manifest.get("frame_ms"),
                "identity": runner.capture_identity(str(dest))}
            image = Image.open(dest / "frames" / f"f_{frame}.png").convert("RGB")
            montage.paste(image, (col * 640, row * 390 + 30))
            draw.text((col * 640 + 10, row * 390 + 8), f"{kind} {mode}, frame {frame}", fill="white")
            penumbra.paste(image.crop((270, 155, 370, 235)).resize((400, 320), Image.Resampling.NEAREST),
                           (col * 400, row * 350 + 30))
            penumbra_draw.text((col * 400 + 10, row * 350 + 8),
                               f"{kind} {mode}: fixed shadow ROI, 4x", fill="white")
        # Fixed shadow ROI shared by the three captures, never chosen using ON error.
        report[kind] = {
            "roi": [270, 155, 100, 80],
            "off_vs_pt_rmse": float(np.sqrt(np.mean((hdr["off"] - hdr["pt"]) ** 2))),
            "on_vs_pt_rmse": float(np.sqrt(np.mean((hdr["on"] - hdr["pt"]) ** 2))),
            "off_mean": float(hdr["off"].mean()), "on_mean": float(hdr["on"].mean()),
            "pt_mean": float(hdr["pt"].mean())}
        off_timing = report["runs"][f"{kind}-off"]["gpu_ms"]["DeferredLighting"]
        on_timing = report["runs"][f"{kind}-on"]["gpu_ms"]["DeferredLighting"]
        report[kind]["added_lighting_ms"] = on_timing - off_timing
        report[kind]["visibility_profile_y180_x270_to369"] = (
            hdr["on"][25].mean(-1) / np.maximum(hdr["off"][25].mean(-1), 1e-8)).tolist()
    report["byte_identity"] = []
    for a, b in (("reference-off-baseline", "reference-off"),
                 ("reference-off", "reference-on"), ("reference-off-baseline", "reference-default"),
                 ("rect-off-baseline", "rect-off"), ("rect-off", "rect-on-nodxr"),
                 ("rect-on", "rect-default")):
        pa, pb = (args.out / name / "frames/f_95.png" for name in (a, b))
        if pa.is_file() and pb.is_file():
            delta = np.abs(np.asarray(Image.open(pa), dtype=int) - np.asarray(Image.open(pb), dtype=int))
            report["byte_identity"].append({"a": a, "b": b, "max_code_delta": int(delta.max())})
    montage.save(args.out / "shadow-montage.png")
    penumbra.save(args.out / "penumbra-montage.png")
    save_json(args.out / "report.json", report)
    print(json.dumps({k: report[k] for k in ("rect", "disk", "tube")}, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("prepare", "capture", "analyze"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--kind", choices=("rect", "disk", "tube", "reference"), default="rect")
    parser.add_argument("--mode", choices=("off", "on", "pt", "default"), default="off")
    parser.add_argument("--frame", type=int, default=95)
    parser.add_argument("--pt-frame", type=int, default=383)
    parser.add_argument("--suffix", default="")
    parser.add_argument("--simulate-unsupported", action="store_true")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--exe", type=Path, default=Path(runner.EXE))
    parser.add_argument("--cwd", type=Path, default=REPO / "runtime")
    args = parser.parse_args()
    args.out = args.out.resolve()
    if args.command == "prepare":
        prepare(args.out)
    elif args.command == "capture":
        capture(args)
    else:
        analyze(args)


if __name__ == "__main__":
    main()
