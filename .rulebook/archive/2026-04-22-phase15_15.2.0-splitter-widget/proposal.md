# Proposal: phase15_15.2.0-splitter-widget

## Why

The 50/50 split needs a user-controlled gutter. Users expect to
drag the divider to re-balance the viewport vs. editor pane.
Every modern editor (UE5, VS Code, Blender) exposes this via a
thin vertical bar with a widened hit region + resize cursor.

## What Changes

1. New `VfxSplitterWidget` — stateless helper for a single
   vertical gutter:
   - `Render(TextRenderer&, bounds)` draws a 2-px vertical bar
     centred horizontally in the 6-px-wide `bounds` rect; bar
     tints `borderSubtle` idle, `borderStrong` hover,
     `accentPrimary` dragging.
   - `HitTest(x, y, bounds)` returns true when the cursor lies
     inside the 6-px-wide hit region.
   - `BeginDrag(mouseX)` + `UpdateDrag(mouseX) -> float delta` +
     `EndDrag()` state machine for drag-and-release.
2. Host feeds the drag delta into
   `layoutConfig.splitFraction += delta / screenW`, clamped by
   the phase-15.1 `[0.2, 0.8]` rule.
3. Cursor feedback: hover / dragging causes the host to swap the
   OS cursor to a horizontal-resize (EW) cursor so the interaction
   reads instantly.

## Impact

- Affected specs: `specs/splitter-widget/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_splitter_widget.{h,cpp}` (new).
- Breaking change: NO — additive.
- User benefit: the gutter between the viewport and the editor
  pane can be dragged to any `[20 %, 80 %]` split.
