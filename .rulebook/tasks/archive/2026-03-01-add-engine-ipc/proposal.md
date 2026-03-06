# Proposal: add-engine-ipc

## Why
To enable AI and external tools to communicate with render.exe in real time, a stable low-level
IPC (Inter-Process Communication) protocol is required. This IPC layer is the **transport layer**
on which all AI capabilities are built — it knows nothing about "VFX" or "camera", it only
exposes primitives that higher layers consume.

Key architectural decision: **the MCP server does not grow with engine features**. The MCP has
stable primitive functions (read file, write file, call engine function, capture frame). Knowledge
of how to use those primitives to create VFX, shaders, animations, etc. lives in Rulebook
**Skills** — recipe documentation consumed by the AI, not by the engine.

## What Changes

### IPC Protocol (file-based, no extra dependencies)

Three files in `build/preview/`:

**`command.json`** — written by external client (MCP server or Claude Code directly):
```json
{
  "id": "uuid-v4",
  "type": "call",
  "function": "scene.setRotation",
  "args": {"y": 2.0}
}
```

**`response.json`** — written by UzEngine after executing the command:
```json
{
  "id": "uuid-v4",
  "ok": true,
  "result": null,
  "error": null,
  "timestamp_ms": 1234567890
}
```

**`status.json`** — written by UzEngine every second, no command required:
```json
{
  "alive": true,
  "fps": 60,
  "frame": 12345,
  "uptime_s": 42,
  "encoder": "MFT/Main@4.2",
  "resolution": "1280x720",
  "scene": {
    "rotation_speed": [0.0, 1.0, 0.0],
    "background": [0.05, 0.05, 0.1],
    "geometry": "pyramid",
    "paused": false
  }
}
```

### Registered Engine Functions (`function` field in command.json)

UzEngine exposes a **function registry** — a map of string → C++ handler:

| Function | Args | Description |
|----------|------|-------------|
| `engine.ping` | — | Responds immediately with current status |
| `engine.screenshot` | `{id?: string}` | Saves `build/preview/frame_<id>.png` |
| `engine.getStatus` | — | Returns full status.json content |
| `scene.setParam` | `{key: string, value: any}` | Sets a generic scene parameter |
| `scene.setRotation` | `{x?, y?, z?}` | Rotation speed per axis (rad/s) |
| `scene.setBackground` | `{r, g, b}` | Background color (0..1) |
| `scene.setGeometry` | `{shape: string}` | Active geometry: "pyramid", "cube" |
| `scene.setPaused` | `{paused: bool}` | Pause/resume render loop |
| `file.read` | `{path: string}` | Read project file, return content |
| `file.write` | `{path: string, content: string}` | Write file in project directory |
| `file.list` | `{path: string}` | List files in a directory |
| `shader.reload` | `{name: string}` | Hot-reload HLSL shader by name |

### C++ — Function Registry (src/common/ipc_registry.h/cpp)
- `IpcRegistry` — map of `string -> std::function<nlohmann::json(nlohmann::json)>`
- Handlers registered at startup via `registry.add("scene.setRotation", handler)`
- Thread-safe: handlers run on IPC thread, communicate with render thread via SceneParams (atomics/mutex)

### C++ — IPC Poller (src/common/ipc_poller.h/cpp)
- Polls `command.json` every 50ms via `GetFileTime` comparison — zero cost when unchanged
- Parses JSON via nlohmann/json (vcpkg, header-only)
- Dispatches to IpcRegistry → writes response.json
- Status writer: updates status.json every 1s regardless of commands

### C++ — SceneParams (src/renderer/scene_params.h)
- Thread-safe struct shared between render thread and IPC thread
- `std::atomic<float>` for rotation speeds, `std::atomic<bool>` for paused
- `std::mutex` + plain floats for background color (needs 3-value atomic update)
- Renderer reads SceneParams every frame

### stb_image_write (screenshot handler)
- `external/stb/stb_image_write.h` — header-only, zero dependencies
- Handler `engine.screenshot`: copies last readback pixels → writes PNG via `stbi_write_png`

## Impact
- Affected specs: docs/architecture.md
- Affected code: src/main.cpp, src/renderer/renderer.h/cpp, new src/common/ipc_*.h/cpp, vcpkg.json
- Breaking change: NO
- User benefit: Stable foundation for all AI capabilities in the engine
