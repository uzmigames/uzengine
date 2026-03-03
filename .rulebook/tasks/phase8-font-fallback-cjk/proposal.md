# Proposal: phase3-font-fallback-cjk

## Why
CJK characters (Chinese, Japanese, Korean — over 80,000 Unicode code points) and emoji render as empty boxes because the default game font (typically a Latin-only typeface) has no glyphs for them. Games targeting East Asian markets, or any game with user-generated text content, are broken without multi-font fallback. The current text renderer in `uz_ui_html.dll` selects a single font per element; there is no per-character fallback chain and no HarfBuzz shaping for complex scripts such as Arabic and Hebrew that require contextual glyph substitution and bidirectional layout.

## What Changes
- Font fallback chain configuration in `.uzproj` `[ui.fonts]` section: ordered list of font paths; per-character fallback walks the chain until a font with the required glyph is found
- CJK font bundle: Noto Sans CJK SC/TC/JP/KR included in engine distribution (subset to the Unicode BMP CJK Unified Ideographs block); activated automatically when CJK characters are detected in text
- Emoji font bundle: Noto Emoji (color CBDT/CBLC format) included and placed last in the default fallback chain
- HarfBuzz integration for complex script shaping: Arabic (RTL, contextual forms), Hebrew (RTL, nikud), Devanagari (cluster reordering); shaping runs are computed per-script-run within a text paragraph
- Bidi text support: Unicode Bidirectional Algorithm (UBA) implemented to correctly mix LTR and RTL runs in a single text node
- Font cache: shaped glyph runs cached by `(string_hash, font_chain_hash, size_px)` to avoid re-shaping unchanged text every frame
- Lua API: `ctx.ui:set_font_chain(element_id, {font_paths})` for per-element override; `ctx.ui:get_font_for_codepoint(cp)` for diagnostics

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/localization.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/ui/text_layout.cpp`, `src/ui/font_fallback.cpp`, `src/ui/font_fallback.h`, `src/ui/harfbuzz_shaper.cpp`, `src/lua/lua_ui_api.cpp`, `assets/fonts/noto-cjk/`, `assets/fonts/noto-emoji/`
- Breaking change: NO
- User benefit: Games immediately support Chinese, Japanese, Korean, Arabic, and Hebrew localization without any font configuration changes; emoji in player chat or user-generated content render correctly
