# Proposal: phase15_15.4.0-split-layout-integration

## Why

Phases 15.1 / 15.2 / 15.3 ship the layout mode + splitter widget +
editor-pane composite standalone. Phase 15.4 wires them into
`VfxEditorOverlay::BuildNiagaraUI` so the running editor renders
the split layout when the host enables it. Completes the
user-requested 50/50 split with draggable gutter + viewport on
the left + all editing on the right.

## What Changes

1. `VfxEditorOverlay` gains `VfxSplitterWidget m_splitter` +
   `VfxEditorPaneWidget m_editorPane`.
2. `SetLayoutMode(VfxEditorLayoutMode)` helper swaps the live
   layout at runtime.
3. A `View > Split Layout` menu item toggles the mode + pushes a
   status-bar notification.
4. When `layoutMode == SplitHorizontal`:
   - Render the splitter bar at the boundary between
     `viewportReserve` and `editorPane`.
   - Route drag input to `m_splitter` → feed delta into
     `m_composition.LayoutConfig().splitFraction`.
   - Render `m_editorPane` inside the `editorPane` rect, bound
     to the existing stack / params / tree / overview widgets.
5. Niagara mode's existing three-column render path stays intact
   — the split path is an `else` branch, not a rewrite.
6. Demo app sets the layout to `SplitHorizontal` at startup so
   the new layout is the first thing the user sees.

## Impact

- Affected specs: none (integration only).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}`.
  - `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp`
    (default mode switch).
- Breaking change: NO — Niagara mode preserved as the legacy
  path; host opts into the split mode explicitly.
- User benefit: the exact layout the user sketched — viewport
  50 % left / all editing 50 % right / timeline bottom /
  draggable gutter in the middle.
