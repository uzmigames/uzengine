"""phase3a 3.33.0 items 2.3 and 2.4: area light vs point light, and a large area light's soft shadows.

prepare --out DIR
capture --out DIR --scene compare --camera hero|low|side|glossy --mode point|rect-unshadowed|rect|pt [--frame N]
capture --out DIR --scene large   --camera oblique|grazing|behind --mode lambert|off|on|pt [--frame N]
analyze --out DIR
montage --out DIR

Every engine run is bounded (default 180 s), renders hidden at 640x360 and dumps the whole frame's
linear-HDR `output` record at the measured frame (raster frame 95, path tracer frame 1535 unless
--frame is given). Captures refuse to overwrite. No repository asset is generated: `prepare` expands
the two checked-in source scenes into fixtures under --out. See README.md.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import os
from pathlib import Path
import shutil
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from flicker import runner, regiondump  # noqa: E402

WIDTH, HEIGHT = 640, 360
RASTER_FOV = 70.0
# On 2026-10-23 the --path-trace camera spanned 2x tan(fov/2) of the raster one and
# area-light-shadows/verify.py halved the PT fov; at 92a9c95b the PT camera matches the raster camera
# at the same fov (checked here by the PNG silhouette delta of the box/wall, see analysis.json), so
# both fixtures use the same fov and every pixel mask comes from a raster run's recorded camera.
PT_FOV = RASTER_FOV
PI = math.pi

CAMERAS = {
    "compare": {"hero": ([0, -650, 160], [0, -100, 30]),
                "low": ([0, -620, 70], [0, -50, 20]),
                "side": ([520, -380, 230], [0, 0, 30]),
                "glossy": ([0, -500, 120], [0, -100, 20])},
    "large": {"oblique": ([0, -300, 130], [0, 40, 0]),
              "grazing": ([-360, -60, 35], [0, 40, 10]),
              "behind": ([0, 520, 130], [0, 150, 0])},
}
# `glossy` is the hero scene with a roughness-0.12 floor: the rectangle's reflection keeps its shape.
FLOOR_ROUGHNESS = {"glossy": 0.12}
MODES = {"compare": ("point", "rect-unshadowed", "rect", "pt"), "large": ("lambert", "off", "on", "pt")}
RASTER_REFERENCE = {"compare": "rect", "large": "on"}      # the mode whose camera block defines masks
UNSHADOWED = {"rect-unshadowed", "off", "lambert"}          # r.Lighting.AreaLightShadows=0
MONTAGE_MODES = {"compare": ("point", "rect-unshadowed", "rect", "pt"), "large": ("off", "on", "pt")}
# Scene geometry (cm) repeated from the source scenes: cube.uzmesh is [-1,1]x[-1,1]x[0,2].
RECT = {"compare": {"center": (0, 0, 200), "half": (40, 60), "radiance": 6.0},    # x: height/2, y: width/2
        "large": {"center": (0, 0, 200), "half": (120, 180), "radiance": 2.5}}
BOXES = {"compare": {}, "large": {"occluder": ((-25, -25, 0), (25, 25, 60)),
                                  "wall": ((-90, 187, 0), (90, 193, 110))}}
SPHERES = {"compare": {"chrome": ((-260, 80, 40), 40), "brushed": ((260, 80, 40), 40),
                       "diffuse": ((0, 260, 40), 40)}, "large": {}}
QUAD_THICKNESS = 0.2
ALBEDO = 0.6


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def rect_view_factor(half_x, half_y, height):
    """Form factor from a point on the axis of a parallel rectangle, 2*half_x by 2*half_y, at `height`."""
    a, b = half_x / height, half_y / height
    ra, rb = math.sqrt(1 + a * a), math.sqrt(1 + b * b)
    return 4 * (a / ra * math.atan(b / ra) + b / rb * math.atan(a / rb)) / (2 * PI)


def matched_point_intensity():
    """Point light (color * intensity / d^2) with the rectangle's on-axis floor irradiance pi*L*F."""
    r = RECT["compare"]
    f = rect_view_factor(r["half"][0], r["half"][1], r["center"][2])
    return PI * r["radiance"] * f * r["center"][2] ** 2, f


