# phase11_11.9.0-niagara-undo-redo-command-stack

Command pattern undo/redo covering every 11.x editor mutation.

Reference: Gamma et al. 1994 Command pattern + UE5 `FTransaction`.

## 1. Core infrastructure

- [ ] 1.1 `IVfxUndoCommand` interface in
      `runtime/engine/vfx/editor/undo/vfx_undo_command.h`:
      `virtual void Apply() = 0;`
      `virtual void Revert() = 0;`
      `virtual const char* Name() const noexcept = 0;`
- [ ] 1.2 `VfxUndoStack` class in
      `runtime/engine/vfx/editor/undo/vfx_undo_stack.{h,cpp}`:
      ring-buffer of 100 commands, `Push(unique_ptr<IVfxUndoCommand>)`,
      `Undo()`, `Redo()`, `Clear()`, `CanUndo()`, `CanRedo()`.

## 2. Concrete commands

Under `runtime/engine/vfx/editor/undo/`:
- [ ] 2.1 `StackAddModuleCommand` — add/remove a VfxModuleInstance.
- [ ] 2.2 `StackRemoveModuleCommand` — reverse of above.
- [ ] 2.3 `StackReorderCommand` — swap two indices within a stage.
- [ ] 2.4 `ModuleEnabledToggleCommand` — flip enabled bit.
- [ ] 2.5 `ParameterAddCommand` / `ParameterRemoveCommand` /
      `ParameterRenameCommand` / `ParameterTypeChangeCommand`.
- [ ] 2.6 `CurveKeyAddCommand` / `CurveKeyRemoveCommand` /
      `CurveKeyMoveCommand` / `CurveKeyInterpChangeCommand`.
- [ ] 2.7 `BurstAddCommand` / `BurstRemoveCommand` / `BurstEditCommand`.

## 3. Integration

- [ ] 3.1 Each 11.x widget holds a pointer to the owner's `VfxUndoStack`
      and wraps mutations in `undoStack->Push(std::make_unique<FooCommand>(...))`
      instead of touching the data directly.
- [ ] 3.2 11.10 keyboard-shortcut handler maps Ctrl+Z → Undo,
      Ctrl+Y / Ctrl+Shift+Z → Redo.
- [ ] 3.3 On `Save` (via `EditorSavePipeline`), clear the redo branch
      via `undoStack->ClearRedoTail()` but keep undo history.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/49-undo-redo.md` listing every
      command class + the state it captures.
- [ ] 4.2 Write tests covering the new behavior
      Per-command apply/revert round-trip, 100-cmd ring buffer,
      clear-redo-on-new-push semantics.
- [ ] 4.3 Run tests and confirm they pass
      `zig build` clean; QA: add module → Ctrl+Z → module gone;
      Ctrl+Y → module back.
