## 1. Prerequisites

- [ ] 1.1 Confirm `phase1-render-graph` is complete — `RgPass` API works
- [ ] 1.2 Confirm `phase2-shadow-maps` is complete — understand existing CSM implementation to replace
- [ ] 1.3 Read `docs/specs/renderer.md` — understand GBuffer, depth pre-pass, lighting pass structure
- [ ] 1.4 Survey VSM literature: Epic Games "Virtual Shadow Maps" (Unreal Engine 5 tech presentation)
- [ ] 1.5 Determine atlas size: default 8192×8192, 16×16 texel pages → 512×512 page grid (256K pages)

## 2. VSM Atlas

- [ ] 2.1 Create `src/engine/renderer/passes/vsm_atlas.h/cpp`
- [ ] 2.2 Define atlas texture: `DXGI_FORMAT_D16_UNORM` or `R16_UINT` stored depth, 8192×8192
- [ ] 2.3 Define page size: 16×16 texels (adjustable via `.uzproj`)
- [ ] 2.4 Implement `VsmAtlas::allocate_page() -> PageIndex` — find free page from freelist
- [ ] 2.5 Implement `VsmAtlas::free_page(PageIndex)` — return to freelist
- [ ] 2.6 Implement `VsmAtlas::clear_page(PageIndex, cmd_list)` — clear to max depth before render

## 3. Page Table GPU Buffer

- [ ] 3.1 Define `VsmPageTableEntry` — 32-bit: `{physical_page_index: 20 bits, flags: 12 bits}` (flags: allocated, dirty, cached)
- [ ] 3.2 Create page table GPU buffer: `StructuredBuffer<uint>` sized to match virtual page grid (one entry per virtual page)
- [ ] 3.3 Implement CPU-side page table mirror — sync to GPU each frame with dirty-page upload
- [ ] 3.4 Register page table buffer as persistent `RgResource` in RenderGraph

## 4. Feedback Buffer

- [ ] 4.1 Define feedback buffer: `RWStructuredBuffer<uint>` on GPU — set page required flag during `vsm_page_mark.hlsl` pass
- [ ] 4.2 Implement `VsmFeedbackPass` as `RgPass` (compute): runs before shadow rendering, marks which pages are needed
- [ ] 4.3 Implement CPU readback: copy feedback buffer to CPU readback heap, read after 2-frame delay (double-buffered)
- [ ] 4.4 Implement page allocation: for each required page not yet allocated, call `VsmAtlas::allocate_page()` and update page table

## 5. Page Mark Shader

- [ ] 5.1 Create `shaders/vsm_page_mark.hlsl` — compute shader
- [ ] 5.2 Input: depth buffer, light ViewProj matrix, page table size, atlas page size
- [ ] 5.3 For each screen pixel: unproject to shadow space, compute virtual page XY, set bit in feedback buffer
- [ ] 5.4 Support directional light (cascades → virtual page grid), point lights (cubemap faces → 6 grids), spot lights (single projection grid)

## 6. VSM Render Pass

- [ ] 6.1 Create `src/engine/renderer/passes/vsm_pass.h/cpp` — `VsmRenderPass` as `RgPass`
- [ ] 6.2 Implement per-page render: for each newly allocated or dirty page, render scene geometry into that 16×16 region of the atlas
- [ ] 6.3 Implement page-aware draw call culling: skip meshes whose shadow bounds don't overlap the page being rendered
- [ ] 6.4 Use existing mesh render pipeline with shadow-specific VS+PS (no PBR shading)
- [ ] 6.5 Implement page caching: mark cached pages as clean; skip render for frames where no casters moved

## 7. VSM Sample Shader Library

- [ ] 7.1 Create `shaders/include/vsm_sample.hlsli`
- [ ] 7.2 Implement `SampleVsm(world_pos, light_index, page_table, vsm_atlas) -> float shadow_factor`:
  - Transform world_pos to shadow NDC
  - Compute virtual page XY
  - Look up `page_table[page_XY]` → physical page index
  - Compute texel offset within page
  - Sample `vsm_atlas` at `(physical_page.xy * page_size + texel_offset)`
  - Return 0 (shadowed) or 1 (lit) with 2×2 PCF
- [ ] 7.3 Handle unallocated pages: return 1.0 (lit — conservative)

## 8. Lighting Pass Integration

- [ ] 8.1 Update `shaders/lighting.hlsl` — replace CSM sample with `SampleVsm()` call
- [ ] 8.2 Bind `page_table` buffer and `vsm_atlas` SRV in lighting pass descriptor set
- [ ] 8.3 Remove old CSM render targets (or keep as fallback behind `#define USE_VSM`)

## 9. Testing

- [ ] 9.1 Unit test: `VsmAtlas::allocate_page` — 3 allocations → 3 unique page indices
- [ ] 9.2 Unit test: feedback readback — page mark shader writes to correct feedback entries
- [ ] 9.3 Unit test: page table lookup — `SampleVsm` finds physical page for known virtual page
- [ ] 9.4 Unit test: page caching — static scene, second frame renders zero pages (cache hit)
- [ ] 9.5 Integration test: directional light VSM — shadow visible on floor, sharp edges at close range
- [ ] 9.6 Integration test: DX12 validation layer reports zero errors with full VSM pass chain
- [ ] 9.7 Visual test: compare shadow quality to CSM baseline at 50m distance — VSM visibly sharper

## 10. Documentation

- [ ] 10.1 Update `docs/specs/renderer.md` — "Virtual Shadow Maps" section replaces CSM section
- [ ] 10.2 Update `docs/specs/point-spot-light-shadows.md` — VSM integration for point/spot
- [ ] 10.3 Update `docs/guides/shader-authoring.md` — how to use `vsm_sample.hlsli` in custom lighting passes
