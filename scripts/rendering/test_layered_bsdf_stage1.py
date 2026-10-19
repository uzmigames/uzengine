"""Bounded RTX coat evidence; furnace quadrature lives in LayeredBSDFStage1 gtests.

Run each --case separately to keep GPU runs below 180 seconds. --case metrics
checks the captured zero-coat and default identities using RGB code values.
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


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=["default", "base", "zero", "coat", "metrics"],
                        required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.case == "metrics":
        base = image(out / "base/frames/f_255.png")
        zero = image(out / "zero/frames/f_255.png")
        coat = image(out / "coat/frames/f_255.png")
        default = image(out / "default/frames/f_95.png")
        reference = image(REPO.parent / "f005-after/frames/f_95.png")
        silhouette = np.any(base != base[0, 0], axis=2)
        interior = silhouette.copy()
        for _ in range(2):
            interior = (interior & np.roll(interior, 1, 0) & np.roll(interior, -1, 0)
                        & np.roll(interior, 1, 1) & np.roll(interior, -1, 1))
        grazing = silhouette & ~interior
        result = {
            "zero_max_code_delta": int(np.abs(base-zero).max()),
            "zero_mean_abs_delta": float(np.abs(base-zero).mean()),
            "base_mean": float(base.mean()), "zero_mean": float(zero.mean()),
            "coat_mean": float(coat.mean()),
            "coat_max_brightening_codes": int((coat-base).max()),
            "grazing_band_pixels": int(grazing.sum()),
            "grazing_mean_brightening_codes": float((coat-base)[grazing].mean()),
            "grazing_max_brightening_codes": int((coat-base)[grazing].max()),
            "default_max_code_delta": int(np.abs(default-reference).max()),
        }
        assert result["zero_max_code_delta"] == 0, result
        assert result["default_max_code_delta"] == 0, result
        assert result["coat_max_brightening_codes"] > 10, result
        assert result["grazing_mean_brightening_codes"] > 0, result
        print(json.dumps(result, indent=2))
        return
    cvars = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0}
    project = REPO / "scripts/rendering/layered-coat-stage1.uzproject"
    extra = ["--path-trace"]
    frames, shots = 256, "255"
    if args.case == "default":
        project = REPO / "scripts/rendering/reflection-reference.uzproject"
        frames, shots, extra = 100, "95", []
        cvars.update({"r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1})
    elif args.case == "zero":
        extra += ["--layered"]
        cvars["r.PathTracer.Coat.Weight"] = 0
    elif args.case == "coat":
        extra += ["--layered"]
    result = runner.run_capture(str(out / args.case), str(project), frames,
        clean=False, timeout=180, cvars=cvars, extra_args=extra, screenshot_frames=shots)
    assert result["exit_code"] == 0, result["exit_code"]
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log, result["log"]
    assert args.case == "default" or "RT-PSO CreateStateObject succeeded" in log
    print(f'{args.case}: exit=0; no compile failed; {result["frames_dir"]}', flush=True)


if __name__ == "__main__":
    main()
