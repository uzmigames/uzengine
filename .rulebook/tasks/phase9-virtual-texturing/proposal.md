# Proposal: Virtual Texturing (SVT)

## Why

Open-world terrain, large architectural surfaces, and photogrammetry assets require extremely high-resolution textures (16K-64K). Storing these fully in GPU memory is impossible on any current hardware. Virtual texturing solves this by dividing textures into 128x128 tiles and streaming only the tiles visible in the current frame -- similar to Virtual Shadow Maps but for albedo/normal/roughness textures. The result: effectively unlimited texture resolution with GPU memory proportional to visible tile count.

## Architecture Reference: Unreal Engine 5 SVT

UE5's `FVirtualTextureSystem` (at `Engine/Source/Runtime/Renderer/Private/VT/`) implements:

1. **Feedback pass**: Shaders write which VT pages (tile + mip) they sampled into a feedback buffer
2. **Feedback analysis**: CPU reads back feedback buffer, deduplicates into `FUniquePageList`
3. **Tile request management**: Compare requested pages vs resident pages in physical atlas
4. **Producer system**: `IVirtualTexture` producers generate page data (disk, runtime RVT, etc.)
5. **Physical space**: Pages uploaded to tiled physical texture atlas (`FVirtualTexturePhysicalSpace`), LRU eviction
6. **Page table**: Indirection texture maps virtual page coords to physical atlas coords
7. **Throttling**: `MaxRVTPageUploads`/`MaxSVTPageUploads` per frame, parallel feedback tasks

Key UE5 design decisions:
- Feedback buffer at 1/4 screen resolution (sufficient for tile-level granularity)
- LRU eviction per physical space (not global)
- Page table as GPU texture for O(1) shader lookup
- Separate throttle for runtime VT (RVT) vs streaming VT (SVT)

## What Changes

- **Offline**: `uzpak` extension transcodes source textures into tiled `.uvtex` format
- **`src/engine/renderer/virtual_texture_manager.h/cpp`** -- tile cache, page table, feedback processing
- **`shaders/vt_feedback.hlsl`** -- compute shader marking required tiles during geometry pass
- **`shaders/vt_resolve.hlsl`** -- resolve virtual address to physical tile address in pixel shader
- **`shaders/include/virtual_texture.hlsli`** -- helper functions for sampling virtual textures

## Prerequisites

- `phase2-fast-texture-loading` Phase 0 (staging ring + async I/O)
- `phase1-render-graph` (compute RgPass support)
- `phase2-source-asset-db` (AssetCooker + importer pipeline)

## Impact

- Affected specs: `renderer.md`, `asset-pipeline.md`
- Breaking change: NO -- opt-in via `CVirtualTexture` component; regular textures still work
- User benefit: photorealistic terrain at any zoom level; no texture resolution budget
