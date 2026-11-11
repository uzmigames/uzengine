"""ReSTIR DI many-light validation: scene generation, bounded captures, and analysis.

Scenes (written on demand by `scenes`, deterministic): G x G grids of emissive cubes over a
0.65-albedo floor with a black sky. G in {4, 8, 16, 32} gives 16 .. 1,024 emitters (12 emissive
triangles each, so the light BVH holds 192 .. 12,288 lights). Cube size shrinks with G so the total
emitted power is the same for every grid (floor radiance stays comparable), and ten log-spaced
intensity levels (0.05 .. 14.7, 294x) are scattered so every row and column carries every level;
the floor under the grid then averages about 0.15 linear (40 codes) and never clips.
Four camera angles: `a` overview, `b` grazing, `c` top-down, `d` corner close-up. `-dolly` scenes
attach `dolly.lua`, a deterministic lateral camera dolly (moving-camera temporal reuse).

Run:  python scripts/rendering/measure_restir_many_lights.py scenes
      python scripts/rendering/measure_restir_many_lights.py capture OUTPUT MODE [--grid 32] [--angle a]
          [--res 360|1080] [--frames N] [--shots A-B[:S]] [--cvar k=v ...] [--dolly]
      python scripts/rendering/measure_restir_many_lights.py analyze OUTPUT [--grid 32] [--angle a]
MODE: restir (ReSTIR DI x light BVH), restir-nospatial, restir-notemporal, restir-initial,
      bvh (light-BVH NEE, 1 sample, ReSTIR off), uniform (uniform-emitter NEE, 1 sample),
      uniform32 (uniform NEE x32), reference (uniform NEE x32, 2,050 accumulated samples),
      readback (restir + r.ReSTIR.Readback), default (flag-free reflection-reference frame 95).
      Append `-profile` to any path-traced mode for a 400-frame GPU-profiled timing run.
Screenshots are linear: r.TonemapOperator=0 writes the HDR colour to the RGBA8_UNORM swapchain, so
PNG code / 255 is the linear radiance clipped at 1.0. Every metric below works in that domain; the
montages apply the sRGB curve for display only.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from flicker.runner import run_capture

REPO = Path(__file__).resolve().parents[2]
SCENE_DIR = REPO / "scripts/rendering/restir-many-lights"
EXTENT = 540.0                  # emitter grid spans -270 .. 270 on x and y
CLEARANCE = 30.0                # bottom face height above the floor (z = 0)
LEVELS, BASE, RATIO = 10, 0.05, 1.88
# Cube half-size per grid: N * (2s)^2 is constant (36,864), so the total power matches across grids.
HALF = {4: 24.0, 8: 12.0, 16: 6.0, 32: 3.0}
ANGLES = {
    "a": {"position": [0, -400, 640], "look_at": [0, 0, 0], "fov": 45},       # overview
    "b": {"position": [-520, -520, 45], "look_at": [0, 0, 40], "fov": 50},     # grazing, cubes occlude
    "c": {"position": [0, -40, 900], "look_at": [0, 0, 0], "fov": 45},        # top-down
    "d": {"position": [330, 330, 170], "look_at": [-90, -90, 10], "fov": 55}, # corner close-up
}
DOLLY_SPEED = 2.0               # units per frame along +x (about one 640x360 pixel per frame at the floor)
LUMA = np.array([0.2126, 0.7152, 0.0722])
RESOLUTIONS = {360: (640, 360), 1080: (1920, 1080)}


def level(i, j):
    return (3 * i + 7 * j) % LEVELS


def intensity(k):
    return round(BASE * RATIO ** k, 4)


def cube(name, position, scale, material, extra=None):
    components = {"transform": {"position": position, "scale": scale},
                  "mesh": {"asset": "sample/content/meshes/cube.uzmesh"},
                  "material": material}
    if extra:
        components.update(extra)
    return {"name": name, "components": components}


def scene_cubes(grid):
    """Emitter centres, half-sizes and intensities for a grid (the same order as the scene file)."""
    s = HALF[grid]
    spacing = EXTENT / (grid - 1)
    origin = -EXTENT / 2
    cubes = []
    for j in range(grid):
        for i in range(grid):
            k = level(i, j)
            cubes.append({"name": f"Emitter {j * grid + i} level {k}",
                          "center": np.array([origin + spacing * i, origin + spacing * j, CLEARANCE + s]),
                          "half": np.array([s, s, s]), "intensity": intensity(k)})
    return cubes


def scene_name(grid, angle, res=360, dolly=False):
    return f"g{grid}-{angle}" + ("-dolly" if dolly else "") + ("-1080p" if res == 1080 else "")


def write_scenes():
    SCENE_DIR.mkdir(parents=True, exist_ok=True)
    diffuse = {"baseColorR": 0.65, "baseColorG": 0.65, "baseColorB": 0.65,
               "roughnessMultiplier": 1, "metallicMultiplier": 0}
    (SCENE_DIR / "dolly.lua").write_text(DOLLY_LUA, encoding="utf-8")
    written = 0
    for grid in HALF:
        for angle, cam in ANGLES.items():
            for dolly in (False, True):
                if dolly and angle != "a":
                    continue
                camera = {"name": "Camera", "components": {
                    "transform": {"position": cam["position"]},
                    "camera": {"fov": cam["fov"], "near": 1, "far": 10000, "active": True,
                               "look_at": cam["look_at"]}}}
                if dolly:
                    camera["components"]["script"] = {"path": "scripts/rendering/restir-many-lights/dolly.lua"}
                entities = [camera,
                            {"name": "Black environment",
                             "components": {"sky_light": {"intensity": 0, "light_color": [0, 0, 0]}}},
                            cube("Floor", [0, 0, -5], [600, 600, 5], diffuse)]
                for c in scene_cubes(grid):
                    material = {"baseColorR": 0, "baseColorG": 0, "baseColorB": 0,
                                "roughnessMultiplier": 1, "metallicMultiplier": 0,
                                "emissiveColorR": 1, "emissiveColorG": 1, "emissiveColorB": 1,
                                "emissiveIntensity": c["intensity"]}
                    entities.append(cube(c["name"], [float(v) for v in c["center"]],
                                         [float(v) for v in c["half"]], material))
                scene = {"name": f"ReSTIR DI many lights: {grid * grid} emissive cubes, angle {angle}"
                                 + (", lateral dolly" if dolly else ""),
                         "version": "1.0", "entities": entities}
                name = scene_name(grid, angle, dolly=dolly)
                (SCENE_DIR / f"{name}.uzscene").write_text(json.dumps(scene, separators=(",", ":")) + "\n",
                                                           encoding="utf-8")
                for res, (w, h) in RESOLUTIONS.items():
                    project = {
                        "name": f"ReSTIR DI many lights {name} {w}x{h}", "version": "1.0",
                        "content_root": "../../..",
                        "default_scene": f"scripts/rendering/restir-many-lights/{name}.uzscene",
                        "window": {"width": w, "height": h},
                        "cvars": {"r.AutoExposure": 0, "r.Exposure": 1, "r.TonemapOperator": 0,
                                  "r.GTAO": 0, "r.RadianceCache": 0, "r.GI.FinalGather": 0,
                                  "r.GI.Reflections": 0, "r.SSGI": 0, "r.TAA": 0, "r.Fog": 0,
                                  "r.TechPanel.Show": 0, "r.Bloom": 0,
                                  "r.PathTracer.MaxBounces": 2, "r.PathTracer.NEESamples": 1},
                    }
                    (SCENE_DIR / f"{scene_name(grid, angle, res, dolly)}.uzproject").write_text(
                        json.dumps(project, indent=2) + "\n", encoding="utf-8")
                    written += 1
    print(f"wrote {written} projects into {SCENE_DIR}")


DOLLY_LUA = """-- Deterministic lateral camera dolly for the moving-camera ReSTIR temporal-reuse test.
-- The camera keeps the scene's `a` lens and slides along +x by DOLLY_SPEED units per frame from
-- the first on_update, so frame k of the run sees the pose x = x0 + DOLLY_SPEED * (k - k0).
local SPEED = %s
local start_frame = nil
local function apply(relative)
    local x = 0 + SPEED * relative
    uz.camera.set_position(x, -400, 640)
    uz.camera.look_at(x, 0, 0)
    if relative %% 20 == 0 then
        uz.log(string.format("Dolly frame=%%d relative=%%d x=%%.3f", uz.frame_index, relative, x))
    end
