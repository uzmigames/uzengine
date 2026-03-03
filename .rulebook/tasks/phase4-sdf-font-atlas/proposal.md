# Proposal: phase2-sdf-font-atlas

## Why
The current UI text renderer uses raster (bitmap) font atlases baked at a fixed pixel size. When text is rendered at a size larger than the baked resolution — common on 4K displays, for large headings, or when the UI is scaled — the glyphs appear blurry and pixelated. Maintaining multiple atlas sizes per font multiplies memory usage and import time. SDF (Signed Distance Field) fonts solve this by encoding the distance to the glyph outline rather than raw pixels, allowing a single atlas to render crisp text at any size from 8px to 300px with a simple shader threshold.

## What Changes
- Integrate `msdf-atlas-gen` into the asset pipeline as the font baking tool; source `.ttf`/`.otf` files produce a `.uzfont` asset containing the MSDF atlas texture and per-glyph UV/metrics data in a compact binary layout
- Write an MSDF HLSL pass for the UI render pipeline: sample the three-channel SDF texture, compute the median of three channels, apply a smooth-step threshold for anti-aliased edge reconstruction, and support outline and drop-shadow via threshold bias
- Update `CText` / UI text rendering to look up per-character SDF UVs from the `.uzfont` metadata and submit quads to the MSDF pass instead of the raster pass
- Expose `ctx.ui:set_font(font_id, size_px)` in the Lua API to select a loaded font at any size without requiring a separate atlas import per size
- Add an Editor font preview panel in the Asset Browser showing the font at several sizes to verify SDF quality after import

## Impact
- Affected specs: docs/specs/ui-system.md, docs/specs/asset-pipeline.md, docs/specs/lua-scripting.md
- Affected code: src/renderer/ (MSDF shader pass), src/ui/ (text layout, CText component), src/assets/ (font importer using msdf-atlas-gen), vscode-extension/ (asset browser font preview)
- Breaking change: YES — existing `.uzfont` assets must be re-imported; raster atlas format is replaced
- User benefit: Text remains sharp at all resolutions and sizes with a single atlas per font, reducing memory usage and eliminating blurry text on 4K displays or in large-scale UI elements
