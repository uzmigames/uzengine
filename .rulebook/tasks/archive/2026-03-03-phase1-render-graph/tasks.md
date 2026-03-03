## 1. Prerequisites

- [x] 1.1 `docs/specs/rendering/render-graph.md` exists and complete (992 lines)
- [x] 1.2 Read spec — internalized all API decisions
- [x] 1.3 Read existing renderer code — understood DX12 barrier usage

## 2. RgResource System (`src/engine/renderer/render_graph.h/.cpp`)

- [x] 2.1 `RgTextureHandle` / `RgBufferHandle` (typed `uint32_t` indices), `kRgHandleInvalid = UINT32_MAX`
- [x] 2.2 `RgTextureDesc` struct — width, height, mip_count, format (RgFormat), name[64]
- [x] 2.3 `RgBufferDesc` struct — byte_size, stride, name[64]
- [x] 2.4 `RgFormat` enum — Unknown, RGBA8, RGBA16F, R32F, D32F, RG16F, BC7
- [x] 2.5 Internal resource registry with kind tagging (Texture/Buffer), imported/persistent flags

## 3. RgPass

- [x] 3.1 `RgPassType` enum — Graphics, Compute, Copy
- [x] 3.2 `RgPassBuilder::read(RgTextureHandle)` — declare read dependency
- [x] 3.3 `RgPassBuilder::write(RgTextureHandle)` — declare write dependency
- [x] 3.4 `RgPassBuilder::read_buffer(RgBufferHandle)` / `write_buffer(RgBufferHandle)` — buffer deps
- [x] 3.5 `RgPassBuilder::force_run()` — prevent culling of passes with no consumed outputs

## 4. RenderGraph Core

- [x] 4.1 `RenderGraph::create_texture(RgTextureDesc)` — register transient texture
- [x] 4.2 `RenderGraph::create_buffer(RgBufferDesc)` — register transient buffer
- [x] 4.3 `RenderGraph::import_texture(RgTextureHandle, name)` / `import_buffer()` — persistent resources
- [x] 4.4 `RenderGraph::add_pass(name, type, setup_fn, execute_fn)` — register pass with dependency builder
- [x] 4.5 PIMPL pattern, non-copyable, movable

## 5. Compile Phase (Kahn's Algorithm + Liveness)

- [x] 5.1 Build writer/reader maps per resource
- [x] 5.2 Build forward adjacency + in-degrees from pass dependencies
- [x] 5.3 Kahn's topological sort with cycle detection
- [x] 5.4 Backward liveness propagation from output frontier (force_run + writes-to-imported/persistent)
- [x] 5.5 Build execution order (topo order filtered to live passes)
- [x] 5.6 Compute per-resource lifetime [first_use, last_use]

## 6. Execute Phase

- [x] 6.1 `RenderGraph::execute(RhiCmdList)` — iterates execution order, calls `execute_fn` for each live pass
- [x] 6.2 Culled passes are skipped; live passes called in topological order

## 7. Query/Diagnostics

- [x] 7.1 `pass_count()`, `live_pass_count()`, `execution_order()` — inspection APIs
- [x] 7.2 `is_pass_culled(name)`, `cull_reason(name)` — per-pass diagnostics
- [x] 7.3 `pass_name(index)` — pass name by execution index

## 8. Constants

- [x] 8.1 `kRgMaxPasses = 64`, `kRgMaxResources = 128`

## 9. Tests (`tests/renderer/test_render_graph.cpp`)

- [x] 9.1 64 tests across 5 suites: construction, resources, passes, culling, execution, reset, imports, edge cases, move semantics, diagnostics, stress test, multi-frame lifecycle
- [x] 9.2 All 64 tests pass — 1117/1117 total after integration

## 10. CMakeLists.txt Integration

- [x] 10.1 `src/engine/renderer/render_graph.cpp` added to `UzEngineTests`
- [x] 10.2 `tests/renderer/test_render_graph.cpp` added to `UzEngineTests`

## Deferred (from original proposal — requires DX12 environment)

- [ ] D1. Physical DX12 heap allocation for transient resources (requires Windows + D3D12)
- [ ] D2. Actual barrier emission via `rhi()->cmd_barrier_texture()` (requires RHI backend)
- [ ] D3. Transient resource aliasing (heap offset packing, first-fit) — Phase 4
- [ ] D4. Descriptor allocation (per-pass CBV/SRV/UAV heap slices) — Phase 4
- [ ] D5. Integration with GBuffer/lighting passes in renderer — Phase 3
