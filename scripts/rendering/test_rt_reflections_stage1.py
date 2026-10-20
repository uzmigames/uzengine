"""Bounded Stage 1 HWRT captures. Run each case separately (180 seconds maximum).

The reflected block is outside the primary camera frustum. Both it and the
mirror have nonzero instance translations. The fixed reflection ROI is shared
by SSR, HWRT, and the converged DXR path-tracer reference.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent / "flicker"))
import runner

REPO = Path(runner.REPO)
ROI = (250, 190, 390, 350)


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=["default", "default-metrics", "ssr", "rt", "pt", "metrics"],
                        required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.case == "default-metrics":
        reference_path = REPO.parent / "f005-after/frames/f_95.png"
        default_path = out / "default/frames/f_95.png"
        delta = np.abs(image(default_path) - image(reference_path))
        result = {
            "default_max_code_delta": int(delta.max()),
            "default_mean_abs_code_delta": float(delta.mean()),
            "default_png_bytes_identical": default_path.read_bytes() == reference_path.read_bytes(),
        }
        print(json.dumps(result, indent=2))
        assert result["default_max_code_delta"] == 0
        return
    if args.case == "metrics":
        reference_path = REPO.parent / "f005-after/frames/f_95.png"
        default_path = out / "default/frames/f_95.png"
        ssr = image(out / "ssr/frames/f_95.png")
        rt = image(out / "rt/frames/f_95.png")
        pt = image(out / "pt/frames/f_511.png")
        previous_pt = image(out / "pt/frames/f_255.png")
        x0, y0, x1, y1 = ROI
        crop = np.s_[y0:y1, x0:x1]
        ssr_error = float(np.sqrt(np.mean((ssr[crop] - pt[crop]) ** 2)))
        rt_error = float(np.sqrt(np.mean((rt[crop] - pt[crop]) ** 2)))
        result = {
            "reflection_roi_xyxy": ROI,
            "default_max_code_delta": int(np.abs(image(default_path) - image(reference_path)).max()),
            "default_png_bytes_identical": default_path.read_bytes() == reference_path.read_bytes(),
            "ssr_reflection_rmse_codes": ssr_error,
            "rt_reflection_rmse_codes": rt_error,
            "rt_minus_ssr_rmse_codes": rt_error - ssr_error,
            "pt_256_to_512_rmse_codes": float(np.sqrt(np.mean(
                (previous_pt[crop] - pt[crop]) ** 2))),
            "full_frame_ssr_rmse_codes": float(np.sqrt(np.mean((ssr - pt) ** 2))),
            "full_frame_rt_rmse_codes": float(np.sqrt(np.mean((rt - pt) ** 2))),
        }
        print(json.dumps(result, indent=2))
        assert result["default_max_code_delta"] == 0
        assert rt_error < ssr_error
        return

    project = REPO / "scripts/rendering/rt-reflections-offscreen.uzproject"
    frames, shots, extra = 100, "95", []
    cvars = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0,
             "r.RayTracing.Reflections": int(args.case == "rt")}
    if args.case == "default":
        project = REPO / "scripts/rendering/reflection-reference.uzproject"
        cvars.update({"r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1})
    elif args.case == "pt":
        # `--screenshot-frames` takes N, A-B or A-B:step (CaptureSequence::Parse), not a list:
        # 255-511:256 captures exactly frames 255 and 511.
        frames, shots, extra = 512, "255-511:256", ["--path-trace"]
    result = runner.run_capture(str(out / args.case), str(project), frames,
        clean=False, timeout=180, cvars=cvars, extra_args=extra, screenshot_frames=shots)
    assert result["exit_code"] == 0, result["exit_code"]
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log, result["log"]
    print(f'{args.case}: exit=0; {result["frames_dir"]}', flush=True)


if __name__ == "__main__":
    main()
