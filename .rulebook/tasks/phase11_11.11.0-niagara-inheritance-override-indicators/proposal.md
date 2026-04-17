# Proposal: phase11_11.11.0-niagara-inheritance-override-indicators

## Why
Phase 8.3 shipped the `VfxEmitterInheritance` resolver (parent + child `.uzvfx` merge with add/replace/remove actions) and phase 9.2 shipped `VfxInheritanceInspectorWidget`. In Niagara, every module card in the stack shows a visual indicator (italic = inherited, colored dot = overridden, strikethrough = removed) so authors instantly know what's coming from where. UzEngine's stack panel (11.2) has no such indicators — overrides are invisible unless the user opens the inheritance inspector separately.

## What Changes
`VfxStackPanelWidget` reads `VfxModuleInstance::overrideFlag` (set during resolver merge) and renders:
- `Inherited` → grey text + small "↳" icon left of card.
- `OverrideAdd` → accent-color dot left of card, bold text.
- `OverrideReplace` → orange dot, "⇄" icon, tooltip "Replaces parent's <ModuleType>".
- `OverrideRemove` → strikethrough text + greyed, "✕" icon, tooltip "Removed from parent".
Right-click on an override row adds a "Revert to Parent" menu item that deletes the child override and re-resolves. Parameter rows get the same treatment in 11.4 widget.

## Impact
- Affected specs: docs/engine/25-vfx/20-emitter-inheritance.md (augmented)
- Affected code: VfxStackPanelWidget (11.2) render path; VfxParameterInspectorWidget (11.4) row render; `VfxEmitterInheritance` resolver writes `overrideFlag` on each returned `VfxModuleInstance`
- Breaking change: NO (flag default = None; no-parent emitters look identical to today)
- User benefit: inheritance state legible at a glance; "Revert to Parent" one-click
- Estimated effort: S

## Source
UE5 `FNiagaraEditorUtilities::GetInheritanceStateIcon` + phase 9.2 inspector.
