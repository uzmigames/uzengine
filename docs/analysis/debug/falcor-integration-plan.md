<!-- doc-meta
title: Falcor Analysis — Patterns to Adopt for UzEngine Debug Infrastructure
category: analysis
created: 2026-03-30
status: proposal
-->

# Falcor Analysis — Patterns to Adopt for UzEngine

## What Falcor Is

NVIDIA Falcor is a **real-time rendering research framework** (BSD licensed). It is NOT a game engine — it is a productivity-focused sandbox where graphics researchers prototype algorithms (path tracers, denoisers, post-process) without reimplementing boilerplate GPU infrastructure.

**Stack**: DX12 + Vulkan, Slang shaders (HLSL superset), Python 3.10 embedded interpreter, CMake.

**Key fact**: Falcor owns its own DX12 device, swap chain, heaps. It cannot be embedded into UzEngine as a library. But its **patterns and architecture** are extremely valuable.

---

## What Falcor Does That We Should Learn From

### 1. PixelDebug — GPU Printf (THE killer feature)

Falcor's `PixelDebug` lets shaders "print" values for a specific pixel, with results displayed in the UI or logged to file. **No shader modification required at the call site** — you just add `print()` calls anywhere.

**How it works technically:**

```
GPU side:
  1. RWByteAddressBuffer counter (1 uint, atomically incremented)
  2. RWStructuredBuffer<PrintRecord> records (N capacity)
  3. Shader calls: InterlockedAdd(counter, 1) → gets slot index
  4. Writes: {messageHash, valueType, float4 data} into records[slot]
  5. Only executes for the selected debug pixel (early-out for others)

CPU side:
  1. End of frame: copy records buffer → readback staging buffer
  2. Fence wait (triple-buffered, no stall)
  3. Map readback, iterate records
  4. Decode messageHash → string via compile-time hash→string map
  5. Display in overlay or log as JSON
```

**What this gives us**: Instead of editing a shader to add `return float4(value,0,0,1)`, an LLM agent would:
```bash
demo.exe --pixel-debug 960 540 --frames 8
# Output: PIXEL_DEBUG_JSON:{"pixel":[960,540],"prints":[
#   {"msg":"shadowFactor","value":0.0},
#   {"msg":"NdotL","value":0.73},
#   {"msg":"cascadeIdx","value":2}
# ]}
```

**Zero rebuilds. Zero shader edits for diagnosis. Just run with coordinates.**

### 2. Image Test Framework — Deterministic Golden Image Pipeline

Falcor's image tests are Python scripts that:
1. Build a render graph
2. Load a specific scene
3. Advance to frame N (deterministic — fixed seed, paused clock)
4. Capture a PNG
5. Compare to a reference image stored in the repo

```python
# Falcor test pattern:
def render_frames(m, name, frames=[64]):
    m.resizeFrameBuffer(640, 360)
    m.clock.pause()
    m.clock.time = 0
    for frame in frames:
        m.clock.frame = frame
        m.renderFrame()
    m.frameCapture.capture()  # → PNG

# UzEngine equivalent (already possible!):
# demo.exe --headless --screenshot-stdout --frames 64 --camera-x 0 --camera-y 0 --camera-z 300
```

We already have the infrastructure. What's missing is the **test runner script** and **reference images**.

### 3. Render Graph `reflect()` Pattern — Pass Self-Description

Every Falcor pass declares its inputs/outputs in `reflect()`:

```cpp
RenderPassReflection reflect() {
    RenderPassReflection r;
    r.addInput("vbuffer", "Visibility buffer");
    r.addOutput("color", "HDR output").format(RGBA32Float);
    r.addInternal("history", "TAA history").flags(Persistent);
    return r;
}
```

The graph compiler uses this to:
- Validate all connections are type-compatible
- Automatically insert barriers
- Alias memory for non-overlapping resources
- Skip passes whose outputs are not consumed

UzEngine's render graph already does barriers and pass ordering. The **Persistent flag** (for TAA history, SSGI accumulation) and **Optional inputs** (skip if not connected) are patterns worth adopting.

### 4. Mogwai Python Console — Interactive Shader R&D