end
function on_start(entity)
    start_frame = uz.frame_index
    uz.log(string.format("Dolly started base_frame=%%d speed=%%.3f", start_frame, SPEED))
    apply(0)
end
function on_update(entity, dt)
    apply(uz.frame_index - start_frame)
end
""" % DOLLY_SPEED


# ── Capture ─────────────────────────────────────────────────────────────────────────────────────

def capture(output, mode, grid, angle, res, frames=None, shots=None, extra_cvars=(), dolly=False,
            camera_pos=None, camera_look=None):
    out = output / mode
    if out.exists():
        raise SystemExit(f"Refusing to overwrite capture: {out}")
    profile = mode.endswith("-profile")
    base = mode[:-len("-profile")] if profile else mode
    cvars = {
        "r.TAA": 0, "r.TechPanel.Show": 0, "r.Toolbar.Show": 0, "r.VolumetricFog": 0,
        "r.PathTracer.NEESamples": 1, "r.PathTracer.MaxBounces": 2, "r.PathTracer.RussianRoulette": 0,
        "r.LightBVH": 1, "r.LightBVH.Uniform": int(base in ("uniform", "uniform32", "reference")),
    }
    args = ["--hidden", "--path-trace", "--pt-bounces", "2"]
    if base.startswith("restir") or base == "readback":
        args += ["--restir-di"]
    if base == "restir-nospatial":
        cvars["r.ReSTIR.Spatial"] = 0
    if base == "restir-notemporal":
        cvars["r.ReSTIR.Temporal"] = 0
    if base == "restir-initial":
        cvars["r.ReSTIR.Spatial"] = 0
        cvars["r.ReSTIR.Temporal"] = 0
    if base in ("uniform32", "reference"):
        cvars["r.PathTracer.NEESamples"] = 32
    if base == "readback":
        cvars["r.ReSTIR.Readback"] = 1
    if profile:
        cvars["r.GpuProfiler.Log"] = 1
        args += ["--gpu-profile"]
    for item in extra_cvars:
        key, value = item.split("=", 1)
        cvars[key] = value
    if camera_pos:
        args += ["--camera-pos", camera_pos]
    if camera_look:
        args += ["--camera-look", camera_look]
    default_frames = 2055 if base == "reference" else 2055
    default_shots = "5-2050:5"
    if profile:
        default_frames, default_shots = 400, "395"
    if base == "readback":
        default_frames, default_shots = 60, "60"
    project = SCENE_DIR / f"{scene_name(grid, angle, res, dolly)}.uzproject"
    if base == "default":
        project = REPO / "scripts/rendering/reflection-reference.uzproject"
        default_frames, default_shots = 100, "95"
        cvars = {"r.TAA": 1, "r.TechPanel.Show": 0, "r.Toolbar.Show": 0,
                 "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1}
        args = []
    if not project.exists():
        raise SystemExit(f"Missing project {project}; run `scenes` first")
    result = run_capture(str(out), str(project), frames or default_frames, cvars=cvars, extra_args=args,
                         screenshot_frames=shots or default_shots, timeout=178, clean=False)
    print(f"CAPTURE {mode} EXIT {result['exit_code']}", flush=True)
    raise SystemExit(result["exit_code"])


# ── Analysis ────────────────────────────────────────────────────────────────────────────────────

def camera_basis(angle, width, height, mirror_x=False, horizontal_fov=False):
    cam = ANGLES[angle] if isinstance(angle, str) else angle
    position, target = np.array(cam["position"], dtype=float), np.array(cam["look_at"], dtype=float)
    forward = target - position
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.array([0.0, 0.0, 1.0]))
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    if mirror_x:
        right = -right
    half = np.tan(np.radians(cam["fov"]) / 2)
    tx, ty = (half, half * height / width) if horizontal_fov else (half * width / height, half)
    return position, right, up, forward, tx, ty


def projector(angle, width, height, mirror_x=False, horizontal_fov=False):
    position, right, up, forward, tx, ty = camera_basis(angle, width, height, mirror_x, horizontal_fov)

    def project(points):
        d = np.atleast_2d(points) - position
        xv, yv, zv = d @ right, d @ up, d @ forward
        zv = np.where(zv <= 1e-6, 1e-6, zv)
        return np.stack([(xv / (zv * tx) * 0.5 + 0.5) * width,
                         (0.5 - yv / (zv * ty) * 0.5) * height], axis=-1)
    return project


def validate_projection(reference, cubes, angle):
    """Picks the camera convention whose projected bright-cube tops land on clipped pixels."""
    h, w = reference.shape[:2]
    lum = reference @ LUMA
    best = None
    for mirror in (False, True):
        for horizontal in (False, True):
            project = projector(angle, w, h, mirror, horizontal)
            bright = [c for c in cubes if c["intensity"] >= 1.0]
            tops = np.array([c["center"] + [0, 0, c["half"][2]] for c in bright])
            px = np.rint(project(tops)).astype(int)
            inside = (px[:, 0] >= 0) & (px[:, 0] < w) & (px[:, 1] >= 0) & (px[:, 1] < h)
            hits = [lum[y, x] >= 0.98 for (x, y), ok in zip(px, inside) if ok]
            score = float(np.mean(hits)) if hits else 0.0
            if best is None or score > best["hit_fraction"]:
                best = {"mirror_x": mirror, "horizontal_fov": horizontal, "hit_fraction": score,
                        "bright_cubes": len(bright), "visible_bright_cubes": len(hits)}
    return best


def floor_footprint_mask(shape, angle, convention, margin=20.0):
    """Pixels whose camera ray meets the floor plane z = 0 inside the emitter grid's footprint."""
    h, w = shape[:2]
    position, right, up, forward, tx, ty = camera_basis(angle, w, h, convention["mirror_x"],
                                                         convention["horizontal_fov"])
    ys, xs = np.mgrid[0:h, 0:w]
    nx = (xs + 0.5) / w * 2.0 - 1.0
    ny = 1.0 - (ys + 0.5) / h * 2.0
    d = forward[None, None, :] + (nx * tx)[..., None] * right[None, None, :] + (ny * ty)[..., None] * up[None, None, :]
    with np.errstate(divide="ignore", invalid="ignore"):
        t = -position[2] / d[..., 2]
    hit = position[None, None, :] + t[..., None] * d
    half = EXTENT / 2 + margin
    return (t > 0) & (np.abs(hit[..., 0]) <= half) & (np.abs(hit[..., 1]) <= half)


