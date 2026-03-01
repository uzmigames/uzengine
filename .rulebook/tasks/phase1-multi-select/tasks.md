## 1. SelectionManager (TypeScript)

- [ ] 1.1 Create `src/editor/selectionManager.ts` — `SelectionManager` class with `selected: Set<number>`, methods `selectOnly`, `add`, `remove`, `toggle`, `selectRange`, `clear`, `has`, `ids`, `count`
- [ ] 1.2 Implement `selectRange(fromId, toId)` — collect all node IDs between the two anchors using depth-first tree order from the cached scene tree
- [ ] 1.3 Fire `onChanged: vscode.EventEmitter<ReadonlySet<number>>` on every state mutation
- [ ] 1.4 Expose `primaryId: number | undefined` — the most recently plain-clicked node (anchor for Shift+click and Inspector title)
- [ ] 1.5 Subscribe to `scene.nodeRemoved` IPC event and remove the deleted ID from the set
- [ ] 1.6 Call `selectionManager.clear()` on `scene.load` event

## 2. Scene Tree Panel (TypeScript)

- [ ] 2.1 Intercept click events in the Scene Tree `TreeView.onDidChangeSelection` and translate to `selectionManager.selectOnly`
- [ ] 2.2 Detect `Ctrl` modifier (via custom webview or VSCode command context) and call `selectionManager.toggle`
- [ ] 2.3 Detect `Shift` modifier and call `selectionManager.selectRange`
- [ ] 2.4 Subscribe to `selectionManager.onChanged` and refresh tree item decorations so all selected nodes appear highlighted (use `TreeItem.description` badge or `iconPath` override)
- [ ] 2.5 Update the `TreeView.reveal` call to reveal and focus the `primaryId` node on each selection change

## 3. Viewport Panel (TypeScript)

- [ ] 3.1 On plain left-click: call `editor.pickNode`; result → `selectionManager.selectOnly`
- [ ] 3.2 On `Ctrl+left-click`: call `editor.pickNode`; result → `selectionManager.toggle`
- [ ] 3.3 On left-click on empty space (pickNode returns null): `selectionManager.clear()`
- [ ] 3.4 Implement rubber-band box select: `mousedown` on empty canvas starts draw; `mousemove` draws rectangle overlay; `mouseup` calls `editor.boxPick` and calls `selectionManager.selectOnly` (or `add` if Ctrl held) with returned IDs
- [ ] 3.5 Subscribe to `selectionManager.onChanged` and send `editor.setSelection({nodeIds: [...]})` IPC to engine so gizmo updates

## 4. Engine — `editor.setSelection` and `editor.boxPick` (C++)

- [ ] 4.1 Register `editor.setSelection` handler — stores list of selected node IDs; gizmo pass uses these to compute centroid and render gizmo
- [ ] 4.2 `editor.boxPick` is already specified in the gizmos task (phase1-editor-gizmos) — verify it is implemented and reuse here

## 5. Inspector Panel — Shared Properties (TypeScript)

- [ ] 5.1 When `selectionManager.count === 1`, Inspector shows that node's properties (existing behaviour unchanged)
- [ ] 5.2 When `selectionManager.count > 1`, fetch properties for all selected nodes and compute the intersection (same name + same type)
- [ ] 5.3 Implement `commonProperties(nodeProps: Map<number, Map<string, PropertyDescriptor>>): Map<string, PropertyDescriptor>`
- [ ] 5.4 For properties with mixed values across nodes, display `"--"` in numeric inputs and empty string in text inputs
- [ ] 5.5 When user edits a mixed-value field, create a `BulkPropertyCommand` capturing old values from each node's cache; push to `UndoStack`
- [ ] 5.6 Show Inspector header `"N nodes selected"` with the count when `count > 1`
- [ ] 5.7 Show empty Inspector with message `"No common properties"` when the intersection is empty

## 6. Bulk Operations (TypeScript)

- [ ] 6.1 Implement `deleteSelected()` — sort selected IDs by depth descending; snapshot each subtree; create `BulkRemoveCommand`; execute via `UndoStack`; then call `selectionManager.clear()`
- [ ] 6.2 Implement `duplicateSelected()` — for each selected node, call `scene.getSubtree` then `scene.restoreSubtree` under the same parent; rename duplicates `"<name> (copy)"`; select duplicates only; push `BulkDuplicateCommand`
- [ ] 6.3 Implement `moveToParent()` — show a `vscode.window.showQuickPick` listing all scene nodes; on selection, create `BulkReparentCommand` for all selected nodes; push to `UndoStack`
- [ ] 6.4 Implement `groupSelected()` — create a new `Node3D` at the centroid of selected nodes; reparent all selected nodes under it; push `GroupCommand` to `UndoStack`

## 7. Keyboard Shortcuts (TypeScript + package.json)

- [ ] 7.1 Register `Ctrl+A` → `uzengine.selectAll` (selects all scene nodes) with `when: uzengine.editorFocused`
- [ ] 7.2 Register `Escape` → `uzengine.clearSelection` with `when: uzengine.editorFocused`
- [ ] 7.3 Register `Delete` → `uzengine.deleteSelected` with `when: uzengine.editorFocused`
- [ ] 7.4 Register `Ctrl+D` → `uzengine.duplicateSelected` with `when: uzengine.editorFocused`
- [ ] 7.5 Register `G` → `uzengine.groupSelected` with `when: uzengine.viewportFocused`

## 8. Testing (TypeScript)

- [ ] 8.1 Unit test `SelectionManager.selectOnly` — replaces any existing selection with exactly one ID
- [ ] 8.2 Unit test `SelectionManager.toggle` — adds if absent, removes if present
- [ ] 8.3 Unit test `SelectionManager.selectRange` — selects contiguous block in tree order; verify correct IDs included
- [ ] 8.4 Unit test `SelectionManager.clear` — set becomes empty; `onChanged` fires
- [ ] 8.5 Unit test `commonProperties` — three nodes sharing `intensity` (float): intersection contains it; node missing `intensity`: not in result
- [ ] 8.6 Unit test mixed-value display — two nodes with `intensity = 1.0` and `intensity = 2.0` → Inspector field shows `"--"`
- [ ] 8.7 Unit test `BulkPropertyCommand` — all nodes updated on `execute`; all reverted on `undo`
- [ ] 8.8 Unit test `deleteSelected` — `BulkRemoveCommand` created with correct number of entries; `UndoStack` has one entry after operation
- [ ] 8.9 Integration test: Ctrl+click three nodes in Scene Tree → `selectionManager.ids.size === 3`; Inspector shows common properties only
- [ ] 8.10 Integration test: Delete after multi-select → all nodes removed; Ctrl+Z restores all

## 9. Documentation

- [ ] 9.1 Verify `docs/specs/multi-select.md` matches implementation
- [ ] 9.2 Document `editor.setSelection` in `docs/guides/ipc-mcp.md`
- [ ] 9.3 Add multi-select usage (Ctrl+click, Shift+click, box-select, Ctrl+D, Delete) to `docs/guides/getting-started.md`
