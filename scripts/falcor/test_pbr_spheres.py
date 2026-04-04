"""
Falcor PBR Spheres — renders material spheres matching UzEngine's PBR Showcase.
Produces ground truth reference for BRDF comparison.

Run:
  cd E:/Falcor/build/windows-vs2022/bin/Release
  Mogwai.exe --headless --script E:/UzEngine/scripts/falcor/test_pbr_spheres.py
"""
from falcor import *
import os

OUTPUT_DIR = "E:/UzEngine/.sandbox/brdf_comparison"
os.makedirs(OUTPUT_DIR, exist_ok=True)

m.loadScene("E:/UzEngine/.sandbox/scenes/pbr_spheres.pyscene")

# Full PathTracer for ground truth
g = RenderGraph("PBRSpheres")

vbuffer = createPass("VBufferRT", {"samplePattern": "Center", "sampleCount": 16, "useAlphaTest": True})
g.addPass(vbuffer, "VBufferRT")

pt = createPass("PathTracer", {"samplesPerPixel": 1})
g.addPass(pt, "PathTracer")

acc = createPass("AccumulatePass", {"enabled": True, "precisionMode": "Single"})
g.addPass(acc, "AccumulatePass")

tone = createPass("ToneMapper", {"autoExposure": False, "exposureCompensation": 0.0, "operator": "Aces"})
g.addPass(tone, "ToneMapper")

g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
g.addEdge("PathTracer.color", "AccumulatePass.input")
g.addEdge("AccumulatePass.output", "ToneMapper.src")
g.markOutput("ToneMapper.dst")

m.addGraph(g)

m.resizeFrameBuffer(1280, 720)
m.ui = False
m.clock.pause()
m.clock.time = 0

# 256 samples for clean convergence
for i in range(256):
    m.clock.frame = i
    m.renderFrame()

m.frameCapture.outputDir = OUTPUT_DIR
m.frameCapture.baseFilename = "falcor_pbr_spheres"
m.frameCapture.capture()

exit()