def prepare(root):
    fixtures = root / "fixtures"
    (fixtures / "meshes").mkdir(parents=True, exist_ok=True)
    # The loose-file VFS refuses paths outside the project's content root, so the two sample meshes
    # are copied next to the fixtures instead of referenced through `..`.
    for mesh in ("cube.uzmesh", "sphere.uzmesh"):
        shutil.copyfile(REPO / "sample" / "content" / "meshes" / mesh, fixtures / "meshes" / mesh)
    project = json.loads((HERE.parent / "area-light-shapes" / "disk.uzproject").read_text(encoding="utf-8"))
    project["cvars"]["r.RayTracing.Reflections"] = 0
    intensity, form_factor = matched_point_intensity()
    for scene_name, cameras in CAMERAS.items():
        base = json.loads((HERE / f"{scene_name}.uzscene").read_text(encoding="utf-8"))
        for camera, (position, look_at) in cameras.items():
            for mode in MODES[scene_name]:
                scene = copy.deepcopy(base)
                drop = {"pt": {"Area light", "Point light"},
                        "point": {"Area light", "PT emitter"}}.get(mode, {"Point light", "PT emitter"})
                scene["entities"] = [e for e in scene["entities"] if e["name"] not in drop]
                for entity in scene["entities"]:
                    c = entity["components"]
                    if entity["name"] == "Camera":
                        c["transform"]["position"] = list(position)
                        c["camera"]["look_at"] = list(look_at)
                        c["camera"]["fov"] = PT_FOV if mode == "pt" else RASTER_FOV
                    if entity["name"] == "Point light":
                        c["light"]["intensity"] = round(intensity, 1)
                    if entity["name"] == "Area light" and mode == "lambert":
                        c["light"]["specularScale"] = 0      # diffuse LTC only, against analytic Lambert
                    if entity["name"] == "Glossy floor" and camera in FLOOR_ROUGHNESS:
                        c["material"]["roughnessMultiplier"] = FLOOR_ROUGHNESS[camera]
                    mesh = c.get("mesh")
                    if mesh and mesh["asset"].startswith("sample/"):
                        mesh["asset"] = "meshes/" + Path(mesh["asset"]).name
                name = f"{scene_name}-{camera}-{mode}"
                save_json(fixtures / f"{name}.uzscene", scene)
                save_json(fixtures / f"{name}.uzproject",
                          {**project, "name": name, "content_root": ".", "default_scene": f"{name}.uzscene"})
    save_json(fixtures / "point-light-match.json", {
        "rule": "point intensity = pi * L * F * h^2: the same floor irradiance directly under the light",
        "rect_half_extents_cm": RECT["compare"]["half"], "height_cm": RECT["compare"]["center"][2],
        "radiance": RECT["compare"]["radiance"], "view_factor": form_factor,
        "irradiance_under_light": PI * RECT["compare"]["radiance"] * form_factor,
        "point_intensity": intensity, "pt_fov": PT_FOV})
    print(fixtures)


def default_frame(mode):
    return 1535 if mode == "pt" else 95


def capture(args):
    runner.EXE = str(args.exe.resolve())
    if args.camera not in CAMERAS[args.scene] or args.mode not in MODES[args.scene]:
        raise SystemExit(f"{args.scene}: cameras {list(CAMERAS[args.scene])}, modes {MODES[args.scene]}")
    frame = args.frame or default_frame(args.mode)
    name = f"{args.scene}-{args.camera}-{args.mode}"
    dest = args.out / name
    if dest.exists():
        raise SystemExit(f"Refusing overwrite: {dest}")
    project = args.out / "fixtures" / f"{name}.uzproject"
    if not project.is_file():
        raise SystemExit(f"Missing fixture {project}; run prepare first")
    old = Path.cwd()
    os.chdir(args.cwd)
    try:
        result = runner.run_capture(
            str(dest), str(project), frame + 5,
            regions=[("frame", 0, 0, WIDTH, HEIGHT)], region_fields="output",
            region_first=frame, region_last=frame, screenshot_frames=str(frame),
            timing_first=32, timeout=args.timeout, clean=False,
            cvars={"r.Lighting.AreaLightShadows": 0} if args.mode in UNSHADOWED else {},
            extra_args=["--hidden"] + (["--path-trace", "--pt-bounces", "2"] if args.mode == "pt" else []))
        if result["exit_code"] != 0:
            raise SystemExit(f"Capture failed: {result['exit_code']} {result['log']}")
        regiondump.load_sequence(str(dest / "region"), frame, frame)   # rejects missing/stale stamps
        save_json(dest / "capture.json", {"scene": args.scene, "camera": args.camera, "mode": args.mode,
                                          "frame": frame, "frames_rendered": frame + 5})
        print(dest)
    finally:
        os.chdir(old)


# ── analysis ──────────────────────────────────────────────────────────────────────────────────────

