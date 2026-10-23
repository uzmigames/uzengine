"""Bounded disk/capsule reference captures; see README.md for commands and limits."""
from __future__ import annotations

import argparse
import copy
import json
import math
import os
from pathlib import Path
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from flicker import runner, regiondump  # noqa: E402

HEIGHT = 200.0
RADIANCE = 6.0
ALBEDO = 0.6
ROI = ("pool", 304, 164, 32, 32)
RESIDUAL = ("Lambert-only target. PT retains intrinsic dielectric Fresnel/specular "
            "and diffuse energy partition at roughness 1; raster specularScale=0 "
            "does not disable PT specular. PT error is not a pure diffuse LTC error.")


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def mesh_bytes(positions, normals, triangles):
    """UZMH v2, matching mesh.h and probe_side_reference.box_mesh_bytes."""
    p, n = np.asarray(positions, float), np.asarray(normals, float)
    indices = []
    for a, b, c in triangles:
        cross = np.cross(p[b] - p[a], p[c] - p[a])
        if np.linalg.norm(cross) < 1e-10:
            continue
        if np.dot(-cross, n[a] + n[b] + n[c]) < 0:
            b, c = c, b
        indices.extend((a, b, c))
    vertices = b"".join(struct.pack("<12f", *pos, *normal, 0, 0, 1, 0, 0, 1)
                        for pos, normal in zip(p, n))
    header = struct.pack(
        "<8I6f2QI6I28x", 0x484D5A55, 2, 0, 48, len(p), 1, len(indices), 1,
        *p.min(0), *p.max(0), 128, 128 + len(vertices), 1, 0, 0, 0, 0, 0, 0)
    return header + vertices + struct.pack(f"<{len(indices)}I", *indices)


def emitter_mesh(shape):
    phi = np.arange(256) * (2 * math.pi / 256)
    if shape == "disk":
        p = [(0, 0, HEIGHT)] + [(60 * math.cos(t), 60 * math.sin(t), HEIGHT) for t in phi]
        return mesh_bytes(p, [(0, 0, -1)] * len(p),
                          [(0, i + 1, (i + 1) % 256 + 1) for i in range(256)])
    # Capsule along world Y = local right at rotation [90,0,0].
    # Two equators joined by a straight cylinder, with true hemispherical ends.
    profiles = [(60 + 15 * math.cos(t), 15 * math.sin(t), math.cos(t), math.sin(t))
                for t in np.linspace(0, math.pi / 2, 49)]
    profiles += [(-60 + 15 * math.cos(t), 15 * math.sin(t), math.cos(t), math.sin(t))
                 for t in np.linspace(math.pi / 2, math.pi, 49)]
    p, n, triangles = [], [], []
    for y, radius, ny, nr in profiles:
        for t in phi:
            p.append((radius * math.cos(t), y, HEIGHT + radius * math.sin(t)))
            n.append((nr * math.cos(t), ny, nr * math.sin(t)))
    for j in range(len(profiles) - 1):
        for i in range(256):
            a, b = j * 256 + i, j * 256 + (i + 1) % 256
            triangles.extend(((a, b, a + 256), (b, b + 256, a + 256)))
    return mesh_bytes(p, n, triangles)


def prepare(root):
    """Expand the checked-in reference specs without touching repository assets."""
    fixtures = root / "fixtures"
    (fixtures / "meshes").mkdir(parents=True, exist_ok=True)
    for shape in ("disk", "tube"):
        (fixtures / "meshes" / f"{shape}.uzmesh").write_bytes(emitter_mesh(shape))
        source = json.loads((HERE / f"{shape}.uzscene").read_text(encoding="utf-8"))
        project = json.loads((HERE / f"{shape}.uzproject").read_text(encoding="utf-8"))
        for mode in ("raster", "pt"):
            scene = copy.deepcopy(source)
            omit = "PT emitter" if mode == "raster" else "Analytic emitter"
            scene["entities"] = [e for e in scene["entities"] if e["name"] != omit]
            for e in scene["entities"]:
                mesh = e["components"].get("mesh")
                if mesh and mesh["asset"].startswith("sample/"):
                    mesh["asset"] = Path(os.path.relpath(REPO / mesh["asset"], fixtures)).as_posix()
            name = f"{shape}-{mode}"
            save_json(fixtures / f"{name}.uzscene", scene)
            expanded = {**project, "content_root": ".",
                        "default_scene": f"{name}.uzscene"}
            save_json(fixtures / f"{name}.uzproject", expanded)
    print(f"Prepared {fixtures}")


