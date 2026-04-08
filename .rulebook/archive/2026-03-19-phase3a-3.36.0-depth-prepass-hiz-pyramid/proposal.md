# Depth Prepass + Hi-Z Pyramid

## Why

Without an explicit depth prepass, the GBuffer pass suffers overdraw on complex scenes (20-40% wasted fillrate). More critically, SSAO (3.15.0), SSR (3.16.0), and contact shadows (3.29.0) all require a mipped depth buffer (Hi-Z pyramid) that can only be built from a clean depth-only pass. P2 architectural blocker from the 2026-03-14 gap analysis — unblocks three Phase 3 tasks.

## What Changes

### New Systems
1. **DepthPrepass** — render graph pass writing depth-only to D32F before GBuffer. Same mesh list, depth-only PSO (no pixel shader).
2. **HiZPyramidPass** — compute shader generating mipped depth buffer from prepass output. Each mip stores min depth of 2x2 region (conservative). 6-8 mip levels.
3. **HiZPyramid resource** — R32F texture with full mip chain, registered as persistent render graph resource.

### Refactoring
- GBuffer pass depth test changes from LESS to EQUAL (depth already written)
- Render graph pipeline: DepthPrepass → HiZPyramid → GBuffer (was just GBuffer)

## Impact
- Affected specs: `docs/engine/12-rendering/03-frame-pipeline.md`, `docs/engine/12-rendering/10-deferred-pipeline.md`
- Affected code: `runtime/engine/renderer/passes/`, `runtime/shaders/`
- Breaking change: NO
- User benefit: 20-40% GPU fillrate savings; enables SSAO/SSR/contact shadows
- Enables: GTAO (3.15.0), SSR (3.16.0), contact shadows (3.29.0), occlusion culling
