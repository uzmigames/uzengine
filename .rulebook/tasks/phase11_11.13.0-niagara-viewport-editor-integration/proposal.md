# Proposal: phase11_11.13.0-niagara-viewport-editor-integration

## Why
Tasks 11.1–11.12 each ship a widget or data piece, but they're isolated — none of them are actually hooked into `vfx_viewport_app`'s overlay layout yet. The Niagara-parity editor only feels coherent when all pieces are composed in a single canvas with consistent layout, focus routing, save pipeline, and a unified editor state. This task is the final integration that ties 11.1–11.12 into a production-ready authoring experience.

## What Changes
Reshape `VfxEditorOverlay::BuildUI` into a 4-panel layout:
- **Left panel (25% width):** `VfxStackPanelWidget` (11.2) showing the 7 stages.
- **Center panel (35% width):** context-sensitive inspector:
  - stack row selected → module parameters (legacy slider form)
  - curve selected → `VfxCurvesEditorWidget` (11.5)
  - parameter selected → `VfxParameterInspectorWidget` (11.4) right-pane form
  - node graph active → `VfxNodeGraphWidget` (phase 10.4)
- **Right panel (20% width):** `VfxParameterInspectorWidget` (11.4) left-pane list + `VfxSnippetLibraryWidget` (phase 9.4) in a tab view.
- **Bottom bar (full width, 32 px):** `VfxTimelineScrubberWidget` (11.6) with burst markers (11.8).
Plus: top menu bar adds File → Save, Edit → Undo/Redo (wired to 11.9); Escape/Tab routes focus per 11.10. Wire 11.12 thumbnail capture queue drain into the app's per-frame tick.

## Impact
- Affected specs: docs/engine/25-vfx/ (new: 51-niagara-layout.md)
- Affected code: VfxEditorOverlay BuildUI/HandleMouse; vfx_viewport_app.cpp render loop; every 11.x widget gets mounted into this layout
- Breaking change: visual layout reshaped (no existing data-format breakage)
- User benefit: single coherent Niagara-like editor — the whole point of phase 11
- Estimated effort: L

## Source
Synthesis of all 11.x tasks + UE5 Niagara System Editor window layout.
