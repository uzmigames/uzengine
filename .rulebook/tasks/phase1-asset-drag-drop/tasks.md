# phase1-asset-drag-drop — Task Checklist

## 1. Extension Infrastructure

- [ ] 1.1 Create `DragDropCoordinator` singleton class in `extension/src/dragDropCoordinator.ts`
  - [ ] `startDrag(asset: AssetDragPayload)` — stores pending asset, notifies Viewport via postMessage
  - [ ] `endDrag()` — clears pending asset, notifies Viewport to exit drop mode
  - [ ] `handleViewportDrop(x: number, y: number)` — calls `editor.placeAsset` IPC, selects new node
  - [ ] State machine: `idle` | `dragging` | `dropping`

- [ ] 1.2 Define `AssetDragPayload` type in `extension/src/types/dragDrop.ts`
  ```typescript
  interface AssetDragPayload {
      resPath: string;      // "res://meshes/house.glb"
      type: AssetType;      // "Mesh" | "Material" | "Texture" | "AudioClip" | "Script"
      displayName: string;
      thumbnailUri?: vscode.Uri;
  }
  ```

- [ ] 1.3 Add `DragDropCoordinator` instantiation to extension `activate()` in `extension.ts`

## 2. Scenario 1 — Scene Tree Re-parenting

- [ ] 2.1 Implement `TreeDragAndDropController<SceneTreeItem>` in `extension/src/panels/sceneTree.ts`
  - [ ] `dragMimeTypes: ['application/vnd.uzengine.scenetree']`
  - [ ] `dropMimeTypes: ['application/vnd.uzengine.scenetree', 'application/vnd.uzengine.asset']`
  - [ ] `handleDrag`: serialize array of `nodeId` into DataTransfer
  - [ ] `handleDrop` (re-parent case): call `scene.reparentNode` for each dragged nodeId

- [ ] 2.2 Register `dragAndDropController` in `createTreeView` call

- [ ] 2.3 Add IPC client method `reparentNode(nodeId: number, newParentId: number): Promise<void>`

- [ ] 2.4 Engine: implement `ipc_scene_reparent_node()` in `src/engine/scene/scene_manager.c`
  - [ ] Validate: node exists, new parent exists, no circular reference (walk up parent chain)
  - [ ] Detach node from current parent's children list
  - [ ] Insert as last child of new parent
  - [ ] Mark scene dirty (triggers `.uzscene` autosave flag)
  - [ ] Return `{ "ok": true }` or `{ "ok": false, "error": "Circular parent" }`

- [ ] 2.5 Unit test: re-parent node, verify parent_index changed in scene graph
- [ ] 2.6 Unit test: circular reference rejected with error

## 3. Scenario 2 — Asset Browser → Scene Tree

- [ ] 3.1 Implement drag source in `extension/src/panels/assetBrowser.ts`
  - [ ] `handleDrag`: serialize `AssetDragPayload` into DataTransfer as `application/vnd.uzengine.asset`

- [ ] 3.2 Extend Scene Tree `handleDrop` to handle `application/vnd.uzengine.asset` case
  - [ ] Deserialize `AssetDragPayload` from DataTransfer
  - [ ] Determine target component type from `asset.type` (Mesh → CMeshRenderer, Material → CMeshRenderer.material, etc.)
  - [ ] Call `scene.assignAsset` IPC

- [ ] 3.3 Add IPC client method `assignAsset(nodeId, assetPath, assetType): Promise<AssignResult>`

- [ ] 3.4 Engine: implement `ipc_scene_assign_asset()` in `src/engine/scene/scene_manager.c`
  - [ ] Load resource via `resource_manager_load()` with appropriate type
  - [ ] If node lacks the required component, auto-create it via `entt::registry::emplace`
  - [ ] Assign resource handle to component field
  - [ ] Return `{ "ok": true, "componentAdded": "CMeshRenderer" }` (null if already existed)

- [ ] 3.5 Integration test: drag mesh asset onto empty Node3D → CMeshRenderer created with correct mesh

## 4. Scenario 3 — Asset Browser → Viewport (Place in World)

- [ ] 4.1 Extend Viewport WebView HTML/JS (`extension/src/panels/viewport/viewport.html`)
  - [ ] Listen for `{ type: 'dragStarted' }` postMessage → add `drop-active` CSS class to canvas
  - [ ] Listen for `{ type: 'dragEnded' }` postMessage → remove `drop-active` class
  - [ ] On `mouseup` when `dropModeActive === true` → `vscode.postMessage({ type: 'assetDropped', x, y })`
  - [ ] Add CSS: `.drop-active { outline: 2px solid #4caf50; }` (green border)

- [ ] 4.2 Extend `ViewportPanel` to relay incoming WebView messages to `DragDropCoordinator`
  - [ ] `webview.onDidReceiveMessage`: on `assetDropped` → call `coordinator.handleViewportDrop(x, y)`

- [ ] 4.3 Add IPC client method `placeAsset(resPath, assetType, screenX, screenY): Promise<PlaceResult>`

- [ ] 4.4 Engine: implement `ipc_editor_place_asset()` in `src/engine/editor/editor_ipc.c`
  - [ ] Build ray from screen coordinates (use same method as `editor.pick`)
  - [ ] Raycast against scene BVH; if miss → use `camera_forward * 5.0` placement
  - [ ] `node_id = scene_create_node(scene, "New Node", NODE_TYPE_NODE3D, 0)` (parent = root)
  - [ ] Call `ipc_scene_assign_asset()` internally
  - [ ] Return `{ "ok": true, "nodeId": N, "worldPos": [x,y,z] }`

