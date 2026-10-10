# Proposal: phase14_14.3.0-ue5-details-panel-rows

## Why

UE5's Details panel is the reference for property editing. Its
visual signature is a tight two-column row: label on the left
(40 % width), value editor on the right (60 % width), separated
by a 1-px vertical hairline. Rows alternate zebra striping
(`panelBg` vs `panelBgAlt`) for scanability. Category headers are
bold rows with a right-pointing chevron that collapses the
category on click. The phase-13 parameter inspector renders rows
flat without zebra or split-column layout — it does not read as
UE5 Details.

## What Changes

1. Rework `VfxParameterInspectorWidget::Render` to emit a strict
   two-column layout:
   - Label column: left-aligned text at x = row.x + indent, row
     width `labelColW = row.w * 0.40`.
   - Value column: editor at x = row.x + labelColW + 1, width
     `row.w - labelColW - 1`.
   - 1-px vertical hairline at column boundary (`borderSubtle`).
2. Add zebra striping — every odd row index uses `panelBgAlt`;
   even rows use `panelBg`.
3. Category headers (`VfxParamItemType::Section`) render as a bold
   row with:
   - Left-edge chevron sprite (`ChevronDown` / `ChevronRight`).
   - Uppercase category name via MSDF bold.
   - 22 px row height (vs 20 px standard rows).
   - Hover fill `cardBgHover` to cue clickability.
4. Property value indentation: nested properties indent by 12 px
   per level (matches UE5 tree indent).
5. Row separator: 1-px `borderSubtle` rect at the bottom of each
   row.

## Impact

- Affected specs: `specs/ue5-details-panel-rows/spec.md` (new).
- Affected code: `runtime/engine/vfx/editor/vfx_parameter_inspector_widget.cpp`.
- Breaking change: NO — item-type enum unchanged; only render path
  rewrites.
- User benefit: property editing feels UE5-native, high-density
  label/value split, clear category collapse affordance.
