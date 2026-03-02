## 1. Research — Read Existing Specs

- [ ] 1.1 Read `docs/specs/renderer.md` — understand current DX12 pipeline, GBuffer layout, present pass
- [ ] 1.2 Read `docs/specs/screen-space-effects.md` — SSAO, SSR, TAA resource dependencies
- [ ] 1.3 Read `docs/specs/shader-system.md` — PSO creation and caching patterns
- [ ] 1.4 Read `docs/specs/shader-variants.md` — `ShaderVariantKey`, DXC flags
- [ ] 1.5 Read `docs/specs/point-spot-light-shadows.md` — shadow atlas resource lifetime
- [ ] 1.6 Survey Framegraph literature: Yuriy O'Donnell "FrameGraph: Extensible Rendering Architecture in Frostbite" (GDC 2017)

## 2. API Design — RgResource

- [ ] 2.1 Define `RgTextureDesc` struct — width, height, mips, format (DXGI_FORMAT), usage flags (RTV/DSV/SRV/UAV)
- [ ] 2.2 Define `RgBufferDesc` struct — byte size, stride, usage flags (CBV/SRV/UAV)
- [ ] 2.3 Define `RgResource` handle type — typed index (16-bit generation + 16-bit index)
- [ ] 2.4 Define `RgResourceVersion` — incremented on each write to support read-after-write tracking
- [ ] 2.5 Specify persistent vs. transient resource distinction — persistent resources skip aliasing

## 3. API Design — RgPass

- [ ] 3.1 Define `RgPassDesc` struct — name, `PassType` enum (Graphics/Compute/Copy), callback signature
- [ ] 3.2 Define `RgPass::read(RgResource, D3D12_RESOURCE_STATES)` — declare read dependency
- [ ] 3.3 Define `RgPass::write(RgResource, D3D12_RESOURCE_STATES)` — declare write, bump version
- [ ] 3.4 Define `RgPass::create_texture(RgTextureDesc) -> RgResource` — pass-local transient allocation
- [ ] 3.5 Define `RgPass::create_buffer(RgBufferDesc) -> RgResource` — pass-local transient allocation
- [ ] 3.6 Specify pass callback: `void(RgPassContext&, ID3D12GraphicsCommandList*)` — no resource handles cross callback boundary

## 4. API Design — RenderGraph Compile Phase

- [ ] 4.1 Specify dependency graph construction — topological sort from pass declarations
- [ ] 4.2 Specify dead pass culling — passes with no consumed outputs are pruned (unless marked `force_run`)
- [ ] 4.3 Specify barrier insertion algorithm — per-resource state tracking, batch at pass boundaries
- [ ] 4.4 Specify transient resource aliasing — lifetime intervals, heap offset packing (first-fit)
- [ ] 4.5 Specify aliasing barrier requirements — `D3D12_RESOURCE_ALIASING_BARRIER` placement
- [ ] 4.6 Specify descriptor allocation — per-pass CBV/SRV/UAV heap slices from ring allocator

## 5. API Design — RenderGraph Execute Phase

- [ ] 5.1 Specify `RenderGraph::execute(ID3D12CommandQueue*, FrameContext&)` signature
- [ ] 5.2 Specify transient resource physical allocation — committed vs. placed resources on aliasing heap
- [ ] 5.3 Specify pass execution order — respects topological sort from compile
- [ ] 5.4 Specify `RgPassContext` binding helpers — `bind_srv(slot, RgResource)`, `bind_rtv(slot, RgResource)`
- [ ] 5.5 Specify frame-end release — transient resources returned to heap; fence-tracked

## 6. Integration Design

- [ ] 6.1 Specify how existing GBuffer pass registers as a persistent producer
- [ ] 6.2 Specify swapchain backbuffer registration as persistent `RgResource`
- [ ] 6.3 Specify PSO cache integration — `RgPass` stores `PsoHandle`; cache looked up at compile time
- [ ] 6.4 Specify multi-frame history resources (TAA, DDGI) — double-buffered persistent resources
- [ ] 6.5 Specify async compute integration — separate `RgCommandListType::Compute` pass queue

## 7. Write Spec

- [ ] 7.1 Create `docs/specs/render-graph.md` — full spec with all sections from steps 2–6
- [ ] 7.2 Add ASCII diagram: pass dependency DAG example (GBuffer → Shadow → Lighting → Bloom → TAA → Present)
- [ ] 7.3 Add resource lifetime diagram — timeline of transient resource allocation/deallocation
- [ ] 7.4 Add barrier insertion table — state transitions per resource per pass boundary
- [ ] 7.5 Update `docs/specs/renderer.md` — add "Render Graph Integration" section linking to new spec
- [ ] 7.6 Update `docs/README.md` — add `render-graph.md` to specs table

## 8. Review

- [ ] 8.1 Verify all post-process passes from `screen-space-effects.md` can be expressed in the API
- [ ] 8.2 Verify DDGI (probe dispatch + irradiance update) can be expressed as compute passes
- [ ] 8.3 Verify VSM feedback loop (readback → page update) fits within the execute model
- [ ] 8.4 Cross-check with `shader-library.md` — `GlobalParams` cbuffer at b15 accessible inside pass callbacks
- [ ] 8.5 Confirm no breaking change to existing pyramid renderer code
