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

These handlers are **only compiled when `UZ_EDITOR=1`**. They expose authoring-specific queries and mutations that the shipping game does not need.

```
src/editor/
├── selection_manager.h/cpp      — SelectionManager: maintain Set<NodeId> of selected
├── undo_system.h/cpp            — UndoStack: max 100 commands, execute/undo/redo
├── prefab_edit_mode.h/cpp       — PrefabEditMode: isolated prefab viewport, override tracking
├── gizmo_controller.h/cpp       — Gizmo state (mode, hovered_axis), mouse ray-cast hit testing
├── ipc_handlers.cpp             — registers all editor.* handlers into IpcRegistry
└── outline_renderer.h/cpp       — Selected node outline, wireframe debug rendering
```

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

## Phase 1+ Editor Features

### Undo/Redo System (Phase 1)

Full undo/redo for all scene edits via `Ctrl+Z` / `Ctrl+Shift+Z`.

**C++ Side** (`src/editor/undo_system.h/cpp`):
- `UndoStack` class: max 100 commands, LIFO with redo buffer
- `EditorCommand` abstract base: `execute()`, `undo()`, `redo()`
- Concrete commands: `CreateNodeCommand`, `DeleteNodeCommand`, `MoveNodeCommand`, `SetPropertyCommand`, `ParentChangeCommand`
- Commands batched per user action (e.g., move + rotate = one undo)

**TypeScript Side** (`vscode-uzengine/src/undoStack.ts`):
- Wraps C++ undo/redo via IPC: `editor.undo`, `editor.redo`
- `Ctrl+Z` → `editor.undo` IPC → C++ pops stack → revert property → send back scene delta
- Keybinding: `"key": "ctrl+z"` in `package.json`

**IPC Commands Added**:
- `editor.undo` → `{success, affected_nodes}`
- `editor.redo` → `{success, affected_nodes}`

### 3D Gizmo Handles (Phase 1)

Transform manipulation with mouse gizmos (translate, rotate, scale).

**C++ Side** (`src/renderer/passes/gizmo_pass.h/cpp`, sort_key=900):
- Registers as DX12 render pass (editor mode only)
- Renders axis-aligned colored lines (red=X, green=Y, blue=Z)
- Depth-tested against scene geometry
- Ray-cast hit test: mouse position → world space ray → check intersection with gizmo quads

**TypeScript Side** (`vscode-uzengine/src/gizmoView.ts`):
- Detects mouse down/drag over 3D viewport
- Sends `editor.selectNode` to set active node
- Shows gizmo for selected node
- Mouse drag → compute world-space delta → `editor.setNodeProperty({id, "position", new_pos})`
- Mode selector buttons: Translate / Rotate / Scale

**Key Invariant**: Gizmo rendering happens AFTER all scene geometry (sort_key=900). Gizmo state (which axis hovered) is communicated from C++ via `engine.getGizmoState` IPC.

### Multi-Select (Phase 1)

Select multiple nodes and apply edits to all at once.

**C++ Side** (`src/editor/selection_manager.h/cpp`):
- `SelectionManager`: maintains `Set<NodeId>` of selected nodes
- `set_selected(node_id, append=false)` → replaces or appends selection
- `get_selected() → Vec<NodeId>` → returned to Inspector for bulk edit UI

**TypeScript Side** (`vscode-uzengine/src/selectionManager.ts`):
- `Ctrl+click` in Scene Tree → `editor.selectNode({id, append:true})`
- Shift+click range select (select node N to node M)
- Scene Tree shows checkboxes for multi-selection
- Inspector shows "Bulk Edit" mode: fields that differ across selected nodes show placeholder text
- Moving selected group: drag one node → compute delta → apply to all via `editor.setNodeProperty` batch

**Gizmo Centroid**: When multiple nodes selected, gizmo is positioned at their center of mass (computed in C++).

### Prefab Edit Mode (Phase 2)

Edit a prefab's template while viewing overrides against the parent.

**Workflow**:
1. Right-click instance node → "Edit Prefab"
2. Breadcrumb trail shows: `Scenes/Level1.uzscene > PrefabInst#42 > Prefab`
3. Viewport zooms/frames the prefab root
4. Inspector shows two columns: "Template" (read-only) and "Overrides" (blue-highlighted)
5. Edits to template apply to all instances; edits to overrides apply only to this instance
6. Click "Exit Prefab Edit Mode" or press Escape → return to main scene

**C++ Side** (`src/editor/prefab_edit_mode.h/cpp`):
- `PrefabEditMode` struct: active prefab instance ID, root node, list of override nodes
- `enter_prefab_edit(node_id)` → load prefab as isolated scene, track overrides
- `exit_prefab_edit()` → save changes, reload instance into parent scene
- `get_prefab_overrides(node_id) → Map<property, value>` → returns overrides for Inspector

**TypeScript Side** (`vscode-uzengine/src/prefabEditMode.ts`):
- Breadcrumb component showing hierarchy: scene > instance > prefab
- Inspector two-column layout: "Template" and "This Instance"
- Edits in "This Instance" create override records in `.uzscene` for that instance

---

## IPC Command Reference

### Editor Commands (Complete Set)

| Command | Phase | Args | Returns | Purpose |
|---------|-------|------|---------|---------|
| `editor.getSceneTree` | 0 | — | JSON node tree | Scene Tree panel population |
| `editor.selectNode` | 0 | `{id, append?}` | `{ok}` | Select for gizmo + Inspector |
| `editor.getNodeProperties` | 0 | `{id}` | JSON property map | Inspector form |
| `editor.setNodeProperty` | 0 | `{id, key, value}` | `{ok, updated_value}` | Live property edit |
| `editor.deleteNode` | 1 | `{id}` | `{ok}` | Delete node + undo-stack entry |
| `editor.createNode` | 1 | `{parent_id, type, name}` | `{ok, node_id}` | Create child node + undo-stack entry |
| `editor.undo` | 1 | — | `{ok, affected_node_ids}` | Pop undo stack |
| `editor.redo` | 1 | — | `{ok, affected_node_ids}` | Pop redo stack |
| `editor.getGizmoState` | 1 | — | `{mode, hovered_axis}` | Current gizmo interaction state |
| `editor.enterPrefabEditMode` | 2 | `{instance_id}` | `{ok, template_root_id}` | Enter prefab editing |
| `editor.exitPrefabEditMode` | 2 | — | `{ok}` | Exit prefab editing |
| `editor.getPrefabOverrides` | 2 | `{node_id}` | `{overrides: Map}` | Get property overrides for Inspector |
| `editor.setResolution` | 0 | `{width, height}` | `{ok}` | Resize viewport swap chain |
| `editor.getLogs` | 0 | `{since_frame?}` | `[{level, msg, frame}]` | Console streaming |

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

## Rulebook Tasks

Phase 1 editor foundations: [phase1-editor-panels](../../.rulebook/tasks/phase1-editor-panels/), [phase1-undo-redo](../../.rulebook/tasks/phase1-undo-redo/), [phase1-editor-gizmos](../../.rulebook/tasks/phase1-editor-gizmos/), [phase1-multi-select](../../.rulebook/tasks/phase1-multi-select/)

Phase 2 advanced editing: [phase2-prefab-edit-mode](../../.rulebook/tasks/phase2-prefab-edit-mode/), [phase2-ai-native-features](../../.rulebook/tasks/phase2-ai-native-features/)

Phase 1 core infrastructure: [phase1-scene-system](../../.rulebook/tasks/phase1-scene-system/), [phase1-asset-browser](../../.rulebook/tasks/phase1-asset-browser/)
