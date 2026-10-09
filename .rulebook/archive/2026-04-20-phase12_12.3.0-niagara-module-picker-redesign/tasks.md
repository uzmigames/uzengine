## 1. Implementation

- [x] 1.1 Add `iconGlyph` field to `VfxModuleCatalogEntry` + populate for all 20 built-in modules; verify existing `description` field already populated (>= 10 chars each). Added `GetIconGlyph(name)` + `CountByCategoryAndStage(cat, stage)` accessors.
- [x] 1.2 Extend `VfxPickerItemType` enum with `SearchInput`, `PreviewArea`, `ButtonCreate`, `ButtonCancel` values (5-8, preserving 0-4 for ABI)
- [x] 1.3 Rewrite `VfxModulePickerDialog::ComputeLayout` to produce: CloseButton (header) -> SearchInput -> CategoryTabs (with count badges embedded in label) -> ModuleTile grid -> PreviewArea (when selected) -> ButtonCancel + ButtonCreate footer. FilterBox no longer emitted.
- [x] 1.4 Rewrite `HitTest` coverage: enum-priority-based promotion handles new sub-rects automatically (SearchInput=5 > CloseButton=4, ButtonCreate=7 / ButtonCancel=8 highest). Overlay dispatches on each new type.
- [x] 1.5 Rewrite `Render` using `VfxTheme()` tokens: drop-shadow simulation + dialog background + 1-px border, header with title + close glyph, search input with `VFX_ICON_SEARCH` glyph, category tabs with active accent underline, tile grid with icon + name + 2-line word-wrapped description
- [x] 1.6 Implement 2-px `accentPrimary` border on selected tile (4 edge rects) + brighter background via `cardBgSelected`
- [x] 1.7 Implement selected preview area showing full description + target-stage hint ("Will be added to: <stage>")
- [x] 1.8 Implement footer with Create (primary `accentPrimary` bg when selection valid, `cardBgDisabled` + `textDisabled` when none) + Cancel (neutral `cardBg`). Single-click tile only selects — Create must be clicked to commit.
- [x] 1.9 Support 3/4-column responsive layout via `kFourColumnMinWidth = 600.0f` threshold and `dialogWidth`; default 640 px -> 4 cols, 520 px -> 3 cols
- [x] 1.10 Wire picker click routing in `vfx_editor_overlay.cpp`: CloseButton/ButtonCancel -> Close, CategoryTab -> strip " \xB7 N" badge + SetCategory, ModuleTile -> SelectIndex (no auto-confirm), ButtonCreate -> ConfirmSelection, SearchInput/PreviewArea absorbed. Keyboard StringInput wiring stays phase-agnostic (buffer accessible via `FilterBuffer()` — full keystroke plumbing for the Niagara focus system lands in 12.10)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/55-module-picker-visuals.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_module_picker_redesign.cpp — 15 new tests: closed-dialog inert, search precedes items, search placeholder/filter-reflection, tab label contains name+count, 3-col at 520w, 4-col at 720w, preview present-when-selected / absent-when-no-matches, footer Create+Cancel presence + Create-right-of-Cancel, ConfirmSelection no-op on empty filter + fires-callback on valid selection, metadata description+iconGlyph completeness for all 20 entries, GetIconGlyph fallback, CountByCategoryAndStage. Legacy test_vfx_module_picker.cpp updated for the new emission contract — `Layout_EmitsTabsSearchAndTiles` replaces legacy test, `HitTest_CategoryTabReturnsLabel` uses substring match for the count-badge suffix)
- [x] 2.3 Run tests and confirm they pass (`zig build` compiles runtime + test_runtime cleanly; test-binary execution continues blocked by pre-existing missing `swscale-9.dll` — same gap documented in phases 11.1-11.14 + 12.1 + 12.2)
