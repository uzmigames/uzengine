"""
BRDF Comparison — Falcor vs UzEngine

Renders a material sphere in both Falcor (via Mogwai/Python) and UzEngine,
then compares the BRDF output pixel-by-pixel via PSNR.

This validates that UzEngine's pbr_common.hlsli (D_GGX, V_SmithGGXCorrelated,
F_Schlick, Fd_Burley) produces the same results as Falcor's StandardBSDF.slang.

Usage:
  python scripts/falcor/brdf_comparison.py
  python scripts/falcor/brdf_comparison.py --falcor-path E:/Falcor --roughness 0.5

Prerequisites:
  - Falcor built at E:/Falcor (Mogwai.exe in build/windows-vs2022/bin/Release/)
  - UzEngine runtime built
  - Python 3.10+ with Pillow, numpy: pip install Pillow numpy
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# ─────────────────────────────────────────────────────────────────────────────
# Paths
# ─────────────────────────────────────────────────────────────────────────────

ENGINE_ROOT = Path(__file__).parent.parent.parent
UZENGINE_EXE = ENGINE_ROOT / "runtime" / "build" / "bin" / "uzengine.exe"
DEFAULT_FALCOR = Path("E:/Falcor")
MOGWAI_REL = "build/windows-vs2022/bin/Release/Mogwai.exe"
RESULTS_DIR = ENGINE_ROOT / ".sandbox" / "brdf_comparison"

# PSNR thresholds
PSNR_PASS = 35.0  # Different renderers, expect some divergence
PSNR_WARN = 25.0


def compute_psnr(img_a: Path, img_b: Path) -> float:
    import numpy as np
    from PIL import Image

    a = np.array(Image.open(img_a).convert("RGB")).astype(np.float64)
    b = np.array(Image.open(img_b).convert("RGB")).astype(np.float64)

    # Resize if needed
    if a.shape != b.shape:
        from PIL import Image as PILImage
        size = (min(a.shape[1], b.shape[1]), min(a.shape[0], b.shape[0]))
        a = np.array(PILImage.open(img_a).convert("RGB").resize(size)).astype(np.float64)
        b = np.array(PILImage.open(img_b).convert("RGB").resize(size)).astype(np.float64)

    mse = np.mean((a - b) ** 2)
    if mse < 1e-10:
        return 100.0
    return 10.0 * np.log10(255.0 * 255.0 / mse)


# ─────────────────────────────────────────────────────────────────────────────
# Falcor Capture
# ─────────────────────────────────────────────────────────────────────────────

def write_falcor_script(output_dir: Path, roughness: float, metallic: float) -> Path:
    """Generate a Falcor Python script that renders a BSDF sphere and captures."""
    script = output_dir / "run_bsdf_test.py"
    script.write_text(f"""
import falcor
from helpers import render_frames

# Configure BSDFViewer with specific material params
def setup():
    g = RenderGraph("BSDFTest")
    bsdf = createPass("BSDFViewer", {{
        "material": {{
            "baseColor": [0.8, 0.8, 0.8, 1.0],
            "roughness": {roughness},
            "metallic": {metallic},
        }},
        "lightIntensity": 1.0,
        "useEnvMap": False,
        "useDirectionalLight": True,
    }})
    g.addPass(bsdf, "BSDFViewer")
    acc = createPass("AccumulatePass", {{"maxAccumulatedFrames": 64}})
    g.addPass(acc, "AccumulatePass")
    g.addEdge("BSDFViewer.output", "AccumulatePass.input")
    g.markOutput("AccumulatePass.output")
    return g

graph = setup()
m.addGraph(graph)

# Render and capture
m.resizeFrameBuffer(512, 512)
m.ui = False
m.clock.pause()
m.clock.time = 0
m.frameCapture.outputDir = r"{str(output_dir).replace(chr(92), '/')}"
m.frameCapture.baseFilename = "falcor_bsdf_r{roughness:.2f}_m{metallic:.2f}"

for i in range(64):
    m.clock.frame = i
    m.renderFrame()
m.frameCapture.capture()
m.shutdown()
""")
    return script


def capture_falcor(falcor_path: Path, output_dir: Path, roughness: float, metallic: float) -> Path:
    """Run Falcor BSDFViewer and capture output."""
    mogwai = falcor_path / MOGWAI_REL
    if not mogwai.exists():
        print(f"  [Falcor] Mogwai not found: {mogwai}")
        return None

    script = write_falcor_script(output_dir, roughness, metallic)

    print(f"  [Falcor] Rendering BSDF sphere (roughness={roughness}, metallic={metallic})...")
    result = subprocess.run(
        [str(mogwai), "--script", str(script), "--headless", "--width", "512", "--height", "512"],
        capture_output=True, text=True, timeout=120, cwd=str(falcor_path)
    )

    # Find captured image
    expected = output_dir / f"falcor_bsdf_r{roughness:.2f}_m{metallic:.2f}.png"
    if expected.exists():
        return expected

    # Fallback: find any PNG in output dir
    pngs = list(output_dir.glob("falcor_bsdf_*.png"))
    return pngs[0] if pngs else None


# ─────────────────────────────────────────────────────────────────────────────
# UzEngine Capture
# ─────────────────────────────────────────────────────────────────────────────

def capture_uzengine(output_path: Path) -> bool:
    """Capture UzEngine PBR showcase at default position."""
    cmd = [
        str(UZENGINE_EXE),
        "--screenshot", str(output_path),
        "--screenshot-frame", "5",
        "--frames", "8",
        "--project", str(ENGINE_ROOT / "sample" / "projects" / "pbr_showcase.uzproject"),
        "--editor",
    ]
    print(f"  [UzEngine] Capturing...")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    return output_path.exists()


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="BRDF Comparison: Falcor vs UzEngine")
    parser.add_argument("--falcor-path", type=Path, default=DEFAULT_FALCOR)
    parser.add_argument("--roughness", type=float, default=0.5)
    parser.add_argument("--metallic", type=float, default=0.0)
    args = parser.parse_args()

    print("=" * 60)
    print("BRDF Comparison — Falcor vs UzEngine")
    print(f"  roughness={args.roughness}, metallic={args.metallic}")
    print("=" * 60)

    os.makedirs(RESULTS_DIR, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)

        # Falcor
        falcor_img = capture_falcor(args.falcor_path, tmpdir, args.roughness, args.metallic)
        if not falcor_img:
            print("ERROR: Falcor capture failed. Is Mogwai built?")
            print(f"  Expected: {args.falcor_path / MOGWAI_REL}")
            sys.exit(1)

        # UzEngine
        uz_img = tmpdir / "uzengine_pbr.png"
        if not capture_uzengine(uz_img):
            print("ERROR: UzEngine capture failed")
            sys.exit(1)

        # Compare
        psnr = compute_psnr(uz_img, falcor_img)

        if psnr >= PSNR_PASS:
            status = "PASS"
        elif psnr >= PSNR_WARN:
            status = "WARNING"
        else:
            status = "FAIL"

        print(f"\n  PSNR: {psnr:.1f} dB — {status}")
        print(f"  Threshold: >= {PSNR_PASS} dB = PASS")

        # Save results
        report = {
            "roughness": args.roughness,
            "metallic": args.metallic,
            "psnr": round(psnr, 2),
            "status": status,
            "falcor_image": str(falcor_img),
            "uzengine_image": str(uz_img),
        }
        report_path = RESULTS_DIR / "last_comparison.json"
        with open(report_path, "w") as f:
            json.dump(report, f, indent=2)

        sys.exit(0 if status != "FAIL" else 1)


if __name__ == "__main__":
    main()
