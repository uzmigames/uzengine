---
name: Deferred Rendering Pipeline Documentation (Milestone 1.3)
description: Complete documentation of the deferred pipeline implementation — G-Buffer layout, passes, shaders, constant buffers
type: project
---

## Overview

Documented the complete deferred rendering pipeline implementation for Milestone 1.3. The implementation includes:

- **GBuffer Pass** (`gbuffer_pass.h`) — writes geometry material data to 3 render targets + depth
- **Lighting Pass** (`lighting_pass.h`) — reads G-Buffer, evaluates up to 64 point lights + directional light
- **Shaders**: `gbuffer_ps.hlsl`, `deferred_lighting_ps.hlsl`, `fullscreen_vs.hlsl`
- **Constant buffers**: `PerFrameCB`, `PerObjectCB`, `LightingCB` (with 64 PointLightData structs)

## Documentation Created

### New File: `10-deferred-pipeline.md` (455 lines)

Comprehensive reference covering:

- **Pipeline architecture** — two-pass geometry + lighting flow
- **G-Buffer layout** — 4 targets (Albedo/RGBA8, Normal/RGBA16F, MetalRough/RGBA8, Depth/D32F)
- **Shader details** — SM6.6 bindless + SM5 fallback paths, texture sampling, position reconstruction
- **Constant buffer specs** — full layout with HLSL struct equivalents and 256-byte alignment
- **Point light attenuation** — smooth windowing function formula: `(saturate(1-(d/r)^4))^2 / (d^2 + 1)`
- **Render graph integration** — resource declaration, barrier scheduling
- **Performance analysis** — 20 bytes/pixel G-Buffer bandwidth, linear lighting cost scaling
- **Limitations & future work** — shadow mapping (Phase 2), clustering (Phase 3+)
- **Engine comparisons** — UE5/Godot approaches vs. UzEngine's simplicity-focused design

### Updated Files

1. **README.md**
   - Updated Milestone 1.3 status from "PLANNED" to "DONE"
   - Added `10-deferred-pipeline.md` to documentation index
   - Updated file manifest with (★★) marker for new 1.3 docs

2. **03-frame-pipeline.md**
   - Corrected G-Buffer layout table to match actual implementation (RGBA16F normals, not RGB10A2)
   - Clarified deferred pipeline benefits with cross-reference to 10-deferred-pipeline.md
   - Explained why deferred decouples geometry from light count

3. **06-lighting.md**
   - Updated status from "Planned (Phases 2–4)" to "Partial (Phase 1 base + Phase 2+ planned)"
   - Added reference to deferred shading implementation via 10-deferred-pipeline.md
   - Updated Purpose section to describe both forward (current) and deferred (new) evaluation paths
   - Extended Implementation Phase table to show Phase 1.3 as complete deferred milestone

## Key Technical Details Documented

- **Fullscreen triangle technique** — uses SV_VertexID to avoid vertex/index buffers, eliminates diagonal seam artifacts
- **Position reconstruction** — uses invViewProj to map screen UV + depth back to world space
- **Smooth attenuation** — explains windowing function vs. hard cutoffs (prevents light discontinuity artifacts)
- **Constant buffer alignment** — all CBs are 256-byte multiples for D3D12 requirements
- **Bindless fallback** — both SM6.6 descriptor heap and SM5 register binding documented with `#ifdef UZ_BINDLESS`
- **Cook-Torrance in deferred context** — same BRDF math as forward pass, evaluated per-pixel per-light in fullscreen shader

## Cross-References Added

- Frame pipeline (03) → Deferred pipeline (10)
- Lighting doc (06) → Deferred pipeline (10)
- README links deferred pipeline in "I want to implement a custom render pass" section

## Rules Documented

1. Always render geometry to GBuffer first before lighting
2. Never read backbuffer in lighting pass (write-only target)
3. Maximum 64 point lights (array size constraint)
4. Always keep depth as SRV in lighting pass, not DSV
5. Always update invViewProj matrix correctly
6. Never pack normals (RGBA16F minimum to avoid banding)
7. Always use smooth attenuation (no hard cutoffs)
8. Validate depth reconstruction via debug visualization
9. Check point light counts via GPU counters/CPU logs

## Testing & Validation

Documentation includes:
- **Bandwidth calculations** — 20 bytes/pixel G-Buffer, 24 bytes/pixel lighting pass
- **Scalability analysis** — linear point light cost (1-64 lights with minimal overhead change)
- **Performance comparison** — vs. UE5's cluster deferred, Godot's forward, shows simplicity advantage
- **Engine comparison table** — deferred complexity tradeoffs

## Files Modified

- `/e/UzEngine/0.2/docs/engine/12-rendering/03-frame-pipeline.md` — updated GBuffer table + deferred benefits
- `/e/UzEngine/0.2/docs/engine/12-rendering/06-lighting.md` — updated status, linked to deferred doc, extended phase table
- `/e/UzEngine/0.2/docs/engine/12-rendering/README.md` — milestone 1.3 marked done, new doc added to index + manifest
- `/e/UzEngine/0.2/docs/engine/12-rendering/10-deferred-pipeline.md` — NEW (455 lines)

**Total doc changes**: 4 files, ~600 lines added/modified

## Why This Matters

The deferred pipeline is a major milestone — it enables:
- Up to 64 dynamic point lights without geometry re-rendering
- Foundation for Phase 2 shadowing, Phase 3 light culling, Phase 4 clustering
- Clear separation of concerns (geometry pass independent of lighting)
- Reusable G-Buffer data for post-processing (SSAO, SSR, TAA)

Documentation ensures future contributors understand the architecture, can debug issues (position reconstruction, attenuation), and can extend for shadows/clustering in later phases.
