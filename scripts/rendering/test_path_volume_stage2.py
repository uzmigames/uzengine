"""Bounded DXR delta/ratio-tracking captures and linear-HDR analytic proofs.

Run each --case separately. Capture baseline with the committed stage-one binary
before building stage two. Every engine process has a 180-second deadline.
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
import regiondump
import runner


def probe(out, case, frame, region="center"):
    path = out / case / "region" / f"region_{frame}.bin"
    if case == "baseline" and not path.exists():
        path = out / "baseline-region" / f"region_{frame}.bin"
    data = regiondump.load_frame(str(path))
    rect = data.rect(region)
    # Standalone "extra" captures contain the GPU TAA footer, but the file
    # writer copies its stamp into the header only for the taa/output fields.
    # Check the actual GPU-written footer, including its valid bit and size.
    footer = data.data[rect.base + rect.pixel_count*data.records_per_pixel + 1]
    stamp = int(footer.view(np.uint32)[0])
    assert (rect.valid and data.frame == frame and stamp == frame
            and footer[1] == data.output_width and footer[2] == data.output_height
            and footer[3] == 1), (path, rect.stamp_flags, stamp, footer)
    return rect.channel("taa_tap_centre")[..., :3].astype(np.float64)


def reference(camera, variation, sigma_a, sigma_s, x0=156):
    # Reconstruct the same rays as DXR, rather than assuming an ideal pinhole
    # projection. Frame-average subpixel jitter is centered.
    x, y = np.meshgrid(np.arange(x0, x0+8)+.5, np.arange(86, 94)+.5)
    clip = np.stack((x/camera.render_width*2-1, 1-y/camera.render_height*2,
                     np.full_like(x, .5), np.ones_like(x)), axis=-1)
    target = clip @ np.linalg.inv(camera.view_proj.astype(np.float64))
    origin = camera.camera_pos.astype(np.float64)
    assert np.array_equal(origin, [0., -300., 150.]), origin
    directions = target[..., :3]/target[..., 3:] - origin
    directions /= np.linalg.norm(directions, axis=-1, keepdims=True)
    center = np.array([0., 0., 150.])
    offset = origin-center
    b = directions @ offset
    root = np.sqrt(b*b-np.dot(offset, offset)+100**2)
    start, length = -b-root, 2*root
    entry = origin+directions*start[..., None]
    axis = np.array([1., 1., 0.])/np.sqrt(2)/100
    q0 = (entry-center) @ axis
    dq = directions @ axis
    sigma = sigma_a+sigma_s
    tau = sigma*length*(1+variation*(q0+.5*dq*length))
    transmitted = .2*np.exp(-tau)
    if sigma_s == 0:
        return transmitted
    scatter = np.zeros_like(transmitted)
    intervals = 4096
    for i in range(intervals):
        s = length*((i+.5)/intervals)
        point = entry+directions*s[..., None]
        q = q0+dq*s
        rho = 1+variation*q
        shadow_b = -(point-center)[..., 1]
        shadow_d = -shadow_b+np.sqrt(shadow_b**2
            -np.sum((point-center)**2, axis=-1)+100**2)
        camera_tau = sigma*s*(1+variation*(q0+.5*dq*s))
        shadow_tau = sigma*shadow_d*(1+variation*(q-.5*axis[1]*shadow_d))
        scatter += sigma_s*10/(4*np.pi)*rho*np.exp(-camera_tau-shadow_tau)*length/intervals
    return transmitted+scatter


def metrics(out):
    result = {}
    camera = regiondump.load_frame(str(out / "absorption/region/region_8191.bin"))
    expected = reference(camera, .8, .005, 0)
    absorption = probe(out, "absorption", 8191).mean(axis=-1)
    result["analytic_transmission"] = float(expected.mean()/.2)
    result["ratio_tracking_transmission"] = float(absorption.mean()/.2)
    result["absorption_max_radiance_error"] = float(np.abs(absorption-expected).max())
    assert abs(absorption.mean()-expected.mean()) < .0006, result
    result["off_axis_density"] = []
    for name, x0 in (("left", 124), ("right", 188)):
        transmitted = reference(camera, .8, .005, 0, x0)
        actual = probe(out, "absorption", 8191, name).mean(axis=-1)
        scattered = probe(out, "scatter", 8191, name).mean(axis=-1)
        scatter_reference = reference(camera, .8, .002, .003, x0)
        result["off_axis_density"].append({
            "region": name,
            "transmission_measured": float(actual.mean()/.2),
            "transmission_analytic": float(transmitted.mean()/.2),
            "scattering_only_measured": float((scattered-transmitted).mean()),
            "scattering_only_reference": float((scatter_reference-transmitted).mean()),
        })
        assert abs(actual.mean()-transmitted.mean()) < .0006, result
        assert abs(scattered.mean()-scatter_reference.mean()) < .002, result
    constant = probe(out, "constant", 2047)
    baseline = probe(out, "baseline", 2047)
    result["constant_vs_stage1_max_hdr_delta"] = float(np.abs(constant-baseline).max())
    result["constant_mean"] = float(constant.mean())
    result["stage1_mean"] = float(baseline.mean())
    assert result["constant_vs_stage1_max_hdr_delta"] == 0, result
    baseline_png = out / "baseline_2047.png"
    if not baseline_png.exists():
        baseline_png = out / "baseline/frames/f_2047.png"
    constant_png = out / "constant/frames/f_2047.png"
    a = np.asarray(Image.open(constant_png).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(baseline_png).convert("RGB"), dtype=np.int16)
    result["constant_vs_stage1_max_code_delta"] = int(np.abs(a-b).max())
    assert result["constant_vs_stage1_max_code_delta"] == 0, result
    expected = reference(camera, .8, .002, .003)
    result["scatter_reference_mean"] = float(expected.mean())
    result["scatter_convergence"] = []
    for frame in (31, 255, 2047, 8191):
        actual = probe(out, "scatter", frame).mean(axis=-1)
        result["scatter_convergence"].append({
            "frame": frame, "mean": float(actual.mean()),
            "rmse": float(np.sqrt(np.mean((actual-expected)**2))),
        })
    final = result["scatter_convergence"][-1]
    assert final["rmse"] < result["scatter_convergence"][0]["rmse"], result
    assert abs(final["mean"]-expected.mean()) < .002, result
    off = probe(out, "off", 255)
    zero = probe(out, "zero", 255)
    result["zero_max_hdr_delta"] = float(np.abs(off-zero).max())
    assert result["zero_max_hdr_delta"] == 0, result
    result["zero_png_bytes_identical"] = (
        (out / "off/frames/f_255.png").read_bytes()
        == (out / "zero/frames/f_255.png").read_bytes())
    assert result["zero_png_bytes_identical"], result
    default = out / "default/frames/f_95.png"
    baseline = Path(runner.REPO).parent / "f005-after/frames/f_95.png"
    a = np.asarray(Image.open(default).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(baseline).convert("RGB"), dtype=np.int16)
    result["default_max_code_delta"] = int(np.abs(a-b).max())
    result["default_png_bytes_identical"] = default.read_bytes() == baseline.read_bytes()
    assert result["default_max_code_delta"] == 0, result
    assert result["default_png_bytes_identical"], result
    report = json.dumps(result, indent=2)
    (out / "metrics.json").write_text(report + "\n", encoding="utf-8")
    print(report)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", required=True, choices=[
        "baseline", "constant", "absorption", "scatter", "off", "zero", "default", "metrics"])
    args = parser.parse_args()
    out = args.out.resolve()
    if args.case == "metrics":
        metrics(out)
        return
    project = HERE / "path-volume-stage2.uzproject"
    cvars = {"r.TAA": 1}
    extra = ["--hidden", "--path-trace", "--volume"]
    frame, shots, first = 2047, "2047", 2047
    if args.case in ("baseline", "constant"):
        project = HERE / "path-volume-stage1.uzproject"
        cvars["r.PathTracer.Volume.Variation"] = 0
    elif args.case in ("scatter", "absorption"):
        frame, shots, first = 8191, "31-8191:32", 31
        if args.case == "absorption":
            cvars.update({"r.PathTracer.Volume.SigmaA": .005, "r.PathTracer.Volume.SigmaS": 0})
    elif args.case in ("off", "zero"):
        frame, shots, first = 255, "255", 255
        if args.case == "off":
            extra.remove("--volume")
        else:
            cvars.update({"r.PathTracer.Volume.SigmaA": 0, "r.PathTracer.Volume.SigmaS": 0})
    else:
        project = HERE / "reflection-reference.uzproject"
        frame, shots, first, extra = 95, "95", 95, []
        cvars.update({"r.VolumetricFog": 0, "r.TAA.LogDecisions": 1,
                      "r.TechPanel.Show": 0, "r.Toolbar.Show": 0})
    regions = [("center", 156, 86, 8, 8)]
    if args.case in ("absorption", "scatter"):
        regions += [("left", 124, 86, 8, 8), ("right", 188, 86, 8, 8)]
    result = runner.run_capture(str(out / args.case), str(project), frame+8,
        regions=regions if args.case != "default" else None,
        region_fields="extra", region_first=first, region_last=frame,
        clean=False, timeout=180, cvars=cvars, extra_args=extra, screenshot_frames=shots)
    assert result["exit_code"] == 0, result
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log and "[error]" not in log, result["log"]
    if args.case not in ("off", "zero", "default"):
        assert "PathVolume:" in log, result["log"]
    print(f'{args.case}: exit 0, clean shaders; {result["log"]}', flush=True)


if __name__ == "__main__":
    main()
