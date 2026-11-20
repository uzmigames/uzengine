"""Bounded RTX proofs for the NVIDIA NRD SDK adapter (r.NRD.SDK, PNG display-code metrics).

python scripts/rendering/test_nrd_sdk.py --out E:/UzmiEngine/nrd-sdk
Each engine invocation has a 180-second deadline. Generated scenes are evidence,
not edits to the production reference scene. Cases mirror test_nrd_stage2.py so
the SDK can be compared with the raw 1-spp input and the native stage-2 path
against the same converged reference.
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
        " if uz.frame_index == 24 then uz.log('NRD_SDK_MOTION frame=24') end\n"
        "end\n")


def capture(out, name, frames, shots, denoiser=None, cvars=None, motion=False, pose=False):
    args = ["--path-trace", "--cvar", "r.LightBVH=1",
            "--cvar", "r.PathTracer.NEESamples=1", "--cvar", "r.TAA=0"]
    if denoiser:
        args += ["--denoiser", denoiser]
    if pose:
        args += ["--camera-pos", "100,-700,220", "--camera-look", "100,30,90"]
    result = runner.run_capture(str(out / name),
        str(out / ("motion.uzproject" if motion else "static.uzproject")),
        frames, screenshot_frames=shots, cvars=cvars, extra_args=args,
        clean=False, timeout=180)
    assert result["exit_code"] == 0, result
    log = Path(result["log"]).read_text(errors="replace")
    assert "compile failed" not in log, result["log"]
    if denoiser == "nrd-sdk":
        assert "NRD SDK v4.17" in log and "initialized" in log, result["log"]
        assert "NRD SDK: frame=" in log, result["log"]
        assert "NrdSdkDenoiser::Init failed" not in log, result["log"]
        failures = [l for l in log.splitlines() if "NRD SDK" in l and ("Error" in l or "failed" in l)]
        assert not failures, failures
    elif denoiser == "nrd":
        assert "pass=reproject" in log and "channel=specular" in log
    print(f"CAPTURE {name} PASS", flush=True)


def image(out, name, frame):
    return np.asarray(Image.open(out / name / "frames" / f"f_{frame}.png").convert("RGB"),
                      dtype=np.float64)


def error(value, reference, rect=None):
    if rect is not None:
        x, y, w, h = rect
        value = value[y:y+h, x:x+w]
        reference = reference[y:y+h, x:x+w]
    return {"mse": float(np.mean((value-reference)**2)),
            "mae": float(np.mean(np.abs(value-reference))),
            "mean": float(value.mean()), "reference_mean": float(reference.mean()),
            "bias_percent": float(100 * (value.mean()/max(reference.mean(), 1e-9)-1)),
            "peak": float(value.max()), "reference_peak": float(reference.max()),
            "p99": float(np.quantile(value, .99)),
            "reference_p99": float(np.quantile(reference, .99))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=["fixtures", "raw", "stage2", "sdk", "reference",
        "motion", "motion-reference", "default", "metrics"])
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cases = [args.case] if args.case else ["fixtures", "raw", "stage2", "sdk", "reference",
                                         "motion", "motion-reference", "default", "metrics"]
    for case in cases:
        if case == "fixtures":
            fixtures(out)
        elif case == "raw":
            capture(out, case, 40, "4-32:4", cvars={"r.PathTracer.Accumulate": 0})
        elif case == "stage2":
            capture(out, case, 40, "4-32:4", denoiser="nrd")
        elif case == "sdk":
            capture(out, case, 40, "4-32:4", denoiser="nrd-sdk")
        elif case == "reference":
            capture(out, case, 1024, "1023")
        elif case == "motion":
            for name, denoiser in (("motion-stage2", "nrd"), ("motion-sdk", "nrd-sdk")):
                capture(out, name, 40, "23-32", denoiser=denoiser, motion=True)
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
            ghost_roi = (400, 80, 40, 80)
            disocclusion_roi = (300, 160, 20, 30)
            report = {"specular_roi": specular_roi, "ghost_roi": ghost_roi,
                      "disocclusion_roi": disocclusion_roi}
            report["full_frame"] = {name: {f"f_{f}": error(image(out, name, f), reference)
                                           for f in (4, 8, 16, 32)}
                                    for name in ("raw", "stage2", "sdk")}
            report["specular_roi_f32"] = {name: error(image(out, name, 32), reference, specular_roi)
                                          for name in ("raw", "stage2", "sdk")}
            report["motion_f24"] = {name: error(image(out, name, 24), moving, ghost_roi)
                                    for name in ("motion-stage2", "motion-sdk")}
            # Ghosting test: after the 100-unit step the frame must match the new pose,
            # not the old one the history was accumulated at.
            report["motion_f24_vs_old_pose"] = {name: error(image(out, name, 24), reference, ghost_roi)
                                                for name in ("motion-stage2", "motion-sdk")}
            report["motion_f24_full"] = {name: {"new_pose": error(image(out, name, 24), moving),
                                                "old_pose": error(image(out, name, 24), reference)}
                                         for name in ("motion-stage2", "motion-sdk")}
            report["disocclusion_f24"] = {name: error(image(out, name, 24), moving, disocclusion_roi)
                                          for name in ("motion-stage2", "motion-sdk")}
            # Temporal stability: frame-to-frame change of the static sequence after convergence.
            report["temporal_f28_f32"] = {name: error(image(out, name, 32), image(out, name, 28))
                                          for name in ("raw", "stage2", "sdk")}
            # Default raster frame: informational only. The TAA raster frame is not deterministic
            # run to run (13-674 differing pixels between runs of one exe) and the f005-after
            # capture predates HEAD; byte identity of the path-traced frames against a pristine
            # build is the report's SHA-256 comparison, not this delta.
            default = image(out, "default", 95)
            baseline_path = REPO.parent / "f005-after/frames/f_95.png"
            if baseline_path.exists():
                baseline = np.asarray(Image.open(baseline_path).convert("RGB"), dtype=np.float64)
                delta = np.abs(default-baseline)
                report["default_vs_f005_after"] = {"max_code_delta": float(delta.max()),
                                                   "pixels_differing": int((delta.max(axis=2) > 0).sum())}
            (out / "metrics.json").write_text(json.dumps(report, indent=2))
            print(json.dumps(report, indent=2))
            sdk32 = report["full_frame"]["sdk"]["f_32"]
            raw32 = report["full_frame"]["raw"]["f_32"]
            assert sdk32["mse"] < .1 * raw32["mse"], (sdk32, raw32)
            assert abs(sdk32["bias_percent"]) < 6, sdk32
            assert report["specular_roi_f32"]["sdk"]["mse"] < .1 * report["specular_roi_f32"]["raw"]["mse"]
            ghost = report["motion_f24_full"]["motion-sdk"]
            assert ghost["new_pose"]["mse"] < .2 * ghost["old_pose"]["mse"], ghost
            reset_frame = report["full_frame"]["sdk"]["f_4"]
            assert reset_frame["mse"] < .25 * report["full_frame"]["raw"]["f_4"]["mse"], reset_frame


if __name__ == "__main__":
    main()
