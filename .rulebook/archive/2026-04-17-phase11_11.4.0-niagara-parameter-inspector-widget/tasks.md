# phase11_11.4.0-niagara-parameter-inspector-widget

Two-pane editor UI for `VfxAttributeStore` — add/rename/type/delete user parameters.

Reference: UE5 `SNiagaraParameterPanel`.

## 1. Left pane (parameter list)

- [x] 1.1 `VfxParameterInspectorWidget` in `runtime/engine/vfx/editor/vfx_parameter_inspector_widget.{h,cpp}`. Binds to a `VfxAttributeStore*` + `VfxStack*`; editor-local `m_types` map keeps the authored `VfxParamType::{Float, Vec3, Vec4}` tag alongside the runtime's universal `Vec4` slot.
- [x] 1.2 Scope groups (System / Emitter) with collapsible headers (`IsScopeExpanded`, `ToggleScope`). Parameter row renders type name + parameter name + usage-count pill (`[N]` via `CountReferences`). `Particles` scope hidden from the inspector (it carries SoA attributes, not author knobs).
- [x] 1.3 Single-click routing via `SelectParameter` + `onParameterSelected` callback (fires only on change). Selected row highlighted (`kColorRowSel`). "+ Add Parameter" button emitted per scope via `VfxParamItemType::AddButton`.

## 2. Right pane (inspector form)

- [x] 2.1 `NameField` layout item emitted when a parameter is selected — host can bind its `StringInput` to a rename commit that calls `RenameParameter(scope, oldName, newName)`. Rename rewrites every `"@oldName"` occurrence in every module's `configJson` across every stage of the bound stack, preserving JSON structure.
- [x] 2.2 `ScopeDropdown` layout item + `ChangeScope(from, name, to)` mutation. Rejects on destination collision; preserves the authored type during the move.
- [x] 2.3 `TypeDropdown` layout item + `ChangeType(scope, name, newType)` mutation. Conversion preserves component 0 and zero-fills / truncates trailing components per the table in `docs/engine/25-vfx/45-parameters-editor.md`.
- [x] 2.4 `ValueField` layout items — one per component of the authored type (1 for Float, 3 for Vec3, 4 for Vec4). `SetDefaultValue(scope, name, Vec4)` writes the store according to the authored type.
- [x] 2.5 Usage count pill rendered on every row; "Jump to usages" layout item (`JumpToUsages`). Widget exposes `FindFirstUsage(name) → UsageHit{stage, instanceId}` in stage execution order so host can feed the stack panel's selection. `onJumpToUsage(stage, instanceId)` callback wired on click.
- [x] 2.6 `DeleteButton` layout item. `RemoveParameter` refuses when `CountReferences > 0` — layout renders the button dimmed (`kColorDisabled`) with "Delete (in use)" label in that case.

## 3. Usage tracking

- [x] 3.1 `CountReferences(name)` scans every `VfxModuleInstance::configJson` in the bound stack for the `"@name"` token (conservative substring scan — matches Niagara's counting). Returns a cumulative total across every stage.
- [x] 3.2 `RenameParameter` rewrites every `@oldName` token to `@newName` in-place across every module's `configJson` so downstream references stay attached. Gravity-style configs without a reference are left byte-identical.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/45-parameters-editor.md` — data-model diagram, UI mock, mutation API, type-conversion table, layout + hit-test contract, test coverage list, and a note that visible overlay wiring follows in phase 11.13 once the overlay gains access to the owning `VfxSystemInstance`.
- [x] 4.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_parameter_inspector.cpp` covers: unbound list is empty; `ListParameters` sorted + typed; `AddParameter` rejects empty name + duplicates; `AddParameter` writes through the right store type; `MakeUniqueName` collision → numeric suffix; `RemoveParameter` success when free / refuses when in use / refuses on unknown; `RenameParameter` rewrites all `@oldName` references across stages, refuses when target exists, same-name is no-op; `ChangeType` Float→Vec3 preserves .x / Vec4→Float truncates; `ChangeScope` carries value + type / rejects collision; `CountReferences` sums across stack / empty name = 0; `FindFirstUsage` returns the first module in stage execution order / miss returns invalid; layout emits 2 scope headers + 2 add buttons when both expanded; collapsed scope hides add + rows; inspector form emits NameField + ScopeDropdown + TypeDropdown + ValueField×N + Jump + Delete when selected; hit-test resolves `ParameterRow` clicks to correct `(scope, name)`; `SelectParameter` callback fires only on change.
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the new widget + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1).
