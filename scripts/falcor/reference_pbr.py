"""
Falcor Reference PBR Render — generates ground truth images for UzEngine comparison.

Usage (from Falcor's Mogwai):
  Mogwai.exe --script ../../scripts/falcor/reference_pbr.py

Or from Python:
  import subprocess
  subprocess.run(["E:/Falcor/build/bin/Release/Mogwai.exe",
                  "--script", "scripts/falcor/reference_pbr.py",
                  "--headless", "--width", "1280", "--height", "720"])

Outputs reference PNGs to runtime/tests/golden/falcor_reference/
"""

import falcor
import os

# ─────────────────────────────────────────────────────────────────────────────
# Configuration
# ─────────────────────────────────────────────────────────────────────────────

SCENE_PATH = "../../sample/scenes/pbr_showcase.gltf"
OUTPUT_DIR = "../../runtime/tests/golden/falcor_reference/"
RESOLUTION = (1280, 720)
CAPTURE_FRAMES = [1, 5, 8]

# ─────────────────────────────────────────────────────────────────────────────
# Render Graph — Rasterized GBuffer + Deferred Lighting (matches UzEngine)
# ─────────────────────────────────────────────────────────────────────────────

def build_raster_graph():
    """Build a raster-based render graph that matches UzEngine's deferred pipeline."""
    g = RenderGraph("ReferencePBR")

    # GBuffer pass (rasterized, not ray-traced)
    g.addPass(createPass("GBufferRaster"), "GBuffer")

    # Deferred lighting with environment map
    g.addPass(createPass("DeferredLighting"), "Lighting")
    g.addEdge("GBuffer.posW", "Lighting.posW")
    g.addEdge("GBuffer.normW", "Lighting.normW")
    g.addEdge("GBuffer.diffuseOpacity", "Lighting.diffuseOpacity")
    g.addEdge("GBuffer.specRough", "Lighting.specRough")

    # Tone mapping (ACES)
    tone_mapper = createPass("ToneMapper", {"operator": "Aces"})
    g.addPass(tone_mapper, "ToneMapper")
    g.addEdge("Lighting.color", "ToneMapper.src")

    g.markOutput("ToneMapper.dst")
    return g


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Load scene
    m.loadScene(SCENE_PATH)
    m.resizeFrameBuffer(*RESOLUTION)

    # Build graph
    graph = build_raster_graph()
    m.addGraph(graph)

    # Deterministic rendering — pause clock, fixed seed
    m.clock.pause()
    m.clock.time = 0

    # Capture reference frames
    for frame in CAPTURE_FRAMES:
        m.clock.frame = frame
        m.renderFrame()

        m.frameCapture.outputDir = OUTPUT_DIR
        m.frameCapture.baseFilename = f"pbr_showcase_frame{frame}"
        m.frameCapture.capture()
        print(f"[Falcor] Captured frame {frame} -> {OUTPUT_DIR}")

    print(f"[Falcor] Reference capture complete. {len(CAPTURE_FRAMES)} frames saved.")


main()
