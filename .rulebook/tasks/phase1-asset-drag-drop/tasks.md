# phase1-asset-drag-drop — Task Checklist

## 1. Extension Infrastructure

- [x] 1.1 Create `DragDropCoordinator` singleton class in `src/editor/dragDropCoordinator.ts`
  - [x] `startDrag(asset: AssetDragPayload)` — stores pending asset, notifies Viewport via postMessage
  - [x] `endDrag()` — clears pending asset, notifies Viewport to exit drop mode
  - [x] `handleViewportDrop(x: number, y: number)` — calls `editor.placeAsset` IPC, selects new node
  - [x] State machine: `idle` | `dragging` | `dropping`

- [x] 1.2 Define `AssetDragPayload` type in `src/editor/dragDropTypes.ts`
  ```typescript
  interface AssetDragPayload {
      resPath: string;      // "res://meshes/house.glb"
      type: AssetType;      // "Mesh" | "Material" | "Texture" | "AudioClip" | "Script"
      displayName: string;
      thumbnailUri?: string;
  }
  ```

- [x] 1.3 Add `DragDropCoordinator` instantiation to extension `activate()` in `extension.ts`

## 2. Scenario 1 — Scene Tree Re-parenting

- [x] 2.1 Implement `TreeDragAndDropController<NodeItem>` in `src/scene-tree-provider.ts`
  - [x] `dragMimeTypes: ['application/vnd.uzengine.scenetree']`
  - [x] `dropMimeTypes: ['application/vnd.uzengine.scenetree', 'application/vnd.uzengine.asset']`
  - [x] `handleDrag`: serialize array of `nodeId` into DataTransfer
  - [x] `handleDrop` (re-parent case): call `scene.reparentNode` for each dragged nodeId

- [x] 2.2 Register `dragAndDropController` in `createTreeView` call

- [x] 2.3 Add IPC client method `reparentNode(nodeId, newParentId)` in DragDropCoordinator

- [x] 2.4 Engine: implement `scene.reparentNode` IPC handler in `src/engine/scene/scene_handlers.cpp`
  - [x] Validate: node exists, new parent exists, no circular reference (uses scene_set_parent)
  - [x] Detach node from current parent's children list
  - [x] Insert as last child of new parent
  - [x] Return `{ "ok": true }` or `{ "ok": false, "error": "..." }`

- [x] 2.5 Unit test: re-parent node, verify parent_index changed in scene graph (9 C++ tests)
- [x] 2.6 Unit test: circular reference rejected with error

## 3. Scenario 2 — Asset Browser → Scene Tree

- [x] 3.1 Implement drag source in `src/asset-browser-provider.ts`
  - [x] `AssetBrowserDragController.handleDrag`: serialize `AssetDragPayload` into DataTransfer as `application/vnd.uzengine.asset`

- [x] 3.2 Extend Scene Tree `handleDrop` to handle `application/vnd.uzengine.asset` case
  - [x] Deserialize `AssetDragPayload` from DataTransfer
  - [x] Determine target component type from `asset.type` (Mesh → StaticMeshData, Material → StaticMeshData.material)
  - [x] Call `scene.assignAsset` IPC

- [x] 3.3 IPC call done directly in `SceneTreeDragController.handleDrop` via `ipc.request('scene.assignAsset', ...)`

- [x] 3.4 Engine: `scene.assignAsset` handler in `src/engine/scene/scene_handlers.cpp`
  - [x] Validates nodeId, assetPath, assetType
  - [x] If node lacks StaticMeshData, auto-creates it via `component_add<StaticMeshData>`
  - [x] Assigns mesh_path or material_path
  - [x] Returns `{ "ok": true, "componentAdded": "StaticMeshData" }` (null if already existed)

- [x] 3.5 Integration tests in `scene-tree-drag-drop.test.ts` (asset drop → assignAsset IPC, error handling, no-target case)

## 4. Scenario 3 — Asset Browser → Viewport (Place in World)

- [x] 4.1 Viewport WebView HTML/JS in `viewport-panel.ts` inline HTML
  - [x] Listen for `{ type: 'dragStarted' }` postMessage → add `drop-active` CSS class to canvas
  - [x] Listen for `{ type: 'dragEnded' }` postMessage → remove `drop-active` class
  - [x] On `mouseup` when `dropModeActive === true` → `vscode.postMessage({ type: 'assetDropped', x, y })`
  - [x] CSS: `.drop-active { outline: 2px solid #4caf50; }` (green border)

- [x] 4.2 `ViewportPanel` relays messages to `DragDropCoordinator`
  - [x] `onAssetDropped` event emitter fires `{ x, y }` on `assetDropped` webview message
  - [x] `onDragCancelled` event emitter fires on `dragCancelled` webview message
  - [x] `postDragState(active)` sends `dragStarted`/`dragEnded` to webview
  - [x] `extension.ts` wires `panel.onAssetDropped` → `coordinator.handleViewportDrop()`

- [x] 4.3 `DragDropCoordinator.handleViewportDrop()` calls `editor.placeAsset` IPC

- [x] 4.4 Engine: `editor.placeAsset` handler in `src/main.cpp`
  - [x] Creates node at camera-forward placement
  - [x] Calls scene.assignAsset internally
  - [x] Returns `{ "ok": true, "nodeId": N }`

- [x] 4.5 `AssetBrowserDragController.handleDrag()` calls `coordinator.startDrag(payload)`
  - [x] Escape key cancels drag in viewport webview JS