def load_run(root, scene, camera, mode):
    dest = root / f"{scene}-{camera}-{mode}"
    info = json.loads((dest / "capture.json").read_text(encoding="utf-8"))
    frame = regiondump.load_sequence(str(dest / "region"), info["frame"], info["frame"])[0]
    if frame.output_width != WIDTH or frame.output_height != HEIGHT:
        raise ValueError(f"{dest}: expected {WIDTH}x{HEIGHT}")
    hdr = frame.rect("frame").channel("output")[..., :3].astype(np.float64)
    if not np.isfinite(hdr).all():
        raise ValueError(f"{dest}: non-finite HDR pixels")
    png = np.asarray(Image.open(dest / "frames" / f"f_{info['frame']}.png").convert("RGB"))
    return {"dir": dest, "frame": frame, "hdr": hdr, "lum": hdr.mean(-1), "png": png, "info": info,
            "identity": runner.capture_identity(str(dest))}


def pixel_rays(frame):
    """Unit ray direction of every output pixel from the recorded unjittered camera (two depths)."""
    xs = (np.arange(WIDTH) + 0.5) / WIDTH * 2.0 - 1.0
    ys = 1.0 - (np.arange(HEIGHT) + 0.5) / HEIGHT * 2.0
    gx, gy = np.meshgrid(xs, ys)
    inv = np.linalg.inv(frame.view_proj.astype(np.float64))

    def unproject(z):
        clip = np.stack([gx, gy, np.full_like(gx, z), np.ones_like(gx)], -1).reshape(-1, 4)
        w = clip @ inv
        return (w[:, :3] / w[:, 3:4]).reshape(HEIGHT, WIDTH, 3)

    cam = frame.camera_pos.astype(np.float64)
    a, b = unproject(0.9), unproject(0.02)
    dirs = b - a
    dirs /= np.maximum(np.linalg.norm(dirs, axis=-1, keepdims=True), 1e-12)
    farther = np.where(np.linalg.norm(a - cam, axis=-1, keepdims=True)
                       > np.linalg.norm(b - cam, axis=-1, keepdims=True), a, b)
    ahead = np.einsum("...i,...i->...", dirs, farther - cam)[..., None]
    return cam, np.where(ahead < 0.0, -dirs, dirs)


def ray_box(cam, dirs, lo, hi):
    """Entry distance of each ray into the box (inf when missed) and the entry point."""
    with np.errstate(divide="ignore", invalid="ignore"):
        t0 = (np.asarray(lo, float) - cam) / dirs
        t1 = (np.asarray(hi, float) - cam) / dirs
    tmin = np.minimum(t0, t1).max(-1)
    tmax = np.maximum(t0, t1).min(-1)
    hit = (tmax >= np.maximum(tmin, 0.0)) & (tmax > 0.0)
    t = np.where(hit, np.where(tmin > 0.0, tmin, tmax), np.inf)
    return t, cam + dirs * t[..., None]


def ray_sphere(cam, dirs, center, radius):
    oc = cam - np.asarray(center, float)
    b = np.einsum("...i,i->...", dirs, oc)
    c = np.dot(oc, oc) - radius * radius
    disc = b * b - c
    t = np.where(disc >= 0.0, -b - np.sqrt(np.maximum(disc, 0.0)), np.inf)
    return np.where(t > 0.0, t, np.inf)


def scene_geometry(scene, frame):
    """Per-pixel floor hit (NaN where a blocker or the sky comes first) and the blockers' first hits."""
    cam, dirs = pixel_rays(frame)
    with np.errstate(divide="ignore", invalid="ignore"):
        t_floor = np.where(dirs[..., 2] < 0.0, -cam[2] / dirs[..., 2], np.inf)
    nearest = np.full(t_floor.shape, np.inf)
    hits = {}
    for name, (lo, hi) in BOXES[scene].items():
        t, point = ray_box(cam, dirs, lo, hi)
        hits[name] = (t, point)
        nearest = np.minimum(nearest, t)
    for name, (center, radius) in SPHERES[scene].items():
        t = ray_sphere(cam, dirs, center, radius)
        hits[name] = (t, None)
        nearest = np.minimum(nearest, t)
    r = RECT[scene]
    lo = (r["center"][0] - r["half"][0], r["center"][1] - r["half"][1], r["center"][2])
    hi = (r["center"][0] + r["half"][0], r["center"][1] + r["half"][1], r["center"][2] + QUAD_THICKNESS)
    t, point = ray_box(cam, dirs, lo, hi)          # the PT emitter geometry, absent from raster
    hits["emitter"] = (t, point)
    nearest = np.minimum(nearest, t)
    floor = np.isfinite(t_floor) & (t_floor < nearest)
    point = cam + dirs * np.where(floor, t_floor, np.nan)[..., None]
    return {"cam": cam, "dirs": dirs, "floor": floor, "floor_point": point, "hits": hits,
            "t_floor": t_floor, "nearest": nearest}


