# UzEngine API Reference

All communication between the VSCode extension (editor) and the engine runtime
happens through two channels: **WebSocket** (real-time) and **IPC** (file-based
JSON request/response). The MCP server wraps IPC for AI tool use.

---

## Transport Overview

| Channel   | Protocol            | Use case                              |
|-----------|---------------------|---------------------------------------|
| WebSocket | JSON text frames    | Real-time input, camera, gizmos, live parameter editing |
| IPC       | File-based JSON     | Scene loading, screenshots, diagnostics, introspection  |
| MCP       | MCP tool calls      | AI agent access to IPC commands       |

---

## 1. WebSocket Commands

Sent as JSON text frames with a `type` field.
Direction: **Editor -> Engine** unless noted.

### 1.1 Viewport

| Command | Fields | Description |
|---------|--------|-------------|
| `viewport_resize` | `width: int, height: int` | Request render target resize (64-7680 x 64-4320) |
| `camera_input` | `w,s,a,d,q,e: float`, `mouseDx,mouseDy: float`, `scroll: float`, `rightMouse,shift: bool` | Per-frame camera input state |

### 1.2 Node Selection & Picking

| Command | Fields | Description |
|---------|--------|-------------|
| `editor.pickNode` | `screen_x, screen_y: float` | Pick nearest node at screen coords |
| `editor.boxPick` | `x0, y0, x1, y1: float` | Box-select nodes in screen rect |
| `editor.selectNode` | `nodeId: uint` | Select a node by ID |
| `editor.focusNode` | `nodeId?: uint` | Focus camera on node (default: selected) |

**Responses** (Engine -> Editor):

| Response | Fields | Description |
|----------|--------|-------------|
| `pickResult` | `nodeId: uint, distance: float` | Nearest picked node |
| `boxPickResult` | `nodeIds: uint[]` | All nodes inside box |
| `cameraFocused` | `nodeId: uint, position: float[3]` | Camera repositioned |

### 1.3 Gizmo System

| Command | Fields | Description |
|---------|--------|-------------|
| `gizmo_mode` | `mode: "translate"\|"rotate"\|"scale"` | Set gizmo type |
| `gizmo_space` | `space: "world"\|"local"` | Set transform space |
| `editor.setGizmo` | `mode: string` | Alternative gizmo mode setter |
| `editor.setGizmoHover` | `axis: "x"\|"y"\|"z"` | Highlight gizmo axis |
| `editor.setGizmoSpace` | `space: "world"\|"local"` | Alternative space setter |
| `editor.gizmoBeginDrag` | `nodeId: uint, axis: string, screen_x, screen_y: float` | Start gizmo drag |
| `editor.gizmoDrag` | `screen_x, screen_y: float` | Continue gizmo drag |
| `editor.gizmoEndDrag` | _(none)_ | End gizmo drag |
| `editor.gizmoHover` | `screen_x, screen_y: float` | Test gizmo hover hit |

**Responses**:

| Response | Fields | Description |
|----------|--------|-------------|
| `gizmoHoverResult` | `axis: string\|null` | Hovered gizmo axis |
| `gizmoDragUpdate` | `nodeId, transform: {...}` | Live transform during drag |

### 1.4 Transform

| Command | Fields | Description |
|---------|--------|-------------|
| `editor.setNodeTransformLive` | `nodeId: uint, transform: {position,rotation,scale}` | Apply live transform (during drag) |
| `editor.commitTransform` | `nodeId: uint` | Finalize transform, get result |

**Responses**:

| Response | Fields | Description |
|----------|--------|-------------|
| `commitTransformResult` | `nodeId: uint, transform: {...}` | Committed transform values |

### 1.5 Camera

| Command | Fields | Description |
|---------|--------|-------------|
| `editor.getCamera` | _(none)_ | Request view/projection matrices |

**Responses**:

| Response | Fields | Description |
|----------|--------|-------------|
| `cameraMatrices` | `view: float[16], proj: float[16], position: float[3]` | Camera matrices |

### 1.6 Benchmark

| Command | Fields | Description |
|---------|--------|-------------|
| `benchmark.orbit` | `enable?: bool` (default `true`) | Toggle auto-orbit camera around scene center. Overrides fly-cam. |

**Responses**:

| Response | Fields | Description |
|----------|--------|-------------|
| `benchmark.orbit` | `enabled: bool` | Confirms orbit mode state |

### 1.7 Material Instances

| Command | Fields | Description |
|---------|--------|-------------|
| `material.createInstance` | `name: string, parent: string` | Create instance from .uzmat path |
| `material.setScalar` | `id: uint, key: string, value: float` | Set scalar param override |
| `material.setVector` | `id: uint, key: string, value: float[]` | Set vector param override |
| `material.setTexture` | `id: uint, key: string, path: string` | Set texture override (async load) |
| `material.clearOverride` | `id: uint, key: string, paramType: "scalar"\|"vector"\|"texture"` | Revert param to parent |
| `material.assignToNode` | `id: uint, nodeId: uint` | Assign instance to scene node |
| `material.listInstances` | _(none)_ | List all instances with overrides |
| `material.destroyInstance` | `id: uint` | Destroy instance (unassigns from nodes) |

