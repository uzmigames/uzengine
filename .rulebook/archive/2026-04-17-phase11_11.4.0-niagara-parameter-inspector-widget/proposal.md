# Proposal: phase11_11.4.0-niagara-parameter-inspector-widget

## Why
UzEngine already has `VfxAttributeStore` (3-scope: System / Emitter / Particles) and `VFXParameterStore`. What's missing is the editor UI — the Niagara "Parameters" panel where authors add, name, type, and edit default values for user-defined parameters. Without this UI, authors can't expose effect knobs (team color, spawn rate, radius) without editing JSON by hand.

## What Changes
New `VfxParameterInspectorWidget` in `runtime/engine/vfx/editor/`. Two-pane layout:
- Left: scrollable list of parameters grouped by scope (`System` / `Emitter`), each entry shows name + type badge (`float` / `vec3` / `vec4`).
- Right: inspector form for the selected parameter — name (editable via phase 10.1 `StringInput` with `Identifier` filter), scope dropdown, type dropdown, default value editor (scalar or 3/4 `NumberInput`), usage count (how many modules reference `@ParamName`), Delete button (disabled when usage > 0).
Plus an "+ Add Parameter" button at the top that appends a fresh entry (auto-name `NewParam`, scope defaults to `Emitter`).

## Impact
- Affected specs: docs/engine/25-vfx/ (new: 45-parameters-editor.md)
- Affected code: runtime/engine/vfx/editor/vfx_parameter_inspector_widget.{h,cpp} (new); `VfxAttributeStore` gains usage-count helper (`CountReferences(paramName)` by scanning modules' configJson)
- Breaking change: NO (reads/writes existing `VfxAttributeStore`)
- User benefit: author-exposed effect knobs without JSON editing
- Estimated effort: M

## Source
UE5 `SNiagaraParameterPanel` + phase 10.1 `StringInput`.