def mirror_mask(geo, scene, scale):
    """Floor pixels whose mirror ray (the camera reflected through z = 0) hits the rectangle, scaled."""
    r = RECT[scene]
    cam = geo["cam"]
    mirrored = np.array([cam[0], cam[1], -cam[2]])
    p = geo["floor_point"]
    d = p - mirrored
    with np.errstate(divide="ignore", invalid="ignore"):
        t = (r["center"][2] - mirrored[2]) / d[..., 2]
    hit = mirrored + d * t[..., None]
    inside = ((np.abs(hit[..., 0] - r["center"][0]) <= r["half"][0] * scale)
              & (np.abs(hit[..., 1] - r["center"][1]) <= r["half"][1] * scale))
    return geo["floor"] & np.nan_to_num(inside, nan=False) & (t > 0)


def floor_box_mask(geo, x0, x1, y0, y1):
    p = geo["floor_point"]
    with np.errstate(invalid="ignore"):
        inside = (p[..., 0] >= x0) & (p[..., 0] <= x1) & (p[..., 1] >= y0) & (p[..., 1] <= y1)
    return geo["floor"] & np.nan_to_num(inside, nan=False)


def rect_form_factor(points, scene):
    """Exact Lambert form factor of the rectangle from floor points (normal +z), no horizon clipping
    needed because the rectangle is above every floor point. @ref Lambert / Arvo contour integral."""
    r = RECT[scene]
    c = np.asarray(r["center"], float)
    hx, hy = r["half"]
    corners = [c + (-hx, -hy, 0), c + (hx, -hy, 0), c + (hx, hy, 0), c + (-hx, hy, 0)]
    p = np.asarray(points, float)
    v = [(k - p) / np.linalg.norm(k - p, axis=-1, keepdims=True) for k in corners]
    total = np.zeros(p.shape[:-1])
    for i in range(4):
        a, b = v[i], v[(i + 1) % 4]
        cross = np.cross(a, b)
        norm = np.linalg.norm(cross, axis=-1)
        theta = np.arccos(np.clip(np.einsum("...i,...i->...", a, b), -1.0, 1.0))
        total += theta * cross[..., 2] / np.maximum(norm, 1e-12)
    return np.abs(total) / (2.0 * PI)


def erode(mask, radius=2):
    """Binary erosion by a (2r+1)^2 window, to keep silhouette pixels out of a face mask."""
    out = mask.copy()
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            out &= np.roll(np.roll(mask, dy, axis=0), dx, axis=1)
    out[:radius] = out[-radius:] = False
    out[:, :radius] = out[:, -radius:] = False
    return out


def wall_back_mask(geo):
    """Pixels whose first hit is the wall's +y face (behind the one-sided light's plane), eroded by
    2 px so jitter-blended silhouette pixels of the lit floor / top face do not count as leaks."""
    t, point = geo["hits"]["wall"]
    first = np.isfinite(t) & (t <= geo["nearest"]) & (t < geo["t_floor"])
    return erode(first & (np.abs(point[..., 1] - BOXES["large"]["wall"][1][1]) < 1e-3))


def rois(scene, geo):
    if scene == "compare":
        return {"floor": geo["floor"],
                "pool": floor_box_mask(geo, -100, 100, -100, 100),
                "mirror": mirror_mask(geo, scene, 1.0),
                "mirror2x": mirror_mask(geo, scene, 2.0)}
    # The 50 cm box's shadow under the 360x240 light reaches |x| <= 87, y in [-113, 113] (edge rays).
    shadow = floor_box_mask(geo, -100, 100, -125, 125)
    p = geo["floor_point"]
    with np.errstate(invalid="ignore"):
        unoccluded = geo["floor"] & ~shadow & np.nan_to_num(p[..., 1] < 180.0, nan=False)
    return {"floor": geo["floor"],
            "unoccluded": unoccluded,
            "shadow": shadow,
            "contact": floor_box_mask(geo, -20, 20, -45, -25),
            "lit": floor_box_mask(geo, 150, 280, -100, 100),
            "wall_shadow": floor_box_mask(geo, -80, 80, 200, 420),
            "wall_back": wall_back_mask(geo)}


