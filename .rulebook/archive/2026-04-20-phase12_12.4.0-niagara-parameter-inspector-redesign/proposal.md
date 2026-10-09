# Proposal: phase12_12.4.0-niagara-parameter-inspector-redesign

## Why

The phase 11.4.0 `VfxParameterInspectorWidget` manages parameter state
(CRUD on `VfxAttributeStore`) but renders every entry as a flat
one-line row with name and scope tag. There is no type-specific
editor, no reset-to-default button, no section grouping, and no
inline binding button to turn a scalar into a curve.

Niagara's parameter inspector is the most content-dense panel in the
editor. Authors rely on:

- **Section headers** ("Spawn", "Initial Value", "Color Over Life",
  "Physics"…) grouping related parameters with a collapse chevron
  and an optional enable checkbox.
- **Two-column layout** — the label on the left (fixed ~40% width)
  and a type-specific editor on the right. Labels right-align for
  visual balance.
- **Type-specific editors** — each parameter type draws itself:
  - `Float` → slider + numeric input
  - `Vec3` → three labeled number fields (X/Y/Z)
  - `Color` → swatch + RGBA fields
  - `Bool` → single checkbox
  - `Enum` → dropdown with current label
  - `Curve` → compact preview + "Edit…" button
  - `String` → text field
- **Reset-to-default button** — a small circular arrow icon to the
  right of every row that resets the parameter to its initial value.
- **Bind to curve / expression** — a chain-link icon that converts a
  scalar to a bound curve reference.
- **Inline validation** — on a red-border row when a value is out of
  the authored range, with a tooltip on hover.

This task delivers the two-column typed editor inspector, leveraging
the data layer (`VfxAttributeStore`, `VFXCurve`, `VFXColorRamp`) that
phase 11 already shipped.

## What Changes

1. **Section grouping** — inspector reads a new optional
   `VfxAttributeSectionName` field on each parameter (falls back to
   "General"). `ComputeLayout` emits one `Section` item per distinct
   section followed by its `ParamRow` items. Section headers are
   collapsible.

2. **Two-column layout** — every `ParamRow` splits into
   `LabelRect` (40%) + `EditorRect` (55%) + `ActionsRect` (5%,
   reset + bind buttons).

3. **Type-specific editors** — eight new layout item kinds:
   `EditorFloat`, `EditorVec3`, `EditorColor`, `EditorBool`,
   `EditorEnum`, `EditorCurve`, `EditorRamp`, `EditorString`. Each has
   a defined sub-rect grammar that `HitTest` can decode.

4. **Reset button** — new layout item `ResetToDefault` emitted per
   `ParamRow`. Clicking fires the new callback
   `onParameterReset(scope, name)`. The widget does not mutate the
   store directly — caller applies.

5. **Bind button** — new layout item `BindToCurve` / `BindToExpression`
   emitted for scalar types. Clicking fires `onParameterBindRequested`.

6. **Inline validation** — `AddParameterRange(scope, name, min, max)`
   registers numeric bounds. When the rendered value is out of range,
   the `Render` pass draws a 1-px `stateError` border around the
   editor rect; hover reveals the error message.

## Impact

- Affected specs: `specs/vfx-parameter-inspector-redesign/spec.md`
  (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_parameter_inspector_widget.{h,cpp}`
    — layout + render rewrite.
  - `runtime/engine/vfx/parameters/vfx_attribute_store.{h,cpp}` —
    optional `sectionName` annotation per parameter.
  - `runtime/tests/engine/vfx/test_vfx_parameter_inspector_redesign.cpp`
    — new.
  - `docs/engine/25-vfx/56-parameter-inspector-visuals.md` — new.
- Breaking change: MINOR — enum grows with new editor kinds;
  `AddParameter` gains optional overload with section argument.
  Existing callers compile unchanged (default section = "General").
- User benefit: parameters become editable with type-appropriate
  widgets, grouped into scannable sections, reset + bind one-click.