def quadrature(shape, order=32):
    """Exact smooth-surface samples and dA, independent of tessellated PT geometry."""
    nodes, weights = np.polynomial.legendre.leggauss(order)
    phi = (np.arange(128) + .5) * (2 * math.pi / 128)
    c, s = np.cos(phi), np.sin(phi)
    positions, normals, areas = [], [], []
    if shape == "disk":
        # u = r^2 / R^2 makes dA = R^2/2 du dphi.
        for u, w in zip((nodes + 1) / 2, weights / 2):
            radius = 60 * math.sqrt(u)
            positions.extend(np.stack((radius * c, radius * s, np.full(128, HEIGHT)), 1))
            normals.extend([(0, 0, -1)] * 128)
            areas.extend([w * 60**2 / 2 * (2 * math.pi / 128)] * 128)
    else:
        for y, w in zip(nodes * 60, weights * 60):
            positions.extend(np.stack((15 * c, np.full(128, y), HEIGHT + 15 * s), 1))
            normals.extend(np.stack((c, np.zeros(128), s), 1))
            areas.extend([w * 15 * (2 * math.pi / 128)] * 128)
        for side in (-1, 1):
            # mu = cos(theta), dA = r^2 dmu dphi on a hemisphere.
            for mu, w in zip((nodes + 1) / 2, weights / 2):
                radial = math.sqrt(1 - mu * mu)
                normal = np.stack((radial * c, np.full(128, side * mu), radial * s), 1)
                positions.extend(normal * 15 + (0, side * 60, HEIGHT))
                normals.extend(normal)
                areas.extend([w * 15**2 * (2 * math.pi / 128)] * 128)
    return np.asarray(positions), np.asarray(normals), np.asarray(areas)


def lambert(points, shape, order=32):
    samples, normals, areas = quadrature(shape, order)
    out = []
    for point in np.asarray(points).reshape(-1, 3):
        delta = samples - point
        d2 = np.sum(delta * delta, axis=1)
        direction = delta / np.sqrt(d2[:, None])
        cos_receiver = np.maximum(direction[:, 2], 0)
        cos_emitter = np.maximum(-np.sum(normals * direction, axis=1), 0)
        out.append(ALBEDO * RADIANCE / math.pi *
                   np.sum(cos_receiver * cos_emitter / d2 * areas))
    return np.asarray(out).reshape(np.asarray(points).shape[:-1])


