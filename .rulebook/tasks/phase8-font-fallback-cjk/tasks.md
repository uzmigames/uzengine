## 1. Font Fallback Chain Configuration

- [ ] 1.1 Add `[ui.fonts]` section to `.uzproj` schema supporting an ordered list of font file paths as the global fallback chain
- [ ] 1.2 Create `src/ui/font_fallback.h` declaring `FontFallbackChain` with methods `add_font(const char* path)`, `find_font_for_codepoint(uint32_t cp)` returning a `FontHandle`
- [ ] 1.3 Create `src/ui/font_fallback.cpp` implementing per-character fallback: iterate the font list in order; return the first font whose glyph set contains `cp`
- [ ] 1.4 Load the fallback chain from `.uzproj` during project initialization before the first UI frame

## 2. CJK Font Bundle

- [ ] 2.1 Add Noto Sans CJK SC/TC/JP/KR font files (subsetted to BMP CJK Unified Ideographs U+4E00–U+9FFF) to `assets/fonts/noto-cjk/`
- [ ] 2.2 Register the CJK font bundle as the default second-to-last entry in the fallback chain (before emoji, after the game's primary font)
- [ ] 2.3 Auto-detect CJK code points (U+4E00–U+9FFF, U+3040–U+30FF, U+AC00–U+D7A3) during text layout and activate the CJK fallback font for those runs

## 3. Emoji Font Bundle

- [ ] 3.1 Add Noto Emoji font (CBDT/CBLC color format) to `assets/fonts/noto-emoji/`
- [ ] 3.2 Register the emoji font as the last entry in the default fallback chain
- [ ] 3.3 Detect emoji code points (U+1F000–U+1FFFF, U+2600–U+27BF, and ZWJ sequences) and route them to the emoji font

## 4. HarfBuzz Integration

- [ ] 4.1 Add `harfbuzz` as a vcpkg dependency in `vcpkg.json`
- [ ] 4.2 Create `src/ui/harfbuzz_shaper.h` declaring `HarfBuzzShaper` with method `shape(const char* utf8, FontHandle font, hb_direction_t dir)` returning a vector of `ShapedGlyph` structs
- [ ] 4.3 Create `src/ui/harfbuzz_shaper.cpp` implementing shaping: create `hb_buffer`, set direction/script/language, call `hb_shape()`, extract glyph IDs and advances
- [ ] 4.4 Integrate shaper into `src/ui/text_layout.cpp`: segment input text by script run; call `HarfBuzzShaper::shape()` per run with the appropriate font
- [ ] 4.5 Support Arabic contextual forms (right-to-left, `HB_SCRIPT_ARABIC`), Hebrew (`HB_SCRIPT_HEBREW`), and Devanagari cluster reordering (`HB_SCRIPT_DEVANAGARI`)

## 5. Bidirectional Text (UBA)

- [ ] 5.1 Implement Unicode Bidirectional Algorithm paragraph-level analysis in `src/ui/text_layout.cpp` to detect mixed LTR/RTL runs
- [ ] 5.2 Reorder visual display of glyph runs according to the resolved bidi levels
- [ ] 5.3 Test correct rendering of a string containing both English (LTR) and Arabic (RTL) words in the same paragraph

## 6. Font Cache

- [ ] 6.1 Add a glyph run cache keyed on `(string_hash, font_chain_hash, size_px)` in `src/ui/text_layout.cpp`
- [ ] 6.2 On a cache hit, return the cached `ShapedGlyph` vector without re-calling `hb_shape()`
- [ ] 6.3 Invalidate cache entries when `large_text_scale` (from `AccessibilitySettings`) changes the effective `size_px`

## 7. Lua API

- [ ] 7.1 Add `ctx.ui:set_font_chain(element_id, {font_paths})` Lua binding in `src/lua/lua_ui_api.cpp` to set a per-element fallback chain override
- [ ] 7.2 Add `ctx.ui:get_font_for_codepoint(cp)` Lua binding returning the font path that would be used for a given Unicode code point; return `nil` if no font covers it

## 8. Testing

- [ ] 8.1 Create `tests/ui/test_font_fallback.cpp` testing `FontFallbackChain::find_font_for_codepoint()` with a Latin character (U+0041), a CJK character (U+4E2D), and an emoji (U+1F600); verify each returns the correct font
- [ ] 8.2 Test fallback chain ordering: a code point covered by multiple fonts returns the first font in the chain
- [ ] 8.3 Test fallback chain with a code point covered by no font returns a null handle (not a crash)
- [ ] 8.4 Test `HarfBuzzShaper::shape()` on a Latin string produces glyph IDs with correct advance widths (non-zero)
- [ ] 8.5 Test HarfBuzz shaping on an Arabic string produces RTL glyph order
- [ ] 8.6 Test glyph run cache: shape the same string twice; verify `hb_shape()` is called only once (second call is cache hit)
- [ ] 8.7 Test cache invalidation when `size_px` changes: verify `hb_shape()` is called again for the new size
- [ ] 8.8 Test CJK auto-detection: a text node containing U+4E2D triggers the CJK font for that run
- [ ] 8.9 Achieve >= 95% line and branch coverage across `font_fallback.cpp`, `harfbuzz_shaper.cpp`, and font-fallback paths in `text_layout.cpp`

## 9. Documentation

- [ ] 9.1 Update `docs/specs/ui-system.md` documenting font fallback chain configuration, CJK/emoji bundles, and HarfBuzz shaping
- [ ] 9.2 Update `docs/specs/localization.md` documenting how to add CJK/Arabic/Hebrew locale support using the fallback chain
- [ ] 9.3 Update `docs/specs/lua-scripting.md` documenting `ctx.ui:set_font_chain()` and `ctx.ui:get_font_for_codepoint()`
