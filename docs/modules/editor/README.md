# Module: Editor

> **TypeScript**: `vscode-uzengine/src/`
> **C++ handlers**: `src/editor/` (compiled only when `UZ_EDITOR=1`)
> **Language**: TypeScript (VSCode extension) + C++23 (IPC handlers)
> **Runtime dependency**: Communicates with `UzEngine.exe --editor` via IPC and WebSocket only

---

## Responsibility

The Editor module is the **authoring environment** built on top of VSCode. It has no direct C++ dependency on any other module — it communicates exclusively via JSON over IPC (file-based) and receives rendered frames via WebSocket (JPEG stream).

The C++ side (`src/editor/`) is a thin layer of IPC handlers that expose editor-specific queries (scene tree, node selection, property editing) that are not needed in shipping. These handlers are compiled into `UzEngine.exe` only when `UZ_EDITOR=1`.

---

## Architecture

```
VSCode (TypeScript Extension)
│
│  Commands:        uzengine.openStreamViewer
│                   uzengine.startServer
│                   uzengine.stopServer
│                   uzengine.play
│                   uzengine.stop
│                   uzengine.pause
│
│  Panels:          Viewport (WebviewPanel + Canvas)
│                   Scene Tree (TreeDataProvider — native VSCode)
│                   Inspector (WebviewPanel — Webview UI Toolkit)
│                   Asset Browser (TreeDataProvider — native VSCode)
│                   Console (OutputChannel — native VSCode)
│
│  Status bar:      ▶ Play  ⏸ Pause  ■ Stop  |  fps  |  Engine: running/stopped
│
│  IPC (JSON files)
│  ├── write: command.json  → UzEngine.exe reads, executes, writes response.json
│  └── read:  response.json ← result of the command
│
│  WebSocket (port 9002)
│  └── receives binary JPEG frames → draws to Canvas in Viewport panel
│
▼
UzEngine.exe --editor  (C++ Runtime + Engine + Renderer)
```

---

## TypeScript Source Structure

```
vscode-uzengine/src/
├── extension.ts               — activation, command registration, StatusBar
├── server-manager.ts          — spawn/kill UzEngine.exe, health-check, reconnect
├── stream-panel.ts            — WebviewPanel: Canvas + WebSocket JPEG display
├── scene-tree-provider.ts     — TreeDataProvider for Scene Tree panel
│                                calls: editor.getSceneTree via IPC
├── inspector-panel.ts         — WebviewPanel (Webview UI Toolkit forms)
│                                calls: editor.getNodeProperties, editor.setNodeProperty
├── asset-browser-provider.ts  — TreeDataProvider for project assets
│                                reads: project dir via file.list IPC handler
├── play-controls.ts           — StatusBar items ▶ / ⏸ / ■
│                                calls: engine.play, engine.stop, engine.pause
├── shader-diagnostics.ts      — maps DXC error output to VSCode inline diagnostics
│                                calls: shader.getErrors
├── console-output.ts          — OutputChannel receiving engine log lines
│                                calls: engine.getLogs (streaming or poll)
└── ipc-client.ts              — low-level: write command.json, poll response.json
                                  with UUID deduplication and timeout
```

### `server-manager.ts` — Process Lifecycle

```
Extension activates
    └─ ServerManager.start()
        ├─ spawn UzEngine.exe --editor --project <workspace>
        ├─ wait for engine.ping → ok  (health-check, 5s timeout)
        ├─ connect WebSocket to ws://localhost:9002
        └─ notify all panels: "engine ready"

Extension deactivates / user clicks ■ Stop
    └─ ServerManager.stop()
        ├─ send engine.shutdown IPC command  (graceful)
        ├─ wait 2s
        └─ kill process if still running     (force)

Engine crashes (WebSocket disconnects + process exits)
    └─ ServerManager detects via process.on('exit')
        ├─ show "Engine crashed — restart?" notification
        └─ auto-restart after 3s (configurable, default: true)
```

