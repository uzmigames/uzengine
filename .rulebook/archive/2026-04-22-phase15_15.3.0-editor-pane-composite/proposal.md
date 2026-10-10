# Proposal: phase15_15.3.0-editor-pane-composite

## Why

Phase 15.1 introduces the `editorPane` rect on the right half.
Something has to render inside it — the user wants "all editing"
there: stack + parameters + emitters. Three panels in one rect
means either:

- a vertical stack (simpler; each sub-panel gets a slice),
- a tab bar (denser; only one sub-panel visible at a time).

Niagara-style tabs match UE5 convention and let the user focus
on one surface at a time without scrolling past the others.

## What Changes

1. `VfxEditorPaneWidget` — composite that owns a `VfxTabStripWidget`
   at its top and routes clicks to one of three sub-panels:
   - `Stack` — renders the `VfxStackPanelWidget` in the body.
   - `Parameters` — renders the `VfxParameterInspectorWidget`.
   - `Emitters` — renders the phase-14.15 `VfxTreeWidget` +
     `VfxSystemOverviewWidget`.
2. Active tab persists in `VfxEditorPaneWidget::m_activeTab`;
   default = `Stack`.
3. `Render(bounds)` slices the top 28 px for the tab strip and
   renders the selected sub-panel body in the remaining rect.
4. `Bind(stack, params, tree, overview)` wires the widget
   pointers; null pointers degrade gracefully (sub-panel skips).

## Impact

- Affected specs: `specs/editor-pane-composite/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_pane_widget.{h,cpp}`
    (new).
- Breaking change: NO — standalone widget; mounting into the
  overlay happens in phase 15.4.
- User benefit: one surface hosting every editing sub-panel with
  a single tab switcher — matches the right-half half of the
  user's requested 50/50 split.
