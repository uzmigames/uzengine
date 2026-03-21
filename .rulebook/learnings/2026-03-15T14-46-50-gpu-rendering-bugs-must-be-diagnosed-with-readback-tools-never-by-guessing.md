# GPU rendering bugs must be diagnosed with readback + tools, never by guessing
**Source**: manual
**Date**: 2026-03-15
**Tags**: rendering, debugging, gpu, atmosphere, lut, renderdoc, pix
When rendering features produce wrong output (wrong colors, white sky, black screen), NEVER iterate blindly by changing shader code and rebuilding. This wastes hours.

**Correct approach:**
1. CPU readback of GPU resources (LUT textures, CBs) to verify actual values
2. RenderDoc/PIX frame capture to inspect pipeline state and descriptor bindings
3. Isolate the pass (render only the LUT to screen)
4. Validate against mathematically known-good values
5. Step through shader in PIX/RenderDoc if needed (compile with -Zi -Od)

**What went wrong:** Sky atmosphere transmittance LUT was being generated but the pixel shader had `#ifdef USE_TRANSMITTANCE_LUT` guards that were never defined. Instead of readback-validating the LUT or checking PIX pipeline state, we spent multiple sessions changing code blindly. A single RenderDoc capture would have shown the LUT was correct but not being sampled.

**Created:** `/gpu-debug` skill and CLAUDE.md directive for systematic GPU diagnosis.