## 1. Implementation

- [x] 1.1 Added `enum class VfxEditorLayoutMode { Niagara, SplitHorizontal }` in `vfx_editor_composition.h`; default `Niagara` preserves every phase-14 call site
- [x] 1.2 Added `VfxEditorLayoutConfig::splitFraction = 0.5f` + `layoutMode = Niagara` fields
- [x] 1.3 Extended `VfxEditorLayoutRects` with `editorPane` + `splitter` rects populated only in SplitHorizontal mode
- [x] 1.4 `VfxComputeEditorLayout` branches on `cfg.layoutMode` — SplitHorizontal emits viewport (left) + 6-px gutter + editorPane (right) with `splitFraction` clamped to `[0.2, 0.8]`
- [x] 1.5 Top strips (menuBar, toolbarBar) + bottom strips (statusBar, timelineBar) preserved identically in both modes
- [x] 1.6 SplitHorizontal zeroes `stackPanel / centerPanel / rightPanel` so phase-14 consumers that check `width > 0` degrade gracefully

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
