"""RTX stage-1 denoiser evidence. Each capture is bounded to 180 seconds.

Usage: python scripts/rendering/test_nrd_stage1.py --out DIR [--case CASE]
The output contains raw, denoised, converged, default, and camera-cut captures.
PNG metrics use RGB code values; linear HDR irradiance remains un-clamped.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent / "flicker"))
import runner

REPO = Path(runner.REPO)


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def metrics(value, reference):
    return {
        "mse": float(np.mean((value - reference) ** 2)),
        "mae": float(np.mean(np.abs(value - reference))),
        "mean": float(value.mean()),
        "reference_mean": float(reference.mean()),
        "mean_bias_percent": float(100 * (value.mean() / reference.mean() - 1)),
    }


def capture(out, name, project, frames, shots, args=(), cvars=None):
    result = runner.run_capture(
        str(out / name), str(project), frames, clean=False, timeout=180,
        cvars={"r.TechPanel.Show": 0, "r.Toolbar.Show": 0, **(cvars or {})},
        screenshot_frames=shots, extra_args=args)
    if result["exit_code"] != 0:
        raise RuntimeError(f'{name}: exit {result["exit_code"]}')
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    if "compile failed" in log:
        raise RuntimeError(f"{name}: compile failed; see run.log")
    if "--nrd" in args and "pass=reproject" not in log:
        raise RuntimeError(f"{name}: denoiser did not execute")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=["default", "raw", "denoised", "reference", "motion", "metrics"])
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    project = REPO / "scripts/rendering/light-bvh-emissive.uzproject"
    common = ("--path-trace", "--cvar", "r.LightBVH=1",
              "--cvar", "r.PathTracer.NEESamples=1", "--cvar", "r.TAA=0")
    cases = [args.case] if args.case else ["default", "raw", "denoised", "reference", "motion", "metrics"]
    for case in cases:
        if case == "default":
            capture(out, case, REPO / "scripts/rendering/reflection-reference.uzproject",
                    100, "95", cvars={"r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1})
        elif case == "raw":
            capture(out, case, project, 40, "4-32:4", common,
                    {"r.PathTracer.Accumulate": 0})
        elif case == "denoised":
            capture(out, case, project, 40, "4-32:4", (*common, "--nrd"))
        elif case == "reference":
            capture(out, case, project, 1024, "1023", common)
        elif case == "motion":
            # Generated fixtures are evidence artifacts, never source-tree edits.
            scene = json.loads((REPO / "scripts/rendering/light-bvh-emissive.uzscene").read_text())
            scene["entities"].append({"name": "NRD camera-cut QA", "components": {
                "script": {"path": os.path.relpath(out / "motion.lua", REPO).replace("\\", "/")}}})
            (out / "motion.uzscene").write_text(json.dumps(scene, indent=2))
            fixture = json.loads(project.read_text())
            fixture["content_root"] = os.path.relpath(REPO, out).replace("\\", "/")
            fixture["default_scene"] = os.path.relpath(out / "motion.uzscene", REPO).replace("\\", "/")
            (out / "motion.uzproject").write_text(json.dumps(fixture, indent=2))
            (out / "motion.lua").write_text(
                "function on_update(entity, dt)\n"
                " local x = uz.frame_index >= 24 and 150 or 0\n"
                " uz.camera.set_position(x, -700, 220)\n"
                " uz.camera.look_at(x, 30, 90)\n"
                " if uz.frame_index == 24 then uz.log('NRD_QA_CAMERA_CUT frame=24') end\n"
                "end\n")
            capture(out, case, out / "motion.uzproject", 40, "22-32",
                    (*common, "--nrd"))
            log = (out / "motion/run.log").read_text(encoding="utf-8", errors="replace")
            assert "NRD_QA_CAMERA_CUT frame=24" in log, "camera fixture did not execute"
            assert "frame=24 pass=reproject history=false" in log, "camera cut retained history"
            capture(out, "cut-fresh", project, 5, "4",
                    (*common, "--nrd", "--camera-pos", "150,-700,220",
                     "--camera-look", "150,30,90"), {"r.PathTracer.Accumulate": 0})
        elif case == "metrics":
            reference = image(out / "reference/frames/f_1023.png")
            report = {"raw": metrics(image(out / "raw/frames/f_4.png"), reference)}
            report["denoised"] = {
                str(frame): metrics(image(out / f"denoised/frames/f_{frame}.png"), reference)
                for frame in (4, 8, 16, 24, 32)}
            default = image(out / "default/frames/f_95.png")
            baseline = image(REPO.parent / "f005-after/frames/f_95.png")
            report["default_max_code_delta"] = float(np.abs(default - baseline).max())
            cut = image(out / "motion/frames/f_24.png")
            fresh = image(out / "cut-fresh/frames/f_4.png")
            report["cut_vs_fresh_max_code_delta"] = float(np.abs(cut - fresh).max())
            report["mse_reduction_percent"] = 100 * (
                1 - report["denoised"]["32"]["mse"] / report["raw"]["mse"])
            (out / "metrics.json").write_text(json.dumps(report, indent=2))
            print(json.dumps(report, indent=2))
            assert report["default_max_code_delta"] == 0
            assert report["mse_reduction_percent"] > 50
            assert abs(report["denoised"]["32"]["mean_bias_percent"]) < 5
            assert report["denoised"]["32"]["mse"] < report["denoised"]["4"]["mse"]
            assert report["cut_vs_fresh_max_code_delta"] == 0


if __name__ == "__main__":
    main()
