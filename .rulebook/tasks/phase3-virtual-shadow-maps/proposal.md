# Proposal: phase3-virtual-shadow-maps

## Why

Traditional cascaded shadow maps (CSM) allocate fixed-resolution textures for each cascade, wasting resolution on far shadows and lacking the per-texel detail needed for sharp near shadows. At large world scales (open worlds, long view distances), CSM quality degrades visibly. Virtual Shadow Maps (VSM) solve this by treating shadow maps as virtual texture pages: only pages that contribute to the final frame are allocated and rendered, providing effectively unlimited resolution while using GPU memory proportional to what is actually visible.

## What Changes

- **`src/engine/renderer/passes/vsm_pass.h/cpp`** — VSM atlas management, page table GPU buffer, feedback buffer readback
- **`shaders/vsm_page_mark.hlsl`** — marks required pages during depth pre-pass
- **`shaders/vsm_render.hlsl`** — renders geometry into allocated VSM pages
- **`shaders/vsm_sample.hlsl`** — HLSLI include for sampling VSM in lighting shader
- **`src/engine/renderer/renderer.cpp`** — replace CSM with VSM integration

## Impact

- Affected specs: `docs/specs/renderer.md` (shadow system replacement), `docs/specs/point-spot-light-shadows.md` (VSM integration)
- Dependencies: `phase1-render-graph` (RgPass API), `phase2-shadow-maps` (CSM as baseline)
- Breaking change: INTERNAL — replaces CSM; `.uzscene` shadow settings unchanged, new VSM-specific settings additive
- User benefit: sharp shadows at any distance; memory scales with visible shadow casters; open world viability
