"""Bounded Stage 2 HWRT captures (glossy GGX-VNDF rays, radiance-cache indirect at the hit, SSR
denoise). Run each case separately (180 seconds maximum).

Scene (Z-up, cm; `--generate` writes it): the camera at (0, 0, 150) looks along +Y at a vertical
glossy metal wall (roughness 0.5, F0 0.95) at y = 600 standing on a sunlit diffuse floor. A warm
block (x 320-520, y 120-320, z 0-300) stands beside the camera, outside the frustum (62 degrees off
axis; the half-width of the view is 33), so only its reflection in the wall is seen — rays the
screen trace cannot resolve. The sun comes from +X, 60 degrees up: the block's +X and top faces are
lit, its +Y face (the one facing the wall, the one its reflection shows) and its -X face are in the
sun's shadow. The +Y face looks onto the sunlit floor strip between the block and the wall, so it is
lit only by that bounce (and the dim sky): the path tracer's multi-bounce lights it, stage 1 (sun +
sky at the hit) renders it black, stage 2 with the radiance-cache indirect term lights it. The wall
is rougher than the screen trace's cutoff (r.SSR.MaxRoughness 0.35), so with
r.RayTracing.Reflections.MaxRoughness 1 every wall texel is ray traced.

The floor and the wall are 300 cm thick slabs and the probe grid is r.GI.RadianceCache.GridExtent
2000 (probes 267 cm apart, the probe-side reference's QA configuration): the radiance cache traces
the global distance field (cascade 0: 78 cm voxels; mesh SDFs: 32 voxels over the mesh), and a
20 cm slab never comes within its 5 cm hit epsilon — with thin slabs and the 5000 cm default grid
every probe near the block saw only sky (r.GI.RadianceCache.Diagnostic readback, 2026-10-20).

Image regions (640 x 360; screen-right is -X for this camera): the wall fills rows 0-303, the
direct floor the rows below; the floor's reflected horizon is row 180; the block's reflection
(its +Y face, with the -X face as a thin strip on its right) lies at columns ~45-190, rows
~95-262.

Cases: `mirror` (stage 1 logic on the rough wall: mirror rays), `glossy` (VNDF rays, denoised),
`glossy-raw` (VNDF rays, one sample per pixel, no denoise), `glossy-indirect` (VNDF + probe SH +
denoise: the stage 2 pass), `mirror-indirect`, `pt` (the converged DXR path tracer, 4 bounces),
`stage1` (the stage 1 scene + harness settings, to be compared with the committed stage 1 capture)
and `metrics`.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "flicker"))
sys.path.insert(0, str(HERE))
import runner  # noqa: E402
from probe_side_reference import box_mesh_bytes  # noqa: E402

REPO = Path(runner.REPO)
SCENE_NAME = "rt-reflections-glossy"

# Regions (x0, y0, x1, y1), pixels (see the module docstring; screen-right is -X).
ROI_WALL = (0, 0, 640, 300)             # the whole reflector (above the direct floor strip)
ROI_BLOCK = (35, 85, 205, 270)          # the block's reflection (+Y face and -X face) + its edge
ROI_SHADOWED_FACE = (55, 110, 150, 250)   # inside the +Y face's reflection: sun-shadowed, floor-lit
ROI_FLOOR_REFLECTION = (260, 200, 620, 290)  # the lit floor reflected in the wall, away from the block

STAGE1_PROOF = REPO.parent / "rt-reflections-stage1-proof"

# Boxes in local coordinates + the instance translation (every instance translated, as the stage 1
# scene: the TLAS transform convention is part of what the capture exercises).
MESHES = {
    "wall":  (((-600.0, 0.0, 0.0), (600.0, 300.0, 400.0)), (0.0, 600.0, 0.0)),
    "floor": (((-900.0, -900.0, -400.0), (900.0, 900.0, -100.0)), (0.0, 0.0, 100.0)),
    "block": (((-100.0, -100.0, 0.0), (100.0, 100.0, 300.0)), (420.0, 220.0, 0.0)),
}


def mesh_asset(name):
    return f"scripts/rendering/{SCENE_NAME}-{name}.uzmesh"


def scene_json():
    return {
        "name": "HWRT reflections stage 2: glossy wall, off-screen block lit by a floor bounce",
        "version": "1.0",
        "entities": [
            {"name": "Camera", "components": {
                "transform": {"position": [0, 0, 150]},
                "camera": {"fov": 40, "near": 5, "far": 10000, "active": True, "look_at": [0, 600, 150]}}},
            {"name": "Sun", "components": {
                "transform": {"rotation": [-60, 0, 0]},
                "light": {"type": "directional", "color": [1, 1, 1], "intensity": 3,
                          "cast_shadows": True, "light_source_angle": 0.1}}},
            {"name": "Dim environment", "components": {"sky_light": {"intensity": 0.03, "light_color": [1, 1, 1]}}},
            {"name": "Glossy wall", "components": {
                "transform": {"position": list(MESHES["wall"][1])},
                "mesh": {"asset": mesh_asset("wall")},
                "material": {"baseColorR": 0.95, "baseColorG": 0.95, "baseColorB": 0.95,
                             "metallicMultiplier": 1, "roughnessMultiplier": 0.5}}},
            {"name": "Sunlit floor", "components": {
                "transform": {"position": list(MESHES["floor"][1])},
                "mesh": {"asset": mesh_asset("floor")},
                "material": {"baseColorR": 0.7, "baseColorG": 0.7, "baseColorB": 0.7,
                             "metallicMultiplier": 0, "roughnessMultiplier": 0.9}}},
            {"name": "Offscreen block", "components": {
                "transform": {"position": list(MESHES["block"][1])},
                "mesh": {"asset": mesh_asset("block")},
                "material": {"baseColorR": 0.8, "baseColorG": 0.5, "baseColorB": 0.3,
                             "metallicMultiplier": 0, "roughnessMultiplier": 0.8}}},
        ],
    }


def project_json():
    return {
        "name": "HWRT reflections stage 2: glossy + indirect + denoise",
        "version": "1.0",
        "content_root": "../..",
        "default_scene": f"scripts/rendering/{SCENE_NAME}.uzscene",
        "window": {"width": 640, "height": 360},
        "cvars": {
            "r.AutoExposure": 0, "r.Exposure": 1, "r.Bloom": 0,
            "r.TAA": 0, "r.Fog": 0, "r.VolumetricFog": 0,
            "r.TechPanel.Show": 0, "r.Toolbar.Show": 0,
            "r.GTAO": 0, "r.SSGI": 0, "r.RadianceCache": 1,
            "r.GI.RadianceCache.GridExtent": 2000,
            "r.GI.FinalGather": 0, "r.GI.Reflections": 0,
            "r.SSR": 1, "r.RayTracing": 1, "r.RayTracing.Reflections": 0,
            "r.PathTracer.MaxBounces": 4, "r.PathTracer.NEESamples": 4,
        },
    }


def generate():
    written = []
    for name, (box, _translation) in MESHES.items():
        path = REPO / mesh_asset(name)
        path.write_bytes(box_mesh_bytes(*box))
        written.append(path)
    scene = HERE / f"{SCENE_NAME}.uzscene"
    scene.write_text(json.dumps(scene_json(), indent=2) + "\n", encoding="utf-8")
    project = HERE / f"{SCENE_NAME}.uzproject"
    project.write_text(json.dumps(project_json(), indent=2) + "\n", encoding="utf-8")
    written += [scene, project]
    for p in written:
        print(p)


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def crop(img, roi):
    x0, y0, x1, y1 = roi
    return img[y0:y1, x0:x1]


def rmse(a, b, roi):
    return float(np.sqrt(np.mean((crop(a, roi) - crop(b, roi)) ** 2)))


def mean_codes(img, roi):
    return float(crop(img, roi).mean())


CASES = {
    # name: (glossy, indirect, denoise)
    "mirror":          (0, 0, 0),
    "glossy-raw":      (1, 0, 0),
    "glossy":          (1, 0, 1),
    "mirror-indirect": (0, 1, 0),
    "glossy-indirect": (1, 1, 1),
}


def metrics(out):
    pt = image(out / "pt/frames/f_511.png")
    pt_prev = image(out / "pt/frames/f_255.png")
    imgs = {name: image(out / name / "frames/f_95.png") for name in CASES}
    result = {
        "roi_wall_xyxy": ROI_WALL, "roi_block_xyxy": ROI_BLOCK,
        "roi_shadowed_face_xyxy": ROI_SHADOWED_FACE, "roi_floor_reflection_xyxy": ROI_FLOOR_REFLECTION,
        "pt_256_to_512_rmse_wall": rmse(pt_prev, pt, ROI_WALL),
        "pt_256_to_512_rmse_block": rmse(pt_prev, pt, ROI_BLOCK),
    }
    for name, img in imgs.items():
        result[f"{name}_rmse_wall_vs_pt"] = rmse(img, pt, ROI_WALL)
        result[f"{name}_rmse_block_vs_pt"] = rmse(img, pt, ROI_BLOCK)
        result[f"{name}_rmse_floor_reflection_vs_pt"] = rmse(img, pt, ROI_FLOOR_REFLECTION)
        result[f"{name}_mean_shadowed_face"] = mean_codes(img, ROI_SHADOWED_FACE)
    result["pt_mean_shadowed_face"] = mean_codes(pt, ROI_SHADOWED_FACE)
    result["glossy_minus_mirror_rmse_wall"] = result["glossy_rmse_wall_vs_pt"] - result["mirror_rmse_wall_vs_pt"]
    result["glossy_indirect_minus_mirror_rmse_wall"] = (result["glossy-indirect_rmse_wall_vs_pt"]
                                                        - result["mirror_rmse_wall_vs_pt"])
    result["glossy_indirect_minus_mirror_rmse_block"] = (result["glossy-indirect_rmse_block_vs_pt"]
                                                         - result["mirror_rmse_block_vs_pt"])
    stage1_ref = STAGE1_PROOF / "rt/frames/f_95.png"
    stage1_now = out / "stage1/frames/f_95.png"
    if stage1_ref.exists() and stage1_now.exists():
        delta = np.abs(image(stage1_now) - image(stage1_ref))
        result["stage1_equivalence_max_code_delta"] = int(delta.max())
        result["stage1_equivalence_mean_abs_code_delta"] = float(delta.mean())
        result["stage1_equivalence_png_bytes_identical"] = stage1_now.read_bytes() == stage1_ref.read_bytes()
    print(json.dumps(result, indent=2))
    (out / "metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    # Glossy converges toward the path tracer: closer than the mirror ray on the rough wall.
    assert result["glossy_rmse_wall_vs_pt"] < result["mirror_rmse_wall_vs_pt"], "glossy not closer than mirror"
    assert result["glossy-indirect_rmse_wall_vs_pt"] < result["mirror_rmse_wall_vs_pt"]
    # The denoiser pulls the one-sample estimate toward the reference.
    assert result["glossy_rmse_wall_vs_pt"] < result["glossy-raw_rmse_wall_vs_pt"]
    # Indirect present: the sun-shadowed face is black without the term and lit with it, as the PT
    # (mirror-indirect isolates the term from the glossy blur of the neighbouring floor reflection).
    assert result["pt_mean_shadowed_face"] > 8.0
    assert result["mirror-indirect_mean_shadowed_face"] > 8.0
    assert result["glossy-indirect_mean_shadowed_face"] > 8.0
    assert result["mirror_mean_shadowed_face"] < 2.0
    assert result["glossy-indirect_rmse_block_vs_pt"] < result["glossy_rmse_block_vs_pt"]
    if "stage1_equivalence_max_code_delta" in result:
        assert result["stage1_equivalence_max_code_delta"] == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--case", choices=["generate", "metrics", "pt", "stage1", *CASES])
    args = parser.parse_args()
    if args.case == "generate":
        generate()
        return
    out = args.out.resolve()
    if args.case == "metrics":
        metrics(out)
        return

    project = HERE / f"{SCENE_NAME}.uzproject"
    frames, shots, extra = 100, "95", []
    cvars = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0}
    if args.case == "pt":
        # `--screenshot-frames` takes N, A-B or A-B:step: 255-511:256 captures frames 255 and 511.
        frames, shots, extra = 512, "255-511:256", ["--path-trace"]
    elif args.case == "stage1":
        # The committed stage 1 capture's settings on the stage 1 scene: the stage 2 CVars at their
        # defaults must reproduce it.
        project = HERE / "rt-reflections-offscreen.uzproject"
        cvars["r.RayTracing.Reflections"] = 1
    else:
        glossy, indirect, denoise = CASES[args.case]
        cvars.update({"r.RayTracing.Reflections": 1, "r.RayTracing.Reflections.MaxRoughness": 1.0,
                      "r.RayTracing.Reflections.Glossy": glossy,
                      "r.RayTracing.Reflections.Indirect": indirect,
                      "r.RayTracing.Reflections.Denoise": denoise})
    result = runner.run_capture(str(out / args.case), str(project), frames,
        clean=False, timeout=180, cvars=cvars, extra_args=extra, screenshot_frames=shots)
    assert result["exit_code"] == 0, result["exit_code"]
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log, result["log"]
    assert "DEVICE_REMOVED" not in log and "device removed" not in log.lower(), result["log"]
    print(f'{args.case}: exit=0; {result["frames_dir"]}', flush=True)


if __name__ == "__main__":
    main()
