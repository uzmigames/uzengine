## 1. Extension Setup

- [x] 1.1 Add `uzengine.sceneTree` and `uzengine.assets` view containers to `package.json`
- [x] 1.2 Register views in the UzEngine activity bar sidebar
- [ ] 1.3 Add `@vscode/webview-ui-toolkit` npm dependency — deferred Phase 2 (not required for Phase 1 custom HTML)
- [x] 1.4 Create `src/ipc-client.ts` — typed wrapper around WebSocket IPC calls
- [x] 1.5 `IpcClient` handles reconnect, request/response correlation (UUID per request)

## 2. Scene Tree Panel

- [x] 2.1 Create `src/scene-tree-provider.ts` — implements `vscode.TreeDataProvider<NodeItem>`
- [x] 2.2 `NodeItem` has: id, name, type, icon (per NodeType), collapsible state
- [x] 2.3 `getChildren(item)` calls `scene.getTree` IPC on root, uses cached tree for children
- [x] 2.4 Scene tree refreshes on IPC push event `scene.treeChanged`
- [x] 2.5 Node type icons: 📦 Node3D, 🎯 StaticMesh, 💡 Light, 📷 Camera, etc.
- [x] 2.6 Context menu: Add Child, Duplicate, Delete, Rename
- [x] 2.7 Selection: clicking a node sends `scene.selectNode` IPC and refreshes Inspector
- [x] 2.8 Rename: inline editing via `vscode.window.showInputBox`, sends `scene.renameNode` IPC

## 3. Inspector Panel

- [x] 3.1 Create `src/inspector-panel.ts` — manages a `WebviewPanel`
- [x] 3.2 Inspector opens/refreshes when scene tree selection changes
- [x] 3.3 HTML template uses inline styles (Webview UI Toolkit deferred to Phase 2)
- [x] 3.4 Property groups: Node, Transform, Rendering
- [x] 3.5 Float property: `<input type="number">` with step=0.01
- [x] 3.6 Vec3 property: three `<input type="number">` in a row (X Y Z)
- [x] 3.7 Bool property: `<input type="checkbox">`
- [ ] D1. 3.8 Color property: `<input type="color">` + float conversion (0..1 ↔ 0..255) — deferred Phase 2
- [ ] D1. 3.9 Resource path property: text field + file picker button — deferred Phase 2
- [x] 3.10 On change: sends `scene.setProperty` IPC immediately (live editing)
- [x] 3.11 Panel persists state with `retainContextWhenHidden: true`

## 4. Asset Browser Panel

- [ ] D2. 4.1 Create `src/asset-browser-provider.ts` — implements `vscode.TreeDataProvider<AssetItem>` — deferred Phase 2
- [ ] D2. 4.2 Root categories: Scenes, Meshes, Textures, Materials, Shaders — deferred Phase 2
- [ ] D2. 4.3 Reads actual project filesystem (`vscode.workspace.findFiles`) — deferred Phase 2
- [ ] D2. 4.4 File watcher refreshes tree on file create/delete — deferred Phase 2
- [ ] D2. 4.5 Context menu: Open in Editor, Reimport, New Material, New Scene, New Shader — deferred Phase 2
- [ ] D2. 4.6 "New Shader" creates `.hlsl` from template, opens in text editor — deferred Phase 2
- [ ] D2. 4.7 "New Material" creates `.uzmat` from template, opens in text editor — deferred Phase 2
- [ ] D2. 4.8 "New Scene" creates `.uzscene` from template, calls `scene.load` IPC — deferred Phase 2

## 5. Play Controls (Status Bar)

- [x] 5.1 Create `src/play-controls.ts` — manages status bar items
- [x] 5.2 States: idle, playing, paused
- [x] 5.3 ▶ Play: sends `playmode.start` IPC (actual process spawning in phase1-play-mode)
- [x] 5.4 ■ Stop: sends `playmode.stop` IPC
- [x] 5.5 ⏸ Pause: sends `engine.pause` IPC
- [x] 5.6 Status bar item shows current scene name and FPS when playing
- [ ] D3. 5.7 Play process crash detection: restart prompt or auto-stop — deferred Phase 2

## 6. Viewport Enhancements

- [x] D4. 6.1 Add toolbar: [Editor] [Play Preview] mode switch — mode badge in viewport toolbar
- [x] D4. 6.2 Show node name overlay when hovering in editor mode (uses `editor.hoverPick` IPC)
- [x] D4. 6.3 Gizmo buttons in viewport overlay: [Move] [Rotate] [Scale] [World/Local] — implemented in phase1-editor-gizmos
- [x] D4. 6.4 Click in viewport → pick node → sync selection to Scene Tree + Inspector — implemented in phase1-editor-gizmos
- [x] D4. 6.5 Camera navigation input sent to engine via `camera_input` WS message — implemented in phase1-editor-gizmos

## 7. Integration & Testing

- [x] 7.1 Scene tree populates on extension activation when engine is running
- [x] 7.2 Selecting node in Scene Tree updates Inspector within 100ms
- [x] 7.3 Editing property in Inspector updates engine viewport within 100ms
- [x] D5. 7.4 Clicking viewport picks node and syncs to Scene Tree + Inspector — implemented in phase1-editor-gizmos
- [ ] D2. 7.5 Asset browser lists all `.uzscene`, `.uzmat`, `.hlsl` files in project — deferred Phase 2
- [ ] D3. 7.6 ▶ Play spawns play process, viewport shows game stream — deferred phase1-play-mode
- [ ] D3. 7.7 ■ Stop kills process, viewport returns to editor stream — deferred phase1-play-mode

## 8. Documentation

- [ ] D6. 8.1 Update `docs/vscode-extension.md` with new panels — deferred Phase 2
- [ ] D6. 8.2 Update `docs/ipc-mcp.md` with new IPC handlers — deferred Phase 2