def emitter_mask(shape, cubes, angle, convention, pad=2):
    """Pixels covered by any emitter's projected bounding box (plus `pad`), i.e. not floor."""
    h, w = shape[:2]
    project = projector(angle, w, h, convention["mirror_x"], convention["horizontal_fov"])
    mask = np.zeros((h, w), dtype=bool)
    corners = np.array([[sx, sy, sz] for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)], dtype=float)
    for c in cubes:
        px = project(c["center"] + corners * c["half"])
        x0, x1 = int(np.floor(px[:, 0].min())) - pad, int(np.ceil(px[:, 0].max())) + pad
        y0, y1 = int(np.floor(px[:, 1].min())) - pad, int(np.ceil(px[:, 1].max())) + pad
        x0, x1, y0, y1 = max(x0, 0), min(x1, w - 1), max(y0, 0), min(y1, h - 1)
        if x1 >= x0 and y1 >= y0:
            mask[y0:y1 + 1, x0:x1 + 1] = True
    return mask


def load_linear(path):
    with Image.open(path) as im:
        return np.asarray(im.convert("RGB"), dtype=np.float64) / 255.0


def frame_path(output, mode, frame):
    return output / mode / "frames" / f"f_{frame}.png"


def read_manifest(output, mode):
    path = output / mode / "manifest.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None


