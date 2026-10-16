#!/usr/bin/env python3
"""Falcor PSNR parity gate for the UzEngine path tracer (tasks.md Step 8, RULE 0).

This is the committed, reproducible replacement for the ad-hoc ``_psnr_*.py`` scratch
scripts that previously lived at the repo root. It compares a UzEngine path-traced
capture against a matched Falcor 8.0 PathTracer reference render and reports the
sphere-crop PSNR under three metrics:

  * raw                 - direct PSNR, no fit (sensitive to any exposure/energy offset).
  * single-global-scalar- one least-squares energy constant applied to all channels and
                          pixels. This isolates transport/shape from a single legitimate
                          exposure/energy difference (the engine Lambert carries ``(1-F0)``
                          plus ``SkyTint*EnvIntensity`` vs Falcor's Frostbite default). This
                          is the GATE metric.
  * per-channel-gain    - three gain scalars (exposure + white balance), reported for
                          reference only.

Evidence and methodology: docs/analysis/path-tracing/09-test-results.md (Tier-4 / Tier-4b).

Usage
-----
  # Compare pre-rendered engine captures (default engine dir) against the Falcor refs:
  python -I scripts/falcor/compare_pt.py

  # Render the engine captures first (needs the RTX GPU + the HDRI asset), then compare:
  python -I scripts/falcor/compare_pt.py --render

  # One case, with an explicit engine capture:
  python -I scripts/falcor/compare_pt.py --case metal015 --engine path/to/uz_metal015.png

Exit code is non-zero if any gating case (known_gap=False) is below its threshold.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
UZENGINE_EXE = os.path.join(REPO, "runtime", "build", "bin", "uzengine.exe")
FALCOR_ASSETS = os.environ.get("UZ_FALCOR_ASSETS", r"E:/falcor_scene_assets")
DEFAULT_ENGINE_DIR = os.path.join(REPO, ".screenshots", "pt-falcor-gate")
PARITY_PROJECT = os.path.join(REPO, "scripts", "rendering", "sphere-parity.uzproject")
PARITY_SCENE = os.path.join(REPO, "sample", "content", "scenes", "sphere-parity.uzscene")

GATE_CVARS = {
    "r.PathTracer.Enable": 1,
    "r.RayTracing": 1,
    "r.PathTracer.MaxBounces": 8,
    "r.PathTracer.ClampFirefly": 0,
    "r.PathTracer.EnvIntensity": 1,
}
GATE_FRAMES = 512

CROP = (0.36, 0.64, 0.18, 0.90)

GATE_DB = 35.0


CASES = {
    "diffuse10": dict(
        material=dict(base=0.5, metallic=0, roughness=1.0),
        falcor="sphere2_out/falcor_sphere2.ToneMapper.dst.511.png",
        gate=GATE_DB, known_gap=False,
        note="dielectric Lambertian r1.0 - diffuse env transport",
    ),
    "diffuse05": dict(
        material=dict(base=0.5, metallic=0, roughness=0.5),
        falcor="sphere_r05_out/falcor_sphere_r05.ToneMapper.dst.511.png",
        gate=GATE_DB, known_gap=False,
        note="dielectric r0.5 - diffuse energy already agrees (no offset)",
    ),
    "metal015": dict(
        material=dict(base=0.9, metallic=1, roughness=0.15),
        falcor="metal015_out/falcor_metal015.ToneMapper.dst.511.png",
        gate=GATE_DB, known_gap=False,
        note="glossy metal r0.15 - VNDF-GGX lobe",
    ),
    "metal03": dict(
        material=dict(base=0.9, metallic=1, roughness=0.3),
        falcor="metal03_out/falcor_metal03.ToneMapper.dst.511.png",
        gate=GATE_DB, known_gap=False,
        note="rough metal r0.3 - VNDF-GGX lobe",
    ),
    "metal005": dict(
        material=dict(base=0.9, metallic=1, roughness=0.05),
        falcor="metal005_out/falcor_metal005.ToneMapper.dst.511.png",
        gate=GATE_DB, known_gap=True,
        note="near-mirror r0.05 - GEOMETRY-PARITY BOUND, not transport (see 09 Tier-4b)",
    ),
}


def load_rgb(path: str) -> np.ndarray:
    return np.array(Image.open(path).convert("RGB"), dtype=np.float64)


def psnr(a: np.ndarray, b: np.ndarray) -> float:
    mse = float(np.mean((a - b) ** 2))
    return 99.0 if mse < 1e-9 else 10.0 * np.log10(255.0 ** 2 / mse)


def crop(img: np.ndarray) -> np.ndarray:
    h, w, _ = img.shape
    x0, x1, y0, y1 = CROP
    return img[int(y0 * h):int(y1 * h), int(x0 * w):int(x1 * w)]


def mean_chroma(img: np.ndarray) -> float:
    return float(np.mean(np.max(img, axis=2) - np.min(img, axis=2)))


def single_scalar_psnr(ref: np.ndarray, test: np.ndarray) -> tuple[float, float]:
    """One least-squares energy constant across all channels/pixels (the gate metric)."""
    s = float(np.sum(ref * test) / max(np.sum(test * test), 1e-9))
    return psnr(ref, np.clip(s * test, 0, 255)), s


def per_channel_gain_psnr(ref: np.ndarray, test: np.ndarray) -> tuple[float, list[float]]:
    out = np.zeros_like(test)
    gains: list[float] = []
    for c in range(3):
        x = test[:, :, c].ravel()
        y = ref[:, :, c].ravel()
        g = float(np.dot(x, y) / max(np.dot(x, x), 1e-9))
        gains.append(g)
        out[:, :, c] = np.clip(g * test[:, :, c], 0, 255)
    return psnr(ref, out), gains


def write_patched_scene(material: dict) -> str:
    with open(PARITY_SCENE, "r", encoding="utf-8") as fh:
        scene = json.load(fh)
    for ent in scene.get("entities", []):
        mat = ent.get("components", {}).get("material")
        if mat is not None:
            mat["baseColorR"] = mat["baseColorG"] = mat["baseColorB"] = material["base"]
            mat["metallicMultiplier"] = material["metallic"]
            mat["roughnessMultiplier"] = material["roughness"]
    fd, path = tempfile.mkstemp(suffix=".uzscene", dir=os.path.dirname(PARITY_SCENE))
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        json.dump(scene, fh, indent=2)
    return path


def render_engine_capture(tag: str, case: dict, out_png: str) -> None:
    """Verified only when the render log shows no 'compile failed' and output is
    non-black: a register collision makes the PT PSO silently fail and go all black
    while zig build still exits 0 (see 09 Tier-4b). This driver surfaces both."""
    if not os.path.isfile(UZENGINE_EXE):
        raise FileNotFoundError(f"uzengine.exe not found: {UZENGINE_EXE} (build first)")
    scene = write_patched_scene(case["material"])
    try:
        # The project's default_scene is overridden by writing the patched scene in place
        # of the default path; simplest robust route is to point default_scene via a temp
        # project. Here we patch the scene the project already references.
        default_scene = os.path.abspath(PARITY_SCENE)
        os.replace(default_scene, default_scene + ".bak")
        os.replace(scene, default_scene)
        out_png = os.path.abspath(out_png)
        cmd = [UZENGINE_EXE, "--project", PARITY_PROJECT,
               "--screenshot", out_png, "--screenshot-frame", str(GATE_FRAMES),
               "--frames", str(GATE_FRAMES + 8), "--verbose"]
        for k, v in GATE_CVARS.items():
            cmd += ["--cvar", f"{k}={v}"]
        print("  [render]", " ".join(cmd))
        proc = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, timeout=1200)
        log = proc.stdout + proc.stderr
        if "compile failed" in log:
            raise RuntimeError(f"{tag}: shader compile failed at runtime (PT PSO not created)")
        if not os.path.isfile(out_png):
            raise RuntimeError(f"{tag}: no screenshot produced (exit {proc.returncode})")
    finally:
        if os.path.isfile(default_scene + ".bak"):
            os.replace(default_scene + ".bak", default_scene)
        if os.path.isfile(scene):
            os.remove(scene)


def run_case(tag: str, case: dict, engine_dir: str, engine_override: str | None,
             do_render: bool) -> dict:
    ref_path = os.path.join(FALCOR_ASSETS, case["falcor"])
    eng_path = engine_override or os.path.join(engine_dir, f"{tag}.png")
    if do_render and not engine_override:
        os.makedirs(engine_dir, exist_ok=True)
        render_engine_capture(tag, case, eng_path)
    if not os.path.isfile(ref_path):
        return dict(tag=tag, error=f"missing Falcor ref: {ref_path}")
    if not os.path.isfile(eng_path):
        return dict(tag=tag, error=f"missing engine capture: {eng_path} (use --render)")

    ref = load_rgb(ref_path)
    eng = load_rgb(eng_path)
    if ref.shape != eng.shape:
        return dict(tag=tag, error=f"shape mismatch ref={ref.shape} eng={eng.shape}")

    if mean_chroma(ref) > 5.0 and mean_chroma(eng) < 2.0:
        return dict(tag=tag, error=(
            f"engine capture is colorless (chroma {mean_chroma(eng):.2f} vs ref "
            f"{mean_chroma(ref):.2f}) - HDRI environment missing. The auto-screenshot "
            f"raced the frame-3 Lua HDRI load; see 09-test-results.md capture-timing note."))

    sf, su = crop(ref), crop(eng)
    raw = psnr(sf, su)
    scalar, s = single_scalar_psnr(sf, su)
    gain, gains = per_channel_gain_psnr(sf, su)
    passed = scalar >= case["gate"]
    return dict(tag=tag, raw=raw, scalar=scalar, s=s, gain=gain, gains=gains,
                gate=case["gate"], known_gap=case["known_gap"], passed=passed,
                note=case["note"])


def main() -> int:
    ap = argparse.ArgumentParser(description="Falcor PSNR parity gate for the UzEngine path tracer")
    ap.add_argument("--render", action="store_true",
                    help="render engine captures with uzengine.exe before comparing")
    ap.add_argument("--case", choices=list(CASES), help="run a single case")
    ap.add_argument("--engine", help="explicit engine capture PNG (implies --case)")
    ap.add_argument("--engine-dir", default=DEFAULT_ENGINE_DIR,
                    help=f"dir of engine captures <tag>.png (default {DEFAULT_ENGINE_DIR})")
    args = ap.parse_args()

    if args.engine and not args.case:
        ap.error("--engine requires --case")

    tags = [args.case] if args.case else list(CASES)
    results = [run_case(t, CASES[t], args.engine_dir, args.engine if args.case else None,
                        args.render) for t in tags]

    print(f"\nFalcor PSNR parity gate (crop {CROP}, gate {GATE_DB} dB on single-global-scalar)")
    print("-" * 92)
    print(f"{'case':10} {'raw':>7} {'scalar':>8} {'(s)':>7} {'per-ch':>8}  {'gate':>5}  result")
    print("-" * 92)
    gating_fail = False
    for r in results:
        if "error" in r:
            print(f"{r['tag']:10} ERROR: {r['error']}")
            gating_fail = True
            continue
        verdict = "PASS" if r["passed"] else ("KNOWN-GAP" if r["known_gap"] else "FAIL")
        if not r["passed"] and not r["known_gap"]:
            gating_fail = True
        print(f"{r['tag']:10} {r['raw']:7.2f} {r['scalar']:8.2f} {r['s']:7.3f} "
              f"{r['gain']:8.2f}  {r['gate']:5.1f}  {verdict}")
        print(f"           {r['note']}")
    print("-" * 92)
    print("OVERALL:", "FAIL" if gating_fail else "PASS")
    return 1 if gating_fail else 0


if __name__ == "__main__":
    sys.exit(main())
