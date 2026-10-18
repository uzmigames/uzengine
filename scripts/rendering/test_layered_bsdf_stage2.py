"""Bounded RTX clearcoat-tint proof; Snell changes optical distance, not base directions.

Thickness is normal-incidence round-trip distance in cm. SigmaA is inverse cm.
The real-time approximation uses d/2 * (1/cos(theta_t_v)+1/cos(theta_t_l)).
This does not implement the deferred physically refracted N-layer stack.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent / "flicker"))
import runner

REPO = Path(runner.REPO)
SIGMA = np.array([0.1, 0.7, 1.4])  # Red-looking: absorb more green and blue than red.


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def metrics(out):
    images = {case: image(out / case / "frames/f_31.png")
              for case in ("base", "clear", "tint", "thick")}
    # Central 8x8 planar patch: NoV ~= 1, NoL = cos(45 deg), no area-light jitter.
    means = {case: pixels[176:184, 316:324].mean(axis=(0, 1))
             for case, pixels in images.items()}
    nv, nl, ior = 1.0, math.sqrt(0.5), 1.5
    path = 0.5 * (1 + 1 / math.sqrt(1 - (1 - nl*nl) / (ior*ior)))
    nh = vh = math.sqrt((1 + nl) / 2)
    alpha = 0.12**2
    a2 = alpha*alpha
    d = (nh*a2-nh)*nh+1
    fresnel = 0.04+0.96*(1-vh)**5
    visibility = 0.5/(nl*math.sqrt(nv*nv*(1-a2)+a2)
                      + nv*math.sqrt(nl*nl*(1-a2)+a2))
    # Tonemap 0 is linear passthrough; isolate the unabsorbed white coat reflection.
    coat_codes = 255 * 2 * fresnel*a2/(math.pi*d*d)*visibility*nl
    expected = np.exp(-SIGMA*path)
    expected_thick = np.exp(-SIGMA*2*path)
    measured = (means["tint"]-coat_codes)/(means["clear"]-coat_codes)
    measured_thick = (means["thick"]-coat_codes)/(means["clear"]-coat_codes)
    error = float(np.abs(measured-expected).max())
    thick_error = float(np.abs(measured_thick-expected_thick).max())
    # PNG 8-bit quantization: two independently rounded captures, >95-code reference.
    assert means["clear"].min() > 95, means
    assert error < 0.015 and thick_error < 0.015, (measured, expected, measured_thick)
    assert means["tint"][0] > means["tint"][1] > means["tint"][2], means
    assert np.all(means["thick"] <= means["tint"]), means
    zero = image(out / "stage1/frames/f_255.png")
    reference = image(REPO.parent / "layered-stage1/coat/frames/f_255.png")
    default = image(out / "default/frames/f_95.png")
    default_reference = image(REPO.parent / "f005-after/frames/f_95.png")
    result = {
        "sigma_a_per_cm": SIGMA.tolist(), "thickness_cm": 1, "NoV": nv, "NoL": nl,
        "snell_path_cm": path, "white_coat_codes": coat_codes,
        "patch_rgb_codes": {k: v.tolist() for k, v in means.items()},
        "expected_transmittance": expected.tolist(), "measured_transmittance": measured.tolist(),
        "thick_expected": expected_thick.tolist(), "thick_measured": measured_thick.tolist(),
        "max_tint_error": error, "max_thick_tint_error": thick_error,
        "stage1_max_code_delta": int(np.abs(zero-reference).max()),
        "stage1_mean_abs_delta": float(np.abs(zero-reference).mean()),
        "default_max_code_delta": int(np.abs(default-default_reference).max()),
    }
    assert result["stage1_max_code_delta"] == 0, result
    assert result["default_max_code_delta"] == 0, result
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", required=True,
                        choices=["base", "clear", "tint", "thick", "stage1", "default", "metrics"])
    args = parser.parse_args()
    out = args.out.resolve()
    if args.case == "metrics":
        metrics(out)
        return
    project = REPO / "scripts/rendering/layered-absorption-stage2.uzproject"
    frames, shots = 32, "31"
    extra = ["--path-trace", "--layered"]
    cvars = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0}
    if args.case == "base":
        extra = ["--path-trace"]
    elif args.case in ("tint", "thick"):
        cvars.update(dict(zip(("r.PathTracer.Coat.SigmaAR", "r.PathTracer.Coat.SigmaAG",
                              "r.PathTracer.Coat.SigmaAB"), SIGMA)))
        cvars["r.PathTracer.Coat.Thickness"] = 2 if args.case == "thick" else 1
    elif args.case == "stage1":
        project = REPO / "scripts/rendering/layered-coat-stage1.uzproject"
        frames, shots = 256, "255"
    elif args.case == "default":
        project = REPO / "scripts/rendering/reflection-reference.uzproject"
        frames, shots, extra = 100, "95", []
        cvars.update({"r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1})
    result = runner.run_capture(str(out / args.case), str(project), frames, clean=False,
        timeout=180, cvars=cvars, extra_args=extra, screenshot_frames=shots)
    assert result["exit_code"] == 0, result["exit_code"]
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log, result["log"]
    assert args.case == "default" or "RT-PSO CreateStateObject succeeded" in log
    print(f'{args.case}: exit=0; no compile failed; {result["frames_dir"]}', flush=True)


if __name__ == "__main__":
    main()
