# Proposal: phase11_11.2.0-niagara-stack-panel-widget

## Why
The existing `VfxEditorOverlay` renders modules as flat property sliders without a visible "stack stage" structure. Niagara's signature authoring flow is its stack panel: a vertical scrolling list of 7 collapsible stage headers, each hosting ordered module cards with drag handles, enable checkboxes, and per-module context menus. Without this, `VfxStackEntry` (11.1) has no way to be edited interactively.

## What Changes
New `VfxStackPanelWidget` inside `runtime/engine/vfx/editor/`. The widget binds to a `VfxStack*` and renders each `VfxStackEntry` as a collapsible section with a module card list. Mouse drag on a module's card reorders within the stage; an "X" button removes. An "+" button at the bottom of each stage triggers 11.3's picker. Checkbox per card toggles `VfxModuleInstance::enabled` (visual dimmed state when off). Replaces the current flat module tree in `VfxEditorOverlay` for non-legacy stacks. Immediate-mode rendering via `TextRenderer` — no ImGui.

## Impact
- Affected specs: docs/engine/25-vfx/44-niagara-stack-model.md (augmented with UI mock)
- Affected code: runtime/engine/vfx/editor/vfx_stack_panel_widget.{h,cpp} (new); VfxEditorOverlay routes stack rendering here when `emitter->stack` is non-empty
- Breaking change: NO (legacy flat path preserved)
- User benefit: authors edit effects through a Niagara-recognizable stack; unblocks 11.3/11.9
- Estimated effort: M

## Source
UE5 `SNiagaraStack` + phase 10.4 `VfxNodeGraphWidget` interaction patterns (selection, rubber-band, keybindings).
