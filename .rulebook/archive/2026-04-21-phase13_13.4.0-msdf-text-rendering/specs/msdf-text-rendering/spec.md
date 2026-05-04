# msdf-text-rendering

## ADDED Requirements

### Requirement: .uzmsdf container round-trips

The `uzfontbake` CLI SHALL produce a `.uzmsdf` file whose runtime
loader can decode without loss: every glyph's UV + metric match the
baked values within 1-pixel precision.

#### Scenario: Bake + load of a known range

Given a TTF font + range `0x41-0x5A` (A-Z) + pixel size 32
When `uzfontbake` writes `test.uzmsdf` and `MsdfFont::Load` reads it
Then `GetGlyph('A').xAdvance` matches the FT-reported advance within
1.0 pixel.

### Requirement: Crisp rendering at arbitrary size

An MSDF glyph SHALL render with an AA edge width of 1-2 pixels at
any caller-specified render size in the range `[10, 48]` px.

#### Scenario: 10 px caption renders without bitmap aliasing

Given the `editor_mono.uzmsdf` atlas baked at 32 px master
When `AddText` renders "Hello" at 10 px height to an offscreen target
Then the vertical edge of any glyph exhibits a 1-2 pixel AA
transition (no hard step, no blurry blob).

### Requirement: Bitmap path remains functional

`TextRenderer::UseFont(BitmapFont*)` SHALL still render phase-12
output identically to the bitmap path when no MSDF font is loaded.

#### Scenario: Legacy path unaffected

Given a phase-12 caller that never invokes `UseFont(MsdfFont*)`
When `AddText("A", ...)` runs
Then the rendered quad is byte-identical to phase 12 bitmap output.
