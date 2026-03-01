## 1. Extension Setup

- [ ] 1.1 Add `uzengine.sceneTree` and `uzengine.assets` view containers to `package.json`
- [ ] 1.2 Register views in the UzEngine activity bar sidebar
- [ ] 1.3 Add `@vscode/webview-ui-toolkit` npm dependency
- [ ] 1.4 Create `src/ipc-client.ts` — typed wrapper around WebSocket IPC calls
- [ ] 1.5 `IpcClient` handles reconnect, request/response correlation (UUID per request)

## 2. Scene Tree Panel

- [ ] 2.1 Create `src/scene-tree-provider.ts` — implements `vscode.TreeDataProvider<NodeItem>`
- [ ] 2.2 `NodeItem` has: id, name, type, icon (per NodeType), collapsible state
- [ ] 2.3 `getChildren(item)` calls `scene.getTree` IPC on root, uses cached tree for children
- [ ] 2.4 Scene tree refreshes on IPC push event `scene.treeChanged`
- [ ] 2.5 Node type icons: 📦 Node3D, 🎯 StaticMesh, 💡 Light, 📷 Camera, etc.
- [ ] 2.6 Context menu: Add Child, Duplicate, Delete, Rename
- [ ] 2.7 Selection: clicking a node sends `scene.selectNode` IPC and refreshes Inspector
- [ ] 2.8 Rename: inline editing via `vscode.window.showInputBox`, sends `scene.setProperty` IPC

## 3. Inspector Panel

- [ ] 3.1 Create `src/inspector-panel.ts` — manages a `WebviewPanel`
- [ ] 3.2 Inspector opens/refreshes when scene tree selection changes
- [ ] 3.3 HTML template uses Webview UI Toolkit components (`vscode-text-field`, `vscode-dropdown`, etc.)
- [ ] 3.4 Property groups: Node, Transform, (type-specific group)
- [ ] 3.5 Float property: `<vscode-text-field>` with number validation
- [ ] 3.6 Vec3 property: three `<vscode-text-field>` in a row (X Y Z)
- [ ] 3.7 Bool property: `<vscode-checkbox>`
- [ ] 3.8 Color property: `<input type="color">` + float conversion (0..1 ↔ 0..255)
- [ ] 3.9 Resource path property: text field + file picker button
- [ ] 3.10 On change: sends `scene.setProperty` IPC immediately (live editing)
- [ ] 3.11 Panel persists state with `retainContextWhenHidden: true`

## 4. Asset Browser Panel

- [ ] 4.1 Create `src/asset-browser-provider.ts` — implements `vscode.TreeDataProvider<AssetItem>`
- [ ] 4.2 Root categories: Scenes, Meshes, Textures, Materials, Shaders
- [ ] 4.3 Reads actual project filesystem (`vscode.workspace.findFiles`)
- [ ] 4.4 File watcher refreshes tree on file create/delete
- [ ] 4.5 Context menu: Open in Editor, Reimport, New Material, New Scene, New Shader
- [ ] 4.6 "New Shader" creates `.hlsl` from template, opens in text editor
- [ ] 4.7 "New Material" creates `.uzmat` from template, opens in text editor
- [ ] 4.8 "New Scene" creates `.uzscene` from template, calls `scene.load` IPC

## 5. Play Controls (Status Bar)

- [ ] 5.1 Create `src/play-controls.ts` — manages status bar items
- [ ] 5.2 States: idle, editor, playing, paused
- [ ] 5.3 ▶ Play: saves scene, spawns `UzEngine.exe --play --scene=<active>`, switches viewport
- [ ] 5.4 ■ Stop: sends SIGTERM to play process, switches viewport back to editor stream
- [ ] 5.5 ⏸ Pause: sends `engine.pause` IPC to play process
- [ ] 5.6 Status bar item shows current scene name and FPS when playing
- [ ] 5.7 Play process crash detection: restart prompt or auto-stop

## 6. Viewport Enhancements

- [ ] 6.1 Add toolbar: [Editor] [Play Preview] mode switch
- [ ] 6.2 Show node name overlay when hovering in editor mode (uses `editor.pick` IPC)
- [ ] 6.3 Gizmo buttons in viewport overlay: [Move] [Rotate] [Scale] [World/Local]
- [ ] 6.4 Click in viewport → pick node → sync selection to Scene Tree + Inspector
- [ ] 6.5 Camera navigation input sent to engine via `editor.cameraMove` IPC

## 7. Integration & Testing

- [ ] 7.1 Scene tree populates on extension activation when engine is running
- [ ] 7.2 Selecting node in Scene Tree updates Inspector within 100ms
- [ ] 7.3 Editing property in Inspector updates engine viewport within 100ms
- [ ] 7.4 Clicking viewport picks node and syncs to Scene Tree + Inspector
- [ ] 7.5 Asset browser lists all `.uzscene`, `.uzmat`, `.hlsl` files in project
- [ ] 7.6 ▶ Play spawns play process, viewport shows game stream
- [ ] 7.7 ■ Stop kills process, viewport returns to editor stream

## 8. Documentation

- [ ] 8.1 Update `docs/vscode-extension.md` with new panels
- [ ] 8.2 Update `docs/ipc-mcp.md` with new IPC handlers