### `stream-panel.ts` — Viewport Rendering

```
WebSocket message received (binary JPEG blob)
    → createImageBitmap(blob)          — browser API, async, hardware-decoded
    → ctx.drawImage(bitmap, 0, 0)      — draw to Canvas
    → update FPS counter in corner
```

Canvas size tracks the panel size. On resize: send `engine.setResolution` IPC command so Runtime adjusts the DX12 swap chain and readback buffers.

---

## C++ IPC Handlers (`src/editor/`)

These handlers are **only compiled when `UZ_EDITOR=1`**. They expose authoring-specific queries that the shipping game does not need.

```
src/editor/ipc_handlers/
├── editor_handlers.cpp   — registers all editor.* handlers into IpcRegistry
```

| IPC Command | Args | Returns | Purpose |
|------------|------|---------|---------|
| `editor.getSceneTree` | — | JSON node tree | Populates Scene Tree panel |
| `editor.selectNode` | `{id}` | `{ok}` | Highlights node in Scene Tree, loads Inspector |
| `editor.getNodeProperties` | `{id}` | JSON property map | Populates Inspector form |
| `editor.setNodeProperty` | `{id, key, value}` | `{ok}` | Live-edits a node property |
| `editor.getLogs` | `{since_frame}` | `[{level, msg, frame}]` | Console panel polling |
| `editor.setResolution` | `{width, height}` | `{ok}` | Resize swap chain to match panel |

All scene-mutating commands go through `SceneCommand` queue (applied on Main thread, start of next frame). See [lifecycle-and-concurrency.md](../../engine/lifecycle-and-concurrency.md).

---

## Panel Details

### Scene Tree
- VSCode native `TreeDataProvider` — no WebView, no HTML
- Each item: node name + type icon + visible/enabled toggles
- Click → calls `editor.selectNode` → Inspector panel updates
- Drag to reorder: calls `editor.setNodeProperty({id, key:"parent", value:new_parent_id})`

### Inspector
- WebView with Webview UI Toolkit (VSCode native components)
- Property form generated from `editor.getNodeProperties` response
- Edits debounced 300ms before sending `editor.setNodeProperty`
- Shows: transform (XYZ position/rotation/scale), node-type-specific fields, script attachment

### Asset Browser
- Native `TreeDataProvider`
- Reads project directory via `file.list` IPC
- Double-click `.uzscene` → `editor.loadScene`
- Double-click `.uzmat` → opens in text editor (VSCode native)
- Double-click `.hlsl/.glsl` → opens with HLSL/GLSL syntax highlighting

### Play Controls (Status Bar)

```
[▶ Play]  [⏸ Pause]  [■ Stop]  |  60 fps  |  ● Engine running
```

- ▶ Play: saves dirty scenes → spawns `UzEngine.exe --play --scene current.uzscene`
- ⏸ Pause: sends `scene.setPaused({paused: true})`
- ■ Stop: kills the `--play` process, resumes `--editor` preview stream

---

## Key Invariants

1. **TypeScript never links against any C++ module.** Communication is IPC + WebSocket only.
2. **Editor C++ handlers compiled out of shipping.** `#ifdef UZ_EDITOR` in `ipc_registry_setup()`.
3. **Scene mutations always via `SceneCommand` queue.** Never from the IPC Poller thread directly.
4. **Play mode = separate process.** The `--editor` process keeps streaming preview; a new `--play` process handles game execution. They do not share memory.

---

## Related Docs

- [lifecycle-and-concurrency.md](../../engine/lifecycle-and-concurrency.md) — IPC poller thread, SceneCommand queue
- [hot-reload.md](../../engine/hot-reload.md) — shader hot-reload diagnostics flow
- [../runtime/README.md](../runtime/README.md) — IPC handler registration
- [../../guides/ipc-mcp.md](../../guides/ipc-mcp.md) — full IPC protocol reference
