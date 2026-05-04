# Proposal: phase15_15.1.0-split-layout-mode

## Why

The current 4-panel composition (stack 22 % / center 24 % / viewport
reserve / right 18 %) cramps the viewport and scatters the editing
surfaces across three columns. User wants a simpler two-column
split: left half = 3D viewport, right half = all editing
(stack + parameters + emitters stacked/tabbed inside). Timeline
stays docked at the bottom.

A 50/50 default + draggable splitter puts the parameters on equal
footing with the render while letting the user bias either side
as needed.

## What Changes

1. New `VfxEditorLayoutMode::SplitHorizontal` enum in
   `VfxEditorComposition` that swaps the 3-column layout for a
   2-column split:
   - Left column: viewport reserve (fills vertical content area
     minus timeline + status bar).
   - Right column: editor pane housing the stack + parameter
     inspector + emitter overview + tree, all stacked vertically
     inside the same rect.
2. `VfxEditorLayoutConfig::splitFraction` (default 0.5) drives the
   boundary between the two columns. Range clamped to `[0.2, 0.8]`
   so neither side collapses entirely.
3. `VfxComputeEditorLayout` branches on `layoutMode`:
   - `Niagara` (existing 4-panel composition).
   - `SplitHorizontal` (new — viewport left, editorPane right).
4. Layout config persists `splitFraction` across frames; host
   updates it from splitter drag deltas (phase 15.2).

## Impact

- Affected specs: `specs/split-layout-mode/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_composition.{h,cpp}`.
- Breaking change: NO — default layout stays `Niagara` until the
  host opts in.
- User benefit: lays the groundwork for the requested 50/50 split
  (phases 15.2–15.4 wire the splitter + editor pane + render).