def stats(mask, runs, reference):
    out = {"pixels": int(mask.sum())}
    if out["pixels"] == 0:
        return out
    ref = runs[reference]["lum"][mask] if reference in runs else None
    # Visibility: the raster's shadow term is on/off (same shading); the PT's is pt/lambert, since the
    # unshadowed PT agrees with the LTC-diffuse `lambert` run to ~0.1 % on open floor.
    if "on" in runs and "off" in runs:
        out["visibility_on"] = float(runs["on"]["lum"][mask].mean() / max(runs["off"]["lum"][mask].mean(), 1e-9))
    if "pt" in runs and "lambert" in runs:
        out["visibility_pt"] = float(runs["pt"]["lum"][mask].mean() / max(runs["lambert"]["lum"][mask].mean(), 1e-9))
    for mode, run in runs.items():
        v = run["lum"][mask]
        entry = {"mean": float(v.mean()), "max": float(v.max()), "p99": float(np.percentile(v, 99))}
        if ref is not None and mode != reference:
            err = v - ref
            entry["rmse_vs_pt"] = float(np.sqrt(np.mean(err * err)))
            entry["rel_rmse_vs_pt"] = float(entry["rmse_vs_pt"] / max(ref.mean(), 1e-9))
            entry["mean_ratio_vs_pt"] = float(v.mean() / max(ref.mean(), 1e-9))
        out[mode] = entry
    return out


def profile(geo, runs, frame, points):
    """HDR luminance of each run at the pixels the given world points project to (NaN if not floor)."""
    px = frame.project(np.asarray(points, float))
    ix = np.rint(px[..., 0]).astype(int)
    iy = np.rint(px[..., 1]).astype(int)
    ok = (ix >= 0) & (ix < WIDTH) & (iy >= 0) & (iy < HEIGHT)
    ok &= geo["floor"][np.clip(iy, 0, HEIGHT - 1), np.clip(ix, 0, WIDTH - 1)]
    out = {}
    for mode, run in runs.items():
        v = np.full(len(points), np.nan)
        v[ok] = run["lum"][iy[ok], ix[ok]]
        out[mode] = v
    return out, ok


def transition_widths(xs, v):
    """Shadow extent at 90 % visibility and the 10-90 % transition widths on each side, in cm."""
    out = {"min": float(np.nanmin(v)), "argmin_cm": float(xs[np.nanargmin(v)])}
    below = np.where(np.nan_to_num(v, nan=1.0) < 0.9)[0]
    if len(below) == 0:
        return {**out, "width_at_0.9": 0.0}
    out["width_at_0.9"] = float(xs[below[-1]] - xs[below[0]])
    lo = out["min"] + 0.1 * (1.0 - out["min"])            # 10 % of the way from the minimum to open
    hi = 1.0 - 0.1 * (1.0 - out["min"])                   # 90 %
    i_min = int(np.nanargmin(v))
    left = v[:i_min + 1]
    right = v[i_min:]
    l_hi = np.where(np.nan_to_num(left, nan=1.0) >= hi)[0]
    l_lo = np.where(np.nan_to_num(left, nan=0.0) <= lo)[0]
    r_hi = np.where(np.nan_to_num(right, nan=1.0) >= hi)[0]
    r_lo = np.where(np.nan_to_num(right, nan=0.0) <= lo)[0]
    if len(l_hi) and len(l_lo):
        out["left_10_90_cm"] = float(xs[l_lo[0]] - xs[l_hi[-1]])
    if len(r_hi) and len(r_lo):
        out["right_10_90_cm"] = float(xs[i_min + r_hi[0]] - xs[i_min + r_lo[-1]])
    return out


def draw_profile(path, xs, series, title, ylabel):
    w, h, m = 900, 420, 50
    img = Image.new("RGB", (w, h), "#181818")
    d = ImageDraw.Draw(img)
    ymax = max(1.05, float(np.nanmax([np.nanmax(v) for v in series.values()])) * 1.05)
    colors = {"off": "#8899aa", "on": "#55ff77", "pt": "#ffffff", "point": "#ff8855",
              "rect-unshadowed": "#8899aa", "rect": "#55ff77"}

    def to_px(x, y):
        return (m + (x - xs[0]) / (xs[-1] - xs[0]) * (w - 2 * m), h - m - y / ymax * (h - 2 * m))

    for gy in np.linspace(0, ymax, 6):
        d.line([to_px(xs[0], gy), to_px(xs[-1], gy)], fill="#333333")
        d.text((4, to_px(xs[0], gy)[1] - 6), f"{gy:.2f}", fill="#aaaaaa")
    for gx in np.linspace(xs[0], xs[-1], 9):
        d.line([to_px(gx, 0), to_px(gx, ymax)], fill="#333333")
        d.text((to_px(gx, 0)[0] - 10, h - m + 6), f"{gx:.0f}", fill="#aaaaaa")
    for i, (name, v) in enumerate(series.items()):
        pts = [to_px(x, y) for x, y in zip(xs, v) if np.isfinite(y)]
        if len(pts) > 1:
            d.line(pts, fill=colors.get(name, "#ffff00"), width=2)
        d.text((w - m - 190, m + 14 * i), name, fill=colors.get(name, "#ffff00"))
    d.text((m, 8), title, fill="white")
    d.text((m, 24), ylabel, fill="#aaaaaa")
    img.save(path)


