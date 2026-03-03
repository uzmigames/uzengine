## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-source-asset-db` is complete — `AssetCooker` and importer pipeline work
- [ ] 1.2 Confirm `phase0-async-streaming-design` spec is complete — `IoThreadPool` and `AsyncHandle` available
- [ ] 1.3 Confirm `phase1-render-graph` is complete — compute `RgPass` support
- [ ] 1.4 Read `docs/specs/asset-pipeline.md` — understand texture cook path (BC7 compression, DDS format)
- [ ] 1.5 Survey virtual texturing literature: Sean Barrett "Sparse Virtual Textures" (GDC 2011)
- [ ] 1.6 Determine tile size: 128×128 texels per page (standard choice — 64KB per RGBA8 tile)

## 2. .uvtex Offline Cook Format

- [ ] 2.1 Extend `uzpak` / `AssetCooker` — add `VirtualTextureImporter` for large textures
- [ ] 2.2 Cook process: input → mip pyramid → tile grid at each mip → BC7 compress each tile → write `.uvtex` binary
- [ ] 2.3 `.uvtex` binary format:
  - Header: virtual width, virtual height, mip count, tile_size (128), format (BC7)
  - Tile index: `uint64` offset per tile (sorted by mip → row → column)
  - Tile data: packed BC7 tile blobs
- [ ] 2.4 Register `.uvtex` in `AssetImporterRegistry` for cook pipeline
- [ ] 2.5 Source textures above `vt_min_resolution` (default 4096) automatically cooked as virtual texture

## 3. Physical Tile Cache

- [ ] 3.1 Create `src/engine/renderer/virtual_texture_manager.h/cpp`
- [ ] 3.2 Define physical tile cache: DX12 `Texture2DArray` with `tile_cache_width × tile_cache_height` tiles (default 64×64 = 4096 tiles)
- [ ] 3.3 Each tile: 128×128×4 bytes BC7 = 8 KB; total cache: 4096 × 8 KB = 32 MB VRAM
- [ ] 3.4 Implement `TileCache` LRU eviction: `oldest_frame_used` tracked per slot; evict least-recently-used when full
- [ ] 3.5 Implement `TileCache::upload(tile_data, slot)` — `ID3D12CommandList::CopyTextureRegion` into array slice

## 4. Page Table GPU Buffer

- [ ] 4.1 Define page table: `Texture2D<uint2>` at 1/128 of virtual texture resolution per mip
- [ ] 4.2 Each entry: `(physical_slot_x, physical_slot_y)` packed into 16 bits each
- [ ] 4.3 Register page table as persistent `RgResource`
- [ ] 4.4 Upload CPU-side page table changes each frame (dirty-page upload via upload heap)

## 5. Feedback Buffer

- [ ] 5.1 Create `RWTexture2D<uint2>` feedback buffer at 1/4 screen resolution
- [ ] 5.2 Create `shaders/vt_feedback.hlsl` — compute shader: for each pixel, compute required tile coordinates and mip level, write to feedback buffer
- [ ] 5.3 Register `VtFeedbackPass` as compute `RgPass` in render graph (before geometry pass)
- [ ] 5.4 Readback feedback buffer to CPU (2-frame delay, double-buffered)
- [ ] 5.5 Process readback: build `required_tiles` set from feedback entries

## 6. Async Tile Streaming

- [ ] 6.1 For each required tile not in cache: submit `IoThreadPool::submit(load_tile_fn) → AsyncHandle<TileData>`
- [ ] 6.2 `load_tile_fn`: open `.uvtex` file, seek to tile offset, read BC7 blob
- [ ] 6.3 On `AsyncHandle` ready: claim cache slot (LRU evict if needed), call `TileCache::upload`
- [ ] 6.4 Update page table entry for newly loaded tile
- [ ] 6.5 Streaming priority: tiles at lower mip (higher resolution) get higher `IoTask::priority`

## 7. Virtual Texture Sampling Library

- [ ] 7.1 Create `shaders/include/virtual_texture.hlsli`
- [ ] 7.2 Implement `SampleVirtualTexture(page_table, tile_cache, virtual_uv, mip_bias) -> float4`:
  - Compute virtual texel address from UV
  - Determine mip level from ddx/ddy
  - Look up page table entry → physical slot
  - Compute physical texel offset: `physical_uv = (slot_xy * 128 + local_texel) / cache_size`
  - Sample `tile_cache` at `physical_uv`
  - Apply mip transition blend (fade between mip levels to hide page loading)
- [ ] 7.3 Handle unloaded page: fall back to nearest loaded parent mip (always available — mip 0 guaranteed loaded)

## 8. Renderer Integration

- [ ] 8.1 Update PBR shader — `sample_albedo_vt` path using `SampleVirtualTexture` for entities with `CVirtualTexture`
- [ ] 8.2 `CVirtualTexture` component: `albedo_vt: string`, `normal_vt: string`, `roughness_vt: string` paths to `.uvtex` files
- [ ] 8.3 Bind page tables and tile cache array in geometry pass descriptor set

## 9. Testing

- [ ] 9.1 Unit test: `.uvtex` cook — verify tile count matches `ceil(width/128) × ceil(height/128) × mip_count`
- [ ] 9.2 Unit test: `TileCache` LRU eviction — insert 4097 tiles; first inserted is evicted
- [ ] 9.3 Unit test: `SampleVirtualTexture` returns correct value from known page table + tile cache state
- [ ] 9.4 Unit test: unloaded page fallback — returns parent mip color (not black/error)
- [ ] 9.5 Integration test: place `CVirtualTexture` on terrain mesh — tiles stream in, surface shows correct detail within 1 second
- [ ] 9.6 Performance test: 4096-tile cache, feedback processing completes in < 0.5ms CPU per frame

## 10. Documentation

- [ ] 10.1 Update `docs/specs/renderer.md` — "Virtual Texturing" section with tile cache, page table, feedback system
- [ ] 10.2 Update `docs/specs/asset-pipeline.md` — `.uvtex` format and cook settings
- [ ] 10.3 Create `docs/specs/virtual-texturing.md` — full spec: offline cook, tile streaming, shader integration
- [ ] 10.4 Update `docs/guides/asset-pipeline-guide.md` — "Using Virtual Textures" section
