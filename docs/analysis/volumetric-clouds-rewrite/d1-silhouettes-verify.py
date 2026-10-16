"""Bounded D1 evidence. Run from the repository root; artifacts are not source."""
import json
import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
OUT = os.path.join(REPO, ".screenshots/cloud-d1-polish")
sys.path.insert(0, os.path.join(REPO, "scripts/rendering/flicker"))

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    mode = sys.argv[1]
    if mode in ("build", "tests", "layouts"):
        command = {
            "build": ["zig", "build", "-Doptimize=ReleaseFast"],
            "tests": ["zig", "build", "test", "-Doptimize=ReleaseFast"],
            "layouts": ["npm.cmd", "run", "test:layouts"],
        }[mode]
        env = dict(os.environ)
        env["GTEST_FILTER"] = "-RuntimeViewportTest.*"
        env["GTEST_OUTPUT"] = "xml:" + os.path.join(OUT, "gtests.xml")
        with open(os.path.join(OUT, mode + ".log"), "wb") as log:
            process = subprocess.Popen(command, cwd=os.path.join(REPO, "runtime")
                                       if mode != "layouts" else REPO,
                                       env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=175)
            except subprocess.TimeoutExpired:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], timeout=5)
                raise
        print(mode, "EXIT", code, flush=True)
        raise SystemExit(code)
    if mode == "compare":
        import numpy as np
        from PIL import Image
        actual = np.asarray(Image.open(os.path.join(OUT, "default/frames/f_95.png")))
        expected = np.asarray(Image.open("E:/UzmiEngine/f005-after/frames/f_95.png"))
        delta = np.abs(actual.astype(np.int16) - expected.astype(np.int16))
        report = {"max_code_delta": int(delta.max()),
                  "different_values": int(np.count_nonzero(delta))}
        print(json.dumps(report), flush=True)
        raise SystemExit(int(report["max_code_delta"] != 0))
    if mode == "metrics":
        import numpy as np
        from PIL import Image
        on = np.asarray(Image.open(os.path.join(OUT, "coherent/frames/f_28.png"))
                        .convert("RGB"), dtype=np.float64)
        off = np.asarray(Image.open(os.path.join(OUT, "off/frames/f_28.png"))
                         .convert("RGB"), dtype=np.float64)
        visibility = on.mean(axis=2) / np.maximum(off.mean(axis=2), 1)
        report = {
            "on_rgb_min": on.min(axis=(0, 1)).tolist(),
            "on_rgb_max": on.max(axis=(0, 1)).tolist(),
            "off_rgb_min": off.min(axis=(0, 1)).tolist(),
            "off_rgb_max": off.max(axis=(0, 1)).tolist(),
            "display_brightness_ratio_percentiles":
                np.percentile(visibility, [0, 10, 50, 90, 100]).tolist(),
            "ground_fraction_below_half_control_brightness": float((visibility < 0.5).mean()),
            "ground_fraction_above_95pct_control_brightness": float((visibility > 0.95).mean()),
        }
        print(json.dumps(report, indent=2), flush=True)
        raise SystemExit(0)
    import runner
    project = {
        "before": "docs/analysis/volumetric-clouds-rewrite/m4-scattered.uzproject",
        "default": "scripts/rendering/reflection-reference.uzproject",
    }.get(mode, "scripts/rendering/cloud-shadow-silhouettes.uzproject")
    cvars = {} if mode == "default" else {"r.Clouds.GroundShadows": int(mode != "off")}
    result = runner.run_capture(
        os.path.join(OUT, mode), os.path.join(REPO, project),
        100 if mode == "default" else 32, timeout=175, clean=False,
        screenshot_frames="95" if mode == "default" else "28",
        timing_first=10, cvars=cvars,
        extra_args=["--gpu-validation"] if mode == "gbv" else [])
    print(json.dumps({"mode": mode, "exit_code": result["exit_code"]}), flush=True)
    raise SystemExit(result["exit_code"])
