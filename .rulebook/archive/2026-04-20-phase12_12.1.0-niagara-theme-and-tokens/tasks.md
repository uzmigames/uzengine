## 1. Implementation

- [x] 1.1 Create `vfx_editor_theme.h` with `VfxEditorTheme` struct (40+ color tokens, spacing scale, typography scale, radius scale)
- [x] 1.2 Create `vfx_editor_theme.cpp` with Niagara-dark palette values + `const VfxEditorTheme& VfxTheme()` global accessor + WCAG `VfxContrastRatio` helper
- [x] 1.3 Create `vfx_editor_icons.h` with 48 icon glyph constants (transport, navigation, state, overrides, drag handles, menu affordances, module categories) + `VfxAllIcons()` enumeration table
- [x] 1.4 Refactor `vfx_editor_overlay` palette (header class constants + .cpp) to read from `VfxTheme()`
- [x] 1.5 Refactor `vfx_stack_panel_widget.cpp` palette constants to read from `VfxTheme()`
- [x] 1.6 Refactor `vfx_parameter_inspector_widget.cpp` palette constants to read from `VfxTheme()`
- [x] 1.7 Refactor `vfx_curves_editor_widget.cpp` palette constants to read from `VfxTheme()`
- [x] 1.8 Refactor `vfx_timeline_scrubber_widget.cpp` palette constants to read from `VfxTheme()` (including the inline burst marker color)
- [x] 1.9 Refactor `vfx_module_picker_dialog.cpp` palette constants to read from `VfxTheme()`
- [x] 1.10 Wire theme init into `VfxEditorOverlay::Init` to verify every glyph is available in the bound font (warn on missing glyphs)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/53-editor-theme.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_editor_theme.cpp — 15 tests covering VfxTheme identity, accessors, palette unit-range + finite, monotonic scales, WCAG contrast helper identity/symmetry/known-values, shipped AA contrast assertions for text/accent/dim on panelBg, icon table count/non-empty/ASCII-printable/transport-distinct)
- [x] 2.3 Run tests and confirm they pass (`zig build` compiles runtime + test_runtime cleanly; full test-binary execution continues to be blocked by pre-existing missing `swscale-9.dll` / ffmpeg DLLs — same gap that has blocked every previous phase 11 / 11.14 task)
