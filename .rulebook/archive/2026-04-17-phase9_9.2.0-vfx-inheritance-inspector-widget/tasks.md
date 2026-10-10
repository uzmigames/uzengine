# phase9_9.2.0-vfx-inheritance-inspector-widget

Editor UI for the phase 8.3.0 inheritance resolver — parent picker + override indicators.

Reference: UE5 Blueprint inheritance indicators.

## 1. Parent picker

- [x] 1.1 "Parent" text field via `ParentPathBuffer()` mutable reference.
      `VfxInheritanceInspectorWidget` in
      `engine/vfx/editor/vfx_inheritance_inspector_widget.h/.cpp` —
      host wires this buffer to phase 10.1 `StringInput` with
      `InputFilter::Any`.
- [x] 1.2 Asset-browser popup filtered to `.uzvfx` files.
      Via phase 10.3 `VfxAssetBrowserWidget` — host shows the widget in
      a popup with a data source filtered to the `.uzvfx` extension.
      Selection callback writes back into `ParentPathBuffer()`.
- [x] 1.3 Live-reload on change: call `Resolve()` which invokes the
      parent loader and rebuilds the row list. Fires `onResolved`.
- [x] 1.4 Cycle / depth-cap errors surface inline via `LastError()`.
      Host renders the string in red below the parent field when
      non-empty.

## 2. Override indicators

- [x] 2.1 Each module row tagged `VfxInheritanceRowKind` enum:
      `Inherited`, `OverrideAdd`, `OverrideReplace`, `OverrideRemove`.
      Stable string via `VfxInheritanceRowKindName()`.
- [x] 2.2 Colour coding — the widget exposes kind; host applies:
      grey for Inherited, accent for Override{Add,Replace},
      strikethrough for OverrideRemove.
- [x] 2.3 `RevertOverride(rowIndex)` removes the override entry from
      the child's `modules[]` array and re-resolves. No-op on invalid
      index or on Inherited rows (`childModulesIndex == UINT32_MAX`).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on all API + ASCII layout mock in header.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_inheritance_inspector_widget.cpp`:
      12 tests covering row kind names, unbound state, invalid JSON,
      full resolve with 3 override types + 1 add, no-parent case,
      parent loader failure, clear parent, revert override,
      invalid/inherited revert no-ops, onResolved callback.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
