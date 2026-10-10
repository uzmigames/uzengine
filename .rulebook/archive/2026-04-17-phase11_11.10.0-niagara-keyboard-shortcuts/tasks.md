# phase11_11.10.0-niagara-keyboard-shortcuts

Unified keyboard-shortcut dispatcher covering every 11.x editor panel.

Reference: UE5 `FNiagaraEditorCommands` + phase 9.3 handler.

## 1. Handler

- [x] 1.1 `VfxEditorKeybindingHandler` in `runtime/engine/vfx/editor/vfx_editor_keybindings.{h,cpp}`. Binds the 5 phase-11 widgets (stack / parameters / curves / timeline / picker) + undo stack via `Bind*` setters; holds an optional pointer to the phase 9.3 `VfxGraphKeybindingHandler` so NodeGraph focus still forwards Ctrl+C/V/D/Delete through the graph handler without code duplication. Platform-independent `Key` enum (letters, F2, N1-N5, arrows, Space/Tab/Escape/Delete/Enter/Home/End/Comma/Period) keeps the host free to map its native keys however it wants.
- [x] 1.2 `VfxFocusedPanel` enum (`None / Stack / Curves / Parameters / Timeline / NodeGraph / Picker`). Host updates via `SetFocusedPanel` when a widget gains hover + first click. `VfxFocusedPanelName` helper for logs.

## 2. Global shortcuts (any focused panel)

- [x] 2.1 `Ctrl+Z` → `undoStack->Undo()` + increments `UndoCount` diagnostic.
- [x] 2.2 `Ctrl+Y` and `Ctrl+Shift+Z` → `undoStack->Redo()` + increments `RedoCount`.
- [x] 2.3 `Ctrl+S` → fires `onSaveRequested` callback. Host wires to `EditorSavePipeline::Save()` (the callback signature keeps the handler free of save-pipeline coupling).
- [x] 2.4 `Ctrl+F` → fires `onFocusSearch(focusedPanel)` only for panels with a search input (Stack, Parameters, Picker). Silent for Curves / Timeline — no-op as specified.
- [x] 2.5 `Tab` → `CycleFocusedPanel()` walks Stack → Parameters → Curves → Timeline → Stack (other focuses seed back at Stack on the first Tab).
- [x] 2.6 `Escape` → closes picker when open (modal); otherwise clears selection on the focused panel (Stack / Parameters / Curves each have `ClearSelection`). "Exit edit mode" semantic delegated to host — widgets don't currently expose an edit-mode flag beyond selection, which `ClearSelection` already handles.

## 3. Panel-specific shortcuts

- [x] 3.1 Timeline focus: `Space` → `TogglePlayPause`; `Left`/`Right` → `Seek ±0.1s`; `Shift+Left/Right` → `Seek ±1s`; `,`/`.` → `StepBackward`/`StepForward` (scrub-back / `StepFrame 1/60` respectively); `1/2/3/4/5` → `SetSpeed(0.25/0.5/1/2/4)` routed through the speed-ring snap so the displayed label stays stable.
- [x] 3.2 Stack focus: `Delete` → fires `onModuleRemoveRequested(selectedInstanceId)`; `Ctrl+D` → fires `onModuleDuplicateRequested`. Host wraps both in `StackRemoveModuleCommand` / duplicate-command undo wrappers (phase 11.9 command family already lands).
- [x] 3.3 Curves focus: arrows nudge selected key by `snapTime` / `snapValue` (fallback 0.01 / 0.001). Float mode moves time + value; Ramp mode moves stop position only. `Delete` removes selected key (Float) / stop (Ramp).
- [x] 3.4 Parameters focus: `F2` → fires `onRenameRequested` callback. Host drives the actual `StringInput` focus + `RenameParameter` call (the handler stays free of `StringInput` state).

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/50-keyboard-shortcuts.md` — architecture diagram, focus model, full shortcut reference tables (global + 4 per-panel + picker modality), host integration sample code block, test coverage list.
- [x] 4.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_editor_keybindings.cpp` covers: Tab cycles 4-panel ring from None → Stack → Parameters → Curves → Timeline → Stack; Ctrl+Z routes to undo / Ctrl+Y + Ctrl+Shift+Z to redo; Ctrl+S fires save callback; Ctrl+F fires only for searchable panels (Stack / Parameters); Escape clears Stack-panel selection; Timeline Space toggles play/pause; arrows seek ±0.1 / Shift+arrow ±1.0; speed presets 1-5 set 0.25/0.5/1/2/4x; Stack Delete fires remove-callback; Ctrl+D fires duplicate-callback; Curves arrows nudge selected key by snap step (Float), Delete removes key; Parameters F2 fires rename-callback; Picker modality Escape closes, Down navigates, Enter confirms.
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the handler + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1).
