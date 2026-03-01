# Proposal: phase1-undo-redo

## Why

Without an undo system, every accidental property change, node deletion, or transform drag is permanent. This makes iterative scene editing frustrating and error-prone. An undo/redo system is a baseline expectation in any editor tool and is required before users can comfortably work with the scene system.

## What Changes

- New TypeScript module `src/editor/undoStack.ts` implementing `UndoStack` and the `EditorCommand` interface.
- Command implementations: `SetPropertyCommand`, `AddNodeCommand`, `RemoveNodeCommand`, `ReparentCommand`, `RenameNodeCommand`, `BulkPropertyCommand`, `TransformCommand`.
- Two new VSCode commands registered in `package.json`: `uzengine.undo`, `uzengine.redo`.
- Keybindings: `Ctrl+Z` → undo, `Ctrl+Shift+Z` → redo, guarded by `uzengine.editorFocused` context key.
- Inspector debounce (300 ms) before committing property edits to the undo stack.
- Dirty tracking: status bar dot indicator; "save before play" prompt.
- Two new engine IPC handlers: `scene.getSubtree` and `scene.restoreSubtree`.

## Impact

- Affected specs: `docs/specs/undo-redo.md` (new), `docs/specs/editor-panels.md` (Inspector debounce), `docs/specs/multi-select.md` (BulkPropertyCommand)
- Affected code: VSCode extension (`src/`), engine IPC layer (`src/ipc/`)
- Breaking change: NO — additive only
- User benefit: Standard Ctrl+Z / Ctrl+Shift+Z undo/redo across all scene editing operations; scene dirty indicator; protection against accidental data loss
