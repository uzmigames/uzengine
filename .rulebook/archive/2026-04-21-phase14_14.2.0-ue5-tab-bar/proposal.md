# Proposal: phase14_14.2.0-ue5-tab-bar

## Why

UE5 editor identity hinges on the tab strip that frames every
dockable panel. Tabs have rounded-top corners, flat bottoms, a
thin 2-px accent bar on the active tab's top edge, and a subtle
shadow under the active tab that separates it from the panel
content. The phase-13 emitter tab pill does not read as UE5 —
pills are flat capsules; UE5 tabs are folded-paper shapes.

Replacing the phase-13 emitter tab pill with a UE5 tab widget
gives the editor its single most recognizable visual signature
and establishes the pattern for every future dockable panel.

## What Changes

1. New `VfxTabStripWidget` that renders a row of tabs with UE5
   geometry:
   - Rounded top corners at `radius.sm=3`.
   - Flat bottom so the tab reads as attached to the panel body.
   - Active tab: fill `#2C2C2C` (panel body colour, merges with
     content below) + 2-px `accentPrimary` bar on the top edge.
   - Inactive tab: fill `#1C1C1C` + text `textSecondary`.
   - Hover: fill `#242424` (header colour).
   - 1-px `borderSubtle` on the right edge of each tab.
   - Close `x` sprite on hover (right edge, 10x10).
2. Migrate the emitter tab bar in `vfx_editor_overlay.cpp` from
   the phase-13 pill render to the new tab strip.
3. Add tab reorder via drag + close callback hook.
4. Hit-test matches UE5: mouse-down anywhere in the tab triggers
   selection; close-button hit-test is a separate region.

## Impact

- Affected specs: `specs/ue5-tab-bar/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_tab_strip_widget.{h,cpp}` (new).
  - `runtime/engine/vfx/editor/vfx_editor_overlay.cpp` (migration).
- Breaking change: NO — old pill render path deletes cleanly;
  existing `onSelectEmitter` callback preserved.
- User benefit: the editor's dockable-panel chrome reads as UE5
  at a glance.
