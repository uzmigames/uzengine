## 1. SelectionManager (TypeScript) ✅

- [x] 1.1 Create `src/editor/selectionManager.ts` — `SelectionManager` class with `selected: Set<number>`, methods `selectOnly`, `add`, `remove`, `toggle`, `selectRange`, `clear`, `has`, `ids`, `count`
- [x] 1.2 Implement `selectRange(fromId, toId)` — collect all node IDs between the two anchors using depth-first tree order from the cached scene tree
- [x] 1.3 Fire `onChanged: vscode.EventEmitter<ReadonlySet<number>>` on every state mutation
- [x] 1.4 Expose `primaryId: number | undefined` — the most recently plain-clicked node (anchor for Shift+click and Inspector title)
- [x] 1.5 `handleNodeRemoved(id)` method removes the deleted ID from the set
- [x] 1.6 `handleSceneLoad()` method clears selection

## 2. Scene Tree Panel (TypeScript) ✅

- [x] 2.1 Wire `TreeView.onDidChangeSelection` → `selectionManager.selectOnly` / `selectMany`
- [x] 2.2 Enable `canSelectMany: true` — VSCode natively handles Ctrl+click (toggle) and Shift+click (range)
- [x] 2.3 Shift+click range handled by VSCode native multi-select (flat tree cache synced to SelectionManager)
- [x] 2.4 Tree item highlighting via VSCode native multi-select (selected items highlighted automatically)
- [x] 2.5 Reverse sync: `selectionManager.onChanged` → `treeView.reveal(primaryNode)` with feedback loop guard

## 3. Viewport Panel (TypeScript)

- [x] 3.1 On plain left-click: call `editor.pickNode`; result → `selectionManager.selectOnly`
- [x] 3.2 On `Ctrl+left-click`: call `editor.pickNode`; result → `selectionManager.toggle`
- [x] 3.3 On left-click on empty space (pickNode returns null): `selectionManager.clear()`
- [x] 3.4 Implement rubber-band box select: `mousedown` on empty canvas starts draw; `mousemove` draws rectangle overlay; `mouseup` calls `editor.boxPick` and calls `selectionManager.selectOnly` (or `add` if Ctrl held) with returned IDs
- [x] 3.5 Subscribe to `selectionManager.onChanged` and send `editor.setSelection({nodeIds: [...]})` IPC to engine so gizmo updates

## 4. Engine — `editor.setSelection` and `editor.boxPick` (C++)

- [x] 4.1 Register `editor.setSelection` handler — stores list of selected node IDs; gizmo pass uses these to compute centroid and render gizmo
- [x] 4.2 `editor.boxPick` is already specified in the gizmos task (phase1-editor-gizmos) — verify it is implemented and reuse here

## 5. Inspector Panel — Shared Properties (TypeScript) ✅

- [x] 5.1 When `selectionManager.count === 1`, Inspector shows that node's properties (existing behaviour unchanged)
- [x] 5.2 When `selectionManager.count > 1`, compute intersection of properties (same key + same type)
- [x] 5.3 Implement `commonProperties(allNodeProps: PropertyDescriptor[][])` — exported, tested (8 tests)
- [x] 5.4 For properties with mixed values across nodes, display `"--"` (MIXED_VALUE sentinel)
- [ ] 5.5 When user edits a mixed-value field, create a `BulkPropertyCommand` — deferred (UndoStack)
- [x] 5.6 Show Inspector header `"N nodes selected"` with the count when `count > 1`
- [x] 5.7 Show empty Inspector with message `"No common properties"` when the intersection is empty

## 6. Bulk Operations (TypeScript) — DEFERRED

- [x] 6.0 Basic `deleteSelected` and `duplicateSelected` implemented without UndoStack (iterates IPC per node)
- [ ] 6.1 `deleteSelected()` with `BulkRemoveCommand` + `UndoStack` — deferred (blocked on undo system)
- [ ] 6.2 `duplicateSelected()` with `BulkDuplicateCommand` + `UndoStack` — deferred
- [ ] 6.3 `moveToParent()` with `BulkReparentCommand` — deferred
- [ ] 6.4 `groupSelected()` with `GroupCommand` — deferred

## 7. Keyboard Shortcuts (TypeScript + package.json) ✅

- [x] 7.1 Register `Ctrl+A` → `uzengine.selectAll` with `when: focusedView == 'uzengine.sceneTree'`
- [x] 7.2 Register `Escape` → `uzengine.clearSelection` with `when: focusedView == 'uzengine.sceneTree'`
- [x] 7.3 Register `Delete` → `uzengine.deleteSelected` with `when: focusedView == 'uzengine.sceneTree'`
- [x] 7.4 Register `Ctrl+D` → `uzengine.duplicateSelected` with `when: focusedView == 'uzengine.sceneTree'`
- [ ] 7.5 Register `G` → `uzengine.groupSelected` — deferred (requires groupSelected impl in Section 6)

## 8. Testing (TypeScript)

- [x] 8.1 Unit test `SelectionManager.selectOnly` — replaces any existing selection with exactly one ID
- [x] 8.2 Unit test `SelectionManager.toggle` — adds if absent, removes if present
- [x] 8.3 Unit test `SelectionManager.selectRange` — selects contiguous block in tree order; verify correct IDs included
- [x] 8.4 Unit test `SelectionManager.clear` — set becomes empty; `onChanged` fires
- [x] 8.5 Unit test `commonProperties` — intersection, missing property exclusion, 8 test cases
- [x] 8.6 Unit test mixed-value display — MIXED_VALUE when values differ, same value when equal
- [ ] 8.7 Unit test `BulkPropertyCommand` — deferred (UndoStack)
- [ ] 8.8 Unit test `deleteSelected` with `BulkRemoveCommand` — deferred (UndoStack)
- [ ] 8.9 Integration test: Ctrl+click three nodes → Inspector shows common properties — deferred
- [ ] 8.10 Integration test: Delete + Ctrl+Z — deferred (UndoStack)

## 9. Documentation — DEFERRED

- [ ] 9.1 Verify `docs/specs/multi-select.md` matches implementation
- [ ] 9.2 Document `editor.setSelection` in `docs/guides/ipc-mcp.md`
- [ ] 9.3 Add multi-select usage to `docs/guides/getting-started.md`

## Deferred to Future Phases

- [ ] D1. UndoStack integration for all bulk operations (5.5, 6.1-6.4, 7.5, 8.7-8.10)
- [ ] D2. `groupSelected()` command + G keybinding (7.5) — blocked on 6.4
- [ ] D3. Documentation (9.1-9.3) — pending final implementation
