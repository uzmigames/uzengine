# phase1-asset-drag-drop — Asset Drag & Drop for VSCode Editor

## Why

UzEngine's editor runs entirely inside VSCode. The engine renders to a WebviewPanel via JPEG streaming; the Asset Browser and Scene Tree are native `TreeDataProvider` views. This architecture creates a fundamental UX challenge: **drag & drop from the asset explorer to the scene viewport is the primary workflow in every commercial engine** (Unity, Godot, Unreal), but VSCode's sandboxing prevents native DnD from TreeViews into WebViews.

Without DnD, every asset placement requires right-click menus, typing resource paths in the Inspector, or keyboard shortcuts — a serious productivity obstacle that would push users away from the editor.

This task implements a complete DnD system that works within VSCode constraints while delivering the expected UX.

## Problem Statement

Five distinct DnD scenarios must be solved:

| # | Scenario | VSCode Support | Blocker |
|---|----------|---------------|---------|
| 1 | Scene Tree → Scene Tree (re-parent nodes) | ✅ Native | None |
| 2 | Asset Browser → Scene Tree (assign mesh/material) | ⚠️ Partial | Cross-view MIME boundary |
| 3 | Asset Browser → Viewport (place in world) | ❌ None | WebView sandboxing |
| 4 | OS File Explorer → Asset Browser (import) | ❌ None | OS-level DnD blocked |
| 5 | Asset Browser → Inspector property slot | ❌ None | WebView sandboxing |

## Approach

### Scenario 1 — Native TreeView DnD
Use `TreeDragAndDropController` with MIME type `application/vnd.uzengine.scenetree`. Drop calls `scene.reparentNode` IPC.

### Scenario 2 — Cross-TreeView Drag Token
Both Asset Browser and Scene Tree declare each other's MIME types. Asset Browser emits `application/vnd.uzengine.asset` on drag. Scene Tree's `handleDrop` checks for this type and calls `scene.assignAsset` IPC.

### Scenario 3 — Phantom Drag via postMessage
`DragDropCoordinator` singleton holds the pending asset. When drag starts, a `postMessage` to the Viewport WebView activates "drop mode". On `mouseup`, the WebView sends pixel coordinates back via `postMessage`. Extension calls `editor.placeAsset` IPC which raycasts screen → world position and creates the node.

### Scenario 4 — FileSystemWatcher + Import Button
`FileSystemWatcher` on `assets/**/*` auto-imports files dropped into the project folder via OS. An "Import File..." button (`+`) in the Asset Browser toolbar opens `showOpenDialog` as explicit fallback.

### Scenario 5 — Click-to-Assign + Asset Picker
Inspector property fields have `[...]` buttons that invoke `showQuickPick` populated from the asset registry. Right-click context menu on any Asset Browser item also shows "Assign to selected node".

## New IPC Handlers (Engine Side, C)

| IPC Function | Implementation |
|---|---|
| `scene.reparentNode` | Validates no cycle, detaches, re-inserts as last child |
| `scene.assignAsset` | Auto-creates missing component (e.g., `CMeshRenderer`), assigns resource handle |
| `editor.placeAsset` | Raycasts screen coord, creates `Node3D` + component, returns `nodeId + worldPos` |
| `editor.dragPreview` | Renders semi-transparent ghost mesh at raycast hit (Phase 2 enhancement) |
| `asset.import` | Calls `resource_manager_import()`, triggers `.import.json` sidecar generation |

## Dependencies

- `phase1-editor-panels` (1.5) — Asset Browser and Scene Tree must exist
- `phase1-asset-browser` (1.6) — Asset registry and tree items must exist
- `phase1-scene-system` (1.1) — SceneManager node operations
- `phase1-undo-redo` (1.9) — All DnD operations must be undoable

## Risk & Mitigation

| Risk | Mitigation |
|------|-----------|
| Phantom drag feels laggy (WebView postMessage latency) | Keep payload minimal; show local cursor change immediately |
| FileSystemWatcher fires on partial file write | 200ms debounce before triggering import |
| `editor.placeAsset` raycast misses (empty scene) | Fall back to `camera_forward * 5m` placement |
| VSCode API changes break `TreeDragAndDropController` | Pin minimum VSCode engine version to `^1.89.0` in package.json |
