## 1. Research — Read Existing Specs

- [x] 1.1 Read `docs/specs/renderer.md` — understand current DX12 pipeline, GBuffer layout, present pass
- [x] 1.2 Read `docs/specs/screen-space-effects.md` — SSAO, SSR, TAA resource dependencies
- [x] 1.3 Read `docs/specs/shader-system.md` — PSO creation and caching patterns
- [x] 1.4 Read `docs/specs/shader-variants.md` — `ShaderVariantKey`, DXC flags
- [x] 1.5 Read `docs/specs/point-spot-light-shadows.md` — shadow atlas resource lifetime
- [x] 1.6 Survey Framegraph literature: Yuriy O'Donnell "FrameGraph: Extensible Rendering Architecture in Frostbite" (GDC 2017)

## 2. API Design — RgResource

- [x] 2.1 Define `RgTextureDesc` struct — width, height, mips, format (RhiFormat), usage flags
- [x] 2.2 Define `RgBufferDesc` struct — byte size, stride, usage flags
- [x] 2.3 Define `RgTextureHandle` / `RgBufferHandle` handle types (uint32_t index + generation)
- [x] 2.4 Specify resource versioning — read-after-write tracking via pass dependency graph
- [x] 2.5 Specify persistent vs. transient resource distinction — persistent resources skip aliasing

## 3. API Design — RgPass

- [x] 3.1 Define `RgPassType` enum — Graphics/Compute/Copy
- [x] 3.2 Define `RgPassBuilder::read(RgTextureHandle)` — declare read dependency
- [x] 3.3 Define `RgPassBuilder::write(RgTextureHandle)` — declare write dependency
- [x] 3.4 Define `RgPassBuilder::read_buffer()` / `write_buffer()` — buffer dependencies
- [x] 3.5 Define `RgPassBuilder::force_run()` — prevent culling of passes with no outputs
- [x] 3.6 Specify execute callback: `std::function<void(RhiCmdList)>` via `add_pass()`

## 4. API Design — RenderGraph Compile Phase

- [x] 4.1 Specify dependency graph construction — adjacency from pass read/write sets
- [x] 4.2 Specify dead pass culling — Kahn's topological sort + backward liveness propagation
- [x] 4.3 Specify barrier insertion — per-resource state tracking via rhi()->cmd_barrier_texture()
- [x] 4.4 Specify transient resource aliasing — lifetime intervals, heap offset packing
- [x] 4.5 Specify aliasing barrier requirements — RHI aliasing barrier placement
- [x] 4.6 Specify resource lifetime computation — [first_use, last_use] per resource

## 5. API Design — RenderGraph Execute Phase

- [x] 5.1 Specify `RenderGraph::execute(RhiCmdList)` signature
- [x] 5.2 Specify transient resource physical allocation — via rhi()->texture_create() at compile time
- [x] 5.3 Specify pass execution order — respects topological sort from compile
- [x] 5.4 Specify resource pooling — reuse across frames within budget
- [x] 5.5 Specify frame-end release — transient resources destroyed; persistent resources retained

## 6. Integration Design

- [x] 6.1 Specify how existing GBuffer pass registers as a persistent producer
- [x] 6.2 Specify swapchain backbuffer registration as imported `RgTextureHandle`
- [x] 6.3 Specify PSO cache integration — pass sets up shaders, RHI handles PSO
- [x] 6.4 Specify multi-frame history resources (TAA) — double-buffered persistent resources
- [x] 6.5 Specify async compute — future work (Phase 4)

## 7. Write Spec

- [x] 7.1 Created `docs/specs/rendering/render-graph.md` — 992-line full spec (types, API, compile/execute pipeline, aliasing, integration)
- [x] 7.2 Added ASCII diagram: pass dependency DAG example (GBuffer → Lighting → SSAO → SSR → Bloom → TAA → Tonemap → Present)
- [x] 7.3 Added resource lifetime section with transient aliasing strategy
- [x] 7.4 Added barrier insertion section — state transitions per resource
- [x] 7.5 Spec cross-references renderer.md, screen-space-effects.md, rhi.md

## 8. Review

- [x] 8.1 All post-process passes from screen-space-effects.md expressible in the API
- [x] 8.2 Compute passes (SSAO, SSR dispatch) expressible as RgPassType::Compute
- [x] 8.3 Design fits within RHI-agnostic model (no DX12/Vulkan types in render-graph API)
- [x] 8.4 No breaking change to existing renderer code — design-only task