**Responses**:

| Response | Fields | Description |
|----------|--------|-------------|
| `material.instanceCreated` | `id, name, parent` | Instance created |
| `material.scalarSet` | `id, key, value` | Scalar override applied |
| `material.vectorSet` | `id, key` | Vector override applied |
| `material.textureSet` | `id, key, path` | Texture override queued |
| `material.overrideCleared` | `id, key` | Override reverted to parent |
| `material.assignedToNode` | `id, nodeId` | Instance assigned |
| `material.instanceList` | `instances: [{id, name, parent, scalarOverrides, vectorOverrides, textureOverrides}]` | All instances |
| `material.instanceDestroyed` | `id` | Instance destroyed |
| `material.error` | `error: string` | Error message |

#### Material Parameter Keys

Scalar: `metallic`, `roughness`, `ao`, `emission_energy`, `parallax_scale`,
`env_multiplier`, `normal_strength`, `specular_multiplier`, `uv_tiling`,
`self_shadow_strength`, `cavity_strength`

Vector: `albedo` (float[3]), `emission` (float[3]), `uv_tiling` (float[2])

Texture: `albedo_texture`, `normal_texture`, `metallic_texture`,
`roughness_texture`, `ao_texture`, `emission_texture`, `displacement_texture`

---

## 2. IPC Commands

File-based JSON request/response via `.uzengine/ipc/`.
All responses include `ok: bool`.

### 2.1 Engine

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `engine.ping` | _(none)_ | `fps, frame` | Health check |
| `engine.getStatus` | _(none)_ | `fps, frame, encoder, resolution, scene` | Full engine status |
| `engine.screenshot` | `id?: string` | `path: string` | Save frame to PNG |
| `engine.getCrashReport` | _(none)_ | `report: {...}` | Latest crash report JSON |
| `engine.getDredReport` | _(none)_ | `report: {...}` | Latest DX12 DRED GPU crash report |
| `engine.status` | _(none)_ | `mode, ready, paused, hasScene, port` | Engine mode and ready state for extension polling |
| `engine.pause` | _(none)_ | `ok` | Pause game simulation (scripts stop ticking) |
| `engine.resume` | _(none)_ | `ok` | Resume game simulation |
| `engine.stop` | _(none)_ | `ok` | Request clean engine shutdown (PostQuitMessage) |
| `playmode.start` | _(none)_ | `ok, mode` | No-op acknowledgment (extension spawns play process separately) |
| `playmode.stop` | _(none)_ | `ok` | Request clean shutdown (same as engine.stop) |
| `playmode.status` | _(none)_ | `ok, mode, scene, paused` | Play mode state query |

### 2.2 Scene

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `scene.load` | `nodes: [{type, name, position, rotation, scale, ...}]` | `count: int` | Load scene from JSON node array |
| `scene.loadFile` | `path: string` | `scene, nodes` | Load .uzscene file from disk |
| `scene.setRotation` | `x?, y?, z?: float` | `ok` | Set rotation speed |
| `scene.setBackground` | `r, g, b: float` | `ok` | Set background color |
| `scene.setPaused` | `paused: bool` | `ok` | Pause/unpause rendering |
| `scene.setGeometry` | `shape: "cube"\|"pyramid"` | `ok` | Set test geometry (stub) |
| `scene.setParam` | `key: string, value: any` | `ok` | Set scene parameter (e.g. `rotation.x`) |

#### scene.load Node Properties

```json
{
  "type": "StaticMesh|Camera|DirectionalLight|SkyAtmosphere|Node3D",
  "name": "MyNode",
  "position": [0, 0, 0],
  "rotation": [0, 0, 0],
  "scale": [1, 1, 1],
  "mesh": "assets/meshes/cube.glb",
  "material": "assets/materials/gold.uzmat",
  "color": [1.0, 1.0, 1.0],
  "intensity": 1.0,
  "fov": 45.0,
  "near": 0.1,
  "far": 500.0,
  "sky_zenith": [0.03, 0.07, 0.45],
  "sky_horizon": [0.22, 0.42, 0.82],
  "sky_haze": [0.55, 0.45, 0.32],
  "sky_ground": [0.20, 0.18, 0.15],
  "fog_density": 0.0
}
```

### 2.3 Camera

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `camera.getState` | _(none)_ | `position, yaw, pitch, speed` | Current camera state |
| `camera.reset` | `x?,y?,z?,yaw?,pitch?,speed?` | `ok` | Reset camera to position |

### 2.4 Renderer

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `renderer.resize` | `width, height: int` | `ok, requested, current, ws_clients` | Resize render target |
| `renderer.info` | _(none)_ | `resolution, ws_clients, renderer_w, renderer_h` | Renderer info |

