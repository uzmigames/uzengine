# Proposal: phase11_11.10.0-niagara-keyboard-shortcuts

## Why
Phase 10.4 / phase 9.3 already shipped `VfxGraphKeybindingHandler` for the node-graph widget (Ctrl+C/V/D, Delete, Escape, arrows). Phase 11 adds several new editor surfaces (stack, parameters, curves, timeline, picker) — each needs shortcuts matching Niagara's conventions (Ctrl+Z, Space, arrows, Ctrl+F, Tab). Without a unified registry, every widget reinvents input handling.

## What Changes
Extend `VfxGraphKeybindingHandler` → `VfxEditorKeybindingHandler` (rename or compose) that dispatches to a `focusedPanel` enum (`Stack / Curves / Parameters / Timeline / NodeGraph`). Per-panel actions routed to the corresponding widget. Space toggles play/pause (timeline), Ctrl+Z/Y route through 11.9 undo stack, Ctrl+F focuses a search field, Tab cycles between focused panels, Escape clears selection / closes picker dialog.

## Impact
- Affected specs: docs/engine/25-vfx/ (new: 50-keyboard-shortcuts.md)
- Affected code: runtime/engine/vfx/editor/vfx_editor_keybindings.{h,cpp} (new or augment existing `vfx_graph_keybindings.h`)
- Breaking change: NO (additive; existing graph shortcuts preserved)
- User benefit: keyboard-driven authoring matches industry convention
- Estimated effort: S

## Source
UE5 `FNiagaraEditorCommands` + phase 9.3 graph keybindings.
