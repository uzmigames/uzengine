## 1. Scene File Format — `node_instance` (C++)

- [x] 1.1 Extend `.uzscene` parser (`src/scene/scene_loader.cpp`) to handle `[node_instance]` sections: `name`, `type`, `source` (res:// path), plus override key-value pairs
- [x] 1.2 On load, resolve `source` path, parse the referenced prefab file, build the node subtree, then apply override properties from the instance section
- [x] 1.3 Extend `.uzscene` serializer (`src/scene/scene_saver.cpp`) to write `[node_instance]` sections — emit only properties that differ from the prefab defaults
- [x] 1.4 Add `SceneNode.prefabSource: string` and `SceneNode.overrides: map<string, Value>` fields to the node data model
- [x] 1.5 Create a test `.uzscene` scene that instances `assets/prefabs/enemy.uzscene` with two instances at different positions

## 2. Engine — Prefab Edit Mode State (C++)

- [x] 2.1 Add `EditorState` enum to engine: `SCENE_EDIT`, `PREFAB_EDIT`; store as global in the editor module
- [x] 2.2 Cache the main scene serialized state (via `scene.save` to an in-memory buffer) when entering prefab mode
- [x] 2.3 Implement `scene.openPrefab({path})` — cache main scene, clear graph, load prefab, set `editorState = PREFAB_EDIT`, return `{prefabPath, rootNodeId}`
- [x] 2.4 Implement `scene.savePrefab({path})` — serialize current scene graph to the `.uzscene` file at `path`; return `{ok: true}`
- [x] 2.5 Implement `scene.closePrefab()` — restore cached main scene from in-memory buffer, set `editorState = SCENE_EDIT`, return `{ok: true}`
- [x] 2.6 Implement `scene.reloadPrefabInstances({prefabPath})` — find all `node_instance` nodes in the current main scene referencing `prefabPath`, reload their subtrees while preserving overrides
- [x] 2.7 Register `editor.setBackground({type: "checkerboard" | "solid", color?: [r,g,b]})` IPC handler for viewport background switching

## 3. Engine — Override IPC Handlers (C++)

- [x] 3.1 Implement `scene.getPrefabOverrides({instanceId})` — compare each property of the instance node against the prefab default; return `{overrides: [{key, instanceValue, prefabDefault}]}`
- [x] 3.2 Implement `scene.revertPrefabOverride({instanceId, key})` — delete the override for `key` on the instance node; property reverts to prefab default value
- [x] 3.3 Implement `scene.revertAllPrefabOverrides({instanceId})` — clear all override entries on the instance; all properties revert to prefab defaults
- [x] 3.4 Register all override handlers in the IPC dispatch table

## 4. TypeScript — EditorStateManager

- [x] 4.1 Create `src/editor/editorStateManager.ts` — `EditorStateManager` class with `mode: EditorMode`, `activePrefabPath: string | undefined`, `activeScenePath: string | undefined`
- [x] 4.2 Implement `enterPrefabMode(prefabPath)` — send `scene.openPrefab`, create fresh `UndoStack` for prefab session, stash main scene `UndoStack`, fire `onModeChanged`
- [x] 4.3 Implement `exitPrefabMode(save: boolean)` — if `save`, send `scene.savePrefab`; then send `scene.closePrefab`; restore stashed `UndoStack`; fire `onModeChanged`
- [x] 4.4 Expose `onModeChanged: vscode.EventEmitter<EditorMode>` — all panels subscribe to update their UI
- [x] 4.5 Register `uzengine.exitPrefabMode` VSCode command wired to `exitPrefabMode(true)` with dirty check

## 5. TypeScript — Viewport Toolbar

- [x] 5.1 Subscribe to `EditorStateManager.onModeChanged` in the viewport webview message bridge
- [x] 5.2 When mode becomes `'prefab'`: show breadcrumb `"Editing: res://prefabs/enemy.uzscene"` in toolbar; hide play controls; show "Exit" button; send `editor.setBackground({type: "checkerboard"})`
- [x] 5.3 When mode returns to `'scene'`: hide breadcrumb; show play controls; send `editor.setBackground({type: "solid", color: [0.18, 0.18, 0.18]})`
- [x] 5.4 "Exit" button in toolbar: if `UndoStack.isDirty`, show quick-pick "Save and Exit / Discard and Exit / Cancel"; call `exitPrefabMode` accordingly

## 6. TypeScript — Inspector Override Panel

- [x] 6.1 When a `node_instance` node is selected (has `prefabSource` property), call `scene.getPrefabOverrides({instanceId})` after loading Inspector properties
- [x] 6.2 Highlight overridden properties in blue in the Inspector webview (CSS class `overridden`)
- [x] 6.3 Add a "Prefab Overrides" collapsible section at the bottom of the Inspector listing each override with its instance value, prefab default, and a "Revert" button
- [x] 6.4 "Revert" button click → call `scene.revertPrefabOverride`; refresh Inspector and remove blue highlight for that property
- [x] 6.5 "Revert All" button click → call `scene.revertAllPrefabOverrides`; refresh full Inspector
- [x] 6.6 Wrap revert actions in `SetPropertyCommand` on the `UndoStack` (old value = instance override, new value = prefab default) so they can be undone

## 7. TypeScript — Asset Browser and Scene Tree Integration

- [x] 7.1 Add "Edit Prefab" context menu item on `.uzscene` files in the Asset Browser → calls `EditorStateManager.enterPrefabMode(path)`
- [x] 7.2 Add "Edit Prefab" context menu item on `node_instance` nodes in the Scene Tree → resolves `prefabSource` path and calls `enterPrefabMode`
- [x] 7.3 After prefab save, if main scene has instances of the saved prefab, show `vscode.window.showInformationMessage` prompt: "Reload instances?" with "Reload" and "Later" actions
- [x] 7.4 "Reload" action → send `scene.reloadPrefabInstances`; refresh Scene Tree

## 8. Testing (TypeScript)

- [x] 8.1 Unit test `EditorStateManager.enterPrefabMode` — sends correct `scene.openPrefab` IPC, stashes UndoStack, fires `onModeChanged`
- [x] 8.2 Unit test `EditorStateManager.exitPrefabMode(save=true)` — sends `scene.savePrefab` then `scene.closePrefab`, restores UndoStack
- [x] 8.3 Unit test `EditorStateManager.exitPrefabMode(save=false)` — sends only `scene.closePrefab`, no save
- [x] 8.4 Unit test dirty check before exit — if `UndoStack.isDirty`, quick-pick is shown; "Cancel" does not call `closePrefab`
- [x] 8.5 Unit test `scene.getPrefabOverrides` response parsing — correct `overrides` array built, overridden properties flagged
- [x] 8.6 Unit test "Revert" button — `scene.revertPrefabOverride` IPC called with correct args; `SetPropertyCommand` pushed to UndoStack
- [x] 8.7 Integration test: enter prefab mode → edit a property → save → exit → main scene restored and `UndoStack` is the main scene stack
- [x] 8.8 Test `scene.reloadPrefabInstances` prompt — appears only when current scene contains at least one instance of the saved prefab path

## 9. Testing (C++ — IPC handlers)

- [x] 9.1 Test `scene.openPrefab` — main scene cached; prefab scene loaded; `editorState == PREFAB_EDIT`
- [x] 9.2 Test `scene.savePrefab` — `.uzscene` file written; round-trip: reload produces identical scene graph
- [x] 9.3 Test `scene.closePrefab` — main scene restored correctly from cache; `editorState == SCENE_EDIT`
- [x] 9.4 Test `scene.getPrefabOverrides` — instance with `rotation.y = 45` when prefab default is `0` → override entry present
- [x] 9.5 Test `scene.revertPrefabOverride` — after revert, `getPrefabOverrides` returns empty list for that key

## 10. Documentation

- [x] 10.1 Verify `docs/specs/prefab-edit-mode.md` matches implementation
- [x] 10.2 Document `[node_instance]` section format in `docs/specs/scene-system.md`
- [x] 10.3 Document all new `scene.openPrefab`, `scene.savePrefab`, `scene.closePrefab`, `scene.getPrefabOverrides`, override IPC calls in `docs/guides/ipc-mcp.md`
- [x] 10.4 Add prefab workflow (create prefab, instance it, edit prefab, revert override) to `docs/guides/getting-started.md`
