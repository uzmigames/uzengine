"""Bounded M4 scattered-cloud evidence; run from the repository root."""
import json
import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
OUT = os.path.join(REPO, ".screenshots/cloud-m4")
sys.path.insert(0, os.path.join(REPO, "scripts/rendering/flicker"))


def bounded(command, log_name, env=None):
    with open(os.path.join(OUT, log_name), "wb") as log:
        process = subprocess.Popen(command, cwd=os.path.join(REPO, "runtime"),
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=175)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], timeout=10)
            raise
    print(log_name, "EXIT", code, flush=True)
    return code


if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "tests":
        env = {**os.environ, "GTEST_FILTER": "-RuntimeViewportTest.*",
               "GTEST_OUTPUT": "xml:" + os.path.join(OUT, "gtests-scattered.xml")}
        raise SystemExit(bounded(["zig", "build", "test", "-Doptimize=ReleaseFast"],
                                 "gtests-scattered.log", env))
    import runner
    motion = mode == "motion"
    cvars = {"r.Clouds.GroundShadows": int(not mode.startswith("off"))}
    if len(sys.argv) > 2:
        cvars.update(json.loads(sys.argv[2]))
    result = runner.run_capture(
        os.path.join(OUT, "scattered-" + mode),
        os.path.join(REPO, "docs/analysis/volumetric-clouds-rewrite/m4-scattered.uzproject"),
        150 if motion else 24, timeout=175, clean=False,
        screenshot_frames="30-145" if motion else "10-20", timing_first=10,
        cvars=cvars,
        regions=[("ground", 0, 180, 1280, 540)] if mode == "probe" else None,
        region_fields="lighting,lighting-extra", region_first=20, region_last=20,
        extra_args=["--gpu-validation"] if mode.startswith("gbv") else [])
    print(json.dumps({"mode": mode, "exit_code": result["exit_code"]}), flush=True)
    raise SystemExit(result["exit_code"])
