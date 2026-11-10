"""Bounded, serialized light-BVH grid captures (100 emissive cubes) and their analysis.

Run: python scripts/rendering/measure_light_bvh_grid.py capture OUTPUT MODE [--frames N] [--shots A-B:S]
or:  python scripts/rendering/measure_light_bvh_grid.py analyze OUTPUT
MODE is combined (ReSTIR DI x light BVH), uniform (ReSTIR DI, uniform emitter candidates),
bvh (light-BVH NEE, ReSTIR off), reference (uniform-emitter NEE, 32 samples/frame, ReSTIR off),
histogram (combined + r.ReSTIR.Readback selection histogram), profile (combined/uniform GPU timing),
or default (flag-free reflection-reference frame 95). Output must be new.
Screenshots are linear: r.TonemapOperator=0 writes the HDR colour to an RGBA8_UNORM swapchain,
so PNG code / 255 is the linear radiance clipped at 1.0; the metrics work in that domain.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from flicker.runner import run_capture

REPO = Path(__file__).resolve().parents[2]
SCENE = "light-bvh-grid"
GRID, SPACING, CUBE, HEIGHT = 10, 60.0, 20.0, 50.0


def capture(output, mode, frames=None, shots=None):
    out = output / mode
    if out.exists():
        raise SystemExit(f"Refusing to overwrite capture: {out}")
    cvars = {
        "r.TAA": 0, "r.TechPanel.Show": 0, "r.Toolbar.Show": 0,
        "r.VolumetricFog": 0, "r.PathTracer.NEESamples": 1,
        "r.PathTracer.MaxBounces": 2, "r.PathTracer.RussianRoulette": 0,
        "r.LightBVH": 1, "r.LightBVH.Uniform": int(mode in ("uniform", "reference")),
    }
    args = ["--hidden", "--path-trace", "--pt-bounces", "2"]
    if mode in ("combined", "uniform", "histogram", "histogram-uniform",
                "profile-combined", "profile-uniform"):
        args += ["--restir-di"]
    if mode == "reference":
        cvars["r.PathTracer.NEESamples"] = 32
    if mode.startswith("histogram"):
        cvars["r.ReSTIR.Readback"] = 1
        cvars["r.LightBVH.Uniform"] = int(mode == "histogram-uniform")
    if mode.startswith("profile-"):
        cvars["r.LightBVH.Uniform"] = int(mode == "profile-uniform")
        cvars["r.GpuProfiler.Log"] = 1
        args += ["--gpu-profile"]
    scene = SCENE
    default_frames, default_shots = 2055, "5-2050:5"
    if mode.startswith("histogram"):
        default_frames, default_shots = 140, "130"
    if mode.startswith("profile-"):
        default_frames, default_shots = 400, "395"
    if mode == "default":
        scene, default_frames, default_shots = "reflection-reference", 100, "95"
        cvars = {"r.TAA": 1, "r.TechPanel.Show": 0, "r.Toolbar.Show": 0,
                 "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1}
        args = []
    result = run_capture(str(out), str(REPO / "scripts/rendering" / f"{scene}.uzproject"),
                         frames or default_frames, cvars=cvars, extra_args=args,
                         screenshot_frames=shots or default_shots, timeout=175, clean=False)
    print(f"CAPTURE {mode} EXIT {result['exit_code']}", flush=True)
    raise SystemExit(result["exit_code"])


# ── Analysis ────────────────────────────────────────────────────────────────────────────────

LUMA = np.array([0.2126, 0.7152, 0.0722])
CAMERA, TARGET, FOV_DEG = np.array([0.0, -400.0, 640.0]), np.zeros(3), 45.0


def scene_cubes():
    scene = json.loads((REPO / "scripts/rendering" / f"{SCENE}.uzscene").read_text())
    cubes = []
    for e in scene["entities"]:
        if not e["name"].startswith("Emitter"):
            continue
        t, m = e["components"]["transform"], e["components"]["material"]
        cubes.append({"name": e["name"], "center": np.array(t["position"], dtype=float),
                      "half": np.array(t["scale"], dtype=float), "intensity": float(m["emissiveIntensity"])})
    return cubes


def camera_basis(width, height, mirror_x=False, horizontal_fov=False):
    forward = TARGET - CAMERA
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.array([0.0, 0.0, 1.0]))
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    if mirror_x:          # opposite handedness: flip the screen x axis only
        right = -right
    half = np.tan(np.radians(FOV_DEG) / 2)
    tx, ty = (half, half * height / width) if horizontal_fov else (half * width / height, half)
    return right, up, forward, tx, ty


def projector(width, height, mirror_x=False, horizontal_fov=False):
    """World (z-up) -> pixel for the scene camera; the variant is chosen by validation, not assumed."""
    right, up, forward, tx, ty = camera_basis(width, height, mirror_x, horizontal_fov)

    def project(points):
        d = np.atleast_2d(points) - CAMERA
        xv, yv, zv = d @ right, d @ up, d @ forward
        return np.stack([(xv / (zv * tx) * 0.5 + 0.5) * width,
                         (0.5 - yv / (zv * ty) * 0.5) * height], axis=-1)
    return project


def floor_unprojector(width, height, mirror_x=False, horizontal_fov=False):
    """Pixel centres -> the world point where their camera ray meets the floor plane z = 0."""
    right, up, forward, tx, ty = camera_basis(width, height, mirror_x, horizontal_fov)

    def unproject(xs, ys):
        nx = (np.asarray(xs, dtype=float) + 0.5) / width * 2.0 - 1.0
        ny = 1.0 - (np.asarray(ys, dtype=float) + 0.5) / height * 2.0
        d = forward[None, :] + np.outer(nx * tx, right) + np.outer(ny * ty, up)
        t = -CAMERA[2] / d[:, 2]
        return CAMERA[None, :] + t[:, None] * d
    return unproject


def validate_projection(reference, cubes):
    """Picks the camera convention whose projected bright-cube tops land on clipped pixels."""
    h, w = reference.shape[:2]
    lum = reference @ LUMA
    best = None
    for mirror in (False, True):
        for horizontal in (False, True):
            project = projector(w, h, mirror, horizontal)
            bright = [c for c in cubes if c["intensity"] >= 1.0]
            tops = np.array([c["center"] + [0, 0, c["half"][2]] for c in bright])
            px = np.rint(project(tops)).astype(int)
            inside = (px[:, 0] >= 0) & (px[:, 0] < w) & (px[:, 1] >= 0) & (px[:, 1] < h)
            hits = [lum[y, x] >= 0.98 for (x, y), ok in zip(px, inside) if ok]
            score = float(np.mean(hits)) if hits else 0.0
            if best is None or score > best["hit_fraction"]:
                best = {"mirror_x": mirror, "horizontal_fov": horizontal, "hit_fraction": score,
                        "bright_cubes": len(bright)}
    return best


def analytic_floor_radiance(points, cubes, albedo=0.65, taps=12):
    """Direct radiance of the Lambertian floor at `points` from every outward cube face.

    One-sided, convex cubes: a face lights the point only from in front of its plane; the cube's
    own body hides its other faces. Cube-to-cube occlusion is ignored, so this is an upper bound
    tight to a few percent for this layout (cubes hang well above the floor, far apart).
    """
    points = np.atleast_2d(points)
    s = (np.arange(taps) + 0.5) / taps
    gu, gv = np.meshgrid(s, s)
    gu, gv = gu.ravel(), gv.ravel()
    irradiance = np.zeros(len(points))
    for c in cubes:
        center, half, L = c["center"], c["half"], c["intensity"]
        for axis in range(3):
            for sign in (-1.0, 1.0):
                normal = np.zeros(3)
                normal[axis] = sign
                a, b = [i for i in range(3) if i != axis]
                face_center = center + normal * half[axis]
                ea, eb = np.zeros(3), np.zeros(3)
                ea[a], eb[b] = half[a], half[b]
                samples = face_center + np.outer(2 * gu - 1, ea) + np.outer(2 * gv - 1, eb)
                area = 4 * half[a] * half[b]
                d = samples[None, :, :] - points[:, None, :]          # point -> emitter
                r2 = np.einsum("pij,pij->pi", d, d)
                r = np.sqrt(r2)
                cos_floor = np.clip(d[..., 2] / r, 0, None)
                cos_face = np.clip(-(d @ normal) / r, 0, None)
                irradiance += L * area * np.mean(cos_floor * cos_face / r2, axis=1)
    return albedo / np.pi * irradiance


def rank_correlation(a, b):
    ra, rb = np.argsort(np.argsort(a)), np.argsort(np.argsort(b))
    return float(np.corrcoef(ra, rb)[0, 1])


def loglog_slope(x, y):
    lx, ly = np.log(x), np.log(y)
    return float(np.polyfit(lx, ly, 1)[0])


def parse_histogram_log(path):
    """Per-emitter rows of the `LightBVH histogram` readback (last detailed frame wins)."""
    rows, summary = {}, None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "LightBVH histogram" not in line:
            continue
        fields = dict(part.split("=", 1) for part in line.split("LightBVH histogram ", 1)[1].split()
                      if "=" in part)
        if "emitter" in fields:
            rows[int(fields["emitter"])] = {k: float(v) for k, v in fields.items()}
        else:
            summary = {k: float(v) for k, v in fields.items()}
    return summary, rows


def histogram_metrics(path):
    summary, rows = parse_histogram_log(path)
    if not rows:
        return {"available": False, "path": str(path)}
    emitters = sorted(rows)
    power = np.array([rows[e]["powerShare"] for e in emitters])
    cand = np.array([rows[e]["candidateShare"] for e in emitters])
    cpu = np.array([rows[e]["cpuShare"] for e in emitters])
    sel = np.array([rows[e]["selectedShare"] for e in emitters])
    vis = np.array([rows[e]["visibleShare"] for e in emitters])
    keep = cand > 0
    rel = np.abs(cand[keep] - cpu[keep]) / np.maximum(cpu[keep], 1e-12)
    big = cpu >= 0.5   # emitters holding >= 0.5% of the candidates: enough counts for a tight check
    order = np.argsort(-power)
    top10 = order[:10]
    return {
        "available": True, "summary": summary, "emitters": len(emitters),
        "candidate_vs_power": {"spearman": rank_correlation(cand, power),
                               "loglog_slope": loglog_slope(power[keep], cand[keep])},
        "selected_vs_power": {"spearman": rank_correlation(sel, power)},
        "gpu_vs_cpu_model": {
            "max_relative_share_delta_all": float(rel.max()),
            "max_relative_share_delta_ge_0p5pct": float((np.abs(cand[big] - cpu[big]) / cpu[big]).max()) if big.any() else None,
            "mean_relative_share_delta_ge_0p5pct": float((np.abs(cand[big] - cpu[big]) / cpu[big]).mean()) if big.any() else None,
            "emitters_ge_0p5pct": int(big.sum()),
            "share_l1_percent": float(np.abs(cand - cpu).sum()),
        },
        "top10_power_share_percent": float(power[top10].sum()),
        "top10_candidate_share_percent": float(cand[top10].sum()),
        "top10_selected_share_percent": float(sel[top10].sum()),
        "bottom50_power_share_percent": float(power[order[50:]].sum()),
        "bottom50_candidate_share_percent": float(cand[order[50:]].sum()),
        "visible_over_selected_percent": float(100 * vis.sum() / max(sel.sum(), 1e-12)),
        "rows": {int(e): rows[e] for e in emitters},
    }


def srgb(linear):
    x = np.clip(linear, 0, 1)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def montage(tiles, path, scale=1):
    """Tiles: list of rows of (title, linear rgb image). Written sRGB-encoded for viewing."""
    from PIL import ImageDraw
    h, w = tiles[0][0][1].shape[:2]
    sheet = Image.new("RGB", (w * len(tiles[0]) * scale, h * len(tiles) * scale))
    draw = ImageDraw.Draw(sheet)
    for r, row in enumerate(tiles):
        for c, (title, img) in enumerate(row):
            tile = Image.fromarray((srgb(img) * 255 + 0.5).astype(np.uint8))
            if scale != 1:
                tile = tile.resize((w * scale, h * scale), Image.NEAREST)
            sheet.paste(tile, (c * w * scale, r * h * scale))
            draw.text((c * w * scale + 6, r * h * scale + 4), title, fill=(255, 220, 90))
    sheet.save(path)


def analyze(output, after=None, early=None):
    """`output` holds the 2,055-frame captures (every 5th frame); `early` the 100-frame every-frame
    replays of the same modes, which supply frame 64. The runs are deterministic, so the replay is
    checked byte-for-byte against `output` at the frames both hold before it is used."""
    def frame_path(mode, frame, root=output):
        path = root / mode / "frames" / f"f_{frame}.png"
        if not path.exists() and early is not None:
            path = early / mode / "frames" / f"f_{frame}.png"
        return path

    def image(mode, frame, root=output):
        return np.asarray(Image.open(frame_path(mode, frame, root)).convert("RGB"),
                          dtype=np.float64) / 255.0

    cubes = scene_cubes()
    reference = image("reference", 2050)
    h, w = reference.shape[:2]
    lum_ref = reference @ LUMA
    projection = validate_projection(reference, cubes)
    project = projector(w, h, projection["mirror_x"], projection["horizontal_fov"])
    unproject = floor_unprojector(w, h, projection["mirror_x"], projection["horizontal_fov"])

    # Emitter pixels: projected cube bounding boxes grown by 3 px. Floor: the lit, unclipped rest.
    emitter = np.zeros((h, w), dtype=bool)
    for c in cubes:
        corners = c["center"] + np.array([[sx, sy, sz] for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]) * c["half"]
        px = project(corners)
        x0, x1 = int(np.floor(px[:, 0].min())) - 3, int(np.ceil(px[:, 0].max())) + 3
        y0, y1 = int(np.floor(px[:, 1].min())) - 3, int(np.ceil(px[:, 1].max())) + 3
        emitter[max(y0, 0):min(y1 + 1, h), max(x0, 0):min(x1 + 1, w)] = True
    floor = ~emitter & (lum_ref >= 0.02) & (lum_ref <= 0.95)
    floor[:4] = floor[-4:] = False
    floor[:, :4] = floor[:, -4:] = False

    report = {"projection": projection, "pixels": {"floor": int(floor.sum()), "emitter": int(emitter.sum()),
                                                   "total": int(h * w)},
              "frames_are_samples": "f_N.png holds N accumulated samples (preload frames 0-3; sample 1 at frame 1)"}
    if early is not None:
        replay = {}
        for mode in ("combined", "uniform", "bvh", "reference"):
            frames = [f for f in range(5, 101, 5)
                      if (output / mode / "frames" / f"f_{f}.png").exists()
                      and (early / mode / "frames" / f"f_{f}.png").exists()]
            identical = [(output / mode / "frames" / f"f_{f}.png").read_bytes()
                         == (early / mode / "frames" / f"f_{f}.png").read_bytes() for f in frames]
            replay[mode] = {"frames_compared": len(frames), "byte_identical": int(sum(identical))}
        report["deterministic_replay"] = replay

    def mean_delta(img, mask):
        return float(100 * (img[mask].mean() / reference[mask].mean() - 1))

    report["converged_means"] = {}
    for mode in ("combined", "uniform", "bvh", "reference"):
        img = image(mode, 2050)
        report["converged_means"][mode] = {
            "floor_linear": float(img[floor].mean()), "floor_delta_percent": mean_delta(img, floor),
            "whole_linear": float(img.mean()), "whole_delta_percent": mean_delta(img, np.ones_like(floor))}

    report["bias_64spp"] = {}
    for mode in ("combined", "uniform", "bvh"):
        img = image(mode, 64)
        own = image(mode, 2050)
        report["bias_64spp"][mode] = {
            "floor_linear": float(img[floor].mean()),
            "floor_delta_vs_reference_percent": mean_delta(img, floor),
            "whole_delta_vs_reference_percent": mean_delta(img, np.ones_like(floor)),
            "floor_delta_vs_own_2050_percent": float(100 * (img[floor].mean() / own[floor].mean() - 1))}

    report["error_variance"] = {}
    for spp in (5, 10, 20, 64, 200, 500, 2050):
        row = {}
        for mode in ("combined", "uniform", "bvh"):
            diff = image(mode, spp) - reference
            row[mode] = float((diff[floor] ** 2).mean())
        row["combined_vs_uniform_percent"] = 100 * (row["combined"] / row["uniform"] - 1)
        row["combined_vs_bvh_percent"] = 100 * (row["combined"] / row["bvh"] - 1)
        report["error_variance"][spp] = row

    # Per-cube floor pools: a 4 px disk under each cube centre, minus emitter pixels.
    yy, xx = np.mgrid[0:h, 0:w]
    pools = []
    for c in cubes:
        foot = c["center"] * [1, 1, 0]
        cx, cy = project(foot)[0]
        disk = ((xx - cx) ** 2 + (yy - cy) ** 2 <= 4.0 ** 2) & ~emitter
        if disk.sum() < 8:
            continue
        # Analytic direct radiance at the floor points the pool's own pixels look at.
        ys_, xs_ = np.nonzero(disk)
        pools.append({"name": c["name"], "intensity": c["intensity"], "mask": disk,
                      "analytic": float(analytic_floor_radiance(unproject(xs_, ys_), cubes).mean())})
    report["pools"] = {"count": len(pools)}
    if pools:
        intensity = np.array([p["intensity"] for p in pools])
        analytic = np.array([p["analytic"] for p in pools])

        def pool_means(img):
            return np.array([(img @ LUMA)[p["mask"]].mean() for p in pools])
        ref_pool = pool_means(reference)
        rows = {}
        for label, img in (("combined_64", image("combined", 64)), ("uniform_64", image("uniform", 64)),
                           ("bvh_64", image("bvh", 64)), ("combined_2050", image("combined", 2050)),
                           ("uniform_2050", image("uniform", 2050)), ("bvh_2050", image("bvh", 2050))):
            ratio = pool_means(img) / ref_pool
            rows[label] = {"ratio_mean": float(ratio.mean()), "ratio_min": float(ratio.min()),
                           "ratio_max": float(ratio.max()), "ratio_std": float(ratio.std()),
                           "pools_outside_5pct": int((np.abs(ratio - 1) > 0.05).sum()),
                           "pools_outside_10pct": int((np.abs(ratio - 1) > 0.10).sum())}
        analytic_ratio = ref_pool / analytic
        report["pools"].update({
            "ratios_vs_reference": rows,
            "reference_vs_analytic": {"ratio_mean": float(analytic_ratio.mean()),
                                      "ratio_min": float(analytic_ratio.min()),
                                      "ratio_max": float(analytic_ratio.max()),
                                      "spearman": rank_correlation(ref_pool, analytic)},
            "combined_2050_vs_analytic_ratio_mean": float((pool_means(image("combined", 2050)) / analytic).mean()),
            "pool_vs_intensity": {"spearman_reference": rank_correlation(ref_pool, intensity),
                                  "spearman_combined_64": rank_correlation(pool_means(image("combined", 64)), intensity),
                                  "spearman_analytic": rank_correlation(analytic, intensity)},
            "per_level": {},
        })
        for level in sorted(set(intensity)):
            sel = intensity == level
            report["pools"]["per_level"][f"{level:g}"] = {
                "pools": int(sel.sum()), "reference_pool_mean": float(ref_pool[sel].mean()),
                "analytic_mean": float(analytic[sel].mean()),
                "combined_64_over_reference": float((pool_means(image("combined", 64))[sel] / ref_pool[sel]).mean())}

    # Diagnostics captured after the histogram change, if present.
    report["histogram"] = {}
    for mode in ("histogram", "histogram-uniform"):
        log = output / mode / "run.log"
        if log.exists():
            report["histogram"][mode] = histogram_metrics(log)
    report["profile"] = {}
    for mode in ("profile-combined", "profile-uniform"):
        manifest = output / mode / "manifest.json"
        if manifest.exists():
            gpu = json.loads(manifest.read_text()).get("gpu_ms", {})
            report["profile"][mode] = {k: gpu[k] for k in sorted(gpu) if k.startswith(("ReSTIR", "PipelinePath", "_"))}
    if "profile-combined" in report["profile"] and "profile-uniform" in report["profile"]:
        c, u = report["profile"]["profile-combined"], report["profile"]["profile-uniform"]
        report["profile"]["traversal_cost_ms"] = {k: c[k] - u[k] for k in c if k in u}

    baseline = Path("E:/UzmiEngine/f005-after/frames/f_95.png")
    default = output / "default/frames/f_95.png"
    if default.exists():
        report["default"] = {
            "byte_identical": baseline.read_bytes() == default.read_bytes(),
            "max_code_delta": float(abs(np.asarray(Image.open(baseline), dtype=float) -
                                        np.asarray(Image.open(default), dtype=float)).max())}
    if after is not None:
        report["identity_after_change"] = {}
        for frame in (64, 2050):
            a, b = output / "combined/frames" / f"f_{frame}.png", after / "combined/frames" / f"f_{frame}.png"
            if a.exists() and b.exists():
                report["identity_after_change"][frame] = {
                    "byte_identical": a.read_bytes() == b.read_bytes(),
                    "max_code_delta": float(abs(np.asarray(Image.open(a), dtype=float) -
                                                np.asarray(Image.open(b), dtype=float)).max())}

    report["logs"] = {}
    for mode in sorted(p.name for p in output.iterdir() if (p / "run.log").exists()):
        text = (output / mode / "run.log").read_text(encoding="utf-8", errors="replace")
        build = [ln for ln in text.splitlines() if "LightBVH: SAOH/cone build" in ln and "0 lights" not in ln]
        report["logs"][mode] = {
            "compile_failed": "compile failed" in text,
            "device_removed": "DEVICE_REMOVED" in text or "0x887A0005" in text,
            "integrated_dispatch": "initial BVH-sourced RIS" in text,
            "bvh_build": build[-1].split("] ")[-1] if build else None,
        }

    # Montages (sRGB-encoded for viewing; the metrics above stay linear).
    montage([[("reference: uniform NEE x32, 2050 spp", reference),
              ("ReSTIR DI x light BVH, 64 spp", image("combined", 64)),
              ("ReSTIR DI uniform candidates, 64 spp", image("uniform", 64))],
             [("light-BVH NEE only, 64 spp", image("bvh", 64)),
              ("ReSTIR DI x light BVH, 2050 spp", image("combined", 2050)),
              ("|combined 64 - reference| x8", np.abs(image("combined", 64) - reference) * 8)]],
            output / "montage_multi_emitter.png")
    montage([[(f"combined {s} spp", image("combined", s)) for s in (5, 20, 64)],
             [(f"uniform-candidate ReSTIR {s} spp", image("uniform", s)) for s in (5, 20, 64)]],
            output / "montage_convergence.png")
    print(json.dumps(report, indent=2))


def _parse(argv):
    opts = {"frames": None, "shots": None, "after": None, "early": None}
    rest = []
    i = 0
    while i < len(argv):
        if argv[i] == "--frames":
            opts["frames"] = int(argv[i + 1]); i += 2
        elif argv[i] == "--shots":
            opts["shots"] = argv[i + 1]; i += 2
        elif argv[i] == "--after":
            opts["after"] = Path(argv[i + 1]); i += 2
        elif argv[i] == "--early":
            opts["early"] = Path(argv[i + 1]); i += 2
        else:
            rest.append(argv[i]); i += 1
    return rest, opts


if __name__ == "__main__":
    rest, opts = _parse(sys.argv[1:])
    action, output = rest[0], Path(rest[1])
    if action == "capture":
        capture(output, rest[2], opts["frames"], opts["shots"])
    elif action == "analyze":
        analyze(output, opts["after"], opts["early"])
    else:
        raise SystemExit(f"Unknown action: {action}")
