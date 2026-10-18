"""Bounded RTX proofs for native NRD stage 2 (PNG display-code metrics).

python scripts/rendering/test_nrd_stage2.py --out E:/UzmiEngine/nrd-stage2
Each engine invocation has a 180-second deadline. Generated scenes are evidence,
not edits to the production reference scene.
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


def fixtures(out):
    scene = json.loads((REPO / "scripts/rendering/light-bvh-emissive.uzscene").read_text())
    floor = next(e for e in scene["entities"] if e["name"] == "Floor")
    floor["components"]["material"].update(
        metallicMultiplier=1, roughnessMultiplier=.45,
        baseColorR=.7, baseColorG=.7, baseColorB=.7)
    scene["entities"].append({"name": "Disocclusion occluder", "components": {
        "transform": {"position": [0, -40, 60], "scale": [80, 80, 120]},
        "mesh": {"asset": "sample/content/meshes/cube.uzmesh"},
        "material": {"baseColorR": .2, "baseColorG": .2, "baseColorB": .2,
                     "metallicMultiplier": 0, "roughnessMultiplier": 1}}})
    project = json.loads((REPO / "scripts/rendering/light-bvh-emissive.uzproject").read_text())
    project["content_root"] = os.path.relpath(REPO, out).replace("\\", "/")
    for name in ("static", "motion"):
        variant = json.loads(json.dumps(scene))
        if name == "motion":
            variant["entities"].append({"name": "Camera step QA", "components": {
                "script": {"path": os.path.relpath(out / "motion.lua", REPO).replace("\\", "/")}}})
        (out / f"{name}.uzscene").write_text(json.dumps(variant, indent=2))
        project["default_scene"] = os.path.relpath(out / f"{name}.uzscene", REPO).replace("\\", "/")
        (out / f"{name}.uzproject").write_text(json.dumps(project, indent=2))
    (out / "motion.lua").write_text(
        "function on_update(entity, dt)\n"
        " local x = uz.frame_index >= 24 and 100 or 0\n"
        " uz.camera.set_position(x, -700, 220)\n"
        " uz.camera.look_at(x, 30, 90)\n"
        " if uz.frame_index == 24 then uz.log('NRD_STAGE2_MOTION frame=24') end\n"
        "end\n")


def capture(out, name, frames, shots, nrd=False, cvars=None, motion=False, pose=False):
    args = ["--path-trace", "--cvar", "r.LightBVH=1",
            "--cvar", "r.PathTracer.NEESamples=1", "--cvar", "r.TAA=0"]
    if nrd:
        args.append("--nrd")
    if pose:
        args += ["--camera-pos", "100,-700,220", "--camera-look", "100,30,90"]
    result = runner.run_capture(str(out / name),
        str(out / ("motion.uzproject" if motion else "static.uzproject")),
        frames, screenshot_frames=shots, cvars=cvars, extra_args=args,
        clean=False, timeout=180)
    assert result["exit_code"] == 0, result
    log = Path(result["log"]).read_text(errors="replace")
    assert "compile failed" not in log, result["log"]
    if nrd:
        assert "pass=reproject" in log
        if not (cvars or {}).get("r.NRD.Specular") == 0:
            assert "channel=specular" in log
    print(f"CAPTURE {name} PASS", flush=True)


def image(out, name, frame):
    return np.asarray(Image.open(out / name / "frames" / f"f_{frame}.png").convert("RGB"),
                      dtype=np.float64)


def error(value, reference, rect):
    x, y, w, h = rect
    value = value[y:y+h, x:x+w]
    reference = reference[y:y+h, x:x+w]
    return {"mse": float(np.mean((value-reference)**2)),
            "mae": float(np.mean(np.abs(value-reference))),
            "mean": float(value.mean()), "reference_mean": float(reference.mean()),
            "bias_percent": float(100 * (value.mean()/reference.mean()-1)),
            "peak": float(value.max()), "reference_peak": float(reference.max()),
            "p99": float(np.quantile(value, .99)),
            "reference_p99": float(np.quantile(reference, .99))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=["fixtures", "raw", "stage1", "stage2", "reference",
        "motion", "motion-reference", "default", "metrics"])
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cases = [args.case] if args.case else ["fixtures", "raw", "stage1", "stage2", "reference",
                                         "motion", "motion-reference", "default", "metrics"]
    for case in cases:
        if case == "fixtures":
            fixtures(out)
        elif case == "raw":
            capture(out, case, 40, "4-32:28", cvars={"r.PathTracer.Accumulate": 0})
        elif case in ("stage1", "stage2"):
            capture(out, case, 40, "4-32:4", nrd=True,
                    cvars={"r.NRD.Specular": int(case == "stage2")})
        elif case == "reference":
            capture(out, case, 1024, "1023")
        elif case == "motion":
            for name, cvars in (("motion-stage1", {"r.NRD.Specular": 0}),
                                ("motion-no-clamp", {"r.NRD.HistoryClamp": 0}),
                                ("motion-stage2", {}),
                                ("motion-no-fix", {"r.NRD.DisocclusionFix": 0})):
                capture(out, name, 40, "23-32", nrd=True, cvars=cvars, motion=True)
        elif case == "motion-reference":
            capture(out, case, 1024, "1023", pose=True)
        elif case == "default":
            result = runner.run_capture(str(out / "default"),
                str(REPO / "scripts/rendering/reflection-reference.uzproject"),
                100, screenshot_frames="95", clean=False, timeout=180,
                cvars={"r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1,
                       "r.TechPanel.Show": 0, "r.Toolbar.Show": 0})
            assert result["exit_code"] == 0
        elif case == "metrics":
            reference = image(out, "reference", 1023)
            moving = image(out, "motion-reference", 1023)
            specular_roi = (160, 250, 320, 75)
            # The occluder spans x=296..344 initially and x=325..374 after
            # translation. This strip is genuinely newly uncovered background.
            disocclusion_roi = (300, 160, 20, 30)
            # Trailing right-hand room boundary after the camera translation.
            ghost_roi = (400, 80, 40, 80)
            report = {"specular_roi": specular_roi, "disocclusion_roi": disocclusion_roi,
                      "ghost_roi": ghost_roi}
            report["static"] = {name: error(image(out, name, 32), reference, specular_roi)
                                for name in ("raw", "stage1", "stage2")}
            report["motion"] = {name: error(image(out, name, 24), moving, ghost_roi)
                for name in ("motion-stage1", "motion-no-clamp", "motion-stage2", "motion-no-fix")}
            report["disocclusion"] = {name: error(image(out, name, 24), moving, disocclusion_roi)
                for name in ("motion-stage1", "motion-stage2", "motion-no-fix")}
            default = image(out, "default", 95)
            baseline = np.asarray(Image.open(REPO.parent / "f005-after/frames/f_95.png").convert("RGB"),
                                  dtype=np.float64)
            report["default_max_code_delta"] = float(np.abs(default-baseline).max())
            (out / "metrics.json").write_text(json.dumps(report, indent=2))
            print(json.dumps(report, indent=2))
            assert report["static"]["stage2"]["mse"] < .5 * report["static"]["stage1"]["mse"]
            assert report["static"]["stage2"]["mse"] < .5 * report["static"]["raw"]["mse"]
            assert abs(report["static"]["stage2"]["bias_percent"]) < 5
            assert report["static"]["stage2"]["p99"] >= .9 * report["static"]["stage2"]["reference_p99"]
            assert report["motion"]["motion-stage2"]["mse"] < report["motion"]["motion-no-clamp"]["mse"]
            assert report["disocclusion"]["motion-stage2"]["mse"] < report["disocclusion"]["motion-stage1"]["mse"]
            assert report["disocclusion"]["motion-stage2"]["mse"] < report["disocclusion"]["motion-no-fix"]["mse"]
            assert report["default_max_code_delta"] == 0


if __name__ == "__main__":
    main()
