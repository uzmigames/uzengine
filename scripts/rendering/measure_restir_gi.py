"""Bounded, serialized GI-ReSTIR captures; compare against the plain path tracer.

Run: python scripts/rendering/measure_restir_gi.py capture OUTPUT MODE
or:  python scripts/rendering/measure_restir_gi.py analyze OUTPUT
MODE is plain, noreuse, temporal, spatialonly, spatial, plain4, temporal4, default
or preview (spatial = temporal + spatial reuse; spatialonly = spatial reuse alone).
An `x8` suffix (plainx8, spatialx8) captures the sun-x3 scene variant so the dark
one-bounce rows sit near code 110 instead of 40, shrinking the 8-bit rounding floor
(r.Exposure does not act on the path-traced output; x8 saturates the display curve).
Output/MODE must be new. Every path-traced mode uses the same scene, resolution,
bounce count, one NEE sample per vertex and no Russian roulette, so the direct
term is the same realization in every mode and the per-frame differences are
the one-bounce indirect term alone. The metrics are spatial PNG error variance
against the converged plain capture, not repeated-seed ensemble variance.
Compare means in CODE space: this output curve is linear in radiance up to
clipping (a x3 sun gives x3.00 code), and a convex inverse-sRGB decode of two
images with different per-pixel noise biases the noisier mean upward (Jensen),
which read as a spurious -0.1% for the smoother GI images in the dark rows.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from flicker.runner import run_capture

REPO = Path(__file__).resolve().parents[2]
BASELINE = Path("E:/UzmiEngine/f005-after/frames/f_95.png")
FRAMES, SHOTS, LAST = 2055, "5-2050:5", 2050


def capture(output, mode):
    out = output / mode
    if out.exists():
        raise SystemExit(f"Refusing to overwrite capture: {out}")
    bright = mode.endswith("x8")
    mode = mode[:-2] if bright else mode
    bounces = 4 if mode.endswith("4") else 2
    cvars = {
        "r.TAA": 0, "r.TechPanel.Show": 0, "r.Toolbar.Show": 0,
        "r.VolumetricFog": 0, "r.PathTracer.NEESamples": 1,
        "r.PathTracer.MaxBounces": bounces, "r.PathTracer.RussianRoulette": 0,
        "r.PathTracer.GIReSTIR.Temporal": int(mode.startswith(("temporal", "spatial"))
                                              and mode != "spatialonly"),
        "r.PathTracer.GIReSTIR.Spatial": int(mode.startswith("spatial")),
    }
    args = ["--hidden", "--path-trace", "--pt-bounces", str(bounces)]
    if mode.startswith(("noreuse", "temporal", "spatial")):
        args += ["--gi-restir"]
    scene, frames, shots = "gi-restir-indirect" + ("-bright" if bright else ""), FRAMES, SHOTS
    if mode == "preview":
        frames, shots = 60, "50"
    if mode == "default":
        scene, frames, shots = "reflection-reference", 100, "95"
        cvars = {"r.TAA": 1, "r.TechPanel.Show": 0, "r.Toolbar.Show": 0,
                 "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1}
        args = []
    result = run_capture(str(out), str(REPO / "scripts/rendering" / f"{scene}.uzproject"),
                         frames, cvars=cvars, extra_args=args, screenshot_frames=shots,
                         timeout=175, clean=False)
    print(f"CAPTURE {mode} EXIT {result['exit_code']}", flush=True)
    raise SystemExit(result["exit_code"])


def analyze(output):
    def image(mode, frame):
        return np.asarray(Image.open(output / mode / "frames" / f"f_{frame}.png")
                          .convert("RGB"), dtype=np.float64)

    def linear(x):
        x = x / 255.
        return np.where(x <= .04045, x / 12.92, ((x + .055) / 1.055) ** 2.4)

    def have(mode):
        return (output / mode / "frames" / f"f_{LAST}.png").exists()

    reference = image("plain", LAST)
    height = reference.shape[0]
    # Interior mask as in the DI study: mid-tone pixels, eroded, border excluded.
    original = (reference.mean(2) > 15) & (reference.mean(2) < 180)
    mask = original.copy()
    for dy in (-4, 0, 4):
        for dx in (-4, 0, 4):
            mask &= np.roll(np.roll(original, dy, axis=0), dx, axis=1)
    mask[:5] = mask[-5:] = False
    mask[:, :5] = mask[:, -5:] = False
    # One-bounce-only rows: the ceiling and the upper walls never see the sun.
    indirect = mask.copy()
    indirect[int(height * 0.42):] = False
    report = {"pixels": int(mask.sum()), "indirect_pixels": int(indirect.sum()),
              "means": {}, "variance": {}, "consistency": {}, "logs": {}}
    modes = [m for m in ("noreuse", "temporal", "spatialonly", "spatial") if have(m)]
    for mode in modes:
        high = image(mode, LAST)
        report["means"][mode] = {
            "code": float(high[mask].mean()),
            "code_delta_percent": float(100 * (high[mask].mean() / reference[mask].mean() - 1)),
            "indirect_code": float(high[indirect].mean()),
            "indirect_code_delta_percent": float(100 * (high[indirect].mean() /
                                                       reference[indirect].mean() - 1)),
            "whole_code_delta_percent": float(100 * (high.mean() / reference.mean() - 1)),
            "linear": float(linear(high)[mask].mean()),
            "linear_delta_percent": float(100 * (linear(high)[mask].mean() /
                                                linear(reference)[mask].mean() - 1)),
            "indirect_linear": float(linear(high)[indirect].mean()),
            "indirect_linear_delta_percent": float(100 * (linear(high)[indirect].mean() /
                                                         linear(reference)[indirect].mean() - 1)),
            "whole_linear": float(linear(high).mean()),
            "whole_linear_delta_percent": float(100 * (linear(high).mean() /
                                                      linear(reference).mean() - 1)),
            "mean_abs_code_delta": float(np.abs(high - reference)[mask].mean()),
        }
    if have("plainx8") and have("spatialx8"):
        high, ref8 = image("spatialx8", LAST), image("plainx8", LAST)
        report["means"]["spatialx8_vs_plainx8"] = {
            "code": float(high[mask].mean()), "reference_code": float(ref8[mask].mean()),
            "indirect_code": float(high[indirect].mean()),
            "indirect_reference_code": float(ref8[indirect].mean()),
            "code_delta_percent": float(100 * (high[mask].mean() / ref8[mask].mean() - 1)),
            "indirect_code_delta_percent": float(100 * (high[indirect].mean() /
                                                       ref8[indirect].mean() - 1)),
            "linear_delta_percent": float(100 * (linear(high)[mask].mean() /
                                                linear(ref8)[mask].mean() - 1)),
            "indirect_linear_delta_percent": float(100 * (linear(high)[indirect].mean() /
                                                         linear(ref8)[indirect].mean() - 1)),
        }
    if have("plain4") and have("temporal4"):
        high, ref4 = image("temporal4", LAST), image("plain4", LAST)
        report["means"]["temporal4_vs_plain4"] = {
            "code_delta_percent": float(100 * (high[mask].mean() / ref4[mask].mean() - 1)),
            "indirect_code_delta_percent": float(100 * (high[indirect].mean() /
                                                       ref4[indirect].mean() - 1)),
            "linear_delta_percent": float(100 * (linear(high)[mask].mean() /
                                                linear(ref4)[mask].mean() - 1)),
            "indirect_linear_delta_percent": float(100 * (linear(high)[indirect].mean() /
                                                         linear(ref4)[indirect].mean() - 1)),
            "whole_linear_delta_percent": float(100 * (linear(high).mean() /
                                                      linear(ref4).mean() - 1)),
        }
    # No-reuse must reproduce the plain estimator sample for sample (same
    # candidate, W = 1/p): only float rounding may differ.
    if have("noreuse"):
        for frame in (5, 50, 500, LAST):
            delta = np.abs(image("noreuse", frame) - image("plain", frame))
            report["consistency"][frame] = {"max_code_delta": float(delta.max()),
                                            "mean_code_delta": float(delta.mean())}
    for frame in (5, 10, 20, 50, 200):
        values = {m: float((image(m, frame) - reference)[mask].var())
                  for m in ["plain"] + modes}
        for m in modes:
            values[f"{m}_vs_plain_percent"] = 100 * (values[m] / values["plain"] - 1)
        values["indirect"] = {m: float((image(m, frame) - reference)[indirect].var())
                              for m in ["plain"] + modes}
        values["whole_image"] = {m: float((image(m, frame) - reference).var())
                                 for m in ["plain"] + modes}
        report["variance"][frame] = values
    default = output / "default/frames/f_95.png"
    if default.exists():
        report["default"] = {
            "byte_identical": BASELINE.read_bytes() == default.read_bytes(),
            "max_code_delta": float(abs(np.asarray(Image.open(BASELINE), dtype=float) -
                                        np.asarray(Image.open(default), dtype=float)).max()),
        }
    for mode in ["plain", "default", "plain4", "temporal4"] + modes:
        log = output / mode / "run.log"
        if not log.exists():
            continue
        text = log.read_text(encoding="utf-8", errors="replace")
        report["logs"][mode] = {
            "compile_failed": "compile failed" in text,
            "device_removed": "DEVICE_REMOVED" in text or "0x887A0005" in text,
            "gi_dispatch": "GI-ReSTIR: temporal dispatched" in text,
            "gi_spatial_dispatch": "GI-ReSTIR: spatial" in text,
        }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    action, output = sys.argv[1], Path(sys.argv[2])
    if action == "capture":
        capture(output, sys.argv[3])
    elif action == "analyze":
        analyze(output)
    else:
        raise SystemExit(f"Unknown action: {action}")
