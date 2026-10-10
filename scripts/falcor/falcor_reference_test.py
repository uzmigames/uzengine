"""
Falcor Reference Test Runner — compares UzEngine output against Falcor ground truth.

Compares explicitly matched captures and their producer metadata via PSNR.

Usage:
  python scripts/falcor/falcor_reference_test.py --uzengine-image capture.png --falcor-image reference.png

Prerequisites:
  - Falcor built at E:/Falcor (or --falcor-path)
  - UzEngine runtime built (runtime/build/bin/uzengine.exe)
  - Python 3.11+ with Pillow and numpy (requirements-gate.txt)
  - Capture sidecars described in docs/analysis/project-stabilization/07-baseline-implementation.md

Existing-image comparisons never launch renderers or modify references. Legacy
capture options still require explicit matching producer metadata; this runner
does not invent missing scene, camera or executable provenance.
"""

import argparse
import json
import hashlib
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# ─────────────────────────────────────────────────────────────────────────────
# Configuration
# ─────────────────────────────────────────────────────────────────────────────

ENGINE_ROOT = Path(__file__).resolve().parents[2]
UZENGINE_EXE = ENGINE_ROOT / "runtime" / "build" / "bin" / "uzengine.exe"
DEFAULT_FALCOR_PATH = Path("E:/Falcor")
MOGWAI_EXE_REL = "build/bin/Release/Mogwai.exe"
REFERENCE_SCRIPT = ENGINE_ROOT / "scripts" / "falcor" / "reference_pbr.py"
GOLDEN_DIR = ENGINE_ROOT / ".sandbox" / "falcor_reference"
PROJECT_FILE = ENGINE_ROOT / "sample" / "projects" / "pbr_showcase.uzproject"

# Required project acceptance threshold. There is no passing warning band.
PSNR_PASS = 35.0


def compute_psnr(img_a_path: Path, img_b_path: Path) -> float:
    """Compute PSNR between two images using numpy."""
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        print("ERROR: pip install Pillow numpy")
        sys.exit(1)

    with Image.open(img_a_path) as image_a, Image.open(img_b_path) as image_b:
        if image_a.size != image_b.size:
            raise ValueError(f"Image dimensions differ: {image_a.size} != {image_b.size}")
        a = np.array(image_a.convert("RGB"), dtype=np.float64)
        b = np.array(image_b.convert("RGB"), dtype=np.float64)

    mse = np.mean((a - b) ** 2)
    if mse < 1e-10:
        return 100.0  # Identical
    return 10.0 * np.log10(255.0 * 255.0 / mse)


