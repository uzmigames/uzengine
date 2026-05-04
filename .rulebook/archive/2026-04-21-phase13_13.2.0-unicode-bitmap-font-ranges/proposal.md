# Proposal: phase13_13.2.0-unicode-bitmap-font-ranges

## Why

`BitmapFont` bakes only ASCII 32-126 (95 glyphs). Every inline icon
in the editor that needs a shape — box drawing, arrows, dividers,
status markers — falls back to an ASCII composition (`v` for chevron,
`::` for grip, `[]` for stop). Result: low readability, no visual
identity, zero typography nuance.

Extending the baked range to cover a curated Unicode set (Box
Drawing, Arrows, Geometric Shapes, and an optional PUA range for
FontAwesome/Material Icons) lifts that floor. Combined with phase
13.1's icon atlas, the editor gains both vector-style icons AND
inline Unicode glyphs — the latter cheaper for small inline marks
that don't justify an atlas slot.

## What Changes

1. **Configurable codepoint ranges**: `BitmapFont::InitWithRanges(
   device, cmdList, upload, fontPath, pixelHeight,
   const std::vector<GlyphRange>& ranges)`. `GlyphRange` = `{uint32_t
   first; uint32_t last;}`. The bake loop iterates ranges and calls
   `stbtt_BakeFontBitmap` per chunk, concatenating into one atlas.
2. **Default ranges for the VFX editor**:
   - ASCII printable (0x20–0x7E) — unchanged
   - Box Drawing (0x2500–0x257F) — gives `│ ─ ┌ ┐ └ ┘` for panel separators without rect spam
   - Block Elements (0x2580–0x259F) — shaded blocks for progress fills
   - Geometric Shapes (0x25A0–0x25FF) — `■ ▲ ▶ ◆ ● ○` for markers
   - Arrows (0x2190–0x21FF) — `← → ↑ ↓ ↺ ↻` for transport / undo hints
   - Miscellaneous Symbols (0x2600–0x26FF) — `★ ☆ ⚙ ⚠ ⚡ ⚫` for state icons
   - Dingbats (0x2700–0x27BF) — `✓ ✕ ✚ ✖` for check / close
3. **Atlas size bump**: current 512x512 may not fit the full added
   range at 14px. Move to 1024x1024 RGBA8; bake with dynamic pack.
4. **Glyph lookup by codepoint**: `BitmapFont::GetGlyph(uint32_t
   codepoint)` replaces the `char` overload (legacy overload retained
   for ASCII call sites).
5. **UTF-8 decode in AddText**: `TextRenderer::AddText` walks the
   string as UTF-8, decoding each codepoint and looking up the glyph.
   Missing codepoints render as the replacement glyph (U+FFFD or `?`).
6. **vfx_editor_icons.h upgrade**: replace the ASCII compositions with
   real Unicode glyphs where a font range covers them. Examples:
   - `VFX_ICON_PLAY` from `">"` to `"▶"`
   - `VFX_ICON_PAUSE` from `"||"` to `"❚❚"` (heavy vertical bar x2)
   - `VFX_ICON_CHEVRON_DOWN` from `"v"` to `"▼"`
   - `VFX_ICON_DRAG_DOTS` from `":"` to `"⋮"`
   Phase 13.5 (theme repaint) consumes the upgraded glyphs.

## Impact

- Affected specs: `specs/unicode-bitmap-font-ranges/spec.md` (new).
- Affected code:
  - `runtime/editor/font/bitmap_font.{h,cpp}` — range list, UTF-8
    decode, atlas upgrade.
  - `runtime/editor/font/text_renderer.{h,cpp}` — codepoint path.
  - `runtime/engine/vfx/editor/vfx_editor_icons.h` — Unicode replacement.
  - `runtime/tests/editor/font/test_bitmap_font_unicode.cpp` (new).
- Breaking change: NO — ASCII `GetGlyph(char)` retained; default
  range set includes everything phase 12 relied on.
- User benefit: inline Unicode glyphs give the editor real typography
  marks (triangles, chevrons, check, cross). Combined with 13.1's
  sprite atlas, the editor finally has a real iconographic
  vocabulary.
