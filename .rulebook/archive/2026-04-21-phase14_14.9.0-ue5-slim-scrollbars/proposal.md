# Proposal: phase14_14.9.0-ue5-slim-scrollbars

## Why

UE5 renders scrollbars as 6-px-wide slim indicators that auto-hide
when not in use and expand on hover. The phase-13 editor clips
content that overflows without surfacing a scrollbar at all —
users cannot tell there's more below the fold in the stack panel,
parameter inspector, module picker, or content browser.

Adding a reusable UE5-style slim scrollbar covers every scroll
region in one widget and gives the user a visible "there is more"
affordance without eating horizontal real estate.

## What Changes

1. New `VfxScrollBarWidget` — stateless data-driven widget:
   - `Render(panelRect, contentHeight, scrollY)` → emits track +
     thumb geometry.
   - Track: 6 px wide, docked at `panelRect.x + panelRect.w - 6`,
     fill `panelBgAlt` at alpha 0.3 (nearly invisible when
     idle).
   - Thumb: height = `panelRect.h * (panelRect.h / contentHeight)`
     clamped to `[20, panelRect.h]`; y = `panelRect.y + scrollY /
     (contentHeight - panelRect.h) * (panelRect.h - thumb.h)`.
   - Idle fill `borderStrong` alpha 0.45; hover fill
     `borderStrong` alpha 0.85; pressed fill `accentPrimary`.
2. Hover expansion: while cursor is within 12 px of the track or
   dragging, widen the thumb to 8 px (with 1-px AA transition —
   handled by fwidth on the rrect shader).
3. Integrate into stack panel, parameter inspector, module
   picker, content browser, overview — every scroll-capable
   widget bolts the scrollbar onto its right edge.
4. Mouse wheel → `scrollY` delta; scrollbar hit-test dispatches
   to drag-thumb / page-up-above-thumb / page-down-below-thumb.

## Impact

- Affected specs: `specs/ue5-slim-scrollbars/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_scrollbar_widget.{h,cpp}`
    (new).
  - Consumer integrations across the widget set.
- Breaking change: NO — consumers opt in; no-op without
  integration.
- User benefit: every scroll region finally surfaces its
  overflow + scroll state.
