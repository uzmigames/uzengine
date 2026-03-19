---
name: Temporal Upscaling Documentation Complete
description: Comprehensive guide to temporal upscaling in UzEngine created 2026-03-18
type: observation
---

# Temporal Upscaling Guide — Complete

**Date**: 2026-03-18  
**File**: `docs/guides/temporal-upscaling.md`  
**Lines**: 627 (well under 800 limit)

## What Was Created

A comprehensive guide covering all 7 required subsections:

1. **Screen Percentage System** — Decoupling render from display resolution, mipmap bias compensation, engine comparison
2. **ITemporalUpscaler Interface** — C++ abstraction, methods, UpscalerInput struct, factory, plugin pattern
3. **TSR Architecture** — Three-pass pipeline (dilate velocity, reject shading, update history) with full algorithm details
4. **TAA vs TSR Comparison** — Feature matrix, use cases, performance tradeoffs, "when to switch" guidance
5. **Dynamic Resolution** — DynamicResolutionController with scale-down/scale-up algorithms, hysteresis logic, configuration
6. **Pipeline Order** — ASCII diagram showing temporal upscaler position in render pipeline, key ordering rules
7. **Quality Settings Guide** — TSR parameters (tsrSharpness, screen %, quality levels)
8. **Lua API** — All 7 functions fully documented:
   - `uz.renderer.set_screen_percentage(pct)`
   - `uz.renderer.get_screen_percentage()`
   - `uz.renderer.set_upscaler(name)` — "none", "taa", "tsr"
   - `uz.renderer.get_upscaler()`
   - `uz.renderer.set_dynamic_resolution(enabled)`
   - `uz.renderer.get_render_resolution()` → width, height
   - `uz.renderer.get_display_resolution()` → width, height

## Key Features

- **All numeric constants cited from UE5 source** (file:line)
  - SceneRenderTargets.cpp:2500-2550 (mipmap bias)
  - TemporalSuperResolution.cpp:2858 (TheoricBlendFactor formula)
  - TSRDilateVelocity.usf:148-417, TSRUpdateHistory.usf:142-148
  - DynamicRenderScaling.h, Unity DynamicResolutionHandler
  
- **Code examples** with proper syntax highlighting (cpp, hlsl, lua)

- **Engine comparison table** (UE5, Unity, Godot, UzEngine)

- **Frontmatter with metadata** (status, theme, created/updated dates)

- **Cross-references** between sections using relative markdown links

- **Troubleshooting section** (ghosting, flickering, blurriness, banding)

- **Reference implementation** — file locations for source code

## Changes Made

- **Created**: `docs/guides/temporal-upscaling.md` (627 lines)
- **Updated**: `docs/README.md` — added temporal-upscaling entry to guides section

## Style Conformance

✓ Kebab-case filename  
✓ Max 800 lines (627)  
✓ All code blocks tagged with language  
✓ Markdown frontmatter present  
✓ No stubs or TODOs  
✓ Complete implementation (no placeholders)  
✓ All UE5 constants cited with file:line  

## Quality Level

**Complete and production-ready** — can be immediately merged to main. Zero outstanding work.
