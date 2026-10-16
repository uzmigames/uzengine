"""Bounded, headful M1 verification using the existing rendering harness.

Every engine invocation has a 175 second deadline. Evidence lives outside the
M0 captures. Baseline hard links preserve the original before images/dumps.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys

import test_clouds as clouds

ROOT = Path(clouds.REPO)
OUT = ROOT / ".screenshots/cloud-m1"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("gate", choices=("gbv", "harness", "tests", "noise"))
    parser.add_argument("--round", default="")
    parser.add_argument("--poses", default="sun,away,horizon,above")
    parser.add_argument("--reuse", action="store_true")
    args = parser.parse_args()
    out = OUT / args.round if args.round else OUT
    out.mkdir(parents=True, exist_ok=True)
    if args.gate == "noise":
        import numpy as np
        for name, size in (("shape", 128), ("shape_detail", 32)):
            data = np.frombuffer((ROOT / f"runtime/content/atmosphere/{name}.bin").read_bytes(),
                                 dtype=np.uint8).reshape(size, size, size).astype(np.float64) / 255
            print(name, "quantiles", np.quantile(data, [0, .1, .5, .9, .99, 1]), flush=True)
            centered = data - data.mean()
            variance = np.mean(centered * centered)
            for shift in (1, 2, 4, 8, 16, 32):
                if shift < size:
                    print("shift", shift, "correlation",
                          [float(np.mean(centered * np.roll(centered, shift, axis)) / variance)
                           for axis in range(3)], flush=True)
        return 0
    if args.gate == "gbv":
        for name, project in (
            ("gbv", ROOT / "sample/projects/cloud_test.uzproject"),
            ("horizon-gbv", ROOT / "scripts/rendering/cloud-reference-horizon-steps16.uzproject"),
        ):
            result = clouds.runner.run_capture(
                str(out / name), str(project), 180,
                screenshot_frames="0-1" if name == "gbv" else "170-171",
                extra_args=("--gpu-validation", "--screenshot-preload") if name == "gbv"
                else ("--gpu-validation",),
                clean=False, timeout=175, timing_first=100)
            print(name, "exit", result["exit_code"], flush=True)
            if result["exit_code"]:
                return result["exit_code"]
        return 0
    if args.gate == "tests":
        with (out / "gtests.log").open("wb") as log:
            process = subprocess.Popen(
                ["zig", "build", "test", "-Doptimize=ReleaseFast"],
                env={**os.environ, "GTEST_FILTER": "-RuntimeViewportTest.*",
                     "GTEST_OUTPUT": "xml:" + str(out / "gtests.xml")},
                cwd=ROOT / "runtime", stdout=log, stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=175)
            except subprocess.TimeoutExpired:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               timeout=10, check=False)
                process.wait(timeout=10)
                print("gtests exceeded 175 seconds; process tree killed", flush=True)
                return 124
        print("gtests exit", code, flush=True)
        return code
    baseline = Path(clouds.OUT) / "baseline"
    target = out / "harness/baseline"
    if not target.exists():
        shutil.copytree(baseline, target, copy_function=os.link)
    capture = clouds.runner.run_capture

    def bounded_capture(*args, **kwargs):
        kwargs["timeout"] = 175
        kwargs["clean"] = False
        return capture(*args, **kwargs)

    clouds.runner.run_capture = bounded_capture
    sys.argv = [sys.argv[0], "--out", str(out / "harness"), "--repeats", "1",
                "--poses", args.poses]
    if args.reuse:
        sys.argv.append("--reuse")
    return clouds.main()


if __name__ == "__main__":
    raise SystemExit(main())
