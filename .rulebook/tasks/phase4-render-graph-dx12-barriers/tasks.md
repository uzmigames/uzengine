## Context

Extends `phase1-render-graph` (C++ DAG core: passes, culling, topological sort).
Adds actual GPU integration: DX12/RHI barrier emission, transient resource aliasing,
descriptor allocation, and integration with the GBuffer/lighting passes.

## 1. Prerequisites

- [ ] 1.1 `phase3-pbr-renderer` complete (GBuffer + lighting passes exist)
- [ ] 1.2 RHI DX12 backend operational on Windows (`rhi_dx12.c` wired up)
- [ ] 1.3 Read `docs/specs/rendering/render-graph.md` sections: barrier insertion, aliasing

## 2. Physical Resource Allocation

- [ ] 2.1 `RenderGraph::compile()` — call `rhi()->texture_create()` for transient textures
- [ ] 2.2 `RenderGraph::compile()` — call `rhi()->buffer_create()` for transient buffers
- [ ] 2.3 Track `RhiTexture`/`RhiBuffer` handles per resource for lifetime [first_use, last_use]
- [ ] 2.4 `RenderGraph::reset()` — call `rhi()->texture_destroy()` / `buffer_destroy()` for transients
- [ ] 2.5 Tests: resource handles non-null after compile, destroyed after reset

## 3. RHI Barrier Emission

- [ ] 3.1 Per-resource state tracking: `std::unordered_map<uint32_t, RhiResourceState>` at compile
- [ ] 3.2 Before each pass: emit `rhi()->cmd_barrier_texture()` for reads (transition to SRV)
- [ ] 3.3 Before each pass: emit `rhi()->cmd_barrier_texture()` for writes (transition to RTV/UAV/DSV)
- [ ] 3.4 `rhi()->cmd_barrier_buffer()` for buffer reads/writes
- [ ] 3.5 Batch barriers per pass boundary (single API call with array of barriers)
- [ ] 3.6 Tests: barrier called correct number of times, states correct

## 4. Transient Resource Aliasing

- [ ] 4.1 Compute lifetime intervals `[first_use, last_use]` per transient resource
- [ ] 4.2 First-fit packing: assign heap offsets to non-overlapping resources
- [ ] 4.3 Create single placed heap large enough for peak aliased usage
- [ ] 4.4 `D3D12_RESOURCE_ALIASING_BARRIER` inserted when aliased resources swap
- [ ] 4.5 Tests: VRAM usage reduced vs non-aliased, aliasing barriers present

## 5. Descriptor Allocation (per-pass CBV/SRV/UAV)

- [ ] 5.1 Ring allocator for per-frame descriptor heap slices
- [ ] 5.2 `RgPassContext::bind_srv(slot, RgTextureHandle)` — allocate SRV descriptor
- [ ] 5.3 `RgPassContext::bind_uav(slot, RgTextureHandle)` — UAV descriptor
- [ ] 5.4 `RgPassContext::bind_rtv(slot, RgTextureHandle)` — RTV descriptor
- [ ] 5.5 Reset ring allocator at frame start
- [ ] 5.6 Tests: descriptors allocated correctly, ring wraps

## 6. Renderer Integration

- [ ] 6.1 Replace ad-hoc GBuffer barrier code with RenderGraph pass declarations
- [ ] 6.2 Register swapchain backbuffer as imported `RgTextureHandle`
- [ ] 6.3 GBuffer pass: declare writes (albedo, normal, depth)
- [ ] 6.4 Lighting pass: declare reads (GBuffer), write (HDR color)
- [ ] 6.5 Tonemap pass: declare read (HDR), write (swapchain)
- [ ] 6.6 Tests: pyramid renders correctly through render graph

## 7. Tests

- [ ] 7.1 ≥95% line + branch coverage on new barrier/aliasing/descriptor code
- [ ] 7.2 No regressions in existing 64 render graph core tests
- [ ] 7.3 Windows GPU integration test: pyramid frame completes without D3D12 errors
