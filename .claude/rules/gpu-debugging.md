# GPU Debugging — Never Guess, Always Diagnose

When rendering produces wrong output, follow this exact diagnostic order:

1. **Buffer Visualization Grid First** — `--shadow-debug 1` renders a 3x3 diagnostic grid showing Albedo, Normal, Raw Atlas, Shadow, LIT, Depth, Cascade, AO, Roughness in a single frame. This is the fastest way to identify which stage is broken.

2. **RenderDoc Capture** — capture a frame and analyze programmatically via CLI. NEVER open the GUI manually — use automated scripts.

3. **CPU Readback** — if shader-side visualization is insufficient, add a render graph pass that copies GPU data to a readback buffer. NEVER do readback outside the render graph (causes device removed).

4. **Validate Against Known-Good Values** — compare with mathematically predictable values from UE5 reference.

## Buffer Visualization Grid

The grid is a permanent debug tool built into the deferred lighting shader. It shows 9 buffers simultaneously at the original scene resolution.

```bash
# Activate grid mode
demo.exe --shadow-debug 1

# Capture grid as base64 (PREFERRED — no disk I/O)
demo.exe --shadow-debug 1 --screenshot-stdout --screenshot-frame 5 --frames 8

# Capture grid to disk (when you need to compose grids)
demo.exe --shadow-debug 1 --screenshot e:/UzEngine/.screenshots/grid.png --screenshot-frame 5 --frames 8
```

**Grid layout (3x3):**
| (0,0) Albedo | (1,0) Normal | (2,0) Raw Atlas |
|---|---|---|
| **(0,1) Shadow** | **(1,1) LIT** | **(2,1) Depth** |
| (0,2) Cascade | (1,2) AO | (2,2) Roughness |

- **Tile 2 (Raw Atlas)**: Shows shadow depth texture content directly — geometry should be visible as darker regions. If the tile is all white (1.0), the shadow pass isn't writing.
- **Tile 3 (Shadow)**: Shows the final shadow value per pixel (white=lit, black=shadowed). If all white, the shadow sampling is broken.
- **Tile 6 (Cascade)**: Color-coded cascade indices — red/green/blue/yellow = cascade 0/1/2/3.

**Implementation**: `runtime/shaders/runtime/include/shadow_debug.hlsli` + integration in `deferred_lighting_ps.hlsl`. Activated via `PerFrameCB::shadowDebugMode` (uint, 0=off, 1=grid).

## RenderDoc Integration

RenderDoc is fully integrated into UzEngine. The API is loaded at runtime from `renderdoc.dll`.

### Capture Methods

```bash
# Method 1: Auto-capture specific frame (best for CI/automated debugging)
demo.exe --renderdoc-capture 10 --frames 15

# Method 2: Capture via renderdoccmd (external, no code changes needed)
renderdoccmd capture \
  -c ".captures/shadow" \
  -w \
  --opt-ref-all-resources \
  demo.exe --frames 15

# Method 3: F12 key at runtime (when renderdoc.dll is loaded)
# Press F12 while running — capture saved to working directory

# Method 4: Launch from RenderDoc GUI
# File → Launch Application → select exe → F12 to capture
```

### Headless Analysis via Python

RenderDoc's Python API runs inside `qrenderdoc --python`. Write scripts that open the `.rdc`, replay, and extract data:

```bash
# Run analysis script (qrenderdoc executes Python then exits)
qrenderdoc --python analyze.py capture.rdc
```

**Script pattern:**
```python
import renderdoc as rd

rd.InitialiseReplay(rd.GlobalEnvironment(), [])
cap = rd.OpenCaptureFile()
cap.OpenFile('capture.rdc', '', None)
result, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)

# List actions
for a in ctrl.GetRootActions():
    print(f"EID {a.eventId}: idx={a.numIndices}")

# Go to specific event
ctrl.SetFrameEvent(40, True)
pipe = ctrl.GetPipelineState()
vp = pipe.GetViewport(0)

# Read pixel from texture
sub = rd.Subresource(0, 0, 0)
px = ctrl.PickPixel(resource_id, x, y, sub, rd.CompType.Float)
depth = px.floatValue[0]

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
```

**Important**: The Python module only works inside qrenderdoc's embedded Python 3.6, NOT with the system Python. Always use `qrenderdoc --python script.py`.

### RenderDoc Capture Options

- `--opt-ref-all-resources`: Include all resources (needed for texture inspection). Do NOT use `--opt-api-validation` — it requires D3D12 Debug Layer which causes E_INVALIDARG on replay.
- Captures are saved as `.rdc` files in the specified directory.
- Use `renderdoccmd thumb --out thumb.jpg capture.rdc` for quick visual check.

### Shader Debugging in RenderDoc

For HLSL step-through in RenderDoc GUI, compile shaders with:
- `-Zi` (debug info symbols)
- `-Od` (disable optimizations)

These flags are available via `ShaderCompileFlags::generateDebugInfo` and `ShaderCompileFlags::disableOptimizations`.

## Required Infrastructure

- PIX Event Markers on every render/compute pass (via `UZ_PROFILE_SCOPE`)
- Buffer visualization grid (permanent, `--shadow-debug 1`)
- RenderDoc integration (`renderdoc_capture.h`) — always available
- DX12 Debug Layer + GPU-Based Validation in debug builds
- DRED Breadcrumbs for device-removed crashes
- Shadow atlas readback (TODO: integrate into render graph as a proper pass)

## NEVER

- Iterate blindly by changing shader code and hoping it works
- Guess at parameter values to "fix" visual bugs
- Add compensation factors to make wrong output look right
- Do GPU readback outside the render graph (causes device removed / hang)
- Use `--opt-api-validation` with renderdoccmd capture (breaks replay)
- Destroy readback buffers before GPU is done with them (triple-buffer fence wait required)
- Open RenderDoc GUI manually — use `renderdoccmd` + `qrenderdoc --python` for automated analysis
