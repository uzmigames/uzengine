"""Bounded headful captures for the cloud aerial-perspective acceptance gates."""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

from flicker.runner import run_capture

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / ".screenshots/cloud-aerial"


def capture(label, project, *, shots="95", frames=130, cvars=None, gbv=False):
    result = run_capture(
        str(OUT / label), str(REPO / project), frames, timeout=175, clean=False,
        screenshot_frames=shots, timing_first=30,
        cvars={"r.TechPanel.Show": 0, "r.Toolbar.Show": 0, **(cvars or {})},
        extra_args=["--gpu-validation"] if gbv else [])
    print(label, "CAPTURE_EXIT", result["exit_code"], flush=True)
    if result["exit_code"]:
        raise SystemExit(result["exit_code"])


def metrics():
    import numpy as np
    from PIL import Image, ImageDraw

    def image(path):
        return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)

    report = {}
    reference = image("E:/UzmiEngine/f005-after/frames/f_95.png")
    actual = image(OUT / "reflection/frames/f_95.png")
    delta = np.abs(actual - reference)
    report["reflection"] = {"max_code_delta": float(delta.max()),
                            "mean_code_delta": float(delta.mean()),
                            "identical_pixels": bool(np.array_equal(actual, reference)),
                            "identical_png_bytes": (
                                (OUT / "reflection/frames/f_95.png").read_bytes()
                                == Path("E:/UzmiEngine/f005-after/frames/f_95.png").read_bytes())}
    for pose in ("sun", "away", "above", "horizon"):
        before = image(OUT / f"before/{pose}/frames/f_95.png")
        after = image(OUT / f"after/{pose}/frames/f_95.png")
        delta = np.abs(after - before)
        report[pose] = {"max_code_delta": float(delta.max()),
                        "mean_code_delta": float(delta.mean())}
    for label in ("after/sun", "temporal"):
        with open(OUT / label / "manifest.json", encoding="utf-8") as source:
            manifest = json.load(source)
        report[label] = {key: manifest.get(key) for key in ("resolution", "gpu_ms", "frame_ms")}
    for label in ("stability", "stability-temporal"):
        paths = sorted((OUT / label / "frames").glob("f_*.png"),
                       key=lambda p: int(p.stem.split("_")[-1]))
        # Central sky/cloud region, excluding the horizon and screen edges.
        previous = image(paths[0])[80:500, 400:1500]
        means, maxima = [], []
        for path in paths[1:]:
            current = image(path)[80:500, 400:1500]
            delta = np.abs(current - previous)
            means.append(float(delta.mean()))
            maxima.append(float(delta.max()))
            previous = current
        report[label] = {"frames": len(paths), "region_xywh": [400, 80, 1100, 420],
                         "max_adjacent_mean_code_delta": max(means),
                         "mean_adjacent_code_delta": float(np.mean(means)),
                         "max_adjacent_code_delta": max(maxima)}
        sheet = Image.new("RGB", (3200, 1080))
        draw = ImageDraw.Draw(sheet)
        for index, path in enumerate(paths):
            x, y = (index % 10) * 320, (index // 10) * 180
            with Image.open(path) as frame:
                sheet.paste(frame.convert("RGB").resize((320, 180)), (x, y))
            draw.text((x + 4, y + 4), path.stem, fill="red")
        sheet.save(OUT / f"{label}-contact.jpg")
    comparison = Image.new("RGB", (1920, 2160))
    draw = ImageDraw.Draw(comparison)
    for row, pose in enumerate(("sun", "away", "above", "horizon")):
        for column, label in enumerate(("before", "after")):
            with Image.open(OUT / label / pose / "frames/f_95.png") as frame:
                comparison.paste(frame.convert("RGB").resize((960, 540)), (column * 960, row * 540))
            draw.text((column * 960 + 8, row * 540 + 8), f"{label} {pose}", fill="red")
    comparison.save(OUT / "comparison.jpg")
    sunset = image(OUT / "sunset/frames/f_95.png") / 255
    sunset = np.where(sunset <= 0.04045, sunset / 12.92, ((sunset + 0.055) / 1.055) ** 2.4)
    y = sunset @ np.array([0.2126, 0.7152, 0.0722])
    # Equal-elevation patches above the disk: whole-sky visual glow, not an isolated phase readback.
    toward = float(y[400:500, 800:1120].mean())
    flank = float(np.concatenate((y[400:500, 100:420], y[400:500, 1500:1820]), axis=1).mean())
    report["sunset_glow"] = {"toward_sun_linear_y": toward, "flank_linear_y": flank,
                              "whole_sky_ratio": toward / flank}
    with open(OUT / "metrics.json", "w", encoding="utf-8") as target:
        json.dump(report, target, indent=2)
    print(json.dumps(report, indent=2))


def quality():
    """Use the existing harness' fixed banding, silver-lining and aerial metrics."""
    import numpy as np
    from PIL import Image
    import cloud_reference as reference
    import test_clouds as checks

    def linear(label):
        codes = np.asarray(Image.open(OUT / label / "frames/f_95.png").convert("RGB"))
        return checks.srgb_to_linear(codes)

    report = {}
    for pose in ("sun", "away", "horizon"):
        forward = np.asarray(reference.view_direction(pose))
        right = np.cross(forward, [0, 0, 1])
        right /= np.linalg.norm(right)
        up = np.cross(right, forward)
        xx, yy = np.meshgrid((np.arange(1920) + 0.5) / 1920 * 2 - 1,
                             1 - (np.arange(1080) + 0.5) / 1080 * 2)
        extent = np.tan(np.radians(reference.FOV) / 2)
        rays = forward + xx[..., None] * extent * (1920 / 1080) * right + yy[..., None] * extent * up
        rays /= np.linalg.norm(rays, axis=-1, keepdims=True)
        elevation, sun_angle = checks.ray_angles(rays)
        region = (elevation > 0.5) & (sun_angle > 6)
        sky = linear(f"control/{pose}")
        for label in ("before", "after"):
            rgb = linear(f"{label}/{pose}")
            y, sky_y = checks.luminance(rgb), checks.luminance(sky)
            masks = checks.cloud_masks(y, sky_y, region)
            value = {"cloud_fraction": float(masks["cloud"][region].mean())}
            if pose == "horizon":
                value["aerial"] = checks.aerial_perspective(rgb, sky, masks, region, elevation)
            else:
                value["silver"] = checks.silver_lining(
                    y, masks["cloud"], region, sun_angle,
                    checks.SUN_WINDOW if pose == "sun" else checks.ANTI_WINDOW, y_sky=sky_y)
                value["banding"] = checks.banding_metrics(
                    y, checks.banding_segments(masks["cloud"], region))
            report[f"{label}/{pose}"] = value
    on = linear("shadows") @ checks.REC709
    off = linear("shadows-off") @ checks.REC709
    ratio = on / np.maximum(off, 1e-6)
    report["ground_shadows"] = {
        "min_linear_ratio": float(ratio.min()),
        "median_linear_ratio": float(np.median(ratio)),
        "fraction_below_half": float((ratio < 0.5).mean()),
        "fraction_above_95_percent": float((ratio > 0.95).mean())}
    with open(OUT / "quality.json", "w", encoding="utf-8") as target:
        json.dump(report, target, indent=2)
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("before", "after", "reflection", "temporal",
                                       "stability", "stability-temporal", "shadows", "sunset",
                                       "controls", "quality", "gbv", "build", "layouts", "tests", "metrics"))
    args = parser.parse_args()
    if args.mode in ("before", "after"):
        for pose in ("sun", "away", "above", "horizon"):
            capture(f"{args.mode}/{pose}", f"scripts/rendering/cloud-reference-{pose}.uzproject")
    elif args.mode == "reflection":
        capture("reflection", "scripts/rendering/reflection-reference.uzproject")
    elif args.mode == "temporal":
        capture("temporal", "scripts/rendering/cloud-reference-sun.uzproject",
                cvars={"r.Clouds.TemporalUpscale": 1})
    elif args.mode.startswith("stability"):
        capture(args.mode, "scripts/rendering/cloud-reference-away.uzproject",
                shots="90-149", frames=155,
                cvars={"r.Clouds.TemporalUpscale": int(args.mode.endswith("temporal"))})
    elif args.mode == "shadows":
        capture("shadows", "scripts/rendering/cloud-shadow-silhouettes.uzproject",
                cvars={"r.Clouds.GroundShadows": 1})
    elif args.mode == "sunset":
        capture("sunset", "scripts/rendering/cloud-reference-sunset.uzproject")
    elif args.mode == "controls":
        for pose in ("sun", "away", "horizon"):
            capture(f"control/{pose}", f"scripts/rendering/cloud-reference-{pose}-noclouds.uzproject")
        capture("shadows-off", "scripts/rendering/cloud-shadow-silhouettes.uzproject",
                cvars={"r.Clouds.GroundShadows": 0})
    elif args.mode == "quality":
        quality()
    elif args.mode == "gbv":
        capture("gbv", "sample/projects/cloud_test.uzproject", frames=180, gbv=True)
    elif args.mode == "metrics":
        metrics()
    else:
        OUT.mkdir(parents=True, exist_ok=True)
        env = {**os.environ, "GTEST_FILTER": "-RuntimeViewportTest.*",
               "GTEST_OUTPUT": "xml:" + str(OUT / "gtests.xml")}
        command = (["cmd", "/c", "npm", "run", "test:layouts"] if args.mode == "layouts"
                   else ["zig", "build", "-Doptimize=ReleaseFast"] if args.mode == "build"
                   else ["zig", "build", "test", "-Doptimize=ReleaseFast"])
        filename = {"layouts": "layouts.log", "build": "build.log", "tests": "gtests.log"}[args.mode]
        with open(OUT / filename, "wb") as log:
            process = subprocess.Popen(command,
                                       cwd=REPO if args.mode == "layouts" else REPO / "runtime", env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=175)
            except subprocess.TimeoutExpired:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], timeout=10)
                raise
        print(args.mode.upper() + "_EXIT", code, flush=True)
        raise SystemExit(code)


if __name__ == "__main__":
    main()
