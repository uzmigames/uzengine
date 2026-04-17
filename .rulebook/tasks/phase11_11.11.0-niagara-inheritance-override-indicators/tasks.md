# phase11_11.11.0-niagara-inheritance-override-indicators

Visual markers on stack + parameter rows showing inheritance state.

Reference: UE5 `FNiagaraEditorUtilities::GetInheritanceStateIcon` + phase 9.2.

## 1. Flag wiring

- [ ] 1.1 `VfxEmitterInheritance` resolver writes `overrideFlag` on every
      returned `VfxModuleInstance`:
      - `Inherited`: module came from parent, not overridden in child.
      - `OverrideAdd`: child-only module (action = "add").
      - `OverrideReplace`: child replaces parent's matching type.
      - `OverrideRemove`: child removes parent's module (kept in list
        but grayed/strikethrough).
- [ ] 1.2 Same flag set on user parameters during attribute-store merge.

## 2. Stack panel rendering (11.2)

- [ ] 2.1 Row left-edge icon + color per flag:
      Inherited=grey "↳", OverrideAdd=accent "●", OverrideReplace=orange
      "⇄", OverrideRemove=red "✕" (text strikethrough + dimmed).
- [ ] 2.2 Right-click override row → context menu adds "Revert to Parent"
      entry (disabled for `Inherited` / `OverrideRemove`-with-no-parent).
      Revert issues 11.9 `RevertOverrideCommand`.

## 3. Parameter inspector rendering (11.4)

- [ ] 3.1 Same icon + color scheme as stack rows.
- [ ] 3.2 Tooltip explains inheritance state + source asset path.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
      Extend `docs/engine/25-vfx/20-emitter-inheritance.md` with UI mock.
- [ ] 4.2 Write tests covering the new behavior
      Resolver flag assignment, revert command round-trip.
- [ ] 4.3 Run tests and confirm they pass
      `zig build` clean; QA: load a child emitter with parent, observe
      icon colors match semantics.
