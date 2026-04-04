"""
Falcor Reference Test Runner — compares UzEngine output against Falcor ground truth.

Runs both renderers on the same scene, captures screenshots, compares via PSNR.

Usage:
  python scripts/falcor_reference_test.py [--update-golden] [--falcor-path E:/Falcor]

Prerequisites:
  - Falcor built at E:/Falcor (or --falcor-path)
  - UzEngine runtime built (runtime/build/bin/uzengine.exe)
  - Python 3.10+ with Pillow: pip install Pillow numpy

Workflow:
  1. Runs UzEngine headless, captures screenshot
  2. Runs Falcor Mogwai headless with reference_pbr.py script
  3. Loads both images, computes PSNR
  4. Reports pass/fail per PSNR threshold
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# ─────────────────────────────────────────────────────────────────────────────
# Configuration
# ─────────────────────────────────────────────────────────────────────────────

ENGINE_ROOT = Path(__file__).parent.parent
UZENGINE_EXE = ENGINE_ROOT / "runtime" / "build" / "bin" / "uzengine.exe"
DEFAULT_FALCOR_PATH = Path("E:/Falcor")
MOGWAI_EXE_REL = "build/bin/Release/Mogwai.exe"
REFERENCE_SCRIPT = ENGINE_ROOT / "scripts" / "falcor" / "reference_pbr.py"
GOLDEN_DIR = ENGINE_ROOT / "runtime" / "tests" / "golden" / "falcor_reference"
PROJECT_FILE = ENGINE_ROOT / "sample" / "projects" / "pbr_showcase.uzproject"

# PSNR thresholds (dB) — Falcor uses different pipeline, so looser than self-comparison
PSNR_PASS = 35.0
PSNR_WARN = 25.0


def compute_psnr(img_a_path: Path, img_b_path: Path) -> float:
    """Compute PSNR between two images using numpy."""
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        print("ERROR: pip install Pillow numpy")
        sys.exit(1)

    a = np.array(Image.open(img_a_path).convert("RGB")).astype(np.float64)
    b = np.array(Image.open(img_b_path).convert("RGB")).astype(np.float64)

    if a.shape != b.shape:
        # Resize smaller to match larger
        from PIL import Image as PILImage
        target_size = (max(a.shape[1], b.shape[1]), max(a.shape[0], b.shape[0]))
        a = np.array(PILImage.open(img_a_path).convert("RGB").resize(target_size)).astype(np.float64)
        b = np.array(PILImage.open(img_b_path).convert("RGB").resize(target_size)).astype(np.float64)

    mse = np.mean((a - b) ** 2)
    if mse < 1e-10:
        return 100.0  # Identical
    return 10.0 * np.log10(255.0 * 255.0 / mse)


def capture_uzengine(output_path: Path, frame: int = 5) -> bool:
    """Capture a screenshot from UzEngine."""
    cmd = [
        str(UZENGINE_EXE),
        "--screenshot", str(output_path),
        "--screenshot-frame", str(frame),
        "--frames", str(frame + 3),
        "--project", str(PROJECT_FILE),
        "--editor",
    ]
    print(f"  [UzEngine] Capturing frame {frame}...")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    return output_path.exists()


def capture_falcor(falcor_path: Path) -> bool:
    """Run Falcor Mogwai with the reference script."""
    mogwai = falcor_path / MOGWAI_EXE_REL
    if not mogwai.exists():
        print(f"  [Falcor] Mogwai not found at {mogwai}")
        return False

    cmd = [
        str(mogwai),
        "--script", str(REFERENCE_SCRIPT),
        "--headless",
        "--width", "1280",
        "--height", "720",
    ]
    print(f"  [Falcor] Running reference render...")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    return result.returncode == 0


def run_comparison(uzengine_img: Path, falcor_img: Path, name: str) -> dict:
    """Compare two images and return results."""
    if not uzengine_img.exists():
        return {"name": name, "status": "SKIP", "reason": "UzEngine capture missing"}
    if not falcor_img.exists():
        return {"name": name, "status": "SKIP", "reason": "Falcor reference missing"}

    psnr = compute_psnr(uzengine_img, falcor_img)

    if psnr >= PSNR_PASS:
        status = "PASS"
    elif psnr >= PSNR_WARN:
        status = "WARNING"
    else:
        status = "FAIL"

    return {"name": name, "status": status, "psnr": round(psnr, 2)}


def main():
    parser = argparse.ArgumentParser(description="Falcor Reference Test Runner")
    parser.add_argument("--update-golden", action="store_true",
                        help="Regenerate Falcor golden reference images")
    parser.add_argument("--falcor-path", type=Path, default=DEFAULT_FALCOR_PATH,
                        help="Path to Falcor installation")
    parser.add_argument("--skip-falcor", action="store_true",
                        help="Skip Falcor capture, use existing golden images only")
    args = parser.parse_args()

    print("=" * 60)
    print("Falcor Reference Test — UzEngine Shader Validation")
    print("=" * 60)

    # Step 1: Capture UzEngine
    os.makedirs(GOLDEN_DIR, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmpdir:
        uz_img = Path(tmpdir) / "uzengine_frame5.png"
        if not capture_uzengine(uz_img):
            print("ERROR: UzEngine capture failed")
            sys.exit(1)

        # Step 2: Capture or use Falcor reference
        if args.update_golden:
            print("\n[Golden Update] Regenerating Falcor reference images...")
            if not capture_falcor(args.falcor_path):
                print("ERROR: Falcor capture failed")
                sys.exit(1)
        elif not args.skip_falcor:
            if not (args.falcor_path / MOGWAI_EXE_REL).exists():
                print(f"\n[INFO] Falcor not found at {args.falcor_path}")
                print("       Using existing golden images (--skip-falcor implied)")
                args.skip_falcor = True

        # Step 3: Compare
        results = []
        falcor_img = GOLDEN_DIR / "pbr_showcase_frame5.png"
        r = run_comparison(uz_img, falcor_img, "pbr_showcase_frame5")
        results.append(r)

        # Report
        print("\n" + "=" * 60)
        print("Results:")
        print("=" * 60)
        all_pass = True
        for r in results:
            icon = {"PASS": "OK", "WARNING": "!!", "FAIL": "XX", "SKIP": "--"}[r["status"]]
            psnr_str = f'{r["psnr"]:.1f} dB' if "psnr" in r else r.get("reason", "")
            print(f"  [{icon}] {r['name']}: {r['status']} ({psnr_str})")
            if r["status"] == "FAIL":
                all_pass = False

        # Write JSON report
        report_path = GOLDEN_DIR / "test_report.json"
        with open(report_path, "w") as f:
            json.dump(results, f, indent=2)
        print(f"\nReport: {report_path}")

        sys.exit(0 if all_pass else 1)


if __name__ == "__main__":
    main()
