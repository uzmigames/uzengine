## 1. Implementation

- [x] 1.1 Added `GlyphRange { uint32_t first, last; }` + `BitmapFont::InitWithRanges(device, cmdList, upload, fontPath, pixelHeight, ranges)` — bakes multiple Unicode ranges into a single atlas via `stbtt_PackBegin` + `stbtt_PackFontRanges` (`skip_missing_codepoints=1`)
- [x] 1.2 Bumped atlas resolution to 1024x1024 R8 (R8 preferred over RGBA8 because pixel shader samples `.r` only — RGBA8 would waste 3 channels for monochrome glyph coverage); see `bitmap_font.cpp::InitWithRanges`
- [x] 1.3 Replaced `GlyphInfo m_glyphs[kGlyphCount]` with `std::unordered_map<uint32_t, GlyphInfo> m_glyphs` keyed by codepoint
- [x] 1.4 Added `GetGlyph(uint32_t codepoint)` primary overload; `GetGlyph(char)` now forwards to the codepoint version for back-compat
- [x] 1.5 Implemented UTF-8 decode (`DecodeUtf8Codepoint` in `bitmap_font.cpp`) shared with `TextRenderer::AddText` — decodes 1/2/3/4-byte sequences, falls back to `?` when a decoded codepoint is not baked, replaces malformed bytes with `U+FFFD`
- [x] 1.6 Declared 7 default ranges in `GetDefaultGlyphRanges()` — ASCII (95) + Arrows (112) + Box Drawing (128) + Block Elements (32) + Geometric Shapes (96) + Misc Symbols (256) + Dingbats (192) = 911 codepoints total
- [x] 1.7 Upgraded `vfx_editor_icons.h` constants to UTF-8 where Consolas-class mono fonts cover the glyph: PLAY `▶` STOP `■` RESTART `↺` LOOP `↻` CHECK `✓` CROSS/DELETE `✕` STAR `★` WARNING `⚠` DOT `●` chevrons `▸ ▾ ◂ ▴` arrows `← → ↑ ↓`; ASCII fallbacks kept for heavy-weight glyphs absent from most mono fonts (PAUSE `||`, PLUS `+`, LOCK `#`, GRIP `::`)
- [x] 1.8 Wired `VfxEditorOverlay::Init` to call `m_font.InitWithRanges(..., GetDefaultGlyphRanges())` so every editor widget inherits the curated range set
- [x] 1.9 Headless screenshot `.screenshots/phase13_2_unicode_v3.png` confirms real Unicode glyphs: stage-header chevrons `▾`/`▸`, timeline `↺` `↻`, transport row spacing correct (space glyph has 0 width but non-zero xAdvance — preserved by the bake logic)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
