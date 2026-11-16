"""phase2_aces-hill-specular-occlusion 2.2.1-2.2.3: GPU evidence for the shipped Hill ACES curve
and for specular occlusion.

    python verify.py list
    python verify.py prepare --out DIR
    python verify.py capture --out DIR (--run NAME | --all)
    python verify.py analyze --out DIR
    python verify.py montage --out DIR

Run from the repository root: the mesh-SDF cache is `cache/meshsdf` under the working directory.
Every engine run is hidden, bounded (default 180 s), refuses to overwrite and captures one
screenshot plus, of the same frame, a region dump:

* tone and swatch runs (TAA on, bloom off, everything else shipped): the TAA `output` record is the
  exact linear HDR the tonemap pass reads and the file header carries that frame's exposure, so
  `analyze` re-applies tonemap_ps.hlsl offline and compares it with the screenshot;
* spec-occlusion runs (`components`): the deferred pass's `specular_indirect` is the split-sum
  specular after the occlusion factor. OFF turns the material AO map and GTAO off, which makes the
  AO input 1 and the factor `ComputeSpecularOcclusion(NoV, 1, r) = 1` exactly.
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
from flicker import runner, regiondump  # noqa: E402
import aces_reference as aces  # noqa: E402

FRAME = 120                     # absolute frame index (pre-load of these scenes is ~5 frames)
HELMET_PROJECT = REPO / "sample" / "projects" / "damaged_helmet.uzproject"
HELMET_SCENE = REPO / "sample" / "content" / "scenes" / "damaged_helmet.uzscene"
HELMET_TARGET = (0, 0, 105)
TONE_CAMERAS = {"quarter": (260, -273, 165), "profile": (0, -377, 165), "visor": (377, 0, 165)}
TONE_MODES = {"default": {}, "hill": {"r.TonemapOperator": 1}, "narkowicz": {"r.TonemapOperator": 2}}
TONE_CVARS = {"r.TAA": 1, "r.Bloom": 0}

# Orange/red emissive swatches beside the helmet (its own emissive map is cyan/green/yellow).
SWATCH_HUES = {"red": (1.0, 0.08, 0.03), "orange": (1.0, 0.35, 0.05), "amber": (1.0, 0.6, 0.08)}
SWATCH_GAINS = (0.5, 2.0, 8.0, 32.0, 128.0)
SWATCH_RADIUS = 11.0
# The CLI reads a value that starts with '-' as a new flag (`--camera-pos -120,...` is ignored), so
# every camera position and target used here starts with a non-negative coordinate; capture_one
# also checks the pose the engine reports.
SWATCH_CAMERA = ((120, -470, 125), (120, 0, 110))


def swatch_centers():
    out = {}
    for row, hue in enumerate(SWATCH_HUES):
        for col, gain in enumerate(SWATCH_GAINS):
            out[(hue, gain)] = (140.0 + 30.0 * col, -80.0, 150.0 - 40.0 * row)
    return out


SPEC_WIDTH, SPEC_HEIGHT = 1280, 720
SPEC_COMMON = {"r.TAA": 0, "r.SSR": 0, "r.GI.Reflections": 0}
SPEC_OFF = {"r.Material.AOMap": 0, "r.GTAO": 0}
SPEC_SCENES = {
    # Shipped helmet: the AO input is the baked ORM occlusion (GTAO is off by default).
    "helmet": {"cameras": {"profile": ((0, -250, 140), (0, 0, 110)), "quarter": ((170, -185, 150), (0, 0, 105))},
               "on": {}},
    # Sphere pair on a ground plane: the contact AO comes from GTAO.
    "sphere": {"cameras": {"low": ((40, -330, 45), (30, 0, 25)), "high": ((20, -260, 210), (30, 0, 10))},
               "on": {"r.GTAO": 1}},
}
SPHERES = {"big": ((0.0, 0.0, 60.0), 60.0), "small": ((98.0, 0.0, 40.0), 40.0)}


def runs():
    out = {}
    for cam, pos in TONE_CAMERAS.items():
        for mode, cv in TONE_MODES.items():
            out[f"tone-{cam}-{mode}"] = dict(kind="tone", project="helmet", camera=(pos, HELMET_TARGET),
                                             cvars={**TONE_CVARS, **cv}, size=(1920, 1080), fields="output")
    for mode in ("hill", "narkowicz"):
        out[f"swatch-{mode}"] = dict(kind="swatch", project="swatches", camera=SWATCH_CAMERA,
                                     cvars={**TONE_CVARS, **TONE_MODES[mode]}, size=(1920, 1080), fields="motion")
    for scene, spec in SPEC_SCENES.items():
        for cam, view in spec["cameras"].items():
            for state in ("on", "off"):
                cv = {**SPEC_COMMON, **(spec["on"] if state == "on" else SPEC_OFF)}
                out[f"specocc-{scene}-{cam}-{state}"] = dict(kind="specocc", project=scene, camera=view, cvars=cv,
                                                             size=(SPEC_WIDTH, SPEC_HEIGHT), fields="components")
    return out


def save_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


# ── fixtures ──────────────────────────────────────────────────────────────────────────────────────

def _copy_assets(dest, names):
    for rel in names:
        (dest / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / "sample" / "content" / rel, dest / rel)


def _material(base, rough, metal=0.0, emissive=None, gain=0.0):
    m = {"baseColorR": base[0], "baseColorG": base[1], "baseColorB": base[2],
         "roughnessMultiplier": rough, "metallicMultiplier": metal}
    if emissive:
        # gbuffer_ps.hlsl emits only through a bound emissive map (else the black fallback), scaled by
        # emissiveColor * emissiveIntensity, and scene_sync.cpp loads a material's maps only when it
        # has an albedo path: the swatches carry white albedo (tinted by baseColor) and emissive maps.
        m.update({"albedo": "textures/white.png", "emissive": "textures/white.png",
                  "emissiveColorR": emissive[0], "emissiveColorG": emissive[1],
                  "emissiveColorB": emissive[2], "emissiveIntensity": gain})
    return m


def _entity(name, position, scale, mesh, material):
    return {"name": name, "components": {"transform": {"position": list(position), "scale": list(scale)},
                                         "mesh": {"asset": mesh}, "material": material}}


def prepare(args):
    fixtures = args.out / "fixtures"
    fixtures.mkdir(parents=True, exist_ok=True)
    base_project = json.loads(HELMET_PROJECT.read_text(encoding="utf-8"))
    helmet = json.loads(HELMET_SCENE.read_text(encoding="utf-8"))

    # swatches: the shipped helmet scene (minus the fly-camera script) plus emissive spheres.
    sw = fixtures / "swatches"
    _copy_assets(sw, ["meshes/DamagedHelmetHQ.uzmesh", "meshes/cube.uzmesh", "meshes/sphere.uzmesh",
                      "textures/DH_HQ_BaseColor.jpg", "textures/DH_HQ_Normal.jpg", "textures/DH_HQ_ORM.png",
                      "textures/DH_HQ_Emissive.jpg", "textures/checker.png"])
    Image.new("RGB", (8, 8), (255, 255, 255)).save(sw / "textures" / "white.png")
    scene = copy.deepcopy(helmet)
    scene["name"] = "aces-visual swatches"
    scene["entities"] = [e for e in scene["entities"] if e["name"] != "Controller"]
    for (hue, gain), center in swatch_centers().items():
        scene["entities"].append(_entity(f"Swatch_{hue}_{gain:g}", center, [SWATCH_RADIUS] * 3, "meshes/sphere.uzmesh",
                                         _material((0.02, 0.02, 0.02), 0.9, 0.0, SWATCH_HUES[hue], gain)))
    save_json(sw / "swatches.uzscene", scene)
    save_json(sw / "swatches.uzproject", {**base_project, "name": "aces-visual swatches", "content_root": ".",
                                          "default_scene": "swatches.uzscene"})

    # sphere: two glossy dielectric spheres resting on a glossy dielectric ground plane, sun + sky.
    sp = fixtures / "sphere"
    _copy_assets(sp, ["meshes/cube.uzmesh", "meshes/sphere.uzmesh"])
    keep = {"Sun", "MainCamera", "SkyAtmosphere", "SkyLight", "HeightFog"}
    scene = {"name": "aces-visual specular occlusion", "version": "1.0",
             "entities": [copy.deepcopy(e) for e in helmet["entities"] if e["name"] in keep]}
    scene["entities"].append(_entity("Ground", (0, 0, -4), (600, 600, 2), "meshes/cube.uzmesh",
                                     _material((0.5, 0.5, 0.5), 0.35)))
    for name, (center, radius) in SPHERES.items():
        scene["entities"].append(_entity(f"Sphere_{name}", center, (radius,) * 3, "meshes/sphere.uzmesh",
                                         _material((0.7, 0.7, 0.7), 0.3)))
    save_json(sp / "sphere.uzscene", scene)
    save_json(sp / "sphere.uzproject", {**base_project, "name": "aces-visual sphere", "content_root": ".",
                                        "default_scene": "sphere.uzscene"})
    print(fixtures)


def project_path(out, name):
    if name == "helmet":
        return HELMET_PROJECT
    return out / "fixtures" / name / f"{name}.uzproject"


# ── capture ───────────────────────────────────────────────────────────────────────────────────────

def capture_one(args, name, spec):
    dest = args.out / name
    if dest.exists():
        raise SystemExit(f"Refusing overwrite: {dest}")
    project = project_path(args.out, spec["project"])
    if not project.is_file():
        raise SystemExit(f"Missing fixture {project}; run prepare first")
    (pos, look), (w, h) = spec["camera"], spec["size"]
    try:
        result = runner.run_capture(
            str(dest), str(project), FRAME + 6,
            regions=[("frame", 0, 0, w, h)], region_fields=spec["fields"],
            region_first=FRAME, region_last=FRAME, screenshot_frames=str(FRAME),
            timing_first=32, timeout=args.timeout, clean=False, cvars=spec["cvars"],
            extra_args=["--hidden", "--width", str(w), "--height", str(h),
                        "--camera-pos", ",".join(map(str, pos)), "--camera-look", ",".join(map(str, look))])
    except subprocess.TimeoutExpired as e:
        raise SystemExit(f"{name}: engine run exceeded {args.timeout} s and was killed ({e})")
    if result["exit_code"] != 0:
        raise SystemExit(f"{name}: engine exit {result['exit_code']}, log {result['log']}")
    got = result["manifest"]["camera"]["position"]
    if max(abs(a - b) for a, b in zip(got, pos)) > 0.5:
        raise SystemExit(f"{name}: engine rendered from {got}, not the requested camera {pos}")
    load_frame(dest, spec)   # rejects a missing, stale or mis-stamped dump before anything is analysed
    shot = dest / "frames" / f"f_{FRAME}.png"
    if not shot.is_file():
        raise SystemExit(f"{name}: no screenshot of frame {FRAME}")
    save_json(dest / "capture.json", {"run": name, **{k: v for k, v in spec.items() if k != "camera"},
                                      "camera_pos": spec["camera"][0], "camera_look": spec["camera"][1],
                                      "frame": FRAME, "exit_code": result["exit_code"],
                                      "command": [str(c) for c in result["command"]]})
    print(dest)


def capture(args):
    runner.EXE = str(args.exe.resolve())
    table = runs()
    names = [n for n in table if not (args.out / n).exists()] if args.all else [args.run]
    for name in names:
        if name not in table:
            raise SystemExit(f"unknown run {name}; see `verify.py list`")
        capture_one(args, name, table[name])


# ── analysis helpers ──────────────────────────────────────────────────────────────────────────────

def load_frame(dest, spec):
    frames = regiondump.load_sequence(str(dest / "region"), FRAME, FRAME,
                                      allow_taa_missing=spec["cvars"].get("r.TAA", 0) == 0)
    return frames[0]


def load_run(out, name):
    spec = runs()[name]
    dest = out / name
    fr = load_frame(dest, spec)
    shot = runner.png_array(str(dest / "frames" / f"f_{FRAME}.png"))
    manifest = json.loads((dest / "manifest.json").read_text(encoding="utf-8"))
    return spec, fr, shot, manifest


def encode8(display):
    return np.round(np.clip(display, 0.0, 1.0) * 255.0).astype(np.uint8)


def compare8(a, b):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16))
    return {"psnr_db": round(aces.psnr(a, b, 255.0), 3), "max_abs_code": int(d.max()),
            "exact_fraction": round(float((d == 0).all(-1).mean()), 6),
            "within_1_fraction": round(float((d <= 1).all(-1).mean()), 6)}


def wrap180(deg):
    return (np.asarray(deg) + 180.0) % 360.0 - 180.0


def view_vectors(fr, rect="frame"):
    world = fr.world_positions(rect)
    v = fr.camera_pos.astype(np.float64) - world
    return world, v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-12)


def spec_occlusion(nov, ao, rough):
    """lighting/ibl.hlsli ComputeSpecularOcclusion (Lagarde 2014)."""
    return np.clip(np.power(nov + ao, np.exp2(-16.0 * rough - 1.0)) - 1.0 + ao, 0.0, 1.0)


LUMA = np.array(regiondump.REC709)


# ── analysis ──────────────────────────────────────────────────────────────────────────────────────

def analyze_tone(out):
    cams, hill_aces = {}, []
    for cam in TONE_CAMERAS:
        loaded = {m: load_run(out, f"tone-{cam}-{m}") for m in TONE_MODES}
        ref = loaded["default"][1]
        hdr = ref.rect("frame").channel("output")[..., :3].astype(np.float64)
        x_in = aces.tonemap_input(hdr, ref.exposure)
        entry = {"exposure": float(ref.exposure), "modes": {}}
        for mode, (_, fr, shot, manifest) in loaded.items():
            # Each run is checked against its own frame: auto exposure follows wall-clock time, so
            # the three runs of a camera need not share an exposure (and TAA weights by it).
            mode_hdr = fr.rect("frame").channel("output")[..., :3].astype(np.float64)
            x = aces.tonemap_input(mode_hdr, fr.exposure)
            entry["modes"][mode] = {
                "effective_r.TonemapOperator": manifest["cvars"].get("r.TonemapOperator"),
                "exposure": float(fr.exposure),
                "hdr_rel_rms_diff_vs_default_run": float(np.sqrt(np.mean((mode_hdr - hdr) ** 2) / np.mean(hdr ** 2))),
                "screenshot_vs_offline_hill": compare8(shot, encode8(aces.linear_to_srgb(aces.hill(x)))),
                "screenshot_vs_offline_narkowicz": compare8(shot, encode8(aces.linear_to_srgb(aces.narkowicz(x)))),
            }
        hill_disp = aces.linear_to_srgb(aces.hill(x_in))
        full = aces.aces_full_srgb(x_in)
        nark = aces.linear_to_srgb(aces.narkowicz(x_in))
        falc = aces.linear_to_srgb(aces.falcor_aces(x_in))
        luma_in = x_in @ LUMA
        entry["psnr_vs_aces_full"] = {
            "hill_float_db": round(aces.psnr(hill_disp, full), 3),
            "hill_8bit_db": round(aces.psnr(encode8(hill_disp), encode8(full), 255.0), 3),
            "narkowicz_float_db": round(aces.psnr(nark, full), 3),
            "falcor_aces_float_db": round(aces.psnr(falc, full), 3),
        }
        entry["psnr_hill_vs_falcor_aces_float_db"] = round(aces.psnr(hill_disp, falc), 3)
        entry["hill_vs_aces_full_abs_error"] = {
            "mean": float(np.abs(hill_disp - full).mean()), "p99": float(np.quantile(np.abs(hill_disp - full), 0.99)),
            "max": float(np.abs(hill_disp - full).max())}
        entry["curve_input_luminance_quantiles"] = {
            q: float(np.quantile(luma_in, float(q))) for q in ("0.01", "0.5", "0.99", "0.999")}
        mean_luma = {m: float((aces.srgb_to_linear(loaded[m][2] / 255.0) @ LUMA).mean()) for m in ("hill", "narkowicz")}
        entry["mean_display_luminance"] = mean_luma
        cams[cam] = entry
        hill_aces.append(entry["psnr_vs_aces_full"]["hill_float_db"])
    return {"cameras": cams, "psnr_hill_vs_aces_full_float_db": {
        "per_camera": dict(zip(TONE_CAMERAS, hill_aces)), "min": min(hill_aces), "mean": float(np.mean(hill_aces))}}


def swatch_masks(fr):
    world, v = view_vectors(fr)
    normal = fr.rect("frame").channel("lighting_normal")[..., :3].astype(np.float64)
    facing = (normal * v).sum(-1) > 0.6
    masks = {}
    for key, center in swatch_centers().items():
        dist = np.linalg.norm(world - np.array(center), axis=-1)
        masks[key] = (dist < SWATCH_RADIUS * 1.05) & facing & fr.rect("frame").geometry_mask()
    return masks


def analyze_swatches(out):
    loaded = {m: load_run(out, f"swatch-{m}") for m in ("hill", "narkowicz")}
    ref = loaded["hill"][1]
    hdr = ref.rect("frame").channel("output")[..., :3].astype(np.float64)
    x_in = aces.tonemap_input(hdr, ref.exposure)
    masks = swatch_masks(ref)
    out_lin = {m: aces.srgb_to_linear(shot / 255.0) for m, (_, _, shot, _) in loaded.items()}
    rows = []
    for (hue, gain), mask in masks.items():
        n = int(mask.sum())
        if n < 20:
            rows.append({"swatch": hue, "gain": gain, "pixels": n, "error": "fewer than 20 masked pixels"})
            continue
        mean_in = x_in[mask].mean(0)
        h_in = float(aces.hue_degrees(mean_in[None])[0])
        row = {"swatch": hue, "gain": gain, "pixels": n, "curve_input_mean_rgb": mean_in.round(4).tolist(),
               "curve_input_peak_channel": float(mean_in.max()), "input_hue_deg": round(h_in, 2)}
        for m in ("hill", "narkowicz"):
            mean_out = out_lin[m][mask].mean(0)
            h_out = float(aces.hue_degrees(mean_out[None])[0])
            row[f"{m}_hue_deg"] = round(h_out, 2)
            row[f"{m}_hue_shift_deg"] = round(float(wrap180(h_out - h_in)), 2)
            row[f"{m}_display_rgb"] = mean_out.round(4).tolist()
        rows.append(row)
    hdr_diff = float(np.abs(loaded["narkowicz"][1].rect("frame").channel("output")[..., :3] - hdr).max())
    # The helmet views hold few over-exposed pixels; this frame adds saturated emissives up to ~28x.
    hill_d, full_d = aces.linear_to_srgb(aces.hill(x_in)), aces.aces_full_srgb(x_in)
    union = np.any(np.stack(list(masks.values())), axis=0)
    stress = {"full_frame_float_db": round(aces.psnr(hill_d, full_d), 3),
              "swatch_pixels_float_db": round(aces.psnr(hill_d[union], full_d[union]), 3),
              "swatch_pixels": int(union.sum())}
    valid = [r for r in rows if "error" not in r]
    summary = {m: {"mean_abs_hue_shift_deg": round(float(np.mean([abs(r[f"{m}_hue_shift_deg"]) for r in valid])), 2),
                   "max_abs_hue_shift_deg": round(float(np.max([abs(r[f"{m}_hue_shift_deg"]) for r in valid])), 2)}
               for m in ("hill", "narkowicz")}
    return {"exposure": float(ref.exposure), "hdr_max_abs_diff_between_runs": hdr_diff,
            "psnr_hill_vs_aces_full": stress, "swatches": rows, "summary": summary}


def analyze_specocc(out):
    result = {}
    for scene, spec in SPEC_SCENES.items():
        for cam in spec["cameras"]:
            _, on, _, _ = load_run(out, f"specocc-{scene}-{cam}-on")
            _, off, _, _ = load_run(out, f"specocc-{scene}-{cam}-off")
            r_on, r_off = on.rect("frame"), off.rect("frame")
            s_on = r_on.channel("specular_indirect")[..., :3].astype(np.float64) @ LUMA
            s_off = r_off.channel("specular_indirect")[..., :3].astype(np.float64) @ LUMA
            metallic = r_on.channel("specular_indirect")[..., 3]
            ao_on = r_on.channel("diffuse_indirect")[..., 3].astype(np.float64)
            ao_off = r_off.channel("diffuse_indirect")[..., 3].astype(np.float64)
            rough = r_on.channel("lighting_reflection")[..., 3].astype(np.float64)
            normal = r_on.channel("lighting_normal")[..., :3].astype(np.float64)
            world, v = view_vectors(on)
            nov = np.maximum((normal * v).sum(-1), 1e-4)
            geo = r_on.geometry_mask() & r_off.geometry_mask()
            dielectric = geo & (metallic <= 0.5)
            lit = dielectric & (s_off > 1e-4)
            predicted = spec_occlusion(nov, ao_on, rough)
            ratio = np.where(lit, s_on / np.maximum(s_off, 1e-12), np.nan)
            occluded = lit & (ao_on < 0.9)
            err = np.abs(ratio - predicted)
            entry = {
                "exposure_on_off": [float(on.exposure), float(off.exposure)],
                "pixels": {"geometry": int(geo.sum()), "dielectric_lit": int(lit.sum()),
                           "dielectric_lit_ao_below_0.9": int(occluded.sum())},
                "off_run_ao_range": [float(ao_off[geo].min()), float(ao_off[geo].max())],
                "metal_ratio_max_abs_dev_from_1": float(np.nanmax(np.abs(
                    np.where(geo & (metallic > 0.5) & (s_off > 1e-4), s_on / np.maximum(s_off, 1e-12), 1.0) - 1.0))),
                "unoccluded_ratio_median": float(np.nanmedian(ratio[lit & (ao_on >= 0.999)])) if (lit & (ao_on >= 0.999)).any() else None,
                "occluded": {
                    "mean_ao": float(ao_on[occluded].mean()) if occluded.any() else None,
                    "mean_ratio_on_over_off": float(np.nanmean(ratio[occluded])) if occluded.any() else None,
                    "mean_predicted_factor": float(predicted[occluded].mean()) if occluded.any() else None,
                    "ratio_vs_formula_abs_error_median": float(np.nanmedian(err[occluded])) if occluded.any() else None,
                    "ratio_vs_formula_abs_error_p95": float(np.nanquantile(err[occluded], 0.95)) if occluded.any() else None,
                    "specular_sum_on_over_off": float(s_on[occluded].sum() / max(s_off[occluded].sum(), 1e-12)) if occluded.any() else None,
                },
            }
            if scene == "sphere":
                # Contact bands: ground and sphere points within 0.5 R of each sphere's contact point.
                bands = {}
                for name, (center, radius) in SPHERES.items():
                    contact = np.array([center[0], center[1], 0.0])
                    band = lit & (np.linalg.norm(world - contact, axis=-1) < 0.5 * radius)
                    if band.any():
                        bands[name] = {"pixels": int(band.sum()), "mean_ao": float(ao_on[band].mean()),
                                       "mean_ratio_on_over_off": float(np.nanmean(ratio[band])),
                                       "mean_predicted_factor": float(predicted[band].mean()),
                                       "specular_sum_on_over_off": float(s_on[band].sum() / max(s_off[band].sum(), 1e-12))}
                entry["contact_bands"] = bands
            result[f"{scene}-{cam}"] = entry
    return result


def analyze(args):
    report = {"reference_selfcheck": aces.selfcheck(), "frame": FRAME}
    report["tone"] = analyze_tone(args.out)
    report["swatches"] = analyze_swatches(args.out)
    report["specocc"] = analyze_specocc(args.out)
    save_json(args.out / "analysis.json", report)
    print(json.dumps({"psnr": report["tone"]["psnr_hill_vs_aces_full_float_db"],
                      "hue": report["swatches"]["summary"]}, indent=2))


# ── montages ──────────────────────────────────────────────────────────────────────────────────────
# One visual vocabulary for every montage: fixed tile size, one label bar style, one gap.
TILE = (640, 360)
GAP = 6
LABEL_H = 26
LABEL_BG = (24, 24, 28)
LABEL_FG = (232, 232, 232)
CANVAS_BG = (12, 12, 14)


def _font():
    try:
        return ImageFont.truetype("arial.ttf", 16)
    except OSError:
        return ImageFont.load_default()


def tile(img, label):
    im = Image.fromarray(img).convert("RGB").resize(TILE, Image.LANCZOS)
    out = Image.new("RGB", (TILE[0], TILE[1] + LABEL_H), LABEL_BG)
    out.paste(im, (0, LABEL_H))
    ImageDraw.Draw(out).text((8, 4), label, fill=LABEL_FG, font=_font())
    return out


def grid(rows, path):
    cols = max(len(r) for r in rows)
    w = cols * TILE[0] + (cols + 1) * GAP
    h = len(rows) * (TILE[1] + LABEL_H) + (len(rows) + 1) * GAP
    canvas = Image.new("RGB", (w, h), CANVAS_BG)
    for y, row in enumerate(rows):
        for x, t in enumerate(row):
            canvas.paste(t, (GAP + x * (TILE[0] + GAP), GAP + y * (TILE[1] + LABEL_H + GAP)))
    canvas.save(path)
    print(path)


def heat(values, vmax):
    """Black -> orange -> white ramp for an error map; `values` already non-negative."""
    t = np.clip(np.asarray(values, np.float64) / vmax, 0.0, 1.0)
    rgb = np.stack([np.clip(t * 2.0, 0, 1), np.clip(t * 2.0 - 0.6, 0, 1), np.clip(t * 2.0 - 1.0, 0, 1)], axis=-1)
    return (rgb * 255).astype(np.uint8)


def crop_169(mask, margin):
    """Row/column slices of a crop with the tile's 16:9 aspect around a mask (so circles stay round)."""
    ys, xs = np.nonzero(mask)
    cy, cx = (ys.min() + ys.max()) / 2.0, (xs.min() + xs.max()) / 2.0
    half_h = max((ys.max() - ys.min()) / 2.0 + margin, ((xs.max() - xs.min()) / 2.0 + margin) * TILE[1] / TILE[0])
    half_w = half_h * TILE[0] / TILE[1]
    h, w = mask.shape
    return (slice(max(int(round(cy - half_h)), 0), min(int(round(cy + half_h)), h)),
            slice(max(int(round(cx - half_w)), 0), min(int(round(cx + half_w)), w)))


