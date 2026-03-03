## 1. Asset Pipeline: msdf-atlas-gen Integration
- [ ] 1.1 Add `msdf-atlas-gen` as a build-time tool dependency in `CMakeLists.txt`; expose it via a `MSDF_ATLAS_GEN_EXECUTABLE` CMake variable pointing to the tool binary
- [ ] 1.2 Create `src/assets/font_importer.cpp` with `FontImporter::Import(const char* ttf_path, const char* out_uzfont_path, FontImportSettings settings)` that invokes `msdf-atlas-gen` as a subprocess with the appropriate flags
- [ ] 1.3 Define `FontImportSettings` struct in `src/assets/font_importer.h` with fields: `uint32_t atlas_size` (default 512), `uint32_t glyph_scale` (default 32), `bool include_kerning` (default true)
- [ ] 1.4 Define the `.uzfont` binary layout: 16-byte magic header `UZFONT\0\0`, version uint32, atlas texture width/height uint32, glyph count uint32, followed by a packed array of `UzFontGlyph` structs and then the raw MSDF atlas PNG bytes
- [ ] 1.5 Define `UzFontGlyph` struct in `src/assets/font_importer.h`: `uint32_t codepoint`, `float uv_x, uv_y, uv_w, uv_h`, `float advance`, `float bearing_x, bearing_y`, `float width, height`
- [ ] 1.6 Implement `FontImporter::WriteBinary(const char* out_path, const MsdfAtlasData& data)` serializing the above binary layout to disk
- [ ] 1.7 Register the `.uzfont` asset type in the asset registry so the asset loader recognizes the magic header and dispatches to `FontAssetLoader::Load()`

## 2. MSDF HLSL Shader Pass
- [ ] 2.1 Create `src/renderer/shaders/msdf_text.hlsl` implementing the MSDF fragment shader: sample the three-channel atlas texture, compute `median(r, g, b)`, apply `smoothstep(0.5 - edge_softness, 0.5 + edge_softness, median)` for anti-aliased edge reconstruction
- [ ] 2.2 Add outline support in `msdf_text.hlsl`: secondary threshold biased by `outline_width` uniform, blended with `outline_color` over the base glyph alpha
- [ ] 2.3 Add drop-shadow support in `msdf_text.hlsl`: sample atlas at UV offset by `shadow_offset` uniform, apply `shadow_softness` smoothstep, composite under the base glyph
- [ ] 2.4 Register the MSDF shader pass in `src/renderer/render_graph.h` as `RenderPassDesc` with `pass_name = "MsdfTextPass"` and required resource inputs declared

## 3. CText Component and UI Text Rendering
- [ ] 3.1 Update `src/ui/CText` component to store a loaded `FontAsset*` handle and a `float size_px` field instead of a raster atlas reference
- [ ] 3.2 Update the text layout path in `src/ui/text_layout.cpp` to look up per-character `UzFontGlyph` UV and metrics from the loaded `.uzfont` asset
- [ ] 3.3 Update the text quad submission path in `src/ui/text_renderer.cpp` to route quads through the MSDF pass instead of the legacy raster pass when the font asset type is `.uzfont`
- [ ] 3.4 Remove the legacy raster atlas font path from `src/ui/text_renderer.cpp`; update any call sites that previously referenced raster atlas fields

## 4. Lua API
- [ ] 4.1 Implement `ctx.ui:set_font(font_id, size_px)` Lua binding in `src/lua/lua_ui_api.cpp`; the binding calls `CText::SetFont(FontAsset*, float)` on the target element
- [ ] 4.2 Ensure `ctx.ui:set_font` works at any `size_px` value (8–300) using the same single atlas without requiring a separate import per size

## 5. Editor Font Preview Panel
- [ ] 5.1 Add a font preview section in the VSCode Asset Browser panel (`vscode-uzengine/src/panels/AssetBrowserPanel.ts`) that activates when a `.uzfont` asset is selected
- [ ] 5.2 The preview renders the font at sizes 12, 24, 48, and 96px using the IPC command `asset.preview_font {font_id, sizes:[12,24,48,96]}` and displays the returned image data
- [ ] 5.3 Implement IPC handler `asset.preview_font` in `src/common/ipc_shader_handlers.cpp` (or a new `src/common/ipc_asset_handlers.cpp`) that renders the font using the MSDF pass and returns PNG bytes

## 6. Testing
- [ ] 6.1 Create `tests/renderer/test_sdf_font_atlas.cpp` with Google Test suite `SdfFontAtlasTest`
- [ ] 6.2 Write test `UzfontBinaryRoundtrip`: serialize a known `MsdfAtlasData` with 3 glyphs via `FontImporter::WriteBinary`, load it back via `FontAssetLoader::Load`, and assert glyph UV and metrics match the original data
- [ ] 6.3 Write test `InvalidMagicHeaderRejected`: attempt to load a `.uzfont` file with a corrupted header; assert `FontAssetLoader::Load` returns null and logs an error
- [ ] 6.4 Write test `GlyphLookupByCodepoint`: load a font asset and call `FindGlyph(0x41)` (ASCII 'A'); assert the returned `UzFontGlyph*` is non-null and `advance > 0`
- [ ] 6.5 Write test `MissingGlyphFallback`: call `FindGlyph(0x10FFFF)` (unmapped codepoint); assert it returns the fallback/replacement glyph, not null
- [ ] 6.6 Write test `TextLayoutProducesCorrectQuadCount`: call the text layout path with string `"ABC"` and assert three quads are submitted with non-zero UV extents
- [ ] 6.7 Verify line coverage >= 95% and branch coverage >= 95% for `font_importer.cpp` and `text_renderer.cpp`

## 7. Documentation
- [ ] 7.1 Update `docs/specs/ui-system.md` to describe the MSDF text rendering pipeline, the new `.uzfont` asset format, and migration from raster atlases
- [ ] 7.2 Update `docs/specs/asset-pipeline.md` with the `FontImporter`, `.uzfont` binary layout, and `msdf-atlas-gen` integration
- [ ] 7.3 Update `docs/specs/lua-scripting.md` with the `ctx.ui:set_font(font_id, size_px)` API entry
