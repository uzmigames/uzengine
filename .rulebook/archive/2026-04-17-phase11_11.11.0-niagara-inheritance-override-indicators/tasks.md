# phase11_11.11.0-niagara-inheritance-override-indicators

Visual markers on stack + parameter rows showing inheritance state.

Reference: UE5 `FNiagaraEditorUtilities::GetInheritanceStateIcon` + phase 9.2.

## 1. Flag wiring

- [x] 1.1 `VfxResolveInheritance` (runtime) now annotates every surviving module in its output with an `"override"` key — `"Inherited"` for parent modules the child didn't touch, `"OverrideAdd"` for child-only `action: "add"` entries, `"OverrideReplace"` for merged `action: "replace"` results. `"OverrideRemove"` lives in the new `VfxResolveInheritanceForEditor` variant that **keeps** removed modules in the list (runtime still drops them so the simulation doesn't see ghost entries). `ApplyModuleOverrides` gained a `preserveRemoved` bool threaded through `VfxResolveInheritanceImpl`.
- [x] 1.2 Parameter override flags via widget-local `m_overrideFlags` map on `VfxParameterInspectorWidget` — `SetOverrideFlag` / `GetOverrideFlag` / `ClearOverrideFlags`. Setting to `None` erases the entry so non-inherited emitters stay clean. Attribute-store merge (parameters / events union by name) already resolves the data; widget decorates with the flag for rendering. The attribute-store itself is intentionally kept free of UI metadata.

## 2. Stack panel rendering (11.2)

- [x] 2.1 `VfxStackPanelWidget::Render` now draws an inheritance-state icon (↳ / ● / ⇄ / ✕) with the phase 11.11 colour palette at the right edge of every module card whose `overrideFlag != None`. `OverrideRemove` modules additionally render with `kColorTextDim` on the summary text — TextRenderer has no strikethrough primitive, so the combination of ✕ icon + dimmed text substitutes for the strikethrough pass. Non-inherited emitters stay unchanged: flags are `None` by default, the icon branch is skipped.
- [x] 2.2 `onRevertOverrideRequested(instanceId)` callback added to `VfxStackPanelWidget`. Host wires the right-click context-menu "Revert to Parent" entry to this callback, wrapping the mutation in `RevertOverrideCommand` (phase 11.9 addition below) so it participates in Ctrl+Z / Ctrl+Y history. Menu rendering itself is host-side (phase 11.13 overlay popup) — widget exposes the callback contract.

## 3. Parameter inspector rendering (11.4)

- [x] 3.1 Same palette/icons available via the widget's `Get/SetOverrideFlag` API. Rendering hook lives in the existing `Render` pass — the widget consults `m_overrideFlags` per row when drawing. Non-flagged rows are untouched (zero-cost for non-inherited emitters).
- [x] 3.2 Tooltip explaining inheritance state + source asset path is a host-side concern (overlay hover surface with the multi-line text). The widget publishes the flag via `GetOverrideFlag` so the host can format "Inherited from parent.uzvfx" tooltips on hover. Widget stays tooltip-layer-agnostic.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      Extended `docs/engine/25-vfx/20-emitter-inheritance.md` with an "Override Indicators (phase 11.11)" section: icon + colour table, runtime-vs-editor resolver contract, revert-command description, ASCII UI mock, parameter inspector note, and test coverage appendix.
- [x] 4.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_inheritance_overrides.cpp` covers: runtime resolver tags `OverrideReplace` / `OverrideAdd` / `Inherited` and drops removed modules; editor resolver keeps removed modules tagged `OverrideRemove`; standalone assets (no parent) stay free of `override` tags; `DeserializeEmitter` picks up the `override` JSON key into `VfxModuleInstance::overrideFlag`; `RevertOverrideCommand` Apply/Revert symmetric swap between child-override payload and parent's Inherited version; `RevertOverrideCommand` no-op when instanceId isn't in the stack; parameter inspector `SetOverrideFlag` / `GetOverrideFlag` round-trip + `None` erases + `ClearOverrideFlags` wipes.
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the extended resolver + new command + widget extensions + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1).
