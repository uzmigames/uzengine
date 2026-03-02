## 1. Prerequisites

- [ ] 1.1 Confirm `docs/specs/render-graph.md` exists and is complete (from `phase0-render-graph-design`)
- [ ] 1.2 Read `docs/specs/render-graph.md` — internalize all API decisions before writing any code
- [ ] 1.3 Read current `src/engine/renderer/renderer.cpp` — understand existing DX12 barriers and command list usage

## 2. RgResource System

- [ ] 2.1 Create `src/engine/renderer/rg_types.h` — `RgResource` handle (16-bit gen + 16-bit index), `RgResourceVersion`
- [ ] 2.2 Define `RgTextureDesc` struct — width, height, mip_count, array_size, format (DXGI_FORMAT), bind_flags (RTV/DSV/SRV/UAV)
- [ ] 2.3 Define `RgBufferDesc` struct — byte_size, stride, bind_flags (CBV/SRV/UAV)
- [ ] 2.4 Define `RgResourceEntry` — internal registry entry: desc union, lifetime interval [first_pass, last_pass], physical heap offset
- [ ] 2.5 Define `RgResourceRegistry` — vector of entries, `create_transient()`, `register_persistent()` APIs

## 3. RgPass

- [ ] 3.1 Create `src/engine/renderer/rg_pass.h` — `RgPassDesc`, `RgPass` struct
- [ ] 3.2 Implement `RgPass::read(RgResource, D3D12_RESOURCE_STATES)` — records read dependency, versioned
- [ ] 3.3 Implement `RgPass::write(RgResource, D3D12_RESOURCE_STATES)` — records write dependency, bumps version
- [ ] 3.4 Implement `RgPass::create_texture(RgTextureDesc) -> RgResource` — allocates transient resource
- [ ] 3.5 Implement `RgPass::create_buffer(RgBufferDesc) -> RgResource` — allocates transient resource
- [ ] 3.6 Implement `RgPass::force_run()` — marks pass as non-cullable (e.g., present pass)
- [ ] 3.7 Define pass callback signature: `void(RgPassContext&, ID3D12GraphicsCommandList4*)`

## 4. RgPassContext

- [ ] 4.1 Create `src/engine/renderer/rg_pass_context.h` — `RgPassContext` struct
- [ ] 4.2 Implement `bind_srv(register_slot, RgResource)` — binds SRV descriptor into per-pass heap slice
- [ ] 4.3 Implement `bind_uav(register_slot, RgResource)` — binds UAV descriptor
- [ ] 4.4 Implement `bind_rtv(slot, RgResource)` — sets render target view
- [ ] 4.5 Implement `bind_dsv(RgResource)` — sets depth-stencil view
- [ ] 4.6 Implement `get_physical(RgResource) -> ID3D12Resource*` — exposes underlying resource (for PSO binding)

## 5. RenderGraph Compile Phase

- [ ] 5.1 Create `src/engine/renderer/render_graph.h/cpp` — `RenderGraph` class
- [ ] 5.2 Implement `add_pass(RgPassDesc, setup_fn, execute_fn) -> RgPass&` — pass registration
- [ ] 5.3 Implement dependency graph construction: adjacency list from `read()`/`write()` declarations
- [ ] 5.4 Implement topological sort (Kahn's algorithm) — determine pass execution order
- [ ] 5.5 Implement dead pass culling — DFS from "force_run" passes; cull unreachable passes
- [ ] 5.6 Implement resource lifetime interval computation — `[first_read_pass, last_read_pass]` per resource
- [ ] 5.7 Implement transient resource aliasing — first-fit bin packing on aliasing heap offsets
- [ ] 5.8 Implement barrier insertion — per-resource state tracking, emit `D3D12_RESOURCE_BARRIER` arrays at pass boundaries
- [ ] 5.9 Implement aliasing barrier placement — `D3D12_RESOURCE_ALIASING_BARRIER` before first use of aliased resources
- [ ] 5.10 Implement descriptor heap slice allocation — per-pass CBV/SRV/UAV offset from ring allocator

## 6. RenderGraph Execute Phase

- [ ] 6.1 Implement `RenderGraph::execute(ID3D12CommandQueue*, FrameContext&)` — ordered pass dispatch
- [ ] 6.2 Implement transient resource physical allocation — `ID3D12Device::CreatePlacedResource` on aliasing heap
- [ ] 6.3 Implement per-pass barrier submission — `ID3D12GraphicsCommandList::ResourceBarrier` before callback
- [ ] 6.4 Implement pass callback invocation — construct `RgPassContext`, call execute_fn
- [ ] 6.5 Implement frame-end resource release — return transient resources to heap; fence-tracked cleanup

## 7. PSO Cache

- [ ] 7.1 Create `src/engine/renderer/pso_cache.h/cpp` — `PsoCache` singleton
- [ ] 7.2 Implement `PsoCache::get_graphics(ShaderVariantKey, D3D12_GRAPHICS_PIPELINE_STATE_DESC) -> ID3D12PipelineState*` — create or return cached PSO
- [ ] 7.3 Implement `PsoCache::get_compute(ShaderVariantKey, D3D12_COMPUTE_PIPELINE_STATE_DESC) -> ID3D12PipelineState*`
- [ ] 7.4 Implement PSO serialization cache: save/load compiled PSO blobs to `cache/pso/` directory
- [ ] 7.5 Implement cache invalidation: recompile when shader source hash changes

## 8. Renderer Integration

- [ ] 8.1 Refactor `renderer.cpp` — replace raw barrier calls with `RenderGraph` pass declarations
- [ ] 8.2 Register GBuffer pass as `RgPass` (geometry → GBuffer textures)
- [ ] 8.3 Register lighting pass as `RgPass` (GBuffer + shadow map → HDR color)
- [ ] 8.4 Register present pass as `RgPass` (HDR → swapchain, `force_run()`)
- [ ] 8.5 Register swapchain backbuffer and depth buffer as persistent `RgResource`
- [ ] 8.6 Verify existing pyramid renders correctly after refactor

## 9. Testing

- [ ] 9.1 Unit test: `RenderGraph::compile` — topological sort correctness with 5-pass chain
- [ ] 9.2 Unit test: dead pass culling — unreferenced pass excluded from execution order
- [ ] 9.3 Unit test: resource lifetime intervals — verify `[first, last]` computed correctly
- [ ] 9.4 Unit test: aliasing heap packing — two non-overlapping transient resources share heap offset
- [ ] 9.5 Unit test: barrier insertion — state transition emitted exactly once per edge
- [ ] 9.6 Integration test: render graph executes pyramid frame, DX12 debug layer reports zero errors
- [ ] 9.7 Integration test: `PsoCache` returns same `ID3D12PipelineState*` on second identical request
- [ ] 9.8 Integration test: aliased resources produce correct pixel output (no corruption)

## 10. Documentation

- [ ] 10.1 Update `docs/specs/renderer.md` — add "Render Graph Integration" section
- [ ] 10.2 Update `docs/modules/renderer/README.md` — document `RenderGraph`, `PsoCache` classes
- [ ] 10.3 Update `docs/engine/cpp-architecture.md` — render graph in architecture diagram
