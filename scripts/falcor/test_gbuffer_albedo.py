"""
Falcor GBuffer Albedo — captures raw diffuse albedo (no lighting) to verify textures load.
"""
from falcor import *
import os

OUTPUT_DIR = "E:/UzEngine/.sandbox/brdf_comparison"
os.makedirs(OUTPUT_DIR, exist_ok=True)

m.loadScene("E:/UzEngine/.sandbox/scenes/damaged_helmet_lit.pyscene")

g = RenderGraph("GBufferCheck")
gbuffer = createPass("GBufferRaster")
g.addPass(gbuffer, "GBuffer")
g.markOutput("GBuffer.diffuseOpacity")
g.markOutput("GBuffer.specRough")
g.markOutput("GBuffer.normW")
m.addGraph(g)

m.resizeFrameBuffer(1280, 720)
m.ui = False
m.clock.pause()
m.clock.time = 0

for i in range(4):
    m.clock.frame = i
    m.renderFrame()

m.frameCapture.outputDir = OUTPUT_DIR
m.frameCapture.baseFilename = "falcor_gbuffer"
m.frameCapture.capture()

exit()
