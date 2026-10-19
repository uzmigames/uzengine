"""Bounded, serialized occlusion captures; compare against ordinary uniform NEE.

Run: python scripts/rendering/measure_restir_occlusion.py capture OUTPUT MODE
or:  python scripts/rendering/measure_restir_occlusion.py analyze OUTPUT
MODE is combined, bvh, uniform, reference, or default. Output must be new.
The metrics are spatial PNG error variance, not repeated-seed ensemble variance.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from flicker.runner import run_capture

REPO = Path(__file__).resolve().parents[2]


def capture(output, mode):
    out = output / mode
    if out.exists():
        raise SystemExit(f"Refusing to overwrite capture: {out}")
    cvars = {
        "r.TAA": 0, "r.TechPanel.Show": 0, "r.Toolbar.Show": 0,
        "r.VolumetricFog": 0, "r.PathTracer.NEESamples": 1,
        "r.PathTracer.MaxBounces": 2, "r.PathTracer.RussianRoulette": 0,
        "r.LightBVH": 1, "r.LightBVH.Uniform": int(mode in ("uniform", "reference")),
    }
    args = ["--hidden", "--path-trace", "--pt-bounces", "2"]
    if mode in ("combined", "uniform"):
        args += ["--restir-di"]
    if mode == "reference":
        cvars["r.PathTracer.NEESamples"] = 32
    scene, frames, shots = "restir-occlusion", 2055, "5-2050:5"
    if mode == "default":
        scene, frames, shots = "reflection-reference", 100, "95"
        cvars = {"r.TAA": 1, "r.TechPanel.Show": 0, "r.Toolbar.Show": 0,
                 "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1}
        args = []
    result = run_capture(str(out), str(REPO / "scripts/rendering" / f"{scene}.uzproject"),
                         frames, cvars=cvars, extra_args=args, screenshot_frames=shots,
                         timeout=170, clean=False)
    print(f"CAPTURE {mode} EXIT {result['exit_code']}", flush=True)
    raise SystemExit(result["exit_code"])


def analyze(output):
    def image(mode, frame):
        return np.asarray(Image.open(output / mode / "frames" / f"f_{frame}.png")
                          .convert("RGB"), dtype=np.float64)

    def linear(x):
        x = x / 255.
        return np.where(x <= .04045, x / 12.92, ((x + .055) / 1.055) ** 2.4)

    reference = image("reference", 2050)
    original = (reference.mean(2) > 15) & (reference.mean(2) < 180)
    mask = original.copy()
    for dy in (-4, 0, 4):
        for dx in (-4, 0, 4):
            mask &= np.roll(np.roll(original, dy, axis=0), dx, axis=1)
    mask[:5] = mask[-5:] = False
    mask[:, :5] = mask[:, -5:] = False
    scene = json.loads((REPO / "scripts/rendering/restir-occlusion.uzscene").read_text())
    dividers = [e["components"]["transform"] for e in scene["entities"]
                if e["name"].startswith("Cell divider")]
    lights = [np.array(e["components"]["transform"]["position"], dtype=float)
              for e in scene["entities"] if e["name"].startswith("Emitter")]
    blocked = total = 0
    for x in range(-250, 251, 25):
        for y in range(-250, 251, 25):
            origin = np.array([x, y, .05])
            for light in lights:
                direction = light - origin
                hit = False
                for divider in dividers:
                    center = np.array(divider["position"])
                    half = np.array(divider["scale"]) / 2
                    lo, hi = center - half, center + half
                    parallel = direction == 0
                    if np.any(parallel & ((origin < lo) | (origin > hi))):
                        continue
                    near, far = np.full(3, -np.inf), np.full(3, np.inf)
                    active = ~parallel
                    a = (lo[active] - origin[active]) / direction[active]
                    b = (hi[active] - origin[active]) / direction[active]
                    near[active], far[active] = np.minimum(a, b), np.maximum(a, b)
                    hit |= near.max() < far.min() and near.max() < 1 and far.min() > 0
                blocked += int(hit)
                total += 1
    report = {"pixels": int(mask.sum()), "means": {}, "variance": {}, "logs": {},
              "geometric_occlusion": {"floor_points": 441, "center_segments": total,
                                      "blocked": blocked, "blocked_percent": 100 * blocked / total}}
    for mode in ("reference", "combined", "bvh", "uniform"):
        high = image(mode, 2050)
        report["means"][mode] = {
            "code": float(high[mask].mean()),
            "code_delta_percent": float(100 * (high[mask].mean() / reference[mask].mean() - 1)),
            "linear": float(linear(high)[mask].mean()),
            "linear_delta_percent": float(100 * (linear(high)[mask].mean() /
                                                linear(reference)[mask].mean() - 1)),
            "whole_code": float(high.mean()),
            "whole_code_delta_percent": float(100 * (high.mean() / reference.mean() - 1)),
            "whole_linear": float(linear(high).mean()),
            "whole_linear_delta_percent": float(100 * (linear(high).mean() /
                                                      linear(reference).mean() - 1)),
        }
    for frame in (5, 10, 20, 50, 200):
        values = {mode: float((image(mode, frame) - reference)[mask].var())
                  for mode in ("combined", "bvh", "uniform")}
        values["vs_bvh_percent"] = 100 * (values["combined"] / values["bvh"] - 1)
        values["vs_uniform_percent"] = 100 * (values["combined"] / values["uniform"] - 1)
        values["whole_image"] = {mode: float((image(mode, frame) - reference).var())
                                 for mode in ("combined", "bvh", "uniform")}
        report["variance"][frame] = values
    baseline = Path("E:/UzmiEngine/f005-after/frames/f_95.png")
    default = output / "default/frames/f_95.png"
    report["default"] = {
        "byte_identical": baseline.read_bytes() == default.read_bytes(),
        "max_code_delta": float(abs(np.asarray(Image.open(baseline), dtype=float) -
                                    np.asarray(Image.open(default), dtype=float)).max()),
    }
    for mode in ("reference", "combined", "bvh", "uniform", "default"):
        text = (output / mode / "run.log").read_text(encoding="utf-8", errors="replace")
        report["logs"][mode] = {
            "compile_failed": "compile failed" in text,
            "device_removed": "DEVICE_REMOVED" in text or "0x887A0005" in text,
            "integrated_dispatch": "initial BVH-sourced RIS" in text,
        }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    action, output = sys.argv[1], Path(sys.argv[2])
    if action == "capture":
        capture(output, sys.argv[3])
    elif action == "analyze":
        analyze(output)
    else:
        raise SystemExit(f"Unknown action: {action}")
