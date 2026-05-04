# Proposal: phase12_12.3.0-niagara-module-picker-redesign

## Why

The phase 11.3.0 `VfxModulePickerDialog` implements the state machine
(open/close, category switch, filter, tile selection, confirm) but
renders as a flat list of filter tabs + text tiles. Niagara's real
picker is a dense modal dialog with:

- A **search bar** pinned to the top with placeholder text and a
  magnifying-glass icon.
- **Category tabs** horizontally scrollable across: Location, Velocity,
  Color, Force, Lifetime, Rotation, Size, Custom.
- A **tile grid** (3 columns on narrow windows, 4 on wide) where each
  tile shows the module icon, name (bold), and a 1-2 line description.
- A selected tile gets an **accent border + brighter background**; it
  also reveals a larger preview area at the bottom with the full
  description + module metadata (authored-by, cost class).
- A **footer** with Create (primary) and Cancel buttons, plus a "?"
  help link.

This task rewrites the rendering + layout math to match. The state
machine and callbacks (`onModuleChosen`, `Close`, `Open`) stay identical
so phase 11.14's wiring still works.

## What Changes

1. **Search bar** — new layout item `VfxPickerItemType::SearchInput`
   with a magnifying glass icon on the left, placeholder "Search
   modules...", and the existing `m_filterText` as buffer. Supports
   focus + keystroke input via the overlay's `StringInput` helper.

2. **Category tabs redesign** — tabs use the category name + count
   badge (e.g. "Velocity · 4"); active tab has a 2-px accent underline.
   Tabs are horizontally scrollable when they overflow the dialog
   width.

3. **Tile grid** — `ComputeLayout` produces 3 or 4 column tiles
   depending on dialog width. Each tile is 140 px wide, 96 px tall,
   with:
   - 32-px icon in the top-left
   - module name (bold, 14 px) in the top-right of the header row
   - 2-line description (12 px, `textDim`) in the body
   - hover → `cardBgHover`, selected → `cardBgSelected` + 2-px
     `accentPrimary` border.

4. **Selected preview area** — when a tile is selected, the bottom
   120 px of the dialog above the footer shows the full description,
   category, and a "This module will be added to the `<StageName>`
   stage" hint.

5. **Footer buttons** — Create button (primary, `accentPrimary`
   background) is disabled until a valid selection exists. Cancel
   button dismisses without firing `onModuleChosen`.

6. **Keyboard navigation** — arrow keys move the highlighted tile,
   Enter activates Create. This is spec-only here; wiring lands in
   12.8 (context menu / keyboard system).

## Impact

- Affected specs: `specs/vfx-module-picker-redesign/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_module_picker_dialog.h` — new
    `VfxPickerItemType::SearchInput` + `PreviewArea` values; new getters
    for dialog dimensions.
  - `runtime/engine/vfx/editor/vfx_module_picker_dialog.cpp` —
    `ComputeLayout`, `HitTest`, `Render` rewritten.
  - `runtime/engine/vfx/modules/vfx_module_metadata.{h,cpp}` — add
    `description` + `iconGlyph` fields (currently missing).
  - `runtime/tests/engine/vfx/test_vfx_module_picker_redesign.cpp` — new.
  - `docs/engine/25-vfx/55-module-picker-visuals.md` — new.
- Breaking change: MINOR — enum grows; `VfxModuleMetadata` gains
  fields. Existing callers tolerate the change.
- User benefit: adding a module becomes a scannable visual search
  instead of reading a flat list.
