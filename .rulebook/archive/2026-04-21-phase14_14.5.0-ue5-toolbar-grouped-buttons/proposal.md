# Proposal: phase14_14.5.0-ue5-toolbar-grouped-buttons

## Why

UE5's Level Editor toolbar is a row of icon-plus-label buttons
grouped by function (Save / Play / Build / Settings) with 1-px
vertical separators between groups. The phase-13 editor has a
flat menu bar but no toolbar. The user needs a way to invoke
frequent actions (save, build, preview play/pause, profile)
without hunting in dropdown menus.

Adding a UE5-style toolbar gives the editor a one-click surface
for the top-5 frequent commands and establishes the grouping
pattern for future toolbars (e.g. curves editor toolbar already
has per-mode buttons that would benefit from group separators).

## What Changes

1. New `VfxToolbarWidget` — horizontal row of buttons with:
   - Icon-plus-label (icon 16x16 sprite, label 10 px MSDF).
   - 28-px row height.
   - Button fill: `barBg` default, `cardBgHover` hover,
     `cardBgSelected` pressed/toggled.
   - Button radius: `radius.xs=2` (UE5 button corner).
   - 4-px inner padding on each button.
2. Group support: `AddGroupSeparator()` emits a 1-px
   `borderSubtle` vertical rect between button runs.
3. Dropdown-button variant: a button with a right-edge
   `ChevronDown` sprite that opens a menu on click.
4. Toggle-button variant: sticks in `cardBgSelected` when active
   until clicked again.
5. Wire the toolbar into the center panel below the menu bar
   for the active emitter's actions (Save System, Restart,
   Play/Pause, Capture Thumbnail).

## Impact

- Affected specs: `specs/ue5-toolbar-grouped-buttons/spec.md`
  (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_toolbar_widget.{h,cpp}` (new).
  - `runtime/engine/vfx/editor/vfx_editor_overlay.cpp` (mount
    the toolbar in the composition).
- Breaking change: NO — additive widget.
- User benefit: one-click access to frequent actions; grouping
  pattern reusable by every future toolbar.
