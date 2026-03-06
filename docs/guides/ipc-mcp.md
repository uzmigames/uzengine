# UzEngine — IPC and MCP Control System

## Overview

UzEngine exposes two complementary interfaces for control and monitoring:

1. **File-based IPC** — JSON commands read from disk by a polling thread in the engine
2. **MCP Server** — Tool interface wrapping IPC, exposing 5 stable primitives to Claude Code

This allows both AI agents and external tools to control scene parameters, trigger actions, and query engine state.

---

## File-Based IPC

### Directory Structure

```
build/
├── Release/
│   ├── render.exe
│   ├── turbojpeg.dll
│   └── preview/          ← IPC base directory
│       ├── command.json  ← external commands written here
│       ├── status.json   ← engine writes metrics here
│       └── frame_*.png   ← screenshots
```

The IPC base directory is computed at startup from the exe path:
```c++
std::string exeDir = "/path/to/build/Release";
std::string ipcBaseDir = exeDir + "/preview";  // build/preview
```

### Command Format (command.json)

```json
{
  "function": "scene.setRotation",
  "args": {
    "y": 2.5
  },
  "id": "unique-request-id"
}
```

**Fields:**
- `function` (required): handler name, e.g., `"scene.setRotation"`
- `args` (optional): JSON object passed to the handler
- `id` (optional): request identifier for response correlation

### Response Format (status.json)

```json
{
  "alive": true,
  "fps": 45.3,
  "frame": 2718,
  "uptime_s": 60,
  "encoder": "JPEG",
  "resolution": "1280x720",
  "scene": {
    "rotation_speed": [0.0, 1.5, 0.0],
    "background": [0.1, 0.1, 0.1],
    "geometry": "pyramid",
    "paused": false
  }
}
```

Updated by the engine ~1 second.

### IPC Poller Implementation (src/common/ipc_poller.h/cpp)

**Polling thread:**
```c++
class IpcPoller {
public:
    IpcPoller(const std::string& baseDir, IpcRegistry& registry,
              SceneParams& scene, EngineMetrics& metrics);
    void start();   // spawn polling thread
    void stop();    // join thread

private:
    void pollLoop();  // reads command.json every 50 ms
};
```

**Poll cycle (50 ms interval):**
1. Check if `command.json` exists
2. Read and parse JSON
3. Extract `function` and `args`
4. Call `IpcRegistry::call(function, args)`
5. Write result to `status.json`
6. Delete `command.json` to indicate processing

**Error handling:**
- Invalid JSON → logged and ignored
- Unknown function → error response in status.json
- Path traversal attempts (`../`) → rejected at runtime

### IPC Handlers Registered in main.cpp

Handlers are registered in `src/main.cpp` during startup:

```c++
ipcRegistry.add("engine.ping", [&](const nlohmann::json&) {
    return {{"ok", true},
            {"fps", g_metrics.fps.load()},
            {"frame", g_metrics.frameCount.load()}};
});

ipcRegistry.add("scene.setRotation", [&](const nlohmann::json& args) {
    if (args.contains("x")) g_scene.rotX.store(args["x"].get<float>());
    if (args.contains("y")) g_scene.rotY.store(args["y"].get<float>());
    if (args.contains("z")) g_scene.rotZ.store(args["z"].get<float>());
    return {{"ok", true}};
});

ipcRegistry.add("scene.setBackground", [&](const nlohmann::json& args) {
    std::lock_guard<std::mutex> lk(g_scene.bgMutex);
    g_scene.bgR = args.value("r", g_scene.bgR);
    g_scene.bgG = args.value("g", g_scene.bgG);
    g_scene.bgB = args.value("b", g_scene.bgB);
    return {{"ok", true}};
});

ipcRegistry.add("scene.setPaused", [&](const nlohmann::json& args) {
    g_scene.paused.store(args.value("paused", false));
    return {{"ok", true}};
});

ipcRegistry.add("engine.screenshot", [&](const nlohmann::json& args) {
    std::string id = args.value("id", "screenshot");
    uz::FrameData frame = renderer.getLastFrameCopy();
    // write frame to PNG file in preview/
    return {{"ok", true}, {"path", filename}};
});

// ... more handlers: file.read, file.write, file.list, etc.
```

---

## MCP Server

### Overview