- [x] 4.6 Integration test in `drag-drop-coordinator.test.ts`: `editor.placeAsset` called with correct args
- [x] 4.7 Engine unit test: `editor.placeAsset` handler added to `scene_handlers.cpp` + 11 tests in `test_scene_handlers.cpp`
  - [x] PlaceMesh creates node with StaticMeshData + mesh_path
  - [x] PlaceMaterial sets material_path
  - [x] Placement at camera_forward * 5.0
  - [x] Display name extracted from resPath
  - [x] Error cases: missing/empty resPath, missing assetType, null scene
  - [x] No params → places at origin
  - [x] Unsupported type creates node without component

## 5. Scenario 4 — OS File Import

- [x] 5.1 `AssetWatcher` in `src/asset-watcher.ts`
  - [x] `vscode.workspace.createFileSystemWatcher` on `**/assets/**/*`
  - [x] On `onDidCreate`: 200ms debounce, check extension in `IMPORTABLE_EXTENSIONS`
  - [x] Call `ipc.request('asset.import', { path: filePath })`
  - [x] Refresh callback fires after import
  - [x] Show `vscode.window.showInformationMessage` with asset name

- [x] 5.2 "Import Files..." button in Asset Browser toolbar
  - [x] `importFiles()` method in `AssetBrowserProvider`
  - [x] Opens `vscode.window.showOpenDialog` with asset type filters
  - [x] Copies selected file(s) to `assets/` directory
  - [x] Calls `asset.import` IPC for each file

- [x] 5.3 Register `uzengine.assetBrowser.importHere` command in `package.json` (context menu on group items)
  - [x] Command handler in `extension.ts` opens file dialog filtered by group type
  - [x] Copies files to `assets/` and calls `asset.import` IPC

- [x] 5.4 Engine: `asset.import` handler in `src/engine/editor/ipc_asset_handlers.cpp`

- [x] 5.5 Unit tests: `asset-watcher.test.ts` (26 tests — debounce, filtering, dispose, refresh callback)
- [x] 5.6 Integration test: file create → `asset.import` IPC fired within 300ms (debounce at 200ms verified)

## 6. Scenario 5 — Inspector Asset Picker

- [x] 6.1 `[...]` asset picker buttons in Inspector WebView for resource-type fields
  - [x] Inspector renders `<button class="asset-picker">...</button>` next to resource fields
  - [x] On click → sends `{ command: 'openAssetPicker', nodeId, key, assetType }` to extension

- [x] 6.2 Extension handles `openAssetPicker` message in `inspector-panel.ts`
  - [x] Populates `QuickPickItem[]` from asset registry filtered by `assetType`
  - [x] Shows `vscode.window.showQuickPick`
  - [x] On selection → calls `scene.assignAsset` IPC

- [x] 6.3 Right-click context menu: `uzengine.assetBrowser.assignToSelected`
  - [x] Command registered in `extension.ts` — calls `scene.assignAsset` for selected node
  - [x] Registered in `package.json` under `view/item/context` for `uzengine.assetBrowser`

## 7. Undo/Redo Integration — DEFERRED

- [ ] 7.1 `scene.reparentNode` generates EditorCommand (execute + undo) — deferred (no UndoStack system)
- [ ] 7.2 `scene.assignAsset` generates EditorCommand — deferred
- [ ] 7.3 `editor.placeAsset` generates EditorCommand — deferred
- [ ] 7.4 All commands pushed to `UndoStack` — deferred (blocked on undo system implementation)

## 8. Quality & Polish

- [x] 8.1 Viewport shows `drop-active` CSS class when coordinator is in `dragging` state
  - [x] `extension.ts` wires `coordinator.onStateChanged` → `panel.postDragState()`

- [x] 8.2 Scene Tree shows visual drop indicator (native VSCode DnD)

- [ ] 8.3 Asset Browser drag icon overlay — deferred (minor polish, optional)

- [x] 8.4 Cancel drag on `Escape` key
  - [x] Viewport webview JS cancels drop mode on Escape
  - [x] `onDragCancelled` event fires → `coordinator.endDrag()`

- [x] 8.5 Error handling: `extension.ts` shows `vscode.window.showErrorMessage` on `editor.placeAsset` failure

- [x] 8.6 `DragDropCoordinator` unit tests (25 tests — state transitions, viewport drop, reparent, dispose)
- [x] 8.7 Viewport panel tests: 20 new tests for `onAssetDropped`, `onDragCancelled`, `onNodePicked`, `onBoxPicked`, `postDragState`

## Deferred to Future Phases

- [ ] D1. Undo/Redo integration (7.1-7.4) — blocked on UndoStack system (needs dedicated task)
- [ ] D2. Asset Browser drag icon overlay (8.3) — minor polish, optional

## Definition of Done

- [x] Scene Tree items can be re-parented by drag & drop within the tree
- [x] Dragging a `.glb` from Asset Browser and dropping on a Scene Tree node assigns the mesh
- [x] Dragging a `.glb` from Asset Browser and dropping on the Viewport creates a new node at the world position
- [x] Viewport shows green-border feedback while an asset is being dragged over it
- [x] Files copied to `assets/` folder are auto-imported within 300ms
- [x] Asset Browser has a working "Import Files..." button
- [x] Inspector property slots have `[...]` asset picker buttons
- [ ] All DnD operations are undoable (Ctrl+Z restores previous state) — deferred (D1)
- [x] All unit and integration tests pass (1494 C++ tests, 1089 TS tests)
- [x] No regression in existing editor panel functionality
