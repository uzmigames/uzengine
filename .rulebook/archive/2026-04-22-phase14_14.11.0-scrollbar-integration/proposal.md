# Proposal: phase14_14.11.0-scrollbar-integration

## Why

Phase 14.9 shipped `VfxScrollBar` — a stateless 6-px idle / 8-px
hover scroll bar with full-coverage unit tests. It is not yet
mounted anywhere. Scroll regions in the editor (stack panel,
parameter inspector, module picker, system overview) clip content
without a visible "there is more below" affordance.

## What Changes

1. Host-side integration of `VfxScrollBar` into every scroll
   region in the overlay's `BuildNiagaraUI`:
   - Stack panel (already has a `scrollOffset` — wire the
     scrollbar to read/write it).
   - Parameter inspector.
   - Module picker dialog (when open).
   - System overview.
2. The overlay owns a single `VfxScrollBar` value type per region
   (stateless — the widget just produces geometry per frame from
   `panelRect` + `contentH` + `scrollY`).
3. Render the scroll bar after the panel content so the thumb
   lands above the content.

## Impact

- Affected specs: none.
- Affected code: `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}`.
- Breaking change: NO — existing scroll state stays live.
- User benefit: every scroll region surfaces its overflow.