- [ ] 4.5 Add `assetBrowser.ts` drag start → `DragDropCoordinator.startDrag()` wiring
  - [ ] On drag begin (user starts dragging a TreeItem): call `coordinator.startDrag(payload)`
  - [ ] On drag cancel (ESC or drop outside): call `coordinator.endDrag()`

- [ ] 4.6 Integration test: simulate dragStarted + assetDropped → verify IPC `editor.placeAsset` called with correct args
- [ ] 4.7 Engine unit test: `ipc_editor_place_asset` with miss ray → node created at `camera_forward * 5m`

## 5. Scenario 4 — OS File Import

- [ ] 5.1 Implement `AssetWatcher` in `extension/src/assetWatcher.ts`
  - [ ] `vscode.workspace.createFileSystemWatcher` on `assets/**/*`
  - [ ] On `onDidCreate`: 200ms debounce, check extension in `IMPORTABLE_EXTENSIONS`
  - [ ] Call `ipc.call('asset.import', { filePath: uri.fsPath })`
  - [ ] Call `assetBrowserProvider.refresh()`
  - [ ] Show `vscode.window.showInformationMessage` with asset name

- [ ] 5.2 Implement "Import File..." button in Asset Browser toolbar
  - [ ] Add `importFile()` method to `AssetBrowserProvider`
  - [ ] Opens `vscode.window.showOpenDialog` with asset type filters
  - [ ] Copies selected file(s) to `assets/` directory (respecting target folder selected in tree)
  - [ ] Calls `asset.import` IPC for each file

- [ ] 5.3 Register `uzengine.importAssetHere` command in `package.json` (context menu on folder items)

- [ ] 5.4 Engine: implement `ipc_asset_import()` in `src/engine/asset/asset_manager.c`
  - [ ] Call `resource_manager_import(filePath)` (existing pipeline)
  - [ ] Generate or update `.import.json` sidecar
  - [ ] Return `{ "ok": true, "assetId": "res://meshes/house.glb" }`

- [ ] 5.5 Unit test: FileSystemWatcher callback debounces correctly (200ms)
- [ ] 5.6 Integration test: copy file to `assets/` → `asset.import` IPC fired within 300ms

## 6. Scenario 5 — Inspector Asset Picker

- [ ] 6.1 Add `[...]` asset picker buttons to Inspector WebView for resource-type fields
  - [ ] Inspector renders `<button class="asset-picker">...</button>` next to resource fields
  - [ ] On click → sends `{ type: 'openAssetPicker', fieldName, assetType }` to extension

- [ ] 6.2 Extension handles `openAssetPicker` message
  - [ ] Populate `QuickPickItem[]` from asset registry filtered by `assetType`
  - [ ] Show `vscode.window.showQuickPick`
  - [ ] On selection → call `scene.assignAsset` IPC
  - [ ] postMessage back to Inspector to update field display

- [ ] 6.3 Right-click context menu on Asset Browser items
  - [ ] Command: `"Assign to selected node"` — calls `scene.assignAsset` if a node is selected in Scene Tree
  - [ ] Registered in `package.json` under `view/item/context` for `uzengine.assetBrowser`

## 7. Undo/Redo Integration

- [ ] 7.1 `scene.reparentNode` IPC generates an `EditorCommand` on engine side:
  - `execute`: re-parent to new parent
  - `undo`: re-parent back to original parent (store original parent + sibling index)

- [ ] 7.2 `scene.assignAsset` IPC generates an `EditorCommand`:
  - `execute`: assign new asset
  - `undo`: restore previous asset handle (or destroy component if it was auto-created)

- [ ] 7.3 `editor.placeAsset` IPC generates an `EditorCommand`:
  - `execute`: create node + assign asset
  - `undo`: destroy the created node

- [ ] 7.4 All commands pushed to `UndoStack` (see `phase1-undo-redo`)

## 8. Quality & Polish

- [ ] 8.1 Viewport shows drag cursor change when `DragDropCoordinator` is in `dragging` state
  - [ ] postMessage sets `cursor: 'copy'` on canvas during drag

- [ ] 8.2 Scene Tree shows visual drop indicator (native VSCode DnD renders this automatically)

- [ ] 8.3 Asset Browser shows drag icon overlay when item is being dragged
  - [ ] Use `vscode.TreeItem.iconPath` with a "dragging" variant (optional)

- [ ] 8.4 Cancel drag on `Escape` key
  - [ ] Extension keydown listener during drag state → calls `coordinator.endDrag()`

- [ ] 8.5 Error handling: if `editor.placeAsset` fails, show `vscode.window.showErrorMessage`

- [ ] 8.6 Write `DragDropCoordinator` unit tests (state machine transitions)
- [ ] 8.7 Write integration tests for all 5 IPC handlers

## Definition of Done

- [ ] Scene Tree items can be re-parented by drag & drop within the tree
- [ ] Dragging a `.glb` from Asset Browser and dropping on a Scene Tree node assigns the mesh
- [ ] Dragging a `.glb` from Asset Browser and dropping on the Viewport creates a new node at the world position
- [ ] Viewport shows green-border feedback while an asset is being dragged over it
- [ ] Files copied to `assets/` folder are auto-imported within 300ms
- [ ] Asset Browser has a working "Import File..." button
- [ ] Inspector property slots have `[...]` asset picker buttons
- [ ] All DnD operations are undoable (Ctrl+Z restores previous state)
- [ ] All unit and integration tests pass
- [ ] No regression in existing editor panel functionality
