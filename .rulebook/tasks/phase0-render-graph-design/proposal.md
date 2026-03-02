# Proposal: phase0-render-graph-design

## Why

Post-process effects (bloom, SSR, SSAO, TAA, DDGI) each allocate their own render targets and submit independent DX12 command lists. Without a unified render graph, every new pass introduces manual barrier insertion, duplicated resource lifetime logic, and no aliasing of transient resources. This causes excessive GPU memory consumption and hard-to-debug barrier hazards.

A render graph — designed before any post-process implementation — provides a declarative pass-dependency API that automates barrier insertion, enables transient resource aliasing, and allows the engine to cull culled passes at zero CPU cost. All future post-process tasks depend on this design being locked before coding begins.

## What Changes

- **No code changes** — this is a design-only task.
- **`docs/specs/render-graph.md`** — new spec defining:
  - `RgResourceDesc`, `RgResource` handle types (buffers, textures)
  - `RgPass` with `read(resource)` / `write(resource)` declarations
  - `RenderGraph::compile()` — barrier insertion, resource aliasing, dead pass culling
  - `RenderGraph::execute(ID3D12GraphicsCommandList*)` — ordered pass submission
  - Persistent resource registration (swapchain, depth buffer, GBuffer)
  - DX12 heap aliasing strategy for transient resources
  - PSO cache integration hooks
- **Existing specs reviewed**: `renderer.md`, `screen-space-effects.md`, `shader-system.md`, `shader-variants.md`

## Impact

- Affected specs: `docs/specs/render-graph.md` (new), `docs/specs/renderer.md` (reference)
- Affected modules: Renderer (future implementation)
- Breaking change: NO — design only
- Downstream tasks that depend on this: `phase1-render-graph`, `phase2-render-postprocess`, `phase2-deferred-decals`, `phase3-virtual-shadow-maps`, `phase3-global-illumination`, `phase3-ssr`, `phase3-taa`, `phase2-ssao`