def analyze(args):
    report = {"width": WIDTH, "height": HEIGHT, "pt_fov": PT_FOV, "notes": [
        "HDR numbers come from the engine's linear `output` region dump of the measured frame "
        "(whole 640x360 frame), never from PNG codes. Luminance = mean of RGB.",
        "Pixel masks come from the raster run's recorded unjittered camera: rays are intersected with "
        "the floor (z = 0), the occluder/wall boxes, the spheres and the PT emitter quad; a floor "
        "pixel is one whose ray reaches z = 0 before any of those.",
        "PT = DXR path tracer, 2 bounces (direct lighting), 4 NEE samples, black environment, "
        "emissive quad of the same size and radiance; the PT's dielectric diffuse/specular energy "
        "partition differs from the raster's plain Lambert + LTC specular, so the residual on the "
        "glossy compare floor (NoV-dependent, documented in stage 1) is not pure LTC error.",
        "Unshadowed raster modes set r.Lighting.AreaLightShadows=0; `rect` and `on` are engine defaults.",
        "Point light: inverse-square, sourceRadius 0, no shadows, intensity pi*L*F*h^2 so the floor "
        "irradiance under the light equals the rectangle's (fixtures/point-light-match.json)."],
        "scenes": {}}
    for scene, cameras in CAMERAS.items():
        report["scenes"][scene] = {}
        for camera in cameras:
            runs = {mode: load_run(args.out, scene, camera, mode) for mode in MODES[scene]
                    if (args.out / f"{scene}-{camera}-{mode}" / "capture.json").is_file()}
            if not runs:
                continue
            ref_mode = RASTER_REFERENCE[scene]
            frame = runs[ref_mode]["frame"]
            geo = scene_geometry(scene, frame)
            masks = rois(scene, geo)
            focus = masks["mirror2x" if scene == "compare" else "shadow"]
            entry = {"runs": {m: {"frame": r["info"]["frame"], "identity": r["identity"],
                                  "mean_hdr": float(r["lum"].mean()), "max_hdr": float(r["hdr"].max())}
                              for m, r in runs.items()},
                     "rois": {name: stats(mask, runs, "pt") for name, mask in masks.items()},
                     "roi_pixels": {name: int(mask.sum()) for name, mask in masks.items()}}
            if focus.any():
                yy, xx = np.nonzero(focus)
                entry["inset_centre"] = [int(xx.mean()), int(yy.mean())]
            if scene == "large" and masks["unoccluded"].any():
                # Every unoccluded floor pixel against the exact Lambert integral of the rectangle.
                m = masks["unoccluded"]
                target = ALBEDO * RECT[scene]["radiance"] * rect_form_factor(geo["floor_point"][m], scene)
                entry["analytic_lambert_unoccluded_floor"] = {
                    "pixels": int(m.sum()), "target_mean": float(target.mean()),
                    "what": "rho * L * F(rectangle) per floor pixel; `lambert` = LTC diffuse only, "
                            "`off`/`on` add the LTC specular of the rough dielectric, PT = its own BRDF"}
                for mode, run in runs.items():
                    err = run["lum"][m] - target
                    entry["analytic_lambert_unoccluded_floor"][mode] = {
                        "mean_ratio": float(run["lum"][m].mean() / target.mean()),
                        "rel_rmse": float(np.sqrt(np.mean(err * err)) / target.mean()),
                        "max_abs_rel": float(np.max(np.abs(err)) / target.mean())}
            # Whole-image PNG delta between the two raster shadow modes and the PT (alignment check).
            if "pt" in runs:
                for m in runs:
                    if m != "pt":
                        delta = np.abs(runs[m]["png"].astype(int) - runs["pt"]["png"].astype(int))
                        entry["runs"][m]["png_mean_abs_delta_vs_pt"] = float(delta.mean())
            if scene == "large" and "pt" in runs and "on" in runs:
                # Raster/PT camera alignment: the box's silhouette columns on the row through its centre.
                row = int(round(frame.project(np.array([[0.0, 0.0, 30.0]]))[0][1]))
                if 0 <= row < HEIGHT:
                    edges = {}
                    for m in ("on", "pt"):
                        g = np.abs(np.diff(runs[m]["png"].mean(-1)[row]))
                        edges[m] = np.where(g > 25)[0].tolist()[:8]
                    entry["box_silhouette_columns"] = {"row": row, **edges}
            if scene == "large":
                # Two lines through the box's shadow: along x just in front of the box (y = -60) and
                # along y from the open floor up to the box's near face (x = 0).
                for key, xs, pts, what in (
                        ("profile_x_y-60", np.linspace(-200, 200, 161),
                         None, "along x at y = -60 (through the box shadow)"),
                        ("profile_y_x0", np.linspace(-200, -26, 88), None,
                         "along y at x = 0 (open floor -> the box's near face)")):
                    if key == "profile_x_y-60":
                        pts = np.stack([xs, np.full_like(xs, -60.0), np.zeros_like(xs)], -1)
                    else:
                        pts = np.stack([np.zeros_like(xs), xs, np.zeros_like(xs)], -1)
                    prof, ok = profile(geo, runs, frame, pts)
                    if ok.sum() <= 20 or "off" not in prof:
                        continue
                    vis = {"on": prof["on"] / np.maximum(prof["off"], 1e-6)} if "on" in prof else {}
                    if "pt" in prof:
                        base = prof["lambert"] if "lambert" in prof else prof["off"]
                        vis["pt"] = prof["pt"] / np.maximum(base, 1e-6)
                    entry[key] = {
                        "cm": xs.tolist(), "valid": ok.tolist(), "what": what,
                        "hdr": {m: np.nan_to_num(v, nan=-1).tolist() for m, v in prof.items()},
                        "visibility": {m: np.nan_to_num(v, nan=-1).tolist() for m, v in vis.items()},
                        "widths": {m: transition_widths(xs, v) for m, v in vis.items()}}
                    if "on" in vis and "pt" in vis:
                        both = np.isfinite(vis["on"]) & np.isfinite(vis["pt"])
                        entry[key]["visibility_rmse_on_vs_pt"] = float(
                            np.sqrt(np.mean((vis["on"][both] - vis["pt"][both]) ** 2)))
                        entry[key]["visibility_max_abs_on_vs_pt"] = float(
                            np.max(np.abs(vis["on"][both] - vis["pt"][both])))
                    draw_profile(args.out / f"{key}-{camera}-hdr.png", xs, prof,
                                 f"{scene} {camera}: floor HDR {what}", "linear HDR luminance")
                    draw_profile(args.out / f"{key}-{camera}-visibility.png", xs, vis,
                                 f"{scene} {camera}: visibility = mode / unshadowed {what}", "visibility")
                ys = np.linspace(195, 450, 103)
                pts = np.stack([np.zeros_like(ys), ys, np.zeros_like(ys)], -1)
                prof, ok = profile(geo, runs, frame, pts)
                if ok.sum() > 20 and "off" in prof:
                    vis = {"on": prof["on"] / np.maximum(prof["off"], 1e-6)} if "on" in prof else {}
                    if "pt" in prof:
                        base = prof["lambert"] if "lambert" in prof else prof["off"]
                        vis["pt"] = prof["pt"] / np.maximum(base, 1e-6)
                    entry["profile_y_behind_wall"] = {
                        "y_cm": ys.tolist(), "valid": ok.tolist(),
                        "visibility": {m: np.nan_to_num(v, nan=-1).tolist() for m, v in vis.items()}}
                    if "on" in vis and "pt" in vis:
                        both = np.isfinite(vis["on"]) & np.isfinite(vis["pt"])
                        entry["profile_y_behind_wall"]["visibility_rmse_on_vs_pt"] = float(
                            np.sqrt(np.mean((vis["on"][both] - vis["pt"][both]) ** 2)))
                    draw_profile(args.out / f"profile-{scene}-{camera}-y-visibility.png", ys, vis,
                                 f"{scene} {camera}: visibility behind the wall along y, x = 0",
                                 "visibility")
            if scene == "compare" and masks["mirror2x"].any():
                # Highlight shape: how much of the 2x mirror footprint is bright (> half its p99).
                for m, r in runs.items():
                    v = r["lum"][masks["mirror2x"]]
                    thresh = 0.5 * np.percentile(v, 99)
                    entry["rois"]["mirror2x"][m]["bright_fraction"] = float((v > thresh).mean())
            report["scenes"][scene][camera] = entry
            print(f"{scene}/{camera}: " + json.dumps({k: {m: round(v.get("mean", 0), 4) if isinstance(v, dict) else v
                                                        for m, v in s.items()}
                                                    for k, s in entry["rois"].items()}))
    save_json(args.out / "analysis.json", report)
    print(args.out / "analysis.json")


