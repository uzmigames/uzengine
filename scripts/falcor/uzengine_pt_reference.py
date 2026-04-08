"""
Falcor Reference Render — UzEngine Path Tracer parity gate.
@ref CLAUDE.md RULE 0 — Falcor is the rendering quality baseline.

Renders a scene with Falcor's production PathTracer (NVIDIA-validated path
tracing) and writes the converged image to disk. The companion script
`uzengine_pt_compare.py` then captures the same scene from UzEngine with
`r.PathTracer.Enable=1` and computes PSNR + FLIP between the two.

Pass criterion: PSNR >= 35 dB. Below that, the divergence indicates a
math bug in UzEngine — fix UzEngine, never Falcor.

Run:
  cd E:/Falcor/build/windows-vs2022/bin/Release
  Mogwai.exe --headless --script E:/UzEngine/scripts/falcor/uzengine_pt_reference.py
"""
from falcor import *
import os
import sys

OUTPUT_DIR = "E:/UzEngine/.sandbox/path_tracer_reference"
os.makedirs(OUTPUT_DIR, exist_ok=True)

# Scene must have been exported with the same camera/lights as the UzEngine
# scene used by uzengine_pt_compare.py. Default test scene: pbr_showcase.
SCENE_PATH = os.environ.get(
    "UZ_PT_SCENE",
    "E:/UzEngine/.sandbox/scenes/pbr_showcase.pyscene",
)

if not os.path.exists(SCENE_PATH):
    print(f"[uzengine_pt_reference] ERROR: scene not found: {SCENE_PATH}")
    print("[uzengine_pt_reference] Export a UzEngine scene to .pyscene first.")
    sys.exit(1)

print(f"[uzengine_pt_reference] Loading scene: {SCENE_PATH}")
m.loadScene(SCENE_PATH)

# ── Render graph: production PathTracer + accumulation + tonemap ────────────
# Mirrors the Falcor official PathTracer.py example.
# @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
# @ref Walter 2007 "Microfacet Models for Refraction through Rough Surfaces"
g = RenderGraph("UzEnginePTReference")

vbuffer = createPass("VBufferRT", {
    "samplePattern": "Center",
    "sampleCount": 16,
    "useAlphaTest": True,
})
g.addPass(vbuffer, "VBufferRT")

pt = createPass("PathTracer", {
    "samplesPerPixel": 1,
    "maxSurfaceBounces": 4,  # Match UzEngine PT_SAMPLES_PER_FRAME loop budget
})
g.addPass(pt, "PathTracer")

# Single-precision accumulation — converges over many frames to ground truth.
acc = createPass("AccumulatePass", {
    "enabled": True,
    "precisionMode": "Single",
})
g.addPass(acc, "AccumulatePass")

# ACES tonemap matches UzEngine's default tonemap operator (when r.TonemapOperator=1).
tone = createPass("ToneMapper", {
    "autoExposure": False,
    "exposureCompensation": 0.0,
    "operator": "Aces",
})
g.addPass(tone, "ToneMapper")

g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
g.addEdge("PathTracer.color",  "AccumulatePass.input")
g.addEdge("AccumulatePass.output", "ToneMapper.src")
g.markOutput("ToneMapper.dst")

m.addGraph(g)

# ── Frame budget — 4000 frames for clean convergence ───────────────────────
m.resizeFrameBuffer(1920, 1080)
m.clock.pause()

NUM_FRAMES = int(os.environ.get("UZ_PT_FRAMES", "4000"))
print(f"[uzengine_pt_reference] Rendering {NUM_FRAMES} frames at 1920x1080")
for frame in range(NUM_FRAMES):
    m.clock.frame = frame
    m.renderFrame()

m.frameCapture.outputDir = OUTPUT_DIR
m.frameCapture.baseFilename = "falcor_pt_reference"
m.frameCapture.capture()

print(f"[uzengine_pt_reference] Wrote reference to: {OUTPUT_DIR}")
exit()
