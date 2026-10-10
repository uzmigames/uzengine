# Proposal: phase13_13.4.0-msdf-text-rendering

## Why

Phase 13.2's multi-range bitmap font covers the glyph inventory
gap, but the rendering is still baked bitmap — sharp at the bake
resolution (14 px) and blurry at anything else. Every modern editor
(VS Code, Figma, UE5 Slate) uses SDF / MSDF text to get crisp text
at any scale / DPI.

Switching to MSDF (multi-channel signed distance field) — the
`msdfgen` de-facto library — gives the editor crisp typography at
any size with a single atlas texture. Combined with 13.3's rounded
rects, the result finally looks like a modern product instead of a
bitmap Win32 app.

## What Changes

1. **`msdfgen` vendored as a thirdparty**: add entry in `setup.ps1` to
   clone `Chlumsky/msdfgen` + build its static lib via vcpkg. Used
   only at the offline bake step, not linked into the runtime.
2. **MSDF atlas baker tool**: new CLI `uzfontbake.exe` that takes a
   TTF + glyph range list + pixel size and writes a `<name>.uzmsdf`
   pack: a 32-bit MSDF atlas (RGBA8, each channel = an SDF channel)
   + glyph metrics JSON. Stored next to the asset.
3. **Runtime loader**: `MsdfFont` class parallel to `BitmapFont` —
   loads the `.uzmsdf` at startup, creates a single RGBA8 SRV atlas.
   Shares the same glyph-metric API (`GetGlyph(codepoint) ->
   GlyphInfo`) so callers can swap fonts transparently.
4. **MSDF pixel shader**: sample the 3 SDF channels, take the median,
   derive `screenPxRange = fwidth` to scale distance, emit anti-
   aliased alpha via `smoothstep`. Standard msdfgen sample code.
5. **`TextRenderer::UseFont(MsdfFont*)`**: host can swap between
   bitmap (cheap, existing) and MSDF (quality, new) modes at frame
   start. Phase 12 widgets keep working — they call `AddText` and
   the active font sampler decides.
6. **Default font swap in `VfxEditorOverlay::Init`**: if the project
   ships a pre-baked `.uzmsdf` for the editor mono font, load MSDF
   mode; otherwise fall back to bitmap.

## Impact

- Affected specs: `specs/msdf-text-rendering/spec.md` (new).
- Affected code:
  - `runtime/thirdparty/msdfgen/` (new vendored dep via setup.ps1).
  - `runtime/tools/uzfontbake/` (new CLI exe).
  - `runtime/editor/font/msdf_font.{h,cpp}` (new).
  - `runtime/editor/font/text_renderer.{h,cpp}` — font handle switch.
  - `runtime/shaders/runtime/editor/ui_msdf_ps.hlsl` (new).
  - `runtime/tests/editor/font/test_msdf_font.cpp` (new).
- Breaking change: NO — bitmap path stays. MSDF is opt-in per-font.
- User benefit: text scales cleanly from 10 px captions to 24 px
  headers without re-baking, stays crisp on any monitor DPI.
  Combined with 13.1 + 13.2 + 13.3, the editor's visual fidelity
  reaches UE5/modern-editor baseline.