The MCP (Model Context Protocol) server wraps the file-based IPC into 5 stable tool primitives. This allows Claude Code to interact with the engine using familiar tool calls.

**Location:** `tools/mcp-uzengine/index.js` (Node.js stdio transport)

**Setup:** Auto-discovered via `.mcp.json` in workspace root:
```json
{
  "mcpServers": {
    "uzengine": {
      "command": "node",
      "args": ["tools/mcp-uzengine/index.js"]
    }
  }
}
```

### Tool Definitions

#### 1. uz_call

**Purpose:** Call any registered engine function.

**Signature:**
```python
def uz_call(
    function: str,           # e.g., "scene.setRotation"
    args: dict = None,       # parameters for the function
    timeout_ms: int = 4000   # max wait time
) -> dict
```

**Returns:** Engine handler response as JSON

**Example:**
```python
result = uz_call("scene.setRotation", {"y": 2.5}, 4000)
# Returns: {"ok": true}

result = uz_call("engine.ping")
# Returns: {"ok": true, "fps": 45.3, "frame": 2718}
```

**Implementation:**
```javascript
async function handleUzCall(args) {
    const fn = args.function;
    const fnArgs = args.args ?? {};
    const timeoutMs = args.timeout_ms ?? 4000;

    const result = await engineCall(fn, fnArgs, timeoutMs);
    return textResult(JSON.stringify(result, null, 2));
}
```

#### 2. uz_screenshot

**Purpose:** Capture the current rendered frame as a PNG image.

**Signature:**
```python
def uz_screenshot(
    wait_ms: int = 4000
) -> dict  # Returns inline base64 image
```

**Returns:**
- `ok: true`, `path`, `size_bytes`
- Inline `image/png` content for Claude to see

**Implementation:**
```javascript
async function handleUzScreenshot(args) {
    const waitMs = args.wait_ms ?? 4000;
    const id = randomUUID();

    // 1. Ask engine to save PNG
    const callResult = await engineCall('engine.screenshot', { id }, waitMs);

    // 2. Wait for PNG file to appear on disk
    const pngPath = path.join(PREVIEW_DIR, `frame_${id}.png`);
    const appeared = await waitForFile(pngPath, waitMs);

    // 3. Read and base64-encode
    const pngBuffer = readFile(pngPath);
    const base64 = pngBuffer.toString('base64');

    // 4. Return text + inline image
    return {
        content: [
            { type: 'text', text: JSON.stringify({ ok: true, size: pngBuffer.length }) },
            { type: 'image', data: base64, mimeType: 'image/png' }
        ]
    };
}
```

#### 3. uz_status

**Purpose:** Read real-time engine metrics.

**Signature:**
```python
def uz_status() -> dict
```

**Returns:**
```json
{
  "alive": true,
  "fps": 45.3,
  "frame": 2718,
  "uptime_s": 60,
  "encoder": "JPEG",
  "resolution": "1280x720",
  "scene": {
    "rotation_speed": [0.0, 1.5, 0.0],
    "background": [0.1, 0.1, 0.1],
    "geometry": "pyramid",
    "paused": false
  }
}
```

Or if engine is not running:
```json
{ "alive": false, "error": "..." }
```

**Implementation:**
```javascript
function handleUzStatus() {
    const status = readStatus();  // reads status.json from disk
    return textResult(JSON.stringify(status, null, 2));
}
```

#### 4. uz_read_file

**Purpose:** Read a project file.

**Signature:**
```python
def uz_read_file(path: str) -> dict
```

