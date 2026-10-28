"""Bounded Phase D stage 4 captures: the surface record, the scheduled relight and the finite-depth
cell-indirect transport (opt-in: r.GI.SurfaceCache.Relight / .MultiBounce). Run each case separately
(180 seconds maximum per engine run); pass `--exe` to capture with another binary (the fresh
8ef475e2 baseline build for the byte-identity cases).

Cases:
  default        reflection-reference, frame 95 of 100, no cache CVar (OFF byte identity vs the baseline)
  ll-pt          surface-cache-locallight --path-trace frames 255 + 511 (PT byte identity vs the baseline)
  cb-off         surface-cache-stage4-cornell (DXR DDGI + RT reflections), the cache off
  cb-s3          the cache on, stage 4 off (the stage 3 estimator: inline shading with the probe feedback)
  cb-b1 / cb-b3 / cb-b4   the cache on with the relight and the cell-indirect transport, Bounces 1 / 3 / 4
                 (surface vertices of the cell; the pixel adds one: b1 ~ a 2-vertex path, b3 ~ 4 vertices)
  cb-pt2 / cb-pt4 / cb-pt5 / cb-pt8   the DXR path tracer on the same scene, MaxBounces 2 / 4 / 5 / 8 (surface
                 vertices; through the mirror a cell of B vertices is matched by MaxBounces B + 1), frames 255 + 511
  step / static  surface-cache-stage4-sunstep / -sunstatic with the full stage 4 (Relight + MultiBounce,
                 Bounces 3): the Lua script on the Sun steps its intensity 13.5 -> 6.75 at a logged frame;
                 the static reference starts at 6.75 (StaleFrames 255: no age eviction in 240 frames). Every frame 60-239 captured.
  step-s3        the sun step with the cache on and stage 4 off (a lighting change clears the table)
  perf-s3 / perf-b3   cornell at 1920x1080, 300 frames timed, Stats 0 (GPU ms per pass from the manifest)
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "flicker"))
import runner  # noqa: E402

REFERENCE_PROJECT = HERE / "reflection-reference.uzproject"
LL_PROJECT = HERE / "surface-cache-locallight.uzproject"
CB_PROJECT = HERE / "surface-cache-stage4-cornell.uzproject"
STEP_PROJECT = HERE / "surface-cache-stage4-sunstep.uzproject"
STATIC_PROJECT = HERE / "surface-cache-stage4-sunstatic.uzproject"

BASE = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0}
# The scene projects pin the exposure chain; the command line pins it again (scene cvars win otherwise).
FIXED = {**BASE, "r.AutoExposure": 0, "r.Exposure": 1, "r.TonemapOperator": 0, "r.Bloom": 0, "r.TAA": 0,
         "r.Fog": 0, "r.VolumetricFog": 0, "r.GTAO": 0, "r.RadianceCache": 1, "r.RayTracing": 1, "r.RayTracing.GI": 1,
         "r.RayTracing.Reflections": 1}
OFF = {"r.GI.SurfaceCache": 0}
S3 = {"r.GI.SurfaceCache": 1, "r.GI.SurfaceCache.Stats": 1}
S4 = {**S3, "r.GI.SurfaceCache.Relight": 1, "r.GI.SurfaceCache.MultiBounce": 1}
# The responsiveness runs keep every cell past the 240 captured frames (StaleFrames 255), so any
# eviction in them is caused by the lighting change, not by the age policy.
KEEP = {"r.GI.SurfaceCache.StaleFrames": 255}
FIRST, LAST = 280, 299

# name: (project, cvars, frames, screenshot_frames, extra_args, timing_first)
CASES = {
    "default": (REFERENCE_PROJECT, {**BASE, "r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1}, 100, "95", [], 200),
    "ll-pt": (LL_PROJECT, BASE, 512, "255-511:256", ["--path-trace"], 200),
    "cb-off": (CB_PROJECT, {**FIXED, **OFF}, 300, f"{FIRST}-{LAST}", [], 200),
    "cb-s3": (CB_PROJECT, {**FIXED, **S3}, 300, f"{FIRST}-{LAST}", [], 200),
    "cb-b1": (CB_PROJECT, {**FIXED, **S4, "r.GI.SurfaceCache.Bounces": 1}, 300, f"{FIRST}-{LAST}", [], 200),
    "cb-b3": (CB_PROJECT, {**FIXED, **S4, "r.GI.SurfaceCache.Bounces": 3}, 300, f"{FIRST}-{LAST}", [], 200),
    "cb-b4": (CB_PROJECT, {**FIXED, **S4, "r.GI.SurfaceCache.Bounces": 4}, 300, f"{FIRST}-{LAST}", [], 200),
    "cb-pt2": (CB_PROJECT, {**FIXED, "r.PathTracer.MaxBounces": 2}, 512, "255-511:256",
               ["--path-trace", "--pt-bounces", "2"], 200),
    "cb-pt4": (CB_PROJECT, {**FIXED, "r.PathTracer.MaxBounces": 4}, 512, "255-511:256",
               ["--path-trace", "--pt-bounces", "4"], 200),
    "cb-pt5": (CB_PROJECT, {**FIXED, "r.PathTracer.MaxBounces": 5}, 512, "255-511:256",
               ["--path-trace", "--pt-bounces", "5"], 200),
    "cb-pt8": (CB_PROJECT, {**FIXED, "r.PathTracer.MaxBounces": 8}, 512, "255-511:256",
               ["--path-trace", "--pt-bounces", "8"], 200),
    "step": (STEP_PROJECT, {**FIXED, **KEEP, **S4, "r.GI.SurfaceCache.Bounces": 3}, 240, "60-239", [], 200),
    "static": (STATIC_PROJECT, {**FIXED, **KEEP, **S4, "r.GI.SurfaceCache.Bounces": 3}, 240, "60-239", [], 200),
    "step-s3": (STEP_PROJECT, {**FIXED, **KEEP, **S3}, 240, "60-239", [], 200),
    "static-s3": (STATIC_PROJECT, {**FIXED, **KEEP, **S3}, 240, "60-239", [], 200),
    "perf-s3": (CB_PROJECT, {**FIXED, **S3, "r.GI.SurfaceCache.Stats": 0}, 300, None,
                ["--width", "1920", "--height", "1080"], 150),
    "perf-b3": (CB_PROJECT, {**FIXED, **S4, "r.GI.SurfaceCache.Stats": 0, "r.GI.SurfaceCache.Bounces": 3}, 300, None,
                ["--width", "1920", "--height", "1080"], 150),
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--case", required=True, choices=sorted(CASES))
    ap.add_argument("--exe", default=None)
    a = ap.parse_args()
    if a.exe:
        runner.EXE = a.exe
    project, cvars, frames, shots, extra, timing_first = CASES[a.case]
    r = runner.run_capture(str(Path(a.out) / a.case), str(project), frames, clean=True, timeout=180, cvars=cvars,
                           extra_args=extra, screenshot_frames=shots, timing_first=timing_first)
    print(f"case {a.case} exit {r['exit_code']} dir {r['dir']}")
    return 0 if r["exit_code"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
