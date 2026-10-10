# unicode-bitmap-font-ranges

## ADDED Requirements

### Requirement: Multi-range font bake

`BitmapFont::InitWithRanges` SHALL bake every `GlyphRange` passed in
the `ranges` vector into a single 1024x1024 RGBA8 atlas. Missing
glyphs (font does not cover the codepoint) are silently skipped.

#### Scenario: Seven default ranges bake successfully

Given the shipped default font + default seven ranges
When `InitWithRanges` is called
Then the method returns `true` and `GetGlyph(0x25B6)` (▶) returns
a non-null `GlyphInfo` with non-zero `width`.

### Requirement: UTF-8 decode in AddText

`TextRenderer::AddText` SHALL decode the `std::string` argument as
UTF-8. Multi-byte codepoints map to their matching glyph. Malformed
sequences render the replacement glyph `?`.

#### Scenario: Play triangle renders via UTF-8

Given the bound font with Geometric Shapes range
When `AddText(font, "\xE2\x96\xB6", x, y, white)` runs (U+25B6 ▶)
Then exactly one quad is appended with UV rect matching
`GetGlyph(0x25B6)`.

### Requirement: ASCII back-compat

`GetGlyph(char)` SHALL continue to resolve printable ASCII as phase
12 did. Every phase-12 call site continues to compile + render
identically.

#### Scenario: ASCII printable path preserved

Given a phase-12 caller passing `GetGlyph('A')`
When invoked on the phase-13 BitmapFont
Then the returned `GlyphInfo` is non-null and matches the legacy
render metrics within 1e-3 tolerance.