### 2.5 File System

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `file.read` | `path: string` | `content: string` | Read file (no `..` traversal) |
| `file.write` | `path, content: string` | `ok` | Write file |
| `file.list` | `path?: string` | `files: string[]` | List directory |

### 2.6 Shader

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `shader.reload` | `name: string` | `ok` | Hot-reload shader (stub) |

### 2.7 Shader Graph Commands

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `shader_cmd.execute` | `type: string, params: object` | `ok, data?, error?, events?` | Execute a single shader graph command (ADD_NODE, DELETE_NODE, CONNECT, DISCONNECT, SET_PARAM, INSERT_BETWEEN, REPLACE_NODE, EXPOSE_PARAM, COMPILE, QUERY) |
| `shader_cmd.batch` | `commands: [{type, params}]` | `ok, results?, error?, events?` | Execute a batch of commands with rollback on failure. Supports `$0`/`$1` variable references to previous batch result nodeIds |
| `shader_cmd.query` | `query: string, ...params` | `ok, data?, error?` | Convenience wrapper for QUERY commands. query: "graph", "node", "findNodes", "nodeTypes", "connection" |

### 2.8 Shader Graph Preview

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `shader_graph.preview` | `width?, height?` | `ok, viewportId` | Get preview status/viewport ID |
| `shader_graph.stopPreview` | _(none)_ | `ok` | Stop the preview viewport |
| `shader_graph.setPreviewMesh` | `mesh: "sphere"\|"cube"\|"plane"` | `ok` | Change preview geometry |
| `shader_graph.setPreviewCamera` | `yaw?, pitch?, distance?, autoRotate?` | `ok` | Control preview camera |
| `shader_graph.setPreviewParam` | `name: string, value: number\|number[]\|string` | `ok` | Update material parameter without recompilation |
| `shader_graph.compile` | _(none)_ | `ok, vertexHlsl?, pixelHlsl?, errors?` | Manual compilation trigger, returns generated HLSL |

### 2.9 Diagnostics & Logging

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `subscribe_logs` | `level?: string, systems?: string[]` | `subscription_id` | Subscribe to log stream |
| `unsubscribe_logs` | `id: string` | `ok` | Unsubscribe from logs |
| `get_error_buffer` | _(none)_ | `errors: [...]` | Get buffered errors |

### 2.10 GPU Introspection

| Command | Args | Response | Description |
|---------|------|----------|-------------|
| `query_render_targets` | _(none)_ | `render_targets: [{name, format, width, height}]` | List render targets |
| `request_texture` | `name: string` | _(stub)_ | Request texture readback (not yet implemented) |
| `query_render_graph` | _(none)_ | `passes: [{name, inputs, outputs, resolution}]` | Render graph structure |
| `query_gpu_memory` | _(none)_ | `total_bytes, used_bytes, budget_bytes` | GPU memory stats |

---

## 3. MCP Tools

The MCP server (`tools/mcp-uzengine/`) wraps IPC for AI agent access.

| Tool | Args | Description |
|------|------|-------------|
| `uz_call` | `method: string, args?: object` | Call any IPC command by name |
| `uz_screenshot` | `id?: string` | Capture frame to PNG and return path |
| `uz_status` | _(none)_ | Engine status (fps, resolution, scene) |
| `uz_read_file` | `path: string` | Read project file |
| `uz_write_file` | `path, content: string` | Write project file |
| `uz_restart` | _(none)_ | Restart engine process |
| `uz_build` | `config?: string, target?: string` | Build engine (CMake + Ninja) |

### uz_call Examples

```json
// Load a scene
uz_call({ method: "scene.loadFile", args: { path: "E:/UzEngine/assets/scenes/demo.uzscene" } })

// Take screenshot
uz_call({ method: "engine.screenshot", args: { id: "test" } })

// Query GPU memory
uz_call({ method: "query_gpu_memory" })

// Reset camera
uz_call({ method: "camera.reset", args: { x: 0, y: 5, z: -10 } })
```

---

## 4. Adding New Commands

### WebSocket (real-time)

Add handler in `src/editor/ws_handlers.cpp`:
```cpp
if (type == "myNamespace.myCommand") {
    // Parse args from j
    // Do work
    // Broadcast response
    nlohmann::json resp;
    resp["type"] = "myNamespace.myCommandResult";
    wsServer.broadcastText(resp.dump());
    return;
}
```

### IPC (request/response)

Add handler in `src/editor/ipc_handlers.cpp`:
```cpp
ipcRegistry.add("myNamespace.myCommand",
    [&](const nlohmann::json& args) -> nlohmann::json {
    // Do work
    return {{"ok", true}, {"result", value}};
});
```

### Documentation

**MANDATORY**: When adding a new WS or IPC command, update this file (`docs/API.md`)
with the command name, arguments, response fields, and description.
