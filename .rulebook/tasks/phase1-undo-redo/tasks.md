## 1. EditorCommand Interface & Base Commands (TypeScript)

- [ ] 1.1 Create `src/editor/undoStack.ts` — `EditorCommand` interface with `id: string`, `label: string`, `execute(): Promise<void>`, `undo(): Promise<void>`
- [ ] 1.2 Implement `UndoStack` class — `stack: EditorCommand[]`, `pointer: number`, `MAX_SIZE = 100`, `dirty: boolean`
- [ ] 1.3 Implement `UndoStack.execute(cmd)` — push command, truncate redo tail, cap at MAX_SIZE, set dirty, fire `onChanged`
- [ ] 1.4 Implement `UndoStack.undo()` — call `stack[pointer].undo()`, decrement pointer, fire `onChanged`
- [ ] 1.5 Implement `UndoStack.redo()` — call `stack[pointer+1].execute()`, increment pointer, fire `onChanged`
- [ ] 1.6 Implement `UndoStack.markSaved()` — set dirty to false, fire `onChanged`
- [ ] 1.7 Implement `UndoStack.clear()` — reset stack and pointer (called on scene load)

## 2. Command Implementations (TypeScript)

- [ ] 2.1 Create `SetPropertyCommand` — captures `nodeId`, `key`, `oldValue`, `newValue`; `execute` calls `scene.setProperty` with `newValue`; `undo` calls with `oldValue`
- [ ] 2.2 Create `AddNodeCommand` — captures `type`, `parentId`, `name`; `execute` calls `scene.addNode` and stores returned `nodeId`; `undo` calls `scene.removeNode`
- [ ] 2.3 Create `RemoveNodeCommand` — captures full subtree snapshot via `scene.getSubtree` before executing; `execute` calls `scene.removeNode`; `undo` calls `scene.restoreSubtree`
- [ ] 2.4 Create `ReparentCommand` — captures `nodeId`, `oldParentId`, `newParentId`; `execute` and `undo` call `scene.reparentNode`
- [ ] 2.5 Create `RenameNodeCommand` — wraps `SetPropertyCommand` for the `name` key
- [ ] 2.6 Create `BulkPropertyCommand` — array of `{nodeId, key, oldValue, newValue}` entries; `execute` and `undo` iterate the array calling `scene.setProperty`
- [ ] 2.7 Create `TransformCommand` — wraps `SetPropertyCommand` for the `transform` key; `newValue` populated from `editor.commitTransform` response

## 3. VSCode Integration

- [ ] 3.1 Register `uzengine.undo` command in `package.json` and wire to `undoStack.undo()`
- [ ] 3.2 Register `uzengine.redo` command in `package.json` and wire to `undoStack.redo()`
- [ ] 3.3 Add keybindings in `package.json`: `ctrl+z` → `uzengine.undo`, `ctrl+shift+z` → `uzengine.redo`, both with `when: uzengine.editorFocused`
- [ ] 3.4 Set `uzengine.editorFocused` context key to `true` when viewport webview or scene tree is focused; `false` otherwise
- [ ] 3.5 Status bar dirty indicator: show `● sceneName` when `undoStack.isDirty`, plain `sceneName` when clean
- [ ] 3.6 Subscribe to `undoStack.onChanged` in all relevant panels to update UI state (undo/redo menu item enabled state)

## 4. Inspector Debounce

- [ ] 4.1 Add debounce logic in `InspectorPanel` message handler — 300 ms timer before creating `SetPropertyCommand`
- [ ] 4.2 Capture `oldValue` from cache on first keystroke before starting the debounce timer
- [ ] 4.3 Send `scene.setProperty` live on every keystroke (for visual feedback) bypassing the undo stack
- [ ] 4.4 On debounce fire: create `SetPropertyCommand` with captured `oldValue` and final `newValue`, push to `undoStack`
- [ ] 4.5 Cancel pending debounce timer when a different property starts being edited

## 5. Dirty State / Save Prompts

- [ ] 5.1 Check `undoStack.isDirty` before launching play mode; show "Save and Play / Play Without Saving / Cancel" quick-pick
- [ ] 5.2 Check `undoStack.isDirty` in extension `deactivate()` hook; if dirty, prompt user to save
- [ ] 5.3 Clear `UndoStack` on `scene.load` event — call `undoStack.clear()` and `undoStack.markSaved()`
- [ ] 5.4 Call `undoStack.markSaved()` after successful `scene.save` IPC response

## 6. Engine IPC Handlers (C++)

- [ ] 6.1 Implement `scene.getSubtree` handler — serializes the node subtree rooted at `id` to JSON; returns `{subtree: {...}}`
- [ ] 6.2 Implement `scene.restoreSubtree` handler — deserializes and inserts a subtree JSON under `parentId`; returns `{rootId: N}`
- [ ] 6.3 Register both handlers in the IPC dispatch table alongside existing `scene.*` handlers
- [ ] 6.4 Validate that `scene.restoreSubtree` produces an identical node tree (same types, same properties) when given a `scene.getSubtree` snapshot

## 7. Testing (TypeScript)

- [ ] 7.1 Unit test `UndoStack` — execute, undo, redo, max-size cap, dirty flag, markSaved, clear
- [ ] 7.2 Unit test `SetPropertyCommand` — execute sends correct IPC args, undo sends old value
- [ ] 7.3 Unit test `RemoveNodeCommand` — snapshot captured before execute; undo restores via restoreSubtree
- [ ] 7.4 Unit test `BulkPropertyCommand` — all entries applied on execute; all reverted on undo
- [ ] 7.5 Unit test Inspector debounce — verify single undo entry created after 300 ms, not one per keystroke
- [ ] 7.6 Integration test: execute 105 commands, verify stack size stays at 100 (oldest dropped)
- [ ] 7.7 Test `uzengine.undo` / `uzengine.redo` VSCode commands call the correct stack methods
- [ ] 7.8 Test dirty prompt before play mode — each choice (save, skip, cancel) produces correct outcome

## 8. Documentation

- [ ] 8.1 Verify `docs/specs/undo-redo.md` matches implementation (update if any detail changed during implementation)
- [ ] 8.2 Add `uzengine.undo` and `uzengine.redo` to the extension commands reference in `docs/guides/vscode-extension.md`
- [ ] 8.3 Document `scene.getSubtree` and `scene.restoreSubtree` in `docs/guides/ipc-mcp.md`