def capture(args, perf=False):
    runner.EXE = str(Path(args.exe).resolve())
    runner.require_exe()
    old_cwd = Path.cwd()
    os.chdir(args.cwd)
    try:
        baseline = args.command == "baseline"
        name = "baseline" if baseline else f"{args.shape}-{args.mode}"
        if perf:
            name += "-perf"
            if args.lights_disabled:
                name += "-off"
        elif not baseline and args.frame != 95:
            name += f"-f{args.frame}"
        dest = args.out / name
        if dest.exists():
            raise SystemExit(f"Refusing to overwrite {dest}; choose a new --out")
        project = args.project if baseline else (
            args.out / "fixtures" / f"{args.shape}-{args.mode}.uzproject")
        if not Path(project).is_file():
            raise SystemExit(f"Missing project: {project}; run prepare first")
        pt = not baseline and args.mode == "pt"
        result = runner.run_capture(
            str(dest), str(project), 128 if perf else args.frame + 5,
            regions=None if baseline or perf else [ROI], region_fields="output",
            region_first=args.frame, region_last=args.frame,
            screenshot_frames=None if perf else str(args.frame),
            # The output HDR diagnostic is written by the temporal resolve, even
            # for PT input. With TAA off it deliberately reports a missing stamp.
            cvars={} if baseline else {"r.TAA": 1, **({"r.Lights.AreaLTC": 0} if args.lights_disabled else {})},
            extra_args=["--hidden"] + (["--path-trace", "--pt-bounces", "2"] if pt else []),
            timing_first=32 if perf else 80, clean=False, timeout=args.timeout)
        if result["exit_code"] != 0:
            raise SystemExit(f"Capture exited {result['exit_code']}: {result['log']}")
        if not result["manifest"]:
            raise SystemExit(f"Missing manifest: {result['manifest_path']}")
        if not baseline and not perf:
            # Reject missing/stale GPU stamps here, not after several more captures.
            regiondump.load_sequence(str(dest / "region"), args.frame, args.frame)
        if perf:
            gpu = result["manifest"].get("gpu_ms")
            if not gpu:
                raise SystemExit("Manifest has no gpu_ms; this executable cannot supply GPU perf.")
            save_json(dest / "perf.json", {
                "identity": runner.capture_identity(str(dest)), "gpu_ms": gpu,
                "frame_ms": result["manifest"].get("frame_ms"),
                "note": "Single bounded run, timing starts at frame 32; see manifest for sample count. "
                        "Not a repeated-seed performance gate."})
        print(dest)
    finally:
        os.chdir(old_cwd)


def floor_points(frame):
    """Intersect recorded unjittered camera rays with z=0; works for PT output-only dumps."""
    if frame.view_proj is None:
        raise ValueError("Camera block missing; use dump format >=3")
    rect = frame.rect("pool")
    xs = (np.arange(rect.w) + rect.x + .5) / frame.output_width * 2 - 1
    ys = 1 - (np.arange(rect.h) + rect.y + .5) / frame.output_height * 2
    x, y = np.meshgrid(xs, ys)
    clip = np.stack((x, y, np.full_like(x, .02), np.ones_like(x)), -1)
    world = clip @ np.linalg.inv(frame.view_proj.astype(float))
    world = world[..., :3] / world[..., 3:]
    ray = world - frame.camera_pos
    t = -frame.camera_pos[2] / ray[..., 2]
    if not np.isfinite(t).all() or np.any(t <= 0):
        raise ValueError("ROI rays do not intersect the floor in front of the camera")
    return frame.camera_pos + t[..., None] * ray


