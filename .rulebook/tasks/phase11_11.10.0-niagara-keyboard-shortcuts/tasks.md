# phase11_11.10.0-niagara-keyboard-shortcuts

Unified keyboard-shortcut dispatcher covering every 11.x editor panel.

Reference: UE5 `FNiagaraEditorCommands` + phase 9.3 handler.

## 1. Handler

- [ ] 1.1 `VfxEditorKeybindingHandler` in
      `runtime/engine/vfx/editor/vfx_editor_keybindings.{h,cpp}`.
      Composes the existing `VfxGraphKeybindingHandler` for graph-specific
      shortcuts, adds cross-panel shortcuts on top.
- [ ] 1.2 `FocusedPanel` enum: `None / Stack / Curves / Parameters /
      Timeline / NodeGraph / Picker`. Set by each widget when it gains
      hover + first click.

## 2. Global shortcuts (any focused panel)

- [ ] 2.1 `Ctrl+Z` → `undoStack->Undo()`.
- [ ] 2.2 `Ctrl+Y` or `Ctrl+Shift+Z` → `undoStack->Redo()`.
- [ ] 2.3 `Ctrl+S` → trigger save via `EditorSavePipeline`.
- [ ] 2.4 `Ctrl+F` → focus the current panel's search input
      (Stack: filter modules; Parameters: filter params; Picker: filter
      module list). If no search applicable, no-op.
- [ ] 2.5 `Tab` → cycle `focusedPanel` through Stack → Parameters →
      Curves → Timeline → Stack.
- [ ] 2.6 `Escape` → close picker dialog, deselect, exit edit mode.

## 3. Panel-specific shortcuts

- [ ] 3.1 Timeline focus: `Space` play/pause, `←`/`→` seek ±0.1s,
      `Shift+←/→` seek ±1s, `1/2/3/4/5` quick-set speed (0.25/0.5/1/2/4×).
- [ ] 3.2 Stack focus: `Delete` removes selected module (prompt if stage
      has only 1 module), `Ctrl+D` duplicates selected module.
- [ ] 3.3 Curves focus: arrows nudge selected key by 1 px (X-time, Y-value),
      `Delete` removes key.
- [ ] 3.4 Parameters focus: `F2` rename selected parameter.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/50-keyboard-shortcuts.md` with table.
- [ ] 4.2 Write tests covering the new behavior
      Per-shortcut dispatch, focus cycling, panel filter correctness.
- [ ] 4.3 Run tests and confirm they pass
      `zig build` clean; QA: drive the whole editor without touching
      the mouse for 30 seconds.
