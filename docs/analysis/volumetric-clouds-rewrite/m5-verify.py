"""Bounded headful M5 captures; run from the repository root."""
import json
import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
OUT = os.path.join(REPO, ".screenshots/cloud-m5")
sys.path.insert(0, os.path.join(REPO, "scripts/rendering/flicker"))
import runner


def capture(label, pose, multi=False, gbv=False):
    project = (f"docs/analysis/volumetric-clouds-rewrite/m5-{pose}.uzproject" if multi
               else f"scripts/rendering/cloud-reference-{pose}.uzproject")
    result = runner.run_capture(
        os.path.join(OUT, label, pose), os.path.join(REPO, project),
        24 if gbv else 150, timeout=175, clean=False,
        screenshot_frames="18-20" if gbv else "130-132", timing_first=10 if gbv else 90,
        cvars={"r.TechPanel.Show": 0, "r.Toolbar.Show": 0},
        extra_args=["--gpu-validation"] if gbv else [])
    print(label, pose, "CAPTURE_EXIT", result["exit_code"], flush=True)
    if result["exit_code"]:
        raise SystemExit(result["exit_code"])


if sys.argv[1] in ("before", "after"):
    for pose in ("sun", "away", "above", "horizon"):
        capture(sys.argv[1], pose)
elif sys.argv[1] in ("multi", "gbv"):
    for pose in ("above", "horizon"):
        capture(sys.argv[1], pose, multi=True, gbv=sys.argv[1] == "gbv")
elif sys.argv[1] == "layout":
    sys.path.insert(0, os.path.join(REPO, "scripts/rendering"))
    import test_cbuffer_layouts as layouts
    cloud = next(entry for entry in layouts.ENTRIES if entry["name"] == "CloudCB")
    cloud["map"].update({
        "g_layerCount": "layerCount", "g_weatherSrv": "weatherSrv",
        "g_explicitLayers": "explicitLayers", "g_layerGeometry": "layerGeometry",
        "g_layerShape": "layerShape", "g_layerProfile": "layerProfile",
        "g_temporalUpscale": "temporalUpscale", "g_frontDepthUav": "frontDepthUav"})
    for name, shader in (("upscale", "cloud_raymarch_upscale_cs.hlsl"),
                         ("shadow", "cloud_shadow_cs.hlsl")):
        layouts.ENTRIES.append({**cloud, "name": f"CloudCB ({name})",
                                "shader": "atmosphere/" + shader, "before": None})
    sys.argv = [sys.argv[0]]
    raise SystemExit(layouts.main())
elif sys.argv[1] == "tests":
    os.makedirs(OUT, exist_ok=True)
    env = {**os.environ, "GTEST_FILTER": "-RuntimeViewportTest.*",
           "GTEST_OUTPUT": "xml:" + os.path.join(OUT, "gtests.xml")}
    with open(os.path.join(OUT, "gtests.log"), "wb") as log:
        process = subprocess.Popen(["zig", "build", "test", "-Doptimize=ReleaseFast"],
                                   cwd=os.path.join(REPO, "runtime"), env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=175)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], timeout=10)
            raise
    print("GTEST_EXIT", code, flush=True)
elif sys.argv[1] == "compare":
    import numpy as np
    from PIL import Image
    report = {}
    for pose in ("sun", "away", "above", "horizon"):
        pairs = []
        costs = {}
        for frame in range(130, 133):
            images = [np.asarray(Image.open(os.path.join(
                OUT, label, pose, "frames", f"f_{frame}.png")).convert("RGB"),
                dtype=np.float64) for label in ("before", "after")]
            delta = np.abs(images[1] - images[0])
            pairs.append({"frame": frame, "max_code_delta": float(delta.max()),
                          "mean_code_delta": float(delta.mean()),
                          "mse": float(np.square(delta).mean()),
                          "after_mean_code": float(images[1].mean())})
        for label in ("before", "after", "multi"):
            path = os.path.join(OUT, label, pose, "manifest.json")
            if os.path.isfile(path):
                with open(path, encoding="utf-8") as source:
                    manifest = json.load(source)
                costs[label] = manifest.get("gpu_ms")
        cloud_ms = {label: sum(value for name, value in passes.items()
                               if name.lower().startswith("cloud"))
                    for label, passes in costs.items() if passes}
        report[pose] = {"pairs": pairs, "gpu_ms": costs, "cloud_ms": cloud_ms,
                        "default_within_one_code": all(pair["max_code_delta"] <= 1 for pair in pairs)}
        if "multi" in cloud_ms:
            report[pose]["multi_over_M4"] = cloud_ms["multi"] / cloud_ms["before"]
            report[pose]["cost_gate"] = report[pose]["multi_over_M4"] <= 1.3
    with open(os.path.join(OUT, "comparison.json"), "w", encoding="utf-8") as output:
        json.dump(report, output, indent=2)
    print(json.dumps(report, indent=2))
else:
    raise SystemExit("expected before, after, multi, gbv, tests, or compare")