Falcor's Mogwai viewer embeds Python 3.10 with full access to the renderer:

```python
m.loadScene("sponza.gltf")
m.activeGraph.getPass("PathTracer").maxBounces = 5
m.renderFrame()
m.frameCapture.capture()
```

This enables: live parameter tweaking, automated batch rendering, automated regression tests — all from Python scripts without recompiling.

---

## The Plan: 3 Adoptable Patterns

### Pattern A: PixelDebug for UzEngine (HIGH PRIORITY)

Implement a simplified version of Falcor's PixelDebug in HLSL + C++.

**New files:**
- `runtime/shaders/runtime/include/pixel_debug.hlsli` — shader-side macros
- `runtime/engine/renderer/passes/debug/pixel_debug.h` — C++ host
- `runtime/engine/renderer/passes/debug/pixel_debug.cpp` — readback + JSON output

**HLSL side (`pixel_debug.hlsli`):**
```hlsl
#ifdef PIXEL_DEBUG_ENABLED

RWByteAddressBuffer g_PixelDebugCounter : register(u10);
RWStructuredBuffer<uint4> g_PixelDebugRecords : register(u11);
cbuffer PixelDebugCB : register(b14) {
    uint2 g_DebugPixel;
    uint  g_DebugMaxRecords;
    uint  g_DebugPad;
};

void PixelDebugPrint(uint2 pixel, uint messageId, float value) {
    if (all(pixel == g_DebugPixel)) {
        uint slot;
        g_PixelDebugCounter.InterlockedAdd(0, 1, slot);
        if (slot < g_DebugMaxRecords) {
            g_PixelDebugRecords[slot] = uint4(
                messageId,
                asuint(value),
                pixel.x,
                pixel.y
            );
        }
    }
}

// Convenience macros
#define PDEBUG(pixel, id, val) PixelDebugPrint(pixel, id, val)
#define PDEBUG_F3(pixel, id, v) { PDEBUG(pixel, id##_X, v.x); PDEBUG(pixel, id##_Y, v.y); PDEBUG(pixel, id##_Z, v.z); }

#else
#define PDEBUG(pixel, id, val)
#define PDEBUG_F3(pixel, id, v)
#endif
```

**C++ side:**
- Creates counter buffer (4 bytes) + records buffer (N * 16 bytes) + readback staging
- Each frame: clear counter → bind UAVs → render → copy to readback → fence wait → map + decode
- Output as `PIXEL_DEBUG_JSON:{...}` to stdout

**CLI:**
```bash
demo.exe --pixel-debug 960 540 --frames 8
# Compiles shaders with PIXEL_DEBUG_ENABLED defined
# Outputs: PIXEL_DEBUG_JSON:{"pixel":[960,540],"records":[{"id":0,"value":0.73},...],"frame":8}
```

**Effort**: ~8 hours (shader + C++ host + CLI flag + JSON output)

### Pattern B: Golden Image Test Runner (MEDIUM PRIORITY)

Create a Python script that automates the existing `--screenshot-stdout` infrastructure:

**New file: `scripts/visual-test.py`**

