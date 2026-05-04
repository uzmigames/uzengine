# Proposal: phase11_11.3.0-niagara-module-picker-dialog

## Why
11.2's "+ Add Module" row fires a request but there's no UI to satisfy it. Niagara's module picker (the dropdown that appears when you click "+") is a category-tabbed, searchable list of every available module. UzEngine already has `ModuleRegistry` (48 built-ins) and `VfxSnippetLibraryWidget` (tile grid + filter) — the picker is the visual glue between them.

## What Changes
New `VfxModulePickerDialog` in `runtime/engine/vfx/editor/`. Modal popup anchored to the "+ Add Module" click position. Tabs at top: `Physics / Forces / Lifetime / Render / Custom`. Tile body mirrors `VfxAssetBrowserWidget` layout (thumbnail + name + tag chips) but datasource is `ModuleRegistry::GetRegisteredNames()` filtered to the target stage (e.g. render modules only show under `Render` stack). Double-click (or Enter key) inserts a fresh `VfxModuleInstance` at the end of the target `VfxStackEntry`, routing through 11.9's UndoCommand. Escape / click-outside cancels.

## Impact
- Affected specs: docs/engine/25-vfx/44-niagara-stack-model.md (picker section)
- Affected code: runtime/engine/vfx/editor/vfx_module_picker_dialog.{h,cpp} (new); `ModuleRegistry` gains `GetCategory(name) → string` + `IsValidInStage(name, VfxStage) → bool`
- Breaking change: NO
- User benefit: authors discover + add modules without leaving the viewport
- Estimated effort: S

## Source
UE5 `SNiagaraModuleAsset` + phase 10.3 `VfxAssetBrowserWidget` (tile grid baseline).
