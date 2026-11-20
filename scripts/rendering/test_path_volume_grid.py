"""Bounded DXR grid-medium captures and linear-HDR analytic proofs.

The heterogeneous grid medium (VolumeComponent + VolumeAsset, pathtracer_volume.hlsli
PathVolumeGrid*) is checked against closed-form or quadrature references built from
the same ray the DXR raygen reconstructs (view-projection inverse at clip z=.5):

  slab      constant Texture3D box, absorption only: Beer-Lambert exp(-sigmaT*chord)
  ramp      R16F linear density ramp, absorption only: exp(-sigmaT*int rho(x(t)) dt)
  emission  constant box, sigmaS=0, Le=1: L = L_bg*exp(-tau) + Le*(1-exp(-tau))
  scatter   constant box, isotropic single scattering with a -Y shadow ray
  off       --path-trace without --volume: the component must be inert
  cloud     builtin:cloud lit by the sun from four camera angles (montage) + 1080p perf

Run each --case separately; every engine process has a 180-second deadline.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
from pathlib import Path
import sys

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "flicker"))
import regiondump
import runner

BOX_CENTER = np.array([0., 0., 150.])
BOX_HALF = 100.
BACKDROP = .2
REGIONS = [("center", 156, 86, 8, 8), ("left", 124, 86, 8, 8), ("right", 188, 86, 8, 8)]


def probe(out, case, frame, region="center"):
    path = out / case / "region" / f"region_{frame}.bin"
    data = regiondump.load_frame(str(path))
    rect = data.rect(region)
    # Standalone "extra" captures carry the GPU TAA footer; check the actual
    # GPU-written stamp, valid bit and size rather than the CPU header copy.
    footer = data.data[rect.base + rect.pixel_count*data.records_per_pixel + 1]
    stamp = int(footer.view(np.uint32)[0])
    assert (rect.valid and data.frame == frame and stamp == frame
            and footer[1] == data.output_width and footer[2] == data.output_height
            and footer[3] == 1), (path, rect.stamp_flags, stamp, footer)
    return rect.channel("taa_tap_centre")[..., :3].astype(np.float64)


def rays(camera, x0, y0=86):
    """The DXR rays of an 8x8 region: origin and unit directions (frame-average jitter)."""
    x, y = np.meshgrid(np.arange(x0, x0+8)+.5, np.arange(y0, y0+8)+.5)
    clip = np.stack((x/camera.render_width*2-1, 1-y/camera.render_height*2,
                     np.full_like(x, .5), np.ones_like(x)), axis=-1)
    target = clip @ np.linalg.inv(camera.view_proj.astype(np.float64))
    origin = camera.camera_pos.astype(np.float64)
    assert np.array_equal(origin, [0., -300., 150.]), origin
    directions = target[..., :3]/target[..., 3:] - origin
    directions /= np.linalg.norm(directions, axis=-1, keepdims=True)
    return origin, directions


def box_interval(origin, directions):
    """Slab test against the [-100,100]^3 box at BOX_CENTER: (enter, exit)."""
    o = origin - BOX_CENTER
    with np.errstate(divide="ignore"):
        inv = 1./directions
    t0 = (-BOX_HALF-o)*inv
    t1 = (BOX_HALF-o)*inv
    enter = np.maximum(np.minimum(t0, t1).max(axis=-1), 0.)
    exit_ = np.maximum(t0, t1).min(axis=-1)
    return enter, np.maximum(enter, exit_)


def ramp_density(x):
    """Trilinear sample of the builtin:ramp grid (64^3 R16F, voxel-centre u in half)."""
    n = 64
    u = (x+BOX_HALF)/(2*BOX_HALF)
    u = np.clip(u, .5/n, 1-.5/n)*n-.5
    i0 = np.clip(np.floor(u).astype(int), 0, n-1)
    i1 = np.minimum(i0+1, n-1)
    f = u-np.floor(u)
    v0 = np.float16((i0+.5)/n).astype(np.float64)
    v1 = np.float16((i1+.5)/n).astype(np.float64)
    return v0+(v1-v0)*f


def reference(camera, case, x0):
    origin, directions = rays(camera, x0)
    enter, exit_ = box_interval(origin, directions)
    chord = exit_-enter
    if case == "slab":
        return BACKDROP*np.exp(-.005*chord)
    if case == "emission":
        tau = .005*chord
        return BACKDROP*np.exp(-tau)+(1-np.exp(-tau))
    intervals = 4096
    if case == "ramp":
        tau = np.zeros_like(chord)
        for i in range(intervals):
            s = enter+chord*((i+.5)/intervals)
            point = origin+directions*s[..., None]
            tau += ramp_density(point[..., 0])*chord/intervals
        return BACKDROP*np.exp(-.01*tau)
    assert case == "scatter"
    sigma_a, sigma_s = .002, .003
    sigma = sigma_a+sigma_s
    transmitted = BACKDROP*np.exp(-sigma*chord)
    scatter = np.zeros_like(chord)
    for i in range(intervals):
        s = enter+chord*((i+.5)/intervals)
        point = origin+directions*s[..., None]
        # The fixture's directional light shadow ray travels along -Y and
        # leaves the box at y = -100.
        shadow = point[..., 1]-(BOX_CENTER[1]-BOX_HALF)
        scatter += sigma_s*10/(4*np.pi)*np.exp(-sigma*((s-enter)+shadow))*chord/intervals
    return transmitted+scatter


def metrics(out):
    result = {}
    frame = 4095
    for case, tolerance in (("slab", .0006), ("ramp", .0006), ("emission", .003), ("scatter", .002)):
        camera = regiondump.load_frame(str(out / case / "region" / f"region_{frame}.bin"))
        entry = {"regions": [], "convergence": []}
        for name, x0, *_ in REGIONS:
            expected = reference(camera, case, x0)
            actual = probe(out, case, frame, name).mean(axis=-1)
            region = {"region": name, "measured_mean": float(actual.mean()),
                      "reference_mean": float(expected.mean()),
                      "max_abs_error": float(np.abs(actual-expected).max())}
            if case in ("slab", "ramp"):
                region["transmission_measured"] = float(actual.mean()/BACKDROP)
                region["transmission_analytic"] = float(expected.mean()/BACKDROP)
            entry["regions"].append(region)
            assert abs(actual.mean()-expected.mean()) < tolerance, (case, region)
        expected = reference(camera, case, 156)
        for f in (255, 1023, 4095):
            actual = probe(out, case, f).mean(axis=-1)
            entry["convergence"].append({"frame": f, "mean": float(actual.mean()),
                                         "rmse": float(np.sqrt(np.mean((actual-expected)**2)))})
        assert entry["convergence"][-1]["rmse"] < entry["convergence"][0]["rmse"], entry
        result[case] = entry
    off = probe(out, "off", 255)
    stage2 = out / "off-stage2" / "region" / "region_255.bin"
    if stage2.exists():
        baseline = probe(out, "off-stage2", 255)
        result["off_vs_stage2_scene_max_hdr_delta"] = float(np.abs(off-baseline).max())
        assert result["off_vs_stage2_scene_max_hdr_delta"] == 0, result
    report = json.dumps(result, indent=2)
    (out / "metrics.json").write_text(report + "\n", encoding="utf-8")
    print(report)


def write_project(out, name, scene, width, height, max_bounces):
    """A temporary project/scene pair next to the fixtures (content_root '.')."""
    tmp = HERE / "_volume-grid-tmp"
    tmp.mkdir(exist_ok=True)
    project = json.loads((HERE / "path-volume-grid.uzproject").read_text(encoding="utf-8"))
    project["default_scene"] = f"{name}.uzscene"
    project["window"] = {"width": width, "height": height}
    project["cvars"]["r.PathTracer.MaxBounces"] = max_bounces
    (tmp / f"{name}.uzscene").write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
    path = tmp / f"{name}.uzproject"
    path.write_text(json.dumps(project, indent=2) + "\n", encoding="utf-8")
    return path


# The sun sits at -Y, 40 degrees up (rotation [-40, -90, 0]: the light
# direction points toward the sun), so the -Y camera is front-lit.
CLOUD_VIEWS = {
    "front-lit": ([0., -820., 260.], [0., 0., 300.]),
    "back-lit": ([0., 820., 260.], [0., 0., 300.]),
    "side": ([880., 0., 220.], [0., 0., 300.]),
    "low-oblique": ([520., 520., 130.], [0., 0., 330.]),
}


def cloud_scene(view):
    scene = json.loads((HERE / "path-volume-grid-cloud.uzscene").read_text(encoding="utf-8"))
    position, target = CLOUD_VIEWS[view]
    for entity in scene["entities"]:
        if entity["name"] == "Camera":
            entity["components"]["transform"]["position"] = position
            entity["components"]["camera"]["look_at"] = target
    return scene


def montage(out, views, frame):
    tiles = [Image.open(out / "cloud" / view / "frames" / f"f_{frame}.png").convert("RGB") for view in views]
    w, h = tiles[0].size
    sheet = Image.new("RGB", (2*w, 2*h))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, ((i % 2)*w, (i//2)*h))
    sheet.save(out / "cloud" / "montage.png")
    print(f'montage: {out / "cloud" / "montage.png"} ({len(views)} views at {w}x{h}, frame {frame})')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", required=True, choices=[
        "slab", "ramp", "emission", "scatter", "off", "off-stage2", "cloud", "perf", "metrics"])
    parser.add_argument("--frames", type=int, default=4095)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.case == "metrics":
        metrics(out)
        return
    cvars = {"r.TAA": 1}
    extra = ["--hidden", "--path-trace", "--volume"]
    if args.case == "cloud":
        views = list(CLOUD_VIEWS)
        frame = args.frames
        for view in views:
            project = write_project(out, f"cloud-{view}", cloud_scene(view), 640, 360, 8)
            result = runner.run_capture(str(out / "cloud" / view), str(project), frame+8,
                clean=False, timeout=180, cvars=cvars, extra_args=extra, screenshot_frames=str(frame))
            assert result["exit_code"] == 0, result
            log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
            assert "compile failed" not in log and "[error]" not in log and "PathVolume: 1 grid" in log, result["log"]
            print(f'cloud/{view}: exit 0, clean shaders; {result["log"]}', flush=True)
        montage(out, views, frame)
        shutil.rmtree(HERE / "_volume-grid-tmp", ignore_errors=True)
        return
    if args.case == "perf":
        # 1080p, eight bounces: the manifest's GPU time with and without the medium.
        project = write_project(out, "cloud-perf", cloud_scene("front-lit"), 1920, 1080, 8)
        timings = {}
        for name, args_ in (("volume", extra), ("no-volume", [a for a in extra if a != "--volume"])):
            result = runner.run_capture(str(out / "perf" / name), str(project), 360,
                clean=False, timeout=180, cvars=cvars, extra_args=args_, timing_first=120)
            assert result["exit_code"] == 0, result
            timings[name] = {"gpu_ms": result["manifest"]["gpu_ms"], "frame_ms": result["manifest"]["frame_ms"]}
        (out / "perf" / "timings.json").write_text(json.dumps(timings, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(timings, indent=2))
        shutil.rmtree(HERE / "_volume-grid-tmp", ignore_errors=True)
        return
    frame, shots, first = args.frames, f"255-{args.frames}:256", 255
    scene = args.case
    project = HERE / "path-volume-grid.uzproject"
    if args.case == "off":
        frame, shots, first = 255, "255", 255
        extra.remove("--volume")
    elif args.case == "off-stage2":
        project = HERE / "path-volume-stage2.uzproject"
        frame, shots, first = 255, "255", 255
        extra.remove("--volume")
    else:
        project = write_project(out, scene, json.loads(
            (HERE / f"path-volume-grid-{scene}.uzscene").read_text(encoding="utf-8")), 320, 180, 1)
    result = runner.run_capture(str(out / args.case), str(project), frame+8,
        regions=REGIONS, region_fields="extra", region_first=first, region_last=frame,
        clean=False, timeout=180, cvars=cvars, extra_args=extra, screenshot_frames=shots)
    assert result["exit_code"] == 0, result
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log and "[error]" not in log, result["log"]
    if args.case not in ("off", "off-stage2"):
        assert "PathVolume: 1 grid" in log, result["log"]
    shutil.rmtree(HERE / "_volume-grid-tmp", ignore_errors=True)
    print(f'{args.case}: exit 0, clean shaders; {result["log"]}', flush=True)


if __name__ == "__main__":
    main()