def analyze(args):
    report = {"reference": "rho * Le / pi * integral cosReceiver*cosEmitter/distance^2 dA",
              "pt_residual": RESIDUAL, "runs": {}, "ltc_vs_pt": {}}
    samples = {}
    known = ALBEDO * RADIANCE * 60**2 / (HEIGHT**2 + 60**2)
    numerical = float(lambert(np.array([[0, 0, 0]]), "disk")[0])
    report["disk_on_axis"] = {"F": 60**2 / (HEIGHT**2 + 60**2),
                              "known_Lo": known, "quadrature_Lo": numerical}
    failed = False
    for shape in ("disk", "tube"):
        for mode in ("raster", "pt"):
            name = f"{shape}-{mode}"
            if args.frame != 95:
                name += f"-f{args.frame}"
            work = args.out / name
            frames = regiondump.load_sequence(str(work / "region"), args.frame, args.frame)
            frame = frames[0]
            if frame.output_width != 640 or frame.output_height != 360:
                raise ValueError(f"{name}: expected output 640x360")
            rect = frame.rect("pool")
            points = floor_points(frame)
            target = lambert(points, shape)
            refined = lambert(points, shape, order=48)
            measured = rect.channel("output")[..., :3].astype(float)
            samples[(shape, mode)] = measured
            if not np.isfinite(measured).all():
                raise ValueError(f"{name}: nonfinite output")
            error = measured - target[..., None]
            rmse = float(np.sqrt(np.mean(error**2)))
            relative = rmse / float(target.mean())
            passed = relative <= args.max_relative_rmse
            failed |= not passed
            report["runs"][name] = {
                "identity": runner.capture_identity(str(work)), "pixels": rect.pixel_count,
                "mean_rgb": measured.mean(axis=(0, 1)).tolist(),
                "target_mean": float(target.mean()), "rmse": rmse,
                "relative_rmse": relative, "mean_bias": float(error.mean()),
                "quadrature_refinement_max_delta": float(np.max(np.abs(refined - target))),
                "threshold": args.max_relative_rmse, "within_threshold": passed,
                "interpretation": RESIDUAL if mode == "pt" else "Diffuse analytic-light error"}
        raster, pt = samples[(shape, "raster")], samples[(shape, "pt")]
        error = raster - pt
        report["ltc_vs_pt"][shape] = {
            "rmse": float(np.sqrt(np.mean(error**2))),
            "relative_rmse": float(np.sqrt(np.mean(error**2)) / target.mean()),
            "mean_bias": float(error.mean()),
            "mean_ratio": float(raster.mean() / pt.mean())}
    save_json(args.out / ("analysis.json" if args.frame == 95 else f"analysis-f{args.frame}.json"), report)
    print(json.dumps(report, indent=2))
    if failed:
        raise SystemExit(1)


def montage(root, frame):
    canvas = Image.new("RGB", (1280, 768), (24, 24, 24))
    draw = ImageDraw.Draw(canvas)
    for row, shape in enumerate(("disk", "tube")):
        for col, mode in enumerate(("raster", "pt")):
            name = f"{shape}-{mode}"
            if frame != 95:
                name += f"-f{frame}"
            path = root / name / "frames" / f"f_{frame}.png"
            with Image.open(path) as im:
                if im.size != (640, 360):
                    raise ValueError(f"{path}: expected 640x360")
                canvas.paste(im.convert("RGB"), (col * 640, row * 384 + 24))
            draw.text((col * 640 + 8, row * 384 + 5), name + f" frame {frame}", fill="white")
    path = root / ("montage.png" if frame == 95 else f"montage-f{frame}.png")
    canvas.save(path)
    print(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "baseline", "capture", "analyze", "montage", "perf"))
    parser.add_argument("--out", type=Path, default=Path("E:/UzmiEngine/area-ltc-shapes"))
    parser.add_argument("--exe", default=runner.EXE)
    parser.add_argument("--cwd", type=Path, default=REPO)
    parser.add_argument("--project", type=Path,
                        default=REPO / "scripts/rendering/reflection-reference.uzproject")
    parser.add_argument("--shape", choices=("disk", "tube"), default="disk")
    parser.add_argument("--mode", choices=("raster", "pt"), default="raster")
    parser.add_argument("--timeout", type=float, default=170)
    parser.add_argument("--frame", type=int, default=95)
    parser.add_argument("--lights-disabled", action="store_true",
                        help="Matched zero-area-light control for per-light GPU cost")
    parser.add_argument("--max-relative-rmse", type=float, default=.1)
    args = parser.parse_args()
    args.out, args.cwd, args.project = args.out.resolve(), args.cwd.resolve(), args.project.resolve()
    if not 0 < args.timeout <= 180:
        parser.error("--timeout must be greater than 0 and at most 180 seconds")
    if args.frame < 1:
        parser.error("--frame must be positive")
    if not 0 < args.max_relative_rmse < 1:
        parser.error("--max-relative-rmse must be between 0 and 1")
    if args.command == "prepare":
        prepare(args.out)
    elif args.command in ("capture", "baseline", "perf"):
        capture(args, perf=args.command == "perf")
    elif args.command == "analyze":
        analyze(args)
    else:
        montage(args.out, args.frame)


if __name__ == "__main__":
    main()
