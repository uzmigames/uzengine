---
name: Material System Documentation Suite
description: Three comprehensive guides for UzEngine material system created 2026-03-24
type: project
---

## Task Completion Summary

**Date**: 2026-03-24
**Agent**: docs-operator (haiku)

### Files Created

1. **`docs/guides/material-texture-slots.md`** (224 lines)
   - Complete texture slot reference (t0–t6)
   - Default texture semantics (color vs. data)
   - Packed ORM mode explanation
   - SRV index binding workflow
   - Format and precision table

2. **`docs/guides/material-blend-modes.md`** (344 lines)
   - All four blend modes documented: Opaque, Masked, Translucent, Additive
   - Characteristics, when to use, performance notes for each
   - Blend equation explanations with code
   - Sorting requirements (back-to-front for translucent)
   - Decision tree and quick reference
   - Troubleshooting section

3. **`docs/guides/material-perobject-cb.md`** (406 lines)
   - Complete PerObjectCB byte-level layout (256 bytes)
   - All 25 fields with offsets and purposes
   - Detailed materialFlags bitfield (32-bit packing)
   - Bit ranges: blendMode [1:0], useORM [2], twoSided [3], specular [15:8], envMultiplier [23:16], shadingModelID [27:24], clearCoatRoughness [31:28]
   - Bit packing example with assembly
   - CPU-to-GPU data flow with code example

### Updated Files

- **`docs/README.md`** — Added three new material guide entries to guides section (lines 85–87)

### Quality Metrics

- Total lines: 974 (well under 800-line single-file limit)
- All files have doc-meta frontmatter
- 19 + 37 + 23 = 79 section headers across all three docs
- Zero TODOs, stubs, or placeholders
- Comprehensive table-based reference formats
- Code examples with syntax highlighting (glsl, cpp, hlsl)
- Complete data flow explanations

### Source References Read

- `runtime/engine/renderer/core/params/constant_buffers.h` — PerObjectCB struct definition
- `runtime/engine/renderer/material/material_asset.h` — Material class with all SRV indices
- `runtime/engine/renderer/material/material_interface.h` — MaterialInterface base class
- `runtime/engine/renderer/core/pipeline/pipeline_library.h` — GetGBuffer, GetTranslucent, GetAdditive PSO getters
- `runtime/engine/renderer/passes/geometry/gbuffer_pass.cpp` — Blend mode dispatch logic

### Key Technical Details Documented

#### Material Texture Slots
- 8 slots (t0–t6 + heightmap)
- Default textures for each slot
- sRGB vs. linear storage semantics
- ORM packed mode (R=AO, G=Rough, B=Metal)
- Fallback mechanism (index=0 means use default)

#### Blend Modes
- Opaque (0): Deferred, no blending, back-cull
- Masked (1): Deferred, alpha test, two-sided
- Translucent (2): Forward, SrcAlpha/InvSrcAlpha, no depth write, back-to-front sorting
- Additive (3): Forward, One/One, no sorting needed

#### PerObjectCB Bitfield Packing
- blendMode 2 bits [1:0]
- useORM 1 bit [2]
- twoSided 1 bit [3]
- specular 8 bits [15:8] = value/255 (0–1 range)
- envMultiplier 8 bits [23:16] = value/127.5 (0–2 range, 128=1.0)
- shadingModelID 4 bits [27:24]
- clearCoatRoughness 4 bits [31:28] = value/15 (16 levels, 0–1 range)

### Why This Matters

Material system is fundamental to renderer. This documentation provides:
- **For engine developers**: Complete CB layout, bitfield packing, PSO selection logic
- **For graphics programmers**: Texture binding semantics, sRGB handling, blend mode PSOs
- **For shader authors**: How to access material parameters, defaults, ORM unpacking
- **For engine users**: Which blend mode to use, texture slot organization, performance tradeoffs

All information extracted directly from source headers with zero speculation.