def srgb8(linear):
    x = np.clip(linear, 0.0, 1.0)
    s = np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)
    return (np.rint(s * 255.0)).astype(np.uint8)


def labelled(linear, text, scale=1.0):
    img = Image.fromarray(srgb8(linear))
    if scale != 1.0:
        img = img.resize((int(img.width * scale), int(img.height * scale)), Image.LANCZOS)
    draw = ImageDraw.Draw(img)
    draw.rectangle([0, 0, 8 + 7 * len(text), 16], fill=(0, 0, 0))
    draw.text((4, 2), text, fill=(255, 255, 255))
    return img


def montage(rows, path, scale=1.0):
    """rows: list of lists of (linear image, label)."""
    tiles = [[labelled(img, lab, scale) for img, lab in row] for row in rows]
    tw, th = tiles[0][0].size
    cols = max(len(r) for r in tiles)
    sheet = Image.new("RGB", (cols * tw, len(tiles) * th), (20, 20, 20))
    for y, row in enumerate(tiles):
        for x, tile in enumerate(row):
            sheet.paste(tile, (x * tw, y * th))
    sheet.save(path)
    return path


def analyze(output, grid, angle, frames=(5, 20, 65, 200), converged=2050):
    cubes = scene_cubes(grid)
    reference = load_linear(frame_path(output, "reference", converged))
    convention = validate_projection(reference, cubes, angle)
    emitters = emitter_mask(reference.shape, cubes, angle, convention)
    lum = reference @ LUMA
    # Floor under the emitter grid, minus the emitter boxes: a geometric mask, not one
    # derived from any estimator's output.
    floor = ~emitters & floor_footprint_mask(reference.shape, angle, convention)
    metrics = {"grid": grid, "lights": grid * grid, "emissive_triangles": grid * grid * 12,
               "angle": angle, "projection": convention,
               "pixels": {"floor": int(floor.sum()), "emitter_box": int(emitters.sum()),
                          "total": int(floor.size)},
               "reference": {"mode": "reference", "samples": converged,
                             "floor_mean": float(reference[floor].mean()),
                             "image_mean": float(reference.mean())},
               "converged": {}, "mse": {}, "tiles": {}}
    modes = [m for m in ("restir", "restir-nospatial", "restir-notemporal", "restir-initial",
                         "bvh", "uniform") if (output / m / "frames").exists()]
    # Coarse 6x6 tile ratios over the floor: local bias would show as a tile far from 1.
    h, w = lum.shape
    ty, tx = h // 6, w // 6
    for mode in modes:
        path = frame_path(output, mode, converged)
        if path.exists():
            img = load_linear(path)
            delta = img[floor].mean() / reference[floor].mean() - 1.0
            metrics["converged"][mode] = {
                "samples": converged, "floor_mean": float(img[floor].mean()),
                "floor_delta_pct": 100.0 * float(delta),
                "image_delta_pct": 100.0 * float(img.mean() / reference.mean() - 1.0),
                "floor_mean_abs_pixel_delta_pct": 100.0 * float(np.abs(img[floor] - reference[floor]).mean()
                                                                 / reference[floor].mean()),
            }
            ratios = []
            for j in range(6):
                for i in range(6):
                    sel = floor[j * ty:(j + 1) * ty, i * tx:(i + 1) * tx]
                    if sel.sum() < 200:
                        continue
                    a = img[j * ty:(j + 1) * ty, i * tx:(i + 1) * tx][sel].mean()
                    b = reference[j * ty:(j + 1) * ty, i * tx:(i + 1) * tx][sel].mean()
                    ratios.append(a / b)
            metrics["tiles"][mode] = {"count": len(ratios), "min": float(min(ratios)), "max": float(max(ratios)),
                                      "mean": float(np.mean(ratios))}
        metrics["mse"][mode] = {}
        for n in frames:
            path = frame_path(output, mode, n)
            if not path.exists():
                continue
            img = load_linear(path)
            err = img[floor] - reference[floor]
            metrics["mse"][mode][str(n)] = {"floor_mse": float((err * err).mean()),
                                            "floor_rmse_pct_of_mean": 100.0 * float(np.sqrt((err * err).mean())
                                                                                     / reference[floor].mean()),
                                            "image_mse": float(((img - reference) ** 2).mean())}
    # Samples a plain estimator needs to reach ReSTIR's accumulated error at n frames:
    # MSE(n) ~ V/n, so n_equivalent = n_plain * MSE_plain(n_plain) / MSE_restir(n).
    def mse(mode, n):
        return metrics["mse"].get(mode, {}).get(str(n), {}).get("floor_mse")
    equiv = {}
    for n in frames:
        r = mse("restir", n)
        if r is None:
            continue
        equiv[str(n)] = {}
        for mode in ("uniform", "bvh"):
            anchor = next((k for k in (200, 64, 20) if mse(mode, k)), None)
            if anchor:
                equiv[str(n)][mode] = {"anchor_samples": anchor,
                                       "samples_to_match": anchor * mse(mode, anchor) / r}
    metrics["equivalent_samples"] = equiv
    # GPU timing from every manifest in this output.
    timing = {}
    for d in sorted(output.iterdir()):
        m = read_manifest(output, d.name)
        if m and m.get("gpu_ms"):
            g = m["gpu_ms"]
            timing[d.name] = {k: g[k] for k in g if k.startswith("ReSTIR") or k.startswith("PipelinePathTracer")
                              or k in ("_total", "_frame", "RT_BLAS_TLAS")}
            timing[d.name]["resolution"] = m.get("resolution")
            timing[d.name]["frames"] = m.get("frames")
    metrics["gpu_ms"] = timing
    (output / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    # Montages: 1-sample frames per mode + the reference, and the ReSTIR convergence row.
    rows = []
    row = []
    early = frames[0]
    for mode in ("restir", "bvh", "uniform"):
        if frame_path(output, mode, early).exists():
            row.append((load_linear(frame_path(output, mode, early)), f"{mode} {early} spp"))
    row.append((reference, f"reference {converged} spp"))
    rows.append(row)
    conv = [(load_linear(frame_path(output, "restir", n)), f"restir {n} spp")
            for n in frames if frame_path(output, "restir", n).exists()]
    if conv:
        rows.append(conv[:4])
    montage(rows, output / "montage.png")
    print(json.dumps({k: metrics[k] for k in ("pixels", "converged", "equivalent_samples")}, indent=1))
    return metrics


def dolly_camera(relative_frame):
    """The `a` lens after `relative_frame` dolly frames (dolly.lua: x = DOLLY_SPEED * relative)."""
    x = DOLLY_SPEED * relative_frame
    return {"position": [x, -400, 640], "look_at": [x, 0, 0], "fov": ANGLES["a"]["fov"]}


def dolly_base_frame(run_dir):
    """Engine frame at which dolly.lua started (`Dolly started base_frame=N` in run.log)."""
    for line in (run_dir / "run.log").read_text(encoding="utf-8", errors="replace").splitlines():
        if "Dolly started base_frame=" in line:
            return int(line.split("base_frame=")[1].split()[0])
    raise SystemExit(f"{run_dir}: dolly.lua never started")


def floor_mask_for(reference, grid, camera):
    cubes = scene_cubes(grid)
    convention = validate_projection(reference, cubes, camera)
    emitters = emitter_mask(reference.shape, cubes, camera, convention)
    return ~emitters & floor_footprint_mask(reference.shape, camera, convention), convention


def analyze_flicker(output, modes, first, count, reference_dir, grid=32, angle="a"):
    """Static camera, r.PathTracer.Accumulate=0: every frame is one independent 1-spp image.

    Reports the per-pixel temporal standard deviation over `count` consecutive frames (flicker) and
    the per-frame floor MSE against the converged reference, per mode.
    """
    reference = load_linear(frame_path(reference_dir, "reference", 2050))
    floor, _ = floor_mask_for(reference, grid, angle)
    result = {"first_frame": first, "frames": count, "floor_pixels": int(floor.sum()), "modes": {}}
    for mode in modes:
        stack = np.stack([load_linear(frame_path(output, mode, f)) for f in range(first, first + count)])
        std = stack.std(axis=0, ddof=1)
        per_frame = [float(((img[floor] - reference[floor]) ** 2).mean()) for img in stack]
        result["modes"][mode] = {
            "temporal_std_floor_mean": float(std[floor].mean()),
            "temporal_std_pct_of_mean": 100.0 * float(std[floor].mean() / reference[floor].mean()),
            "per_frame_floor_mse_mean": float(np.mean(per_frame)),
            "per_frame_floor_mse_min": float(np.min(per_frame)),
            "per_frame_floor_mse_max": float(np.max(per_frame)),
        }
    (output / "flicker.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=1))
    return result


def analyze_dolly(output, modes, poses, reference_root, grid=32):
    """Moving camera (dolly.lua, r.PathTracer.Accumulate=0): frame at relative pose k against the
    static converged reference captured at that pose (`reference_root/dolly-ref-x{X}/reference`).
    """
    result = {"poses": {}, "modes": modes}
    for relative in poses:
        camera = dolly_camera(relative)
        x = int(camera["position"][0])
        ref_dir = reference_root / f"dolly-ref-x{x}"
        reference = load_linear(frame_path(ref_dir, "reference", 2050))
        floor, convention = floor_mask_for(reference, grid, camera)
        entry = {"x": x, "floor_pixels": int(floor.sum()), "projection": convention, "modes": {}}
        for mode in modes:
            run = output / mode
            frame = dolly_base_frame(run) + relative
            path = frame_path(output, mode, frame)
            if not path.exists():
                entry["modes"][mode] = {"frame": frame, "missing": True}
                continue
            img = load_linear(path)
            err = img[floor] - reference[floor]
            entry["modes"][mode] = {"frame": frame, "floor_mse": float((err * err).mean()),
                                    "floor_mean_delta_pct": 100.0 * float(img[floor].mean() / reference[floor].mean() - 1.0)}
        result["poses"][str(relative)] = entry
    (output / "dolly.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=1))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("scenes")
    cap = sub.add_parser("capture")
    cap.add_argument("output", type=Path)
    cap.add_argument("mode")
    cap.add_argument("--grid", type=int, default=32)
    cap.add_argument("--angle", default="a")
    cap.add_argument("--res", type=int, default=360)
    cap.add_argument("--frames", type=int)
    cap.add_argument("--shots")
    cap.add_argument("--cvar", action="append", default=[])
    cap.add_argument("--dolly", action="store_true")
    cap.add_argument("--cam-pos", help="x,y,z override of the active camera (uzengine --camera-pos)")
    cap.add_argument("--cam-look", help="x,y,z look-at override (uzengine --camera-look)")
    ana = sub.add_parser("analyze")
    ana.add_argument("output", type=Path)
    ana.add_argument("--grid", type=int, default=32)
    ana.add_argument("--angle", default="a")
    fl = sub.add_parser("flicker")
    fl.add_argument("output", type=Path)
    fl.add_argument("--modes", nargs="+", default=["restir", "restir-notemporal", "bvh"])
    fl.add_argument("--first", type=int, default=60)
    fl.add_argument("--count", type=int, default=10)
    fl.add_argument("--reference", type=Path, required=True, help="OUTPUT dir holding reference/frames/f_2050.png")
    do = sub.add_parser("dolly")
    do.add_argument("output", type=Path)
    do.add_argument("--modes", nargs="+", default=["restir", "restir-notemporal", "bvh"])
    do.add_argument("--poses", nargs="+", type=int, default=[40, 60, 80])
    do.add_argument("--references", type=Path, required=True, help="runs root holding dolly-ref-x{X}/reference")
    args = parser.parse_args()
    if args.command == "scenes":
        write_scenes()
    elif args.command == "capture":
        capture(args.output, args.mode, args.grid, args.angle, args.res, args.frames, args.shots,
                args.cvar, args.dolly, args.cam_pos, args.cam_look)
    elif args.command == "analyze":
        analyze(args.output, args.grid, args.angle)
    elif args.command == "flicker":
        analyze_flicker(args.output, args.modes, args.first, args.count, args.reference)
    else:
        analyze_dolly(args.output, args.modes, args.poses, args.references)


if __name__ == "__main__":
    main()
