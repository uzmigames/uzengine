## 1. Implementation

- [x] 1.1 Extend `VfxStackItemType` enum: add `EmitterHeader`, `CardDragHandle`, `CardOverflowMenu`, `StageAddButton` values (numeric priority ordering preserved: existing 0-4 kept, new values appended 5-8 so HitTest priority works out-of-the-box)
- [x] 1.2 Add `onModuleContextMenuRequested(const std::string&, float, float)` callback to `VfxStackPanelWidget`
- [x] 1.3 Rewrite `ComputeLayout` to emit the new item sequence (optional emitter header -> stage header with inline add -> module cards with sub-rects) — AddRow no longer emitted
- [x] 1.4 Rewrite `HitTest` to dispatch to the new sub-rects (drag grip, checkbox, card body, overflow menu, stage add button) — priority ordering handled automatically by the existing enum-value-based promotion
- [x] 1.5 Rewrite `Render` to draw emitter header, stage headers (chevron + cat icon + name + count badge + inline add), module cards (stripe + grip + checkbox + module icon + name + override badge + overflow menu) using `VfxTheme()`
- [x] 1.6 Add `VfxModuleIconFor(const std::string& moduleTypeName)` helper that resolves a category icon glyph from `VfxModuleMetadata` (7 category mappings + generic fallback)
- [x] 1.7 Implement 3-px accent left stripe on selected cards
- [x] 1.8 Implement 1-px borderSubtle divider between stages
- [x] 1.9 Wire overflow menu click in `vfx_editor_overlay.cpp` (both legacy `BuildStackPanel` and Niagara-mode `BuildNiagaraUI`) to fire `onModuleContextMenuRequested` with screen coords; also wired CardDragHandle -> BeginDrag and StageAddButton -> onAddModuleRequested
- [x] 1.10 Remove the standalone "+ Add Module" row from the layout (moved inline onto the stage header). AddRow enum value retained for ABI stability; ComputeLayout never emits it and existing tests were updated (`HitTest_AddRow` renamed to `HitTest_StageAddButton`)
- [x] 1.11 Add emitter-header wiring: `SetEmitterName` + `SetEmitterEnabledPtr` setters on the widget; both overlay paths populate them from the active emitter's name and `isEmitting` flag

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/54-stack-panel-visuals.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_stack_panel_redesign.cpp — 13 new tests covering emitter header not-emitted-when-empty / precedes-stages / grows-content-height, no-AddRow-items, one-StageAddButton-per-stage, StageAddButton-right-aligned-inside-header, all-four-card-sub-rects-emitted, hit-priority tests for overflow-menu/drag-handle/stage-add-button, context-menu callback roundtrip, VfxModuleIconFor category mappings + fallback, emitter-enabled pointer storage; plus existing test_vfx_stack_panel_widget.cpp updated)
- [x] 2.3 Run tests and confirm they pass (`zig build` compiles runtime + test_runtime cleanly; full test-binary execution continues blocked by pre-existing missing `swscale-9.dll` — same gap documented in phases 11.1-11.14 + 12.1)
