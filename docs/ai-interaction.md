# AI Interaction Guide — UzEngine

This document explains how an AI agent (Claude Code) interacts with a running
UzEngine instance in real time via the MCP server.

## Architecture

```
Claude Code
  ↓ calls MCP tools (5 stable primitives)
@uzmigames/uzengine-mcp  (node tools/mcp-uzengine/index.js)
  ↓ reads/writes build/Release/preview/*.json
UzEngine.exe  (IPC poller thread — 50ms poll)
  ↓ executes registered handlers
SceneParams / DX12Renderer / file system
```

The MCP tool surface is **intentionally minimal and never grows** with engine
features. High-level knowledge — how to create VFX, configure shaders, build
scenes — lives in **Rulebook Skills**, not in tool definitions.

---

## MCP Tools Reference

### `uz_call(function, args?, timeout_ms?)`

Call any function registered in the UzEngine IPC registry.
This is the primary tool — everything else is convenience wrappers.

**Registered functions:**

| Function | Args | Description |
|----------|------|-------------|
| `engine.ping` | — | Returns `{ok, fps, frame}` immediately |
| `engine.getStatus` | — | Full status snapshot |
| `engine.screenshot` | `{id?}` | Saves PNG to preview dir |
| `scene.setRotation` | `{x?, y?, z?}` | Rotation speed in rad/s |
| `scene.setBackground` | `{r, g, b}` | Background color (0..1) |
| `scene.setGeometry` | `{shape}` | `"pyramid"` or `"cube"` |
| `scene.setPaused` | `{paused}` | Pause/resume render |
| `scene.setParam` | `{key, value}` | Generic param (e.g. `"rotation.y"`) |
| `file.read` | `{path}` | Read file relative to build dir |
| `file.write` | `{path, content}` | Write file relative to build dir |
| `file.list` | `{path}` | List directory contents |
| `shader.reload` | `{name}` | Hot-reload HLSL shader (future) |

**Example:**
```
uz_call("scene.setRotation", {"y": 2.5})
uz_call("scene.setBackground", {"r": 0.0, "g": 0.05, "b": 0.15})
uz_call("engine.ping")
```

---

### `uz_screenshot(wait_ms?)`

Capture the current rendered frame. Returns an **inline image** that Claude
can see directly — no file path needed.

**Use this to:**
- Verify visual changes after calling `uz_call`
- Detect rendering artifacts or encoding issues
- Confirm scene geometry and colors

**Example:**
```
uz_screenshot()                  # default 4s timeout
uz_screenshot({"wait_ms": 2000}) # faster timeout
```

---

### `uz_status()`

Read real-time engine metrics. Returns immediately from `status.json`
(written by the engine every ~1 second).

**Returns:**
```json
{
  "alive": true,
  "fps": 60.0,
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

If UzEngine is not running: `{"alive": false, "error": "..."}`.

---

### `uz_read_file(path)`

Read a project file. Path is relative to `build/Release/` (the engine's
working directory). Use `../../` to reach the project root.

**Examples:**
```
uz_read_file({"path": "../../src/renderer/renderer.cpp"})
uz_read_file({"path": "../../shaders/fire.hlsl"})
```

---

### `uz_write_file(path, content)`

Write a file. Same path rules as `uz_read_file`.

**Examples:**
```
uz_write_file({"path": "../../shaders/fire.hlsl", "content": "..."})
uz_write_file({"path": "../../assets/scene.json", "content": "..."})
```

---

## Skills Model

Skills are **Rulebook recipe documents** that teach the AI how to combine
the 5 primitive tools to achieve complex goals. The MCP tools themselves
never change — Skills extend capabilities without touching engine code.

### Example Skill: "Change Scene Color Scheme"
```
1. uz_call("scene.setBackground", {"r": 0.0, "g": 0.02, "b": 0.05})
2. uz_screenshot() → verify dark background applied
3. uz_call("scene.setRotation", {"y": 1.5}) → slow rotation for cinematic look
4. uz_screenshot() → verify final look
```

### Example Skill: "Write and Reload a Shader"
```
1. uz_read_file({"path": "../../src/renderer/renderer.h"}) → understand shader interface
2. uz_write_file({"path": "../../shaders/custom.hlsl", "content": "<shader code>"})
3. uz_call("shader.reload", {"name": "custom"}) → hot-reload
4. uz_screenshot() → verify result
```

### Example Skill: "Debug Performance Issue"
```
1. uz_status() → check fps (if < 30, something is wrong)
2. uz_screenshot() → look for visual artifacts
3. uz_call("scene.setPaused", {"paused": true}) → freeze render to isolate
4. uz_call("engine.getStatus") → check detailed metrics
5. uz_call("scene.setPaused", {"paused": false}) → resume
```

---

## Setup

The MCP server starts automatically when Claude Code opens this workspace,
via `.mcp.json` at the project root:

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

**Requirements:**
1. Run `npm install` in `tools/mcp-uzengine/` once
2. Open the UzEngine stream viewer in VSCode (starts UzEngine.exe)
3. Claude Code auto-discovers the MCP server on workspace open

If UzEngine is not running, `uz_status()` returns `alive: false` and
`uz_call` returns a timeout error after 4 seconds.
