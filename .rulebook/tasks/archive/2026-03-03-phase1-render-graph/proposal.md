# Proposal: phase1-render-graph

## Why

Every post-process pass added to the renderer currently requires manual `D3D12_RESOURCE_BARRIER` insertion, hand-tracked resource state, and separate command list recording. Adding SSAO, bloom, TAA, and DDGI without a render graph means hundreds of hand-written barriers, high risk of GPU validation errors, and no transient resource aliasing (every pass owns its render targets permanently, wasting GPU memory).

The render graph provides a declarative pass-dependency API: passes declare which resources they read and write, the graph compiler inserts all barriers automatically, aliases transient resources on a single heap, and culls unreferenced passes. All post-process tasks (`phase2-render-postprocess`, `phase2-ssao`, `phase3-ssr`, `phase3-taa`, `phase3-global-illumination`) are blocked on this implementation.

## What Changes

- **`src/engine/renderer/render_graph.h/cpp`** — core RenderGraph implementation:
  - `RgResource`, `RgTextureDesc`, `RgBufferDesc` handle types
  - `RgPass` with `read()`/`write()`/`create_texture()`/`create_buffer()` declarations
  - `RenderGraph::compile()` — topological sort, dead pass culling, barrier insertion, aliasing heap packing
  - `RenderGraph::execute(ID3D12CommandQueue*, FrameContext&)` — ordered pass dispatch
- **`src/engine/renderer/pso_cache.h/cpp`** — `PsoCache` singleton: DX12 PSO creation and reuse by `ShaderVariantKey`
- **`src/engine/renderer/renderer.cpp`** — existing pyramid pass ported to `RgPass` API
- **`CMakeLists.txt`** — new source files added to `uz_engine` target
- **Tests** — compile/execute correctness, barrier validation, aliasing correctness

## Impact

- Affected specs: `docs/specs/render-graph.md` (spec complete from `phase0-render-graph-design`)
- Affected modules: Renderer
- Dependencies: `phase0-render-graph-design` (spec must be complete)
- Breaking change: INTERNAL — renderer API refactored; no user-facing `.uzscene` changes
- Downstream unblocked: `phase2-render-postprocess`, `phase2-ssao`, `phase2-deferred-decals`, `phase3-ssr`, `phase3-taa`, `phase3-global-illumination`, `phase3-virtual-shadow-maps`
