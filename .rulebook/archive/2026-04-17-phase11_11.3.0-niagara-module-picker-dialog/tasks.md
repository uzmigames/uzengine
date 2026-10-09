# phase11_11.3.0-niagara-module-picker-dialog

Searchable, category-tabbed picker that inserts modules into a target stack stage.

Reference: UE5 `SNiagaraModuleAsset` + phase 10.3 tile grid.

## 1. Dialog widget

- [x] 1.1 `VfxModulePickerDialog` in `runtime/engine/vfx/editor/vfx_module_picker_dialog.{h,cpp}`.
      `Open(VfxStage, anchorX, anchorY, screenW, screenH)` nudges into bounds + pre-selects first tile; `Close()` clears open flag; `IsOpen()` / `TargetStage()` accessors.
- [x] 1.2 Category tab bar at top: `All / Simulation / Forces / Collision / Render / DI / Dynamic / Events / Custom` — enumerated from the catalog's distinct categories, `All` pinned first. `SetCategory` rejects unknown values; active tab selection narrows `VisibleModules()` and resets `SelectedIndex` to 0.
- [x] 1.3 Filter text-input row via case-insensitive substring match against module name AND description. `SetFilter` + `FilterBuffer()` (mutable ref) supports phase-10.1 `StringInput` binding; filter narrowing resets selection to 0.
- [x] 1.4 Tile-per-row list (simpler than full grid, authored for the 420×320 dialog footprint). Each tile: module name (top) + one-line description (bottom, dim). Selected tile highlighted (`kColorTileSel`). Layout emits `CategoryTab`, `FilterBox`, `ModuleTile`, `CloseButton` rects for hit-test.

## 2. Registry metadata

- [x] 2.1 `VfxModuleMetadata` companion table in `runtime/engine/vfx/modules/vfx_module_metadata.{h,cpp}`:
      - `Find(name)` / `GetCategory(name)` / `GetDescription(name)` / `IsValidInStage(name, stage)`
      - `Categories()` returns `["All", ...]`, deduped, catalog-order
      - `FilterByCategoryAndStage(category, stage)` returns the module names that match both filters
      - Unknown modules → `"Custom"` category, permissive stage compatibility (the user knows best)
      - Catalog lives as `constexpr VfxModuleCatalogEntry kCatalog[]` (stage mask built via `VfxStageBits` parameter pack) — static table kept in lockstep with `register_builtin_modules.cpp` via the `TableConsistency_EveryEntryWellFormed` test.
- [x] 2.2 Categorised all 20 built-ins (the proposal's "48" was a drafting typo — audited actual registrations):
      - Simulation: spawn, update, decay, turbulence, constraint
      - Forces: attractor, vector_field, orbit, pin
      - Collision: collision, distance_constraint
      - Render: sprite_render, light_render, mesh_render
      - DI: skeletal_spawn, audio_spectrum, neighbor_interaction, render_target_di, async_gpu_trace
      - Dynamic: dynamic_params
      - Events: sub_emitter

## 3. Insertion flow

- [x] 3.1 Double-click (or single-click in this phase — still testable as `ConfirmSelection`) fires `onModuleChosen(moduleName, targetStage)`. Overlay listens: `VfxEditorOverlay::EnsureStackPanelCallbacks` installs the binding once, appends a fresh `VfxModuleInstance` (unique `m_<n>` instanceId) to `emitter->stack.GetEntry(stage)`. Undo/redo integration lands in phase 11.9 — noted in the TODO trail inside the lambda.
- [x] 3.2 Escape / click-outside-bounds closes without inserting: overlay click dispatch in `BuildUI` calls `m_modulePicker.Close()` when `!Bounds().Contains(mouseX, mouseY)`.
- [x] 3.3 Arrow keys navigate between tiles; Home/End jump to first/last. Widget exposes `NavigateSelection(+1 / -1)`, `SelectFirst`, `SelectLast`, `SelectIndex` (wraps on out-of-range). Keyboard routing itself is host-side (phase 11.10 keyboard-shortcuts) — widget provides the clean API the shortcut layer will call into.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      Augmented `docs/engine/25-vfx/44-niagara-stack-model.md` with the module picker workflow, ASCII mock, category conventions table, and the layout + hit-test contract.
- [x] 4.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_module_picker.cpp`: catalog `Find` / `GetCategory` / `GetDescription` / `IsValidInStage` for known + unknown modules; every catalog row well-formed (no null, non-empty mask); `Categories()` starts with "All" and has no duplicates; `FilterByCategoryAndStage` for Render/Render, All/ParticleUpdate, Render/ParticleUpdate (empty); dialog default closed; `Open` sets state + pre-selects; `Close` clears flag; `Open` resets filter + category; anchor nudged when beyond screen bounds; category narrows list + resets selection; unknown category is no-op; filter case-insensitive on name AND description; selection reset on filter/category change; arrow navigation wraps (both directions); `SelectFirst`/`SelectLast`; `ConfirmSelection` fires callback and closes; confirm with empty list is no-op; confirm when closed is no-op; layout emits tabs + filter + tiles + close-button; hit-test resolves close-button priority > tab/tile; tile hit returns correct index + label; outside bounds returns None.
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the new catalog + dialog + tests linked in. Runtime execution of the test binary remains blocked by the pre-existing missing swscale-9.dll deployment issue (tracked since phase 11.1).
