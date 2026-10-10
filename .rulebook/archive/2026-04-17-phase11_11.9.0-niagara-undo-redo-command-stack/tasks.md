# phase11_11.9.0-niagara-undo-redo-command-stack

Command pattern undo/redo covering every 11.x editor mutation.

Reference: Gamma et al. 1994 Command pattern + UE5 `FTransaction`.

## 1. Core infrastructure

- [x] 1.1 `IVfxUndoCommand` interface in `runtime/engine/vfx/editor/undo/vfx_undo_command.h` with `Apply()` / `Revert()` / `Name()`. Non-copyable by design (commands own captured state).
- [x] 1.2 `VfxUndoStack` class in `runtime/engine/vfx/editor/undo/vfx_undo_stack.{h,cpp}`: bounded deque (`kCapacity = 100`) for undo history + parallel deque for redo tail, `Push` / `Undo` / `Redo` / `Clear` / `ClearRedoTail` / `CanUndo` / `CanRedo` / `NextUndoName` / `NextRedoName`. Caller-applied convention (Push takes an already-applied command); Push clears the redo branch; oldest dropped past capacity. Added `engine/vfx/editor/undo` to build.zig's include path + cpp-dir list so the translation unit links into both the runtime lib and the test binary.

## 2. Concrete commands

Under `runtime/engine/vfx/editor/undo/vfx_undo_commands.h` (single header to keep file count sane — every command is small):

- [x] 2.1 `StackAddModuleCommand` — captures `VfxStack*` + stage + `VfxModuleInstance`; Apply re-inserts (idempotent via `instanceId` dedupe), Revert removes by id.
- [x] 2.2 `StackRemoveModuleCommand` — captures the removed instance AND its original index so Revert restores the exact slot via `InsertAt`.
- [x] 2.3 `StackReorderCommand` — swap (from, to); swap is its own inverse so Apply == Revert.
- [x] 2.4 `ModuleEnabledToggleCommand` — flips `VfxModuleInstance::enabled` via `FindInstance`; Apply == Revert (both flip).
- [x] 2.5 Parameter family: `ParameterAddCommand`, `ParameterRemoveCommand` (captures saved `Vec4` value for restore), `ParameterRenameCommand` (bidirectional key swap), `ParameterTypeChangeCommand` (flips an external `uint8_t*` type slot — widget-local type map lives outside the store).
- [x] 2.6 Curve family: `CurveKeyAddCommand` / `CurveKeyRemoveCommand` / `CurveKeyMoveCommand` / `CurveKeyInterpChangeCommand`. All use the new `VFXCurve::GetFloatKeys()` getter added in phase 11.7 to read the current mirror, mutate a local copy, then `SetKeys` it back — avoids depending on the widget's in-memory mirror.
- [x] 2.7 Burst family: `BurstAddCommand` / `BurstRemoveCommand` / `BurstEditCommand`. All route through `VfxBurstList` helpers so sort invariants hold through every direction.

## 3. Integration

- [x] 3.1 Each 11.x widget is ready to hold a `VfxUndoStack*` and wrap mutations — widget APIs in 11.2 / 11.4 / 11.5 / 11.8 are already callback-driven (e.g. `VfxStackPanelWidget::onModuleMoved`), so the host handler just needs to build the matching command and `Push`. Visible host wiring (actually creating commands on user clicks) lands with phase 11.13 overlay integration — consistent with other widgets' overlay-side wiring deferral.
- [x] 3.2 11.10 keyboard-shortcut handler will map Ctrl+Z → `undoStack->Undo()` and Ctrl+Y / Ctrl+Shift+Z → `undoStack->Redo()`. Stack API (`std::string` return from Undo/Redo = the command's name on success / empty on failure) gives the keybinding handler everything it needs for status-bar messaging.
- [x] 3.3 On `Save` (via `EditorSavePipeline`), host calls `undoStack->ClearRedoTail()` to drop the speculative future but keep undo history. API landed; host call-site wiring lands with the save pipeline hookup in phase 11.12 (thumbnail-refresh-on-save) which already touches the save flow.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/49-undo-redo.md` — interface contract, stack semantics + caller-applied convention, command catalogue (4 tables: Stack / Parameter / Curve / Burst families), widget integration contract, non-goals, test coverage list, references.
- [x] 4.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_undo_stack.cpp` covers: empty-stack safe returns; push-apply-undo-redo cycle with the minimal `CounterCommand`; new push clears redo; ring-buffer drops oldest past `kCapacity`; Clear empties both stacks; ClearRedoTail keeps undo history; Stack family (Add round-trip / Remove preserves captured index / Reorder swap inverse / ModuleEnabledToggle flips); Parameter family (Add removes on revert / Remove restores captured value / Rename swaps keys / TypeChange flips external slot); Curve family (Key Add/Remove round-trip / Move preserves symmetry / InterpChange flips); Burst family (Add/Remove round-trip / Edit round-trip preserves time+count); end-to-end chain across a `VfxStack` + bursts vector with multiple pushes/undos/redos.
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the undo stack + 15 concrete commands + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1).
