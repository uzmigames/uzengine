# phase11_11.3.0-niagara-module-picker-dialog

Searchable, category-tabbed picker that inserts modules into a target stack stage.

Reference: UE5 `SNiagaraModuleAsset` + phase 10.3 tile grid.

## 1. Dialog widget

- [ ] 1.1 `VfxModulePickerDialog` in `runtime/engine/vfx/editor/vfx_module_picker_dialog.{h,cpp}`.
      Open at `(anchorX, anchorY)` with explicit `targetStage: VfxStage`.
      `IsOpen()` / `Open(stage, anchor)` / `Close()` lifecycle.
- [ ] 1.2 Category tab bar at top: `All / Physics / Forces / Lifetime /
      Render / Custom`. Click switches active tab, filters the list below.
- [ ] 1.3 Filter text-input row using phase 10.1 `StringInput` (case-
      insensitive substring match against module name + category).
- [ ] 1.4 Tile grid reusing phase 10.3 `VfxAssetBrowserWidget` layout.
      Each tile: module name, category badge, 1-line description.

## 2. Registry metadata

- [ ] 2.1 Extend `ModuleRegistry` (or add companion metadata table) with:
      - `GetCategory(moduleName) → std::string`
      - `GetDescription(moduleName) → std::string`
      - `IsValidInStage(moduleName, VfxStage) → bool`
      Metadata seeded in `register_builtin_modules.cpp` alongside factory registration.
- [ ] 2.2 Categorize the 48 built-ins: forces (curl/vortex/wind/drag/...),
      lifetime (age/decay/...), render (sprite/mesh/light/...), DIs, etc.

## 3. Insertion flow

- [ ] 3.1 Double-click or Enter on a tile → dispatches
      `onModuleChosen(moduleName)` callback with the dialog's
      `targetStage`. Host (VfxStackPanelWidget) wraps this in an
      UndoCommand (11.9) that appends a new `VfxModuleInstance` to the
      corresponding `VfxStackEntry`.
- [ ] 3.2 Escape / click-outside closes without inserting.
- [ ] 3.3 Arrow keys navigate between tiles; Home/End jump to first/last.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
      Section in `docs/engine/25-vfx/44-niagara-stack-model.md` covering
      the picker workflow + category conventions.
- [ ] 4.2 Write tests covering the new behavior
      Registry category lookup + stage-filter correctness;
      dialog state machine (open/filter/cancel/confirm).
- [ ] 4.3 Run tests and confirm they pass
      `zig build` clean; QA: pick "Curl Noise" from picker, confirm it
      lands in ParticleUpdate stage as a new VfxModuleInstance.