def capture_uzengine(output_path: Path, frame: int = 5) -> bool:
    """Capture a screenshot from UzEngine."""
    before = capture_identity(output_path)
    cmd = [
        str(UZENGINE_EXE),
        "--screenshot", str(output_path),
        "--screenshot-frame", str(frame),
        "--frames", str(frame + 3),
        "--project", str(PROJECT_FILE),
        "--editor",
        "--hidden",
    ]
    print(f"  [UzEngine] Capturing frame {frame}...")
    result = subprocess.run(cmd, cwd=ENGINE_ROOT, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        print(f"  [UzEngine] Capture exited {result.returncode}: {result.stderr[-2000:]}")
    after = capture_identity(output_path)
    return result.returncode == 0 and after is not None and after != before


def capture_falcor(falcor_path: Path) -> bool:
    """Run Falcor Mogwai with the reference script."""
    mogwai = falcor_path / MOGWAI_EXE_REL
    if not mogwai.exists():
        print(f"  [Falcor] Mogwai not found at {mogwai}")
        return False

    expected_image = GOLDEN_DIR / "pbr_showcase_frame5.png"
    before = capture_identity(expected_image)

    cmd = [
        str(mogwai),
        "--script", str(REFERENCE_SCRIPT),
        "--headless",
        "--width", "1280",
        "--height", "720",
    ]
    print(f"  [Falcor] Running reference render...")
    result = subprocess.run(cmd, cwd=REFERENCE_SCRIPT.parent, capture_output=True, text=True, timeout=120)
    after = capture_identity(expected_image)
    return result.returncode == 0 and after is not None and after != before


def capture_identity(path: Path):
    """Require a newly written output, even when a renderer exits successfully."""
    if not path.is_file():
        return None
    stat = path.stat()
    return stat.st_mtime_ns, stat.st_size, file_hash(path)


MATCH_FIELDS = ("scene_sha256", "camera", "width", "height", "frame", "simulation_time",
                "exposure", "tonemapping", "color_space")


def file_hash(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_metadata(image_path: Path, metadata_path: Path, renderer: str) -> dict:
    """Require producer provenance bound to the actual capture, never infer it."""
    from PIL import Image

    def reject_constant(value: str):
        raise ValueError(f"Non-finite metadata number: {value}")

    data = json.loads(metadata_path.read_text(encoding="utf-8"), parse_constant=reject_constant)
    if (not isinstance(data, dict) or type(data.get("schema_version")) is not int
            or data["schema_version"] != 1 or data.get("renderer") != renderer):
        raise ValueError(f"Invalid {renderer} metadata schema/renderer")
    for key in (*MATCH_FIELDS, "image_sha256", "source_manifest_sha256", "executable_sha256"):
        if key not in data:
            raise ValueError(f"{renderer} metadata missing {key}")
    for key in ("scene_sha256", "image_sha256", "source_manifest_sha256", "executable_sha256"):
        value = data[key]
        if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
            raise ValueError(f"Invalid {renderer} {key}")
    for key in ("width", "height", "frame"):
        if type(data[key]) is not int or data[key] < (0 if key == "frame" else 1):
            raise ValueError(f"Invalid {renderer} {key}")
    for key in ("simulation_time", "exposure"):
        if type(data[key]) not in (int, float) or not math.isfinite(data[key]):
            raise ValueError(f"Invalid {renderer} {key}")
    camera = data["camera"]
    if not isinstance(camera, dict):
        raise ValueError(f"Invalid {renderer} camera")
    for key in ("view_matrix", "projection_matrix"):
        matrix = camera.get(key)
        if not isinstance(matrix, list) or len(matrix) != 16 or any(
                type(x) not in (int, float) or not math.isfinite(x) for x in matrix):
            raise ValueError(f"Invalid {renderer} camera {key}")
    for key in ("tonemapping", "color_space"):
        if not isinstance(data[key], str) or not data[key].strip():
            raise ValueError(f"Invalid {renderer} {key}")
    if data["image_sha256"] != file_hash(image_path):
        raise ValueError(f"Stale {renderer} metadata: image hash mismatch")
    with Image.open(image_path) as image:
        if image.size != (data["width"], data["height"]):
            raise ValueError(f"{renderer} metadata dimensions differ from image")
    return data


def run_comparison(uzengine_img: Path, falcor_img: Path, name: str,
                   uzengine_metadata: Path | None = None, falcor_metadata: Path | None = None) -> dict:
    """A required comparison passes only with complete, compatible evidence."""
    try:
        for label, path in (("UzEngine capture", uzengine_img), ("Falcor reference", falcor_img)):
            if not path.is_file():
                raise ValueError(f"{label} missing: {path}")
        uz = read_metadata(uzengine_img, uzengine_metadata or uzengine_img.with_suffix(".json"), "UzEngine")
        falcor = read_metadata(falcor_img, falcor_metadata or falcor_img.with_suffix(".json"), "Falcor")
        for key in MATCH_FIELDS:
            if uz[key] != falcor[key]:
                raise ValueError(f"Capture metadata differs: {key}")
        psnr = float(compute_psnr(uzengine_img, falcor_img))
        if not math.isfinite(psnr):
            raise ValueError("PSNR is not finite")
        result = {"name": name, "status": "PASS" if psnr >= PSNR_PASS else "FAIL",
                  "psnr": psnr, "required_psnr": PSNR_PASS,
                  "uzengine_metadata": uz, "falcor_metadata": falcor}
        if result["status"] == "FAIL":
            result["reason"] = f"PSNR below required {PSNR_PASS:g} dB"
        return result
    except (OSError, ValueError, TypeError, ImportError) as error:
        return {"name": name, "status": "FAIL", "reason": str(error)}


def main(argv=None):
    parser = argparse.ArgumentParser(description="Falcor Reference Test Runner")
    parser.add_argument("--update-golden", action="store_true",
                        help="Regenerate Falcor golden reference images")
    parser.add_argument("--falcor-path", type=Path, default=DEFAULT_FALCOR_PATH,
                        help="Path to Falcor installation")
    parser.add_argument("--skip-falcor", action="store_true",
                        help="Skip Falcor capture, use existing golden images only")
    parser.add_argument("--uzengine-image", type=Path, help="Compare an existing capture without launching renderers")
    parser.add_argument("--falcor-image", type=Path, help="Existing Falcor reference (required with --uzengine-image)")
    parser.add_argument("--uzengine-metadata", type=Path, help="Capture sidecar; defaults to image path with .json suffix")
    parser.add_argument("--falcor-metadata", type=Path, help="Reference sidecar; defaults to image path with .json suffix")
    parser.add_argument("--report", type=Path, default=GOLDEN_DIR / "test_report.json")
    args = parser.parse_args(argv)
    if bool(args.uzengine_image) != bool(args.falcor_image):
        parser.error("--uzengine-image and --falcor-image must be supplied together")
    if args.uzengine_image and args.update_golden:
        parser.error("--update-golden cannot be used with existing-image comparison")
    inputs = [args.uzengine_image, args.falcor_image, args.uzengine_metadata, args.falcor_metadata,
              GOLDEN_DIR / "pbr_showcase_frame5.png", GOLDEN_DIR / "pbr_showcase_frame5.json"]
    inputs += [p.with_suffix(".json") for p in (args.uzengine_image, args.falcor_image) if p]
    if any(p and p.resolve() == args.report.resolve() for p in inputs):
        parser.error("Report path must not overwrite capture evidence")

    print("=" * 60)
    print("Falcor Reference Test — UzEngine Shader Validation")
    print("=" * 60)

    # Step 1: Capture UzEngine
    os.makedirs(GOLDEN_DIR, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmpdir:
        uz_img = args.uzengine_image or Path(tmpdir) / "uzengine_frame5.png"
        if not args.uzengine_image:
            try:
                if not capture_uzengine(uz_img):
                    raise ValueError("UzEngine capture failed")
            except (OSError, subprocess.TimeoutExpired, ValueError) as error:
                return write_report([{"name": "capture", "status": "FAIL", "reason": str(error)}], args.report)

        # Step 2: Capture or use Falcor reference
        if args.update_golden:
            print("\n[Golden Update] Regenerating Falcor reference images...")
            try:
                if not capture_falcor(args.falcor_path):
                    raise ValueError("Falcor capture failed")
            except (OSError, subprocess.TimeoutExpired, ValueError) as error:
                return write_report([{"name": "capture", "status": "FAIL", "reason": str(error)}], args.report)

        # Step 3: Compare
        results = []
        falcor_img = args.falcor_image or GOLDEN_DIR / "pbr_showcase_frame5.png"
        r = run_comparison(uz_img, falcor_img, "pbr_showcase_frame5", args.uzengine_metadata, args.falcor_metadata)
        results.append(r)

        # Report
        print("\n" + "=" * 60)
        print("Results:")
        print("=" * 60)
        return write_report(results, args.report)


def write_report(results: list[dict], report_path: Path) -> int:
    for result in results:
        detail = f"{result['psnr']:.3f} dB" if "psnr" in result else result.get("reason", "")
        print(f"  {result['name']}: {result['status']} ({detail})")
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(results, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"Report: {report_path}")
    return 0 if results and all(result.get("status") == "PASS" for result in results) else 1


if __name__ == "__main__":
    sys.exit(main())
