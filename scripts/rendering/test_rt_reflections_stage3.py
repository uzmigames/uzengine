"""Bounded Stage 3 HWRT captures: the ray-traced reflections WITHOUT the screen trace. Run each case
separately (180 seconds maximum).

Stage 3 decouples the ray pass from SSR: with `r.SSR 0` (or `r.SSR.Trace 0`) the pass owns the
resolved reflection buffer (every eligible texel traced, zero weight where it does not trace) and
the lighting blends it through the same `PerFrameCB::ssrSrvIndex`; on the denoise route the SSR
temporal + spatial passes run over the ray texels. The SSR-on route is unchanged by construction,
and the captures prove it byte for byte.

Scenes: the stage 2 glossy wall + off-screen block (`rt-reflections-glossy.uzproject`, which sets
`r.SSR 1`; the `ssr-off-*` cases override it to 0) and the stage 1 mirror + off-screen block
(`rt-reflections-offscreen.uzproject`). The `before/` set is captured with the committed stage 1/2
binary by the stage 1/2 harnesses (cases `mirror`, `glossy-indirect`, `rt`, `default`); the same
cases captured with the stage 3 binary must be byte-identical to it.

Cases (all at frame 95, 100 frames):
  ssr-on-mirror / ssr-on-glossy-indirect  the stage 2 `mirror` / `glossy-indirect` settings (r.SSR 1)
  ssr-on-stage1                            the stage 1 `rt` settings (r.SSR 1)
  ssr-off-mirror                           r.SSR 0 + mirror rays (stage 1 logic, MaxRoughness 1)
  ssr-off-glossy-indirect                  r.SSR 0 + glossy + indirect + denoise (the SSR denoiser reused)
  trace-off-glossy-indirect                r.SSR 1, r.SSR.Trace 0 + glossy + indirect + denoise
  ssr-off-stage1                           stage 1 scene, r.SSR 0 + mirror rays
  both-off                                 r.SSR 0, r.RayTracing.Reflections 0 (control: no reflection layer)
  metrics                                  byte-identity vs before/, RMSE vs the path tracer references
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "flicker"))
import runner  # noqa: E402

REPO = Path(runner.REPO)
STAGE1_PROOF = REPO.parent / "rt-reflections-stage1-proof"
STAGE2_PROOF = REPO.parent / "rt-reflections-stage2-proof"

# Stage 2 regions (640 x 360; screen-right is -X): the wall, the block's reflection, the
# sun-shadowed +Y face inside it, the reflected sunlit floor away from the block.
ROI_WALL = (0, 0, 640, 300)
ROI_BLOCK = (35, 85, 205, 270)
ROI_SHADOWED_FACE = (55, 110, 150, 250)
ROI_FLOOR_REFLECTION = (260, 200, 620, 290)
# Stage 1 region: the mirror's reflection of the off-screen block.
ROI_STAGE1 = (250, 190, 390, 350)

GLOSSY_PROJECT = HERE / "rt-reflections-glossy.uzproject"
STAGE1_PROJECT = HERE / "rt-reflections-offscreen.uzproject"

# name: (project, cvars)
RT_ON = {"r.RayTracing.Reflections": 1, "r.RayTracing.Reflections.MaxRoughness": 1.0}
GLOSSY_INDIRECT_DENOISE = {"r.RayTracing.Reflections.Glossy": 1, "r.RayTracing.Reflections.Indirect": 1,
                           "r.RayTracing.Reflections.Denoise": 1}
MIRROR = {"r.RayTracing.Reflections.Glossy": 0, "r.RayTracing.Reflections.Indirect": 0,
          "r.RayTracing.Reflections.Denoise": 0}
CASES = {
    "ssr-on-mirror":             (GLOSSY_PROJECT, {**RT_ON, **MIRROR}),
    "ssr-on-glossy-indirect":    (GLOSSY_PROJECT, {**RT_ON, **GLOSSY_INDIRECT_DENOISE}),
    "ssr-on-stage1":             (STAGE1_PROJECT, {"r.RayTracing.Reflections": 1}),
    "ssr-off-mirror":            (GLOSSY_PROJECT, {"r.SSR": 0, **RT_ON, **MIRROR}),
    "ssr-off-glossy-indirect":   (GLOSSY_PROJECT, {"r.SSR": 0, **RT_ON, **GLOSSY_INDIRECT_DENOISE}),
    "trace-off-glossy-indirect": (GLOSSY_PROJECT, {"r.SSR": 1, "r.SSR.Trace": 0, **RT_ON, **GLOSSY_INDIRECT_DENOISE}),
    "ssr-off-stage1":            (STAGE1_PROJECT, {"r.SSR": 0, "r.RayTracing.Reflections": 1}),
    "both-off":                  (GLOSSY_PROJECT, {"r.SSR": 0, "r.RayTracing.Reflections": 0}),
}


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def crop(img, roi):
    x0, y0, x1, y1 = roi
    return img[y0:y1, x0:x1]


def rmse(a, b, roi):
    return float(np.sqrt(np.mean((crop(a, roi) - crop(b, roi)) ** 2)))


def mean_codes(img, roi):
    return float(crop(img, roi).mean())


def identity(a: Path, b: Path):
    delta = np.abs(image(a) - image(b))
    return {"max_code_delta": int(delta.max()), "mean_abs_code_delta": float(delta.mean()),
            "png_bytes_identical": a.read_bytes() == b.read_bytes()}


def metrics(out):
    before = out.parent / "before"
    frame = lambda case: out / case / "frames/f_95.png"  # noqa: E731
    result = {"roi_wall_xyxy": ROI_WALL, "roi_block_xyxy": ROI_BLOCK, "roi_shadowed_face_xyxy": ROI_SHADOWED_FACE,
              "roi_floor_reflection_xyxy": ROI_FLOOR_REFLECTION, "roi_stage1_xyxy": ROI_STAGE1}

    # SSR-on route unchanged: byte-identical to the committed stage 1/2 binary's captures (before/)
    # and to the committed proof dirs.
    for case, ref in (("ssr-on-mirror", before / "mirror/frames/f_95.png"),
                      ("ssr-on-glossy-indirect", before / "glossy-indirect/frames/f_95.png"),
                      ("ssr-on-stage1", before / "rt/frames/f_95.png")):
        result[f"{case}_vs_before"] = identity(frame(case), ref)
    for case, ref in (("ssr-on-mirror", STAGE2_PROOF / "mirror/frames/f_95.png"),
                      ("ssr-on-glossy-indirect", STAGE2_PROOF / "glossy-indirect/frames/f_95.png"),
                      ("ssr-on-stage1", STAGE1_PROOF / "rt/frames/f_95.png")):
        if ref.exists():
            result[f"{case}_vs_committed_proof"] = identity(frame(case), ref)
    # Default route unchanged (captured by the stage 1 harness into out/default).
    result["default_vs_f005"] = identity(out / "default/frames/f_95.png", REPO.parent / "f005-after/frames/f_95.png")

    # RT without SSR renders: non-black reflector, close to the path tracer, as the SSR-on route.
    pt2 = image(STAGE2_PROOF / "pt/frames/f_511.png")
    imgs = {c: image(frame(c)) for c in ("ssr-on-mirror", "ssr-on-glossy-indirect", "ssr-off-mirror",
                                          "ssr-off-glossy-indirect", "trace-off-glossy-indirect", "both-off")}
    for c, img in imgs.items():
        result[f"{c}_rmse_wall_vs_pt"] = rmse(img, pt2, ROI_WALL)
        result[f"{c}_rmse_block_vs_pt"] = rmse(img, pt2, ROI_BLOCK)
        result[f"{c}_rmse_floor_reflection_vs_pt"] = rmse(img, pt2, ROI_FLOOR_REFLECTION)
        result[f"{c}_mean_block"] = mean_codes(img, ROI_BLOCK)
        result[f"{c}_mean_shadowed_face"] = mean_codes(img, ROI_SHADOWED_FACE)
    result["pt_mean_block"] = mean_codes(pt2, ROI_BLOCK)
    result["pt_mean_shadowed_face"] = mean_codes(pt2, ROI_SHADOWED_FACE)
    result["ssr-off-mirror_rmse_wall_vs_ssr-on-mirror"] = rmse(imgs["ssr-off-mirror"], imgs["ssr-on-mirror"], ROI_WALL)
    result["ssr-off-glossy-indirect_rmse_wall_vs_ssr-on-glossy-indirect"] = rmse(
        imgs["ssr-off-glossy-indirect"], imgs["ssr-on-glossy-indirect"], ROI_WALL)
    result["trace-off_rmse_wall_vs_ssr-off_glossy-indirect"] = rmse(
        imgs["trace-off-glossy-indirect"], imgs["ssr-off-glossy-indirect"], ROI_WALL)

    pt1 = image(STAGE1_PROOF / "pt/frames/f_511.png")
    s1_on, s1_off = image(frame("ssr-on-stage1")), image(frame("ssr-off-stage1"))
    result["stage1_ssr-on_rmse_roi_vs_pt"] = rmse(s1_on, pt1, ROI_STAGE1)
    result["stage1_ssr-off_rmse_roi_vs_pt"] = rmse(s1_off, pt1, ROI_STAGE1)
    result["stage1_ssr-off_rmse_roi_vs_ssr-on"] = rmse(s1_off, s1_on, ROI_STAGE1)
    result["stage1_ssr-off_full_frame_rmse_vs_ssr-on"] = float(np.sqrt(np.mean((s1_off - s1_on) ** 2)))
    result["stage1_ssr-off_mean_roi"] = mean_codes(s1_off, ROI_STAGE1)
    result["stage1_ssr-on_mean_roi"] = mean_codes(s1_on, ROI_STAGE1)

    print(json.dumps(result, indent=2))
    (out / "metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")

    # SSR-on and default routes untouched.
    for key in ("ssr-on-mirror_vs_before", "ssr-on-glossy-indirect_vs_before", "ssr-on-stage1_vs_before",
                "default_vs_f005"):
        assert result[key]["max_code_delta"] == 0, key
    # RT without SSR renders the off-screen block (the control without any reflection layer does not)
    # and lands near the path tracer, as the SSR-on route.
    assert result["ssr-off-mirror_mean_block"] > result["both-off_mean_block"] + 5.0
    assert result["ssr-off-mirror_rmse_block_vs_pt"] < result["both-off_rmse_block_vs_pt"]
    assert result["ssr-off-glossy-indirect_rmse_wall_vs_pt"] < result["ssr-off-mirror_rmse_wall_vs_pt"]
    assert result["ssr-off-glossy-indirect_mean_shadowed_face"] > 8.0
    assert result["stage1_ssr-off_mean_roi"] > 0.0
    assert result["stage1_ssr-off_rmse_roi_vs_pt"] < 2.0 * max(result["stage1_ssr-on_rmse_roi_vs_pt"], 1.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=["metrics", *CASES], required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.case == "metrics":
        metrics(out)
        return
    project, case_cvars = CASES[args.case]
    cvars = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0, **case_cvars}
    result = runner.run_capture(str(out / args.case), str(project), 100,
        clean=False, timeout=180, cvars=cvars, screenshot_frames="95")
    assert result["exit_code"] == 0, result["exit_code"]
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log, result["log"]
    assert "DEVICE_REMOVED" not in log and "device removed" not in log.lower(), result["log"]
    print(f'{args.case}: exit=0; {result["frames_dir"]}', flush=True)


if __name__ == "__main__":
    main()
