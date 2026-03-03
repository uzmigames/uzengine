# Proposal: phase1-multi-select

## Why

Without multi-select, users can only edit one node at a time. Tasks like moving a group of objects, deleting an entire set of nodes, or applying a property change to multiple nodes require tedious repetition. Multi-select is a fundamental productivity feature in every scene editor; without it the editor is impractical for real scene construction.

## What Changes

- New TypeScript class `SelectionManager` (`src/editor/selectionManager.ts`) — owns the `Set<nodeId>` of currently selected nodes and fires `onChanged` events.
- Scene Tree panel updated to support `Ctrl+click` (toggle), `Shift+click` (range select), and multi-node highlighting.
- Viewport webview updated: `Ctrl+left-click` toggles selection; rubber-band box-select calls `editor.boxPick` IPC.
- Inspector updated to compute shared properties across multi-selection and display `"--"` for mixed values; property edits create `BulkPropertyCommand`.
- Bulk operations: Delete (BulkRemoveCommand), Duplicate, Move to Parent, Group.
- New IPC handlers: `editor.setSelection` (syncs selection to engine for gizmo rendering), `editor.boxPick`.
- Keyboard shortcuts: `Ctrl+A`, `Escape`, `Delete`, `Ctrl+D`, `G`, `F` with appropriate `when` clauses.

## Impact

- Affected specs: `docs/specs/multi-select.md` (new), `docs/specs/editor-panels.md` (Scene Tree + Inspector), `docs/specs/editor-gizmos.md` (centroid gizmo), `docs/specs/undo-redo.md` (BulkPropertyCommand)
- Affected code: VSCode extension (`src/panels/sceneTreePanel.ts`, `src/panels/inspectorPanel.ts`, `src/panels/viewportPanel.ts`), new `src/editor/selectionManager.ts`
- Breaking change: NO — additive; single-select paths still work unchanged
- User benefit: Edit multiple nodes simultaneously; bulk delete/duplicate; shared Inspector for batch property changes; box-select in viewport
