# phase11_11.4.0-niagara-parameter-inspector-widget

Two-pane editor UI for `VfxAttributeStore` — add/rename/type/delete user parameters.

Reference: UE5 `SNiagaraParameterPanel`.

## 1. Left pane (parameter list)

- [ ] 1.1 `VfxParameterInspectorWidget` in
      `runtime/engine/vfx/editor/vfx_parameter_inspector_widget.{h,cpp}`.
- [ ] 1.2 Group parameters by scope (System / Emitter headers, collapsible).
      Each row: color-coded type badge + name + usage-count pill.
- [ ] 1.3 Single-click selects; selected row highlighted accent colour.
      "+ Add Parameter" button at the top of each scope section.

## 2. Right pane (inspector form)

- [ ] 2.1 Name field via `StringInput(InputFilter::Identifier)` — live
      rename with rebind-on-commit (updates `VfxAttributeStore` key
      + migrates all `@ParamName` references in module configJson).
- [ ] 2.2 Scope dropdown (System / Emitter) — move between stores.
- [ ] 2.3 Type dropdown (Float / Vec3 / Vec4) — converting between types
      preserves component 0; zero-extends / truncates for the rest.
- [ ] 2.4 Default value editor: `NumberInput` per component with per-
      type min/max hints (Float: free, Vec3/Vec4: -∞/+∞ free).
- [ ] 2.5 Usage count badge (read-only) + "Jump to usages" button
      (selects the first module referencing the parameter in 11.2
      stack panel).
- [ ] 2.6 Delete button — disabled when `usageCount > 0`; tooltip
      explains "detach from modules first".

## 3. Usage tracking

- [ ] 3.1 `VfxAttributeStore::CountReferences(scope, name, modules)` scans
      every `VfxModuleInstance::configJson` for `"@{name}"` patterns.
- [ ] 3.2 Rename (2.1) rewrites every matched reference to the new name.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/45-parameters-editor.md`.
- [ ] 4.2 Write tests covering the new behavior
      Add/rename/delete round-trip, usage-count math, reference rewrite.
- [ ] 4.3 Run tests and confirm they pass
      `zig build` clean; QA: add `teamColor`, wire it into a module, verify count.