```python
#!/usr/bin/env python3
"""Visual regression test runner for UzEngine.

Captures screenshots from demos at predefined camera positions,
compares against golden reference images using PSNR.

Usage:
    python scripts/visual-test.py                    # run all tests
    python scripts/visual-test.py --update           # update golden images
    python scripts/visual-test.py --test shadows     # run specific test
"""

import subprocess, base64, json, sys, os
from pathlib import Path

TESTS = {
    "local-shadows-default": {
        "demo": "demos/local-shadows/zig-out/bin/local-shadows.exe",
        "args": ["--headless", "--screenshot-stdout", "--frames", "8"],
        "camera": {"x": 0, "y": 0, "z": 300, "yaw": 0, "pitch": -15},
    },
    "local-shadows-altitude": {
        "demo": "demos/local-shadows/zig-out/bin/local-shadows.exe",
        "args": ["--headless", "--screenshot-stdout", "--frames", "8"],
        "camera": {"x": 0, "y": 0, "z": 700, "yaw": 0, "pitch": -30},
    },
    "local-shadows-diag": {
        "demo": "demos/local-shadows/zig-out/bin/local-shadows.exe",
        "args": ["--diag"],
        "type": "diag",  # parse DIAG_JSON instead of screenshot
    },
}

GOLDEN_DIR = Path("tests/golden")
PSNR_THRESHOLD = 40.0  # dB — below this = regression

def run_test(name, spec):
    args = [spec["demo"]] + spec["args"]
    if "camera" in spec:
        cam = spec["camera"]
        for k, v in cam.items():
            args.extend([f"--camera-{k}", str(v)])

    result = subprocess.run(args, capture_output=True, text=True, timeout=60)

    if spec.get("type") == "diag":
        for line in result.stdout.splitlines():
            if line.startswith("DIAG_JSON:"):
                report = json.loads(line[len("DIAG_JSON:"):])
                if report["hasFailure"]:
                    return False, f"FAIL: {[t['name'] for t in report['targets'] if t['status'].startswith('FAIL')]}"
                if report["hasWarning"]:
                    return False, f"WARN: {[t['name'] for t in report['targets'] if t['status'] == 'WARN_ZERO']}"
                return True, "OK"
        return False, "No DIAG_JSON output"

    # Screenshot comparison
    for line in result.stdout.splitlines():
        if line.startswith("SCREENSHOT_BASE64:"):
            png_data = base64.b64decode(line[len("SCREENSHOT_BASE64:"):])
            # ... compare with golden using Pillow or ImageMagick
            return True, f"PSNR > {PSNR_THRESHOLD}"
    return False, "No screenshot output"
```

**Effort**: ~6 hours (Python script + golden image capture + CI integration)

### Pattern C: Falcor as a Parallel Prototyping Sandbox (LOW PRIORITY)

Use Falcor separately to **validate rendering techniques before porting to UzEngine**:

1. Prototype a new effect in Falcor (e.g., volumetric fog, SSR, GTAO)
2. Use Falcor's PixelDebug to verify shader math
3. Run Falcor's image tests to establish golden references
4. Port the validated HLSL/Slang math to UzEngine's HLSL shaders
5. Compare UzEngine's output against Falcor's reference using `--diff`

**This does NOT require any code integration** — Falcor runs as a separate application.

**When to use this workflow:**
- Implementing a new SIGGRAPH paper (Falcor already has a full path tracer as ground truth)
- Debugging complex multi-bounce lighting (Falcor has reference path tracer for comparison)
- Validating BRDF implementations (Falcor has BSDFViewer pass for isolated material testing)

**Effort**: Just install Falcor. No UzEngine code changes needed.

---

## Priority Summary

| Pattern | Impact | Effort | Dependency |
|---------|--------|--------|------------|
| **A: PixelDebug** | Eliminates shader-edit-rebuild cycle entirely | 8 hours | New HLSL include + C++ pass + CLI flag |
| **B: Golden Test Runner** | Automated regression detection | 6 hours | Python script only, no C++ changes |
| **C: Falcor Sandbox** | Validated technique prototyping | 0 hours | Install Falcor separately |

**Recommended order**: C (free) → A (biggest debug impact) → B (CI automation)

---

## What We Should NOT Do

1. **Do NOT try to embed Falcor into UzEngine** — they have incompatible DX12 device ownership models
2. **Do NOT adopt Slang** — our HLSL pipeline works, and Slang adds a complex dependency
3. **Do NOT copy Falcor's scene format** — our `.uzscene` format is sufficient
4. **Do NOT replace our render graph with Falcor's** — ours already works; adopt specific patterns (Persistent flag, Optional inputs) instead

## References

- NVIDIA Falcor: `github.com/NVIDIAGameWorks/Falcor`
- Falcor render graph tutorial: `docs/tutorials/03-creating-and-editing-render-graphs.md`
- Falcor PixelDebug: `Source/Falcor/Utils/Debug/PixelDebug.h`
- Falcor image tests: `tests/image_tests/`
- FLIP perceptual metric: `Source/RenderPasses/FLIPPass/`
- Shader Printf technique: `therealmjp.github.io/posts/hlsl-printf/`
