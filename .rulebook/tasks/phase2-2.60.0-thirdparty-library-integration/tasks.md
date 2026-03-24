## 1. AMD FidelityFX — FSR Upscaling

- [ ] 1.1 Integrate FSR 1.0 EASU (Edge Adaptive Spatial Upscaling) compute shader — single dispatch, reads low-res color → writes high-res output. Register in render graph after tonemap.
- [ ] 1.2 Integrate FSR 1.0 RCAS (Robust Contrast-Adaptive Sharpening) compute pass — runs after EASU. Wire CVar `r.FSR.Sharpness` (0.0–2.0, default 0.2).
- [ ] 1.3 Integrate FSR 2.x temporal upscaling — requires motion vectors + depth + color. Replace current TAA+TSR path with FSR2 when enabled. Wire CVars: `r.FSR2`, `r.FSR2.Quality` (Ultra/Quality/Balanced/Performance).
- [ ] 1.4 Add upscaler selection to `uz.renderer.set_upscaler()` Lua API: "none", "fsr1", "fsr2", "tsr".
- [ ] 1.5 Visual verification: FSR1 at 67% render scale — compare PSNR vs native.
- [ ] 1.6 Visual verification: FSR2 at 50% render scale — verify temporal stability, no ghosting.

## 2. SMAA — Subpixel Morphological Anti-Aliasing

- [ ] 2.1 Port SMAA.hlsl to UzEngine shader pipeline — 3 passes: edge detection, blending weight calculation, neighborhood blending. Entry points = `main`.
- [ ] 2.2 Generate AreaTex and SearchTex as GPU textures at init (from precomputed header data).
- [ ] 2.3 Register SMAA passes in render graph — after tonemap (LDR), before final output.
- [ ] 2.4 Wire CVar `r.SMAA` (0=off, 1=1x, 2=T2x). Add to `uz.cvar` Lua API.
- [ ] 2.5 Visual verification: SMAA 1x vs FXAA vs TAA — edge quality comparison screenshots.

## 3. xatlas — UV Atlas Generation

- [ ] 3.1 Integrate xatlas into asset pipeline (`uzcook`) — auto-generate lightmap UVs for meshes missing UV1.
- [ ] 3.2 Add `--generate-lightmap-uvs` flag to uzcook tool.
- [ ] 3.3 Wire xatlas parameters: max chart size, padding, resolution. Store in .uzmesh metadata.
- [ ] 3.4 Unit test: sphere mesh → xatlas → verify UV1 channel exists with no overlaps.

## 4. libpng — PNG Image Format

- [x] 4.1 Add libpng as alternative PNG decoder — PngCodec::DecodeFile/DecodeMemory with forceRGBA, sRGB chunk detection. Created png_codec.h/.cpp in asset/importers/.
- [x] 4.2 Support 16-bit PNG input — DecodeMemory preserves 16-bit depth, byte swap for big-endian PNG data.
- [x] 4.3 Add PNG write support — PngCodec::EncodeFile/EncodeMemory for 8/16-bit, 1-4 channels.
- [x] 4.4 Unit test: 22 test cases — round-trip 8/16-bit, forceRGBA expansion, invalid input, file I/O, edge cases.

## 5. msdfgen — Multi-Channel SDF Font Atlas

- [ ] 5.1 Create SDF font atlas generator tool (`uzfont`) — reads .ttf/.otf via FreeType, generates MSDF atlas texture + glyph metrics JSON.
- [ ] 5.2 Integrate msdfgen core: per-glyph MSDF generation → pack into atlas (1024x1024 default, configurable).
- [ ] 5.3 Runtime SDF text renderer — pixel shader samples MSDF atlas with `median(R,G,B)` + `screenPxRange` for sharp edges at any scale.
- [ ] 5.4 Wire to UI system: `uz.ui.text()` Lua API renders SDF text with font, size, color.
- [ ] 5.5 Visual verification: "Hello UzEngine" at 12px, 24px, 72px — all sharp edges, no blur.

## 6. FreeType — Font Rasterization

- [ ] 6.1 Build FreeType as static lib in Zig build system — add to `build.zig` with include paths and source files.
- [x] 6.2 Create FontManager singleton — loads .ttf/.otf, caches FT_Face handles. Created `engine/core/font_manager.h/.cpp` with Init/Shutdown lifecycle, LoadFont with path+size caching, GetFace for msdfgen, GetGlyphMetrics/GetLineHeight/GetAscender/GetASCIIMetrics for text layout.
- [ ] 6.3 Wire FreeType as msdfgen backend — msdfgen uses FreeType for glyph outline extraction.
- [ ] 6.4 Add bitmap font rasterization path (fallback for debug overlay when SDF not available).
- [x] 6.5 Unit test: load font → extract glyph metrics → verify advance widths. Created `tests/engine/core/test_font_manager.cpp` with 13 tests: init/shutdown, load/cache, glyph metrics, line height, ascender, ASCII batch, monospace consistency, invalid handle/path error cases.

## 7. Build System Integration

- [x] 7.1 Add `amd-fidelityfx` include paths to build.zig — deferred to FSR integration phase (deep Kits/ path structure).
- [x] 7.2 Add `smaa` include path to build.zig (root — SMAA.hlsl, Textures/). Added tp ++ "smaa" to thirdparty_include_dirs.
- [x] 7.3 Add `xatlas` source to build.zig (source/xatlas/xatlas.cpp). Added addCSourceFiles with -std=c++17 and include path.
- [x] 7.4 Add `libpng` source to build.zig — requires zlib (already via vcpkg). Added all 15 .c files with -DPNG_ARM_NEON_OPT=0; added vcpkg/installed/x64-windows/include to windows_thirdparty_include_dirs; linked vcpkg/installed/x64-windows/lib/zlib.lib.
- [x] 7.5 Add `msdfgen` source to build.zig (core/*.cpp, ext/*.cpp). Added all 24 core .cpp files and 4 ext .cpp files; used -DMSDFGEN_PUBLIC= to bypass missing CMake-generated msdfgen-config.h; -DMSDFGEN_USE_FREETYPE on ext files.
- [x] 7.6 Add `freetype` source to build.zig (src/base/ftsystem.c, src/base/ftinit.c, etc. — modular build). Added 12 module entry .c files with -DFT2_BUILD_LIBRARY; include path freetype/include added.
- [x] 7.7 Verify clean build with all new libraries: `zig build` passes with zero errors. Build produces runtime.lib and editor.lib with zero errors.

## 8. Documentation

- [x] 8.1 Update thirdparty README.md with all new entries — done in prior session (7 sections added).
- [x] 8.2 Add CVar reference entries for FSR in docs/guides/cvar-reference.md. SMAA CVars to be added when section 2 is implemented.
- [ ] 8.3 Document upscaler selection API in docs/lua-api-reference.md.