def display(image, gamma=2.2):
    """Gamma-encodes a linear 8-bit capture for viewing; the analysis never reads these."""
    lut = [int(round(255.0 * (i / 255.0) ** (1.0 / gamma))) for i in range(256)]
    return image.point(lut * 3)


def label(draw, xy, text):
    draw.rectangle([xy[0] - 2, xy[1] - 1, xy[0] + 6 * len(text) + 4, xy[1] + 11], fill="#000000")
    draw.text(xy, text, fill="white")


def montage(args):
    for scene, cameras in CAMERAS.items():
        modes = list(MONTAGE_MODES[scene])
        cols = len(modes) + 1
        canvas = Image.new("RGB", (WIDTH * cols, (HEIGHT + 24) * len(cameras)), "#181818")
        draw = ImageDraw.Draw(canvas)
        crops = []
        for row, camera in enumerate(cameras):
            runs = {}
            for mode in modes:
                dest = args.out / f"{scene}-{camera}-{mode}"
                if not (dest / "capture.json").is_file():
                    continue
                info = json.loads((dest / "capture.json").read_text(encoding="utf-8"))
                runs[mode] = (Image.open(dest / "frames" / f"f_{info['frame']}.png").convert("RGB"), info["frame"])
            for col, mode in enumerate(modes):
                if mode not in runs:
                    continue
                canvas.paste(runs[mode][0], (col * WIDTH, row * (HEIGHT + 24) + 24))
                label(draw, (col * WIDTH + 6, row * (HEIGHT + 24) + 6),
                      f"{scene} {camera} {mode} frame {runs[mode][1]}")
            ref = RASTER_REFERENCE[scene]
            if ref in runs and "pt" in runs:
                a = np.asarray(runs[ref][0], int)
                b = np.asarray(runs["pt"][0], int)
                diff = Image.fromarray(np.clip(np.abs(a - b) * 4, 0, 255).astype(np.uint8))
                canvas.paste(diff, (len(modes) * WIDTH, row * (HEIGHT + 24) + 24))
                label(draw, (len(modes) * WIDTH + 6, row * (HEIGHT + 24) + 6), f"|{ref} - pt| x4")
            crops.append((camera, runs))
        path = args.out / f"montage-{scene}.png"
        canvas.save(path)
        print(path)
        shown = display(canvas)
        ImageDraw.Draw(shown).text((WIDTH * cols - 300, 6), "display: gamma 2.2 of the linear codes", fill="white")
        shown.save(args.out / f"montage-{scene}-display.png")
        # 3x insets of the interesting region, one row per camera.
        inset = {"compare": (3, 3), "large": (3, 3)}[scene]
        cw, ch = WIDTH // inset[0], HEIGHT // inset[1]
        sheet = Image.new("RGB", (cw * 3 * len(modes), (ch * 3 + 24) * len(cameras)), "#181818")
        sd = ImageDraw.Draw(sheet)
        for row, (camera, runs) in enumerate(crops):
            # Centre the inset on the brightest/darkest feature: use the analysis ROI centroid if present.
            cx, cy = WIDTH // 2, HEIGHT // 2
            analysis = args.out / "analysis.json"
            if analysis.is_file():
                rep = json.loads(analysis.read_text(encoding="utf-8"))
                centre = rep.get("scenes", {}).get(scene, {}).get(camera, {}).get("inset_centre")
                if centre:
                    cx, cy = centre
            box = (max(0, min(WIDTH - cw, cx - cw // 2)), max(0, min(HEIGHT - ch, cy - ch // 2)))
            for col, mode in enumerate(modes):
                if mode not in runs:
                    continue
                crop = runs[mode][0].crop((box[0], box[1], box[0] + cw, box[1] + ch)).resize(
                    (cw * 3, ch * 3), Image.Resampling.NEAREST)
                sheet.paste(crop, (col * cw * 3, row * (ch * 3 + 24) + 24))
                label(sd, (col * cw * 3 + 6, row * (ch * 3 + 24) + 6), f"{camera} {mode} 3x inset at {box}")
        path = args.out / f"insets-{scene}.png"
        sheet.save(path)
        print(path)
        shown = display(sheet)
        ImageDraw.Draw(shown).text((sheet.width - 300, 6), "display: gamma 2.2 of the linear codes", fill="white")
        shown.save(args.out / f"insets-{scene}-display.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("prepare", "capture", "analyze", "montage"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--scene", choices=tuple(CAMERAS), default="compare")
    parser.add_argument("--camera", default="hero")
    parser.add_argument("--mode", default="rect")
    parser.add_argument("--frame", type=int, default=0, help="measured frame (default 95 raster, 383 PT)")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--exe", type=Path, default=Path(runner.EXE))
    parser.add_argument("--cwd", type=Path, default=REPO)
    args = parser.parse_args()
    args.out = args.out.resolve()
    if not 0 < args.timeout <= 180:
        parser.error("--timeout must be in (0, 180]")
    if args.command == "prepare":
        prepare(args.out)
    elif args.command == "capture":
        capture(args)
    elif args.command == "analyze":
        analyze(args)
    else:
        montage(args)


if __name__ == "__main__":
    main()