**Path:** Relative to `build/Release/` (the engine's working directory)
- `"../../src/renderer/renderer.cpp"` → project root/src/renderer/renderer.cpp
- `"../../shaders/custom.hlsl"` → project root/shaders/custom.hlsl

**Returns:**
```json
{ "ok": true, "content": "file contents..." }
```

**Implementation:** Calls IPC handler `file.read`:
```javascript
async function handleUzReadFile(args) {
    const result = await engineCall('file.read', { path: args.path });
    return textResult(JSON.stringify(result, null, 2));
}
```

#### 5. uz_write_file

**Purpose:** Write a project file.

**Signature:**
```python
def uz_write_file(path: str, content: str) -> dict
```

**Returns:**
```json
{ "ok": true }
```

**Security:** Path traversal attempts (`../`) are rejected by the engine.

**Implementation:** Calls IPC handler `file.write`:
```javascript
async function handleUzWriteFile(args) {
    const result = await engineCall('file.write', {
        path: args.path,
        content: args.content
    });
    return textResult(JSON.stringify(result, null, 2));
}
```

#### 6. uz_restart (bonus)

**Purpose:** Kill and restart the engine process.

**Signature:**
```python
def uz_restart(wait_ms: int = 8000, windowed: bool = False) -> dict
```

**Implementation:**
```javascript
async function handleUzRestart(args) {
    const waitMs = args.wait_ms ?? 8000;
    const windowed = args.windowed ?? false;

    killEngine();
    await new Promise(resolve => setTimeout(resolve, 800));

    const spawnResult = spawnEngine({ windowed });
    if (!spawnResult.ok) {
        return textResult(JSON.stringify({ ok: false, error: spawnResult.error }));
    }

    // Poll until engine becomes alive or timeout
    const deadline = Date.now() + waitMs;
    while (Date.now() < deadline) {
        await new Promise(resolve => setTimeout(resolve, 500));
        const status = readStatus();
        if (status.alive) {
            return textResult(JSON.stringify({
                ok: true,
                pid: spawnResult.pid,
                elapsed_ms: Date.now() - start
            }));
        }
    }
    return textResult(JSON.stringify({
        ok: false,
        error: `Engine did not come online within ${waitMs}ms`
    }));
}
```

---

## Registered IPC Functions (Complete List)

### Editor (UZ_EDITOR only)

Requires engine running in `--editor` mode with the scene system.

#### `editor.setGizmo`

Set the active node and gizmo mode for transformation handles.

**Args:**
```json
{
  "nodeId": 5,
  "mode": "translate"
}
```

**Parameters:**
- `nodeId` (uint32): Scene node ID to attach gizmo to; `null` to hide gizmo
- `mode` (string): `"translate"` | `"rotate"` | `"scale"` | `"none"`

**Returns:**
```json
{ "ok": true }
```

**Example:**
```json
uz_call("editor.setGizmo", {"nodeId": 5, "mode": "translate"})
uz_call("editor.setGizmo", {"nodeId": null, "mode": "none"})
```

---

#### `editor.setGizmoHover`

Highlight a specific axis or plane on the active gizmo (hover feedback).

**Args:**
```json
{
  "axis": "x"
}
```

**Parameters:**
- `axis` (string): `"x"` | `"y"` | `"z"` | `"xy"` | `"xz"` | `"yz"` | `"center"` | `null`
  - `"x"`, `"y"`, `"z"`: single axis (arrow or arc or line)
  - `"xy"`, `"xz"`, `"yz"`: plane quads
  - `"center"`: uniform scale cube or ring
  - `null`: clear all highlights

**Returns:**
```json
{ "ok": true }
```

**Color Scheme:**
- X: `#CC2222` (red) → hover `#FFFFFF` (white)
- Y: `#22CC22` (green) → hover `#FFFFFF` (white)
- Z: `#2222CC` (blue) → hover `#FFFFFF` (white)
- Center: `#CCCC22` (yellow) → hover `#FFFFFF` (white)

---

#### `editor.setGizmoSpace`

Toggle between local (node-relative) and world (global) coordinate frames.

**Args:**
```json
{
  "space": "world"
}
```

**Parameters:**
- `space` (string): `"world"` | `"local"`
  - `"world"`: gizmo arrows align with global X/Y/Z axes
  - `"local"`: gizmo arrows align with the node's rotation

**Returns:**
```json
{ "ok": true }
```

---

#### `editor.setNodeTransformLive`

Apply a live transform during drag (updates in real-time, no undo entry yet).

**Args:**
```json
{
  "nodeId": 5,
  "position": [1.0, 0.0, 0.5],
  "rotation": [0.707, 0.0, 0.0, 0.707],
  "scale": [1.0, 1.0, 1.0]
}
```

**Parameters:**
- `nodeId` (uint32): Scene node to transform
- `position` (array, optional): `[x, y, z]` world position
- `rotation` (array, optional): `[w, x, y, z]` quaternion (w is scalar)
- `scale` (array, optional): `[x, y, z]` per-axis scale

**Returns:**
```json
{ "ok": true }
```

**Notes:**
- All transform components are optional — omit any not being modified
- Called frequently during mouse drag (up to 60 times/sec)
- No undo entry created — used for live feedback only

---

#### `editor.commitTransform`

Finalize a drag operation. The engine returns the authoritative final transform (after any clamping/rounding) for undo stack registration.

**Args:**
```json
{
  "nodeId": 5
}
```

**Returns:**
```json
{
  "ok": true,
  "transform": {
    "position": [1.0, 0.0, 0.5],
    "rotation": [0.707, 0.0, 0.0, 0.707],
    "scale": [1.0, 1.0, 1.0]
  }
}
```

**Notes:**
- Called on `mouseup` to finalize the drag
- The returned `transform` is the engine's authoritative state
- Extension uses the returned transform as `newValue` when pushing `TransformCommand` to undo stack
- This ensures undo/redo stays synchronized with engine state

---

#### `editor.getCamera`

Fetch the current camera matrices and position for ray unprojection and gizmo scale computation.

**Args:**
```json
{}
```

**Returns:**
```json
{
  "ok": true,
  "view": [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0],
  "proj": [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0],
  "position": [0.0, 5.0, 10.0]
}
```

**Matrix Format:**
- `view` (array[16]): Column-major view matrix (transposed from DX12's row-major)
- `proj` (array[16]): Column-major projection matrix
- `position` (array[3]): Camera world position `[x, y, z]`

**Usage:**
```typescript
// Unproject screen coordinates to world ray
const ndcX = (px / canvasWidth) * 2 - 1;
const ndcY = 1 - (py / canvasHeight) * 2;
const invVP = mat4Invert(mat4Multiply(proj, view));
const nearPoint = mat4TransformPoint(invVP, [ndcX, ndcY, -1]);
const farPoint  = mat4TransformPoint(invVP, [ndcX, ndcY,  1]);
const direction = vec3Normalize(vec3Subtract(farPoint, nearPoint));

// Compute gizmo scale based on distance
const distance = vec3Length(vec3Subtract(gizmoOrigin, cameraPos));
const gizmoScale = distance * 0.1;
```

---

#### `editor.pickNode`

GPU-accelerated picking: read the pick buffer at screen coordinates to determine which node (if any) is under the cursor.

**Args:**
```json
{
  "screen_x": 512,
  "screen_y": 300
}
```

**Returns:**
```json
{ "nodeId": 5 }
```

or if clicking empty space:

```json
{ "nodeId": null }
```

**Notes:**
- Uses the pre-rendered pick buffer (R8G8B8A8_UNORM, 1/4 resolution)
- Each pixel encodes a 24-bit node ID as RGB bytes
- Call this on left-click to determine selection

---

#### `editor.boxPick`

GPU-accelerated multi-select: read the pick buffer within a screen-space rectangle to get all nodes in the selection.

**Args:**
```json
{
  "x": 100,
  "y": 50,
  "width": 200,
  "height": 150
}
```

**Returns:**
```json
{
  "nodeIds": [5, 12, 8]
}
```

or if no nodes in rect:

```json
{ "nodeIds": [] }
```

**Notes:**
- Scans all pixels within the rectangle and returns unique node IDs
- Used for Ctrl+drag box-select operations
- Returns a deduplicated list

---

### Phase 0: Core (Currently Active)

| Function | Args | Description |
|----------|------|-------------|
| `engine.ping` | — | Quick status: `{ok, fps, frame}` |
| `engine.getStatus` | — | Full snapshot (rotation, background, geometry, paused) |
| `engine.screenshot` | `{id?}` | Save current frame as PNG (id is optional identifier) |
| `scene.setRotation` | `{x?, y?, z?}` | Set rotation speeds (rad/s), each axis optional |
| `scene.setBackground` | `{r, g, b}` | Set clear color (0..1 floats) |
| `scene.setGeometry` | `{shape}` | `"pyramid"` or `"cube"` (stored, not yet hot-swapped) |
| `scene.setPaused` | `{paused}` | Boolean: pause/resume animation |
| `scene.setParam` | `{key, value}` | Generic key-value (e.g., `"rotation.y"`) |
| `file.read` | `{path}` | Read file relative to build dir (with `../` support) |
| `file.write` | `{path, content}` | Write file (path traversal rejected) |
| `file.list` | `{path}` | List directory contents |
| `shader.reload` | `{name}` | Hot-reload shader by name (not yet implemented) |

### Phase 1+: Lua Scripting (Editor-only, UZ_EDITOR)

Requires scene system and Lua VM running.

| Function | Args | Description |
|----------|------|-------------|
| `lua.eval` | `{code}` | Evaluate Lua expression, return result (JSON-serializable) |
| `lua.reload_script` | `{path}` | Force reload a `.lua` file from disk, restart affected nodes |
| `lua.get_errors` | — | List all Lua runtime errors with file:line, error message |
| `lua.get_globals` | — | Dump Lua global table (_G) as JSON for inspection |

**Example:**
```json
uz_call("lua.eval", {"code": "return math.sqrt(16)"})
→ {"ok": true, "result": 4.0}

uz_call("lua.get_errors")
→ {"ok": true, "errors": [{"file": "game/logic.lua", "line": 42, "message": "bad argument #1"}]}
```

### Phase 2+: Audio (Editor-only, UZ_EDITOR)

Requires XAudio2 audio system.

| Function | Args | Description |
|----------|------|-------------|
| `audio.play` | `{path, volume?, pitch?}` | Test play a sound clip; volume [0..1], pitch [0.5..2.0] |
| `audio.stop_all` | — | Stop all currently playing audio |
| `audio.set_bus_volume` | `{bus, volume}` | Set volume of a bus (e.g., "Music", "SFX", "Voice"); volume [0..1] |

**Example:**
```json
uz_call("audio.play", {"path": "res://assets/sounds/jump.wav", "volume": 0.8})
uz_call("audio.set_bus_volume", {"bus": "Music", "volume": 0.5})
```

### Phase 3+: Physics (Editor-only, UZ_EDITOR)

Requires Jolt Physics system.

| Function | Args | Description |
|----------|------|-------------|
| `physics.debug_draw` | `{enabled}` | Toggle physics wireframe visualization on/off |
| `physics.set_gravity` | `{x, y, z}` | Override gravity vector (default: [0, -9.81, 0]) |
| `physics.ray_cast` | `{origin, direction, max_dist}` | Cast a ray; returns hit info (object, distance, position, normal) |

**Example:**
```json
uz_call("physics.debug_draw", {"enabled": true})
uz_call("physics.set_gravity", {"x": 0, "y": -20, "z": 0})
uz_call("physics.ray_cast", {"origin": [0, 5, 0], "direction": [0, -1, 0], "max_dist": 100})
```

### Phase 2+: Console (Editor-only, UZ_EDITOR)

Requires in-game console system.

| Function | Args | Description |
|----------|------|-------------|
| `console.execute` | `{command}` | Execute a console command (e.g., "god_mode", "time_scale 0.5") |
| `console.get_commands` | — | List all registered console commands with descriptions |
| `console.get_history` | — | Last 100 console commands + their outputs |

**Example:**
```json
uz_call("console.execute", {"command": "god_mode"})
uz_call("console.get_commands")
→ {"ok": true, "commands": [{"name": "god_mode", "desc": "Toggle invincibility"}, ...]}
```

### Phase 3+: Saves (Editor-only, UZ_EDITOR)

Requires save/load system.

| Function | Args | Description |
|----------|------|-------------|
| `saves.list_slots` | — | List all save slots (0-9, -1 autosave) with metadata (timestamp, playtime, thumbnail) |
| `saves.delete_slot` | `{slot}` | Delete a save slot |
| `saves.inspect_slot` | `{slot}` | Dump save as JSON for debugging (node tree, Lua state snapshot) |

**Example:**
```json
uz_call("saves.list_slots")
→ {"ok": true, "slots": [{"slot": 0, "timestamp": "2026-03-01T12:34:56", "playtime_sec": 1234}, ...]}

uz_call("saves.inspect_slot", {"slot": 0})
→ {"ok": true, "data": {... full game state as JSON ...}}
```

### Phase 2+: Localization (Editor-only, UZ_EDITOR)

Requires localization system.

| Function | Args | Description |
|----------|------|-------------|
| `locale.get_missing_keys` | — | Find keys present in one locale but missing in others (localization audit) |
| `locale.set` | `{code}` | Change active locale for editor preview (e.g., "pt_br", "ja") |

**Example:**
```json
uz_call("locale.set", {"code": "pt_br"})
uz_call("locale.get_missing_keys")
→ {"ok": true, "missing": {"en": ["ui.menu.newgame"], "pt_br": []}}
```

---

## Thread Safety

### IPC Poller Thread

- Spawned at startup, killed on shutdown
- Polls command.json every 50 ms
- Calls handlers synchronously (blocks poller while executing)
- Handlers must be thread-safe (use mutexes where needed)

### SceneParams Synchronization

```c++
struct SceneParams {
    std::atomic<float> rotX, rotY, rotZ;      // lock-free reads
    std::atomic<int> geometryType;
    std::atomic<bool> paused;

    std::mutex bgMutex;
    float bgR, bgG, bgB;  // protected by mutex
};
```

- **Atomics** for rotation speeds and pause state (fast reads from render thread)
- **Mutex** for background color (3 related floats, keep atomic together)

### EngineMetrics

```c++
struct EngineMetrics {
    std::atomic<float> fps;
    std::atomic<uint32_t> frameCount;
    std::string encoderName;    // set once at startup
    std::string resolution;     // set once at startup
};
```

- FPS and frame count are atomic (written by render thread, read by IPC)
- Encoder name and resolution never change (no synchronization needed)

---

## Example Use Cases

### Claude Code: Change Scene Appearance

```python
# Call the MCP tools
uz_call("scene.setBackground", {"r": 0.0, "g": 0.05, "b": 0.15})
uz_call("scene.setRotation", {"y": 1.2})
screenshot = uz_screenshot()  # Verify the result
# Claude sees the screenshot inline
```

### Claude Code: Debug Performance

```python
status = uz_status()
if status['alive']:
    print(f"Engine FPS: {status['fps']}")
    if status['fps'] < 30:
        # Something is wrong
        uz_call("scene.setPaused", {"paused": True})
        screenshot = uz_screenshot()  # Check for visual issues
```

### Claude Code: Write and Reload Shader

```python
shader_code = """
// Custom HLSL shader
cbuffer TransformCB : register(b0) {
    float4x4 worldViewProj;
};

struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
};

// ... rest of shader
"""

uz_write_file({"path": "../../shaders/custom.hlsl", "content": shader_code})
uz_call("shader.reload", {"name": "custom"})
uz_screenshot()
```

---

## Common Patterns

### Poll Until Done

```python
import time

def wait_until_online(timeout_s=10):
    """Wait for engine to come online."""
    start = time.time()
    while time.time() - start < timeout_s:
        status = uz_status()
        if status.get('alive'):
            return True
        time.sleep(0.5)
    return False
```

### Batch Operations

```python
def setup_scene():
    """Set up initial scene configuration."""
    uz_call("scene.setBackground", {"r": 0.02, "g": 0.02, "b": 0.05})
    uz_call("scene.setRotation", {"x": 0.1, "y": 1.5, "z": 0.05})
    uz_call("scene.setPaused", {"paused": False})
    return uz_screenshot()
```

### Exponential Backoff on Timeout

```python
import asyncio

async def call_with_backoff(fn, args, max_retries=3):
    """Call uz_call with exponential backoff."""
    delay = 1
    for attempt in range(max_retries):
        try:
            return await uz_call(fn, args)
        except TimeoutError:
            if attempt < max_retries - 1:
                await asyncio.sleep(delay)
                delay *= 2
            else:
                raise
```

---

## Troubleshooting

### "Engine not running" (uz_status returns alive: false)

- Check that render.exe was started (VSCode extension opens the stream viewer)
- Check that `build/Release/preview/` directory exists
- Restart the engine via `uz_restart()`

### "Timeout waiting for response"

- Engine may be stuck or slow
- Try `uz_restart()` to kill and restart
- Check CPU load with `uz_screenshot()` for visual artifacts

### "Path traversal not allowed"

- Avoid `../` in file paths; use them from `build/Release/` as base
- Correct: `"../../src/renderer/renderer.cpp"`
- Incorrect: `"../../../../../../../../etc/passwd"`

### "Unknown function"

- Check the function name spelling
- Refer to the complete list above
- Some functions (shader.reload, geometry hot-swap) are not yet implemented