def montage(args):
    report = json.loads((args.out / "analysis.json").read_text(encoding="utf-8"))
    tone = report["tone"]["cameras"]
    rows_tone, rows_psnr = [], []
    for cam in TONE_CAMERAS:
        hill_shot = load_run(args.out, f"tone-{cam}-default")
        nark_shot = load_run(args.out, f"tone-{cam}-narkowicz")[2]
        fr = hill_shot[1]
        diff = np.abs(hill_shot[2].astype(np.int16) - nark_shot.astype(np.int16)).max(-1)
        rows_tone.append([tile(hill_shot[2], f"{cam}: default (r.TonemapOperator=1, Hill)"),
                          tile(nark_shot, f"{cam}: r.TonemapOperator=2 (Narkowicz)"),
                          tile(heat(diff, 64.0), f"{cam}: |Hill - Narkowicz| (8-bit, 64 = white)")])
        x_in = aces.tonemap_input(fr.rect("frame").channel("output")[..., :3].astype(np.float64), fr.exposure)
        hill_d, full_d = aces.linear_to_srgb(aces.hill(x_in)), aces.aces_full_srgb(x_in)
        p = tone[cam]["psnr_vs_aces_full"]["hill_float_db"]
        rows_psnr.append([tile(encode8(hill_d), f"{cam}: Hill fit (shipped)"),
                          tile(encode8(full_d), f"{cam}: ACES 1.3 RRT + sRGB ODT (CTL port)"),
                          tile(heat(np.abs(hill_d - full_d).max(-1), 0.05), f"{cam}: |diff| (0.05 = white), PSNR {p:.2f} dB")])
    grid(rows_tone, args.out / "montage-tonemap-hill-vs-narkowicz.png")
    grid(rows_psnr, args.out / "montage-hill-vs-aces-full.png")

    # swatches: crop around the emissive grid
    _, fr, hill_s, _ = load_run(args.out, "swatch-hill")
    nark_s = load_run(args.out, "swatch-narkowicz")[2]
    sy, sx = crop_169(np.any(np.stack(list(swatch_masks(fr).values())), axis=0), 30)
    rows = [[tile(hill_s, "swatches: Hill (shipped)"), tile(nark_s, "swatches: Narkowicz")],
            [tile(hill_s[sy, sx], "Hill: rows red/orange/amber, gain x128 (left) .. x0.5 (right)"),
             tile(nark_s[sy, sx], "Narkowicz: same swatches")]]
    grid(rows, args.out / "montage-emissive-hue-swatches.png")

    rows, contact_rows = [], []
    for scene, spec in SPEC_SCENES.items():
        for cam in spec["cameras"]:
            _, on, shot_on, _ = load_run(args.out, f"specocc-{scene}-{cam}-on")
            _, off, shot_off, _ = load_run(args.out, f"specocc-{scene}-{cam}-off")
            s_on = on.rect("frame").channel("specular_indirect")[..., :3].astype(np.float64)
            s_off = off.rect("frame").channel("specular_indirect")[..., :3].astype(np.float64)
            l_on, l_off = s_on @ LUMA, s_off @ LUMA
            ratio = np.where(l_off > 1e-4, l_on / np.maximum(l_off, 1e-12), 1.0)
            gray = (np.clip(ratio, 0, 1) * 255).astype(np.uint8)
            # One linear display gain for both specular tiles of a row: the OFF run's 99th percentile
            # (over geometry) maps to 0.9, so ON and OFF stay directly comparable.
            geo = on.rect("frame").geometry_mask()
            gain = 0.9 / max(float(np.quantile(l_off[geo], 0.99)), 1e-12)
            spec_on, spec_off = (encode8(aces.linear_to_srgb(np.clip(s * gain, 0.0, 1.0))) for s in (s_on, s_off))
            ratio_img = np.stack([gray] * 3, -1)
            rows.append([tile(shot_on, f"{scene}-{cam}: AO on (shipped)"),
                         tile(shot_off, f"{scene}-{cam}: AO input 1 (map+GTAO off)"),
                         tile(spec_on, f"specular indirect, occlusion on (x{gain:.3g})"),
                         tile(spec_off, f"specular indirect, factor 1 (x{gain:.3g})"),
                         tile(ratio_img, "spec on/off ratio (white 1, black 0)")])
            if scene == "sphere":
                world, _ = view_vectors(on)
                near = np.zeros(gray.shape, bool)
                for center, radius in SPHERES.values():
                    near |= np.linalg.norm(world - np.array([center[0], center[1], 0.0]), axis=-1) < 0.6 * radius
                sy, sx = crop_169(near & geo, 40)
                contact_rows.append([tile(spec_on[sy, sx], f"sphere-{cam} contacts: specular, occlusion on"),
                                     tile(spec_off[sy, sx], "contacts: specular, factor 1 (AO input 1)"),
                                     tile(ratio_img[sy, sx], "contacts: on/off ratio (white 1, black 0)")])
    grid(rows, args.out / "montage-specular-occlusion.png")
    grid(contact_rows, args.out / "montage-specular-occlusion-contacts.png")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    for name in ("prepare", "capture", "analyze", "montage"):
        p = sub.add_parser(name)
        p.add_argument("--out", type=Path, required=True)
        if name == "capture":
            g = p.add_mutually_exclusive_group(required=True)
            g.add_argument("--run")
            g.add_argument("--all", action="store_true")
            p.add_argument("--timeout", type=float, default=180.0)
            p.add_argument("--exe", type=Path, default=Path(runner.EXE))
    args = ap.parse_args()
    if args.cmd == "list":
        print("\n".join(runs()))
        return
    args.out = args.out.resolve()
    {"prepare": prepare, "capture": capture, "analyze": analyze, "montage": montage}[args.cmd](args)


if __name__ == "__main__":
    main()
