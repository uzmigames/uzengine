# Thirdparty Library Integration — FSR, SMAA, xatlas, libpng, msdfgen, FreeType

## Why

Gap analysis against Godot Engine's thirdparty dependencies revealed 6 libraries
that fill critical gaps in UzEngine: temporal/spatial upscaling (FSR), anti-aliasing
(SMAA), UV atlas generation (xatlas), 16-bit PNG support (libpng), SDF font rendering
(msdfgen), and font rasterization (FreeType). Without these, the engine lacks
production-quality upscaling, text rendering, and asset pipeline features.

## Context

Gap analysis comparing UzEngine's thirdparty dependencies against Godot Engine's
identified 6 libraries that fill immediate needs in rendering quality (upscaling,
anti-aliasing), asset pipeline (UV atlas, 16-bit PNG), and text rendering (SDF fonts).

## Libraries Added

| Library | Version | Purpose | Priority |
|---------|---------|---------|----------|
| amd-fidelityfx | main (2025) | FSR 1.0/2.x/3.x temporal+spatial upscaling | High |
| smaa | master | Subpixel morphological anti-aliasing | High |
| xatlas | master | Auto UV atlas generation for lightmaps | Medium |
| libpng | 1.6.49 | 16-bit PNG, robust asset pipeline | Medium |
| msdfgen | 1.13 | Multi-channel SDF font atlas | High |
| freetype | 2.13.3 | Font rasterization (CJK, hinting) | High |

## Dependencies

- libpng requires zlib (already available via vcpkg)
- msdfgen uses FreeType for glyph outline extraction
- FSR 2.x requires motion vectors + depth (already produced by velocity pass)
- SMAA requires LDR color input (runs after tonemap)

## Implementation Order

1. **Build system** (7.x) — get everything compiling first
2. **FreeType + msdfgen** (5.x, 6.x) — text rendering foundation
3. **SMAA** (2.x) — immediate rendering quality win, simple 3-pass pipeline
4. **FSR 1.0** (1.1–1.2) — spatial upscaling, simplest upscaler
5. **FSR 2.x** (1.3) — temporal upscaling, replaces TAA+TSR
6. **libpng** (4.x) — asset pipeline improvement
7. **xatlas** (3.x) — lightmap UV generation

## Risk

- FidelityFX SDK is large (~200MB clone). Only the `sdk/` subfolder is needed.
- FreeType modular build requires careful source file selection in build.zig.
- FSR 2.x integration touches motion vector and depth buffer formats.
