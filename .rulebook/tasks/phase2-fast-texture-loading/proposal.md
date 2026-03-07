# Proposal: Fast Texture Loading Pipeline

## Purpose

Eliminate catastrophic texture loading performance bottleneck: each texture upload currently creates a new DX12 command allocator + command list + staging buffer and calls `wait_for_gpu()` (full GPU stall). With 50 textures in a trivial PBR scene, this causes ~850ms of load time. A game with thousands of assets in a single level would freeze for many seconds.

This task implements proven patterns from Unreal Engine 5 and Godot 4, targeting a 50x+ speedup.

## Architecture Research (UE5 + Godot 4)

### Unreal Engine 5 Patterns (enterprise-grade, ~10000+ lines)
1. **FStreamingManager** -- Multi-frame amortized updates (5 frames), async mip calc task
2. **Priority System** -- Screen-space size x distance x boost factor x LOD group
3. **IoDispatcher** -- Priority-based async I/O, batch reads, zero-copy to upload heap
4. **Strategy Pattern** -- FTextureMipDataProvider (I/O) + FTextureMipAllocator (GPU) decoupled
5. **Upload Heap Pages** -- 4MB max (D3D12 perf cliff above 4MB), page pool with reuse
6. **Per-Mip Streaming** -- Only loads mip levels actually needed, min-resident mips always in GPU
7. **Memory Budget** -- Pool size limit, eviction by retention priority
8. **Virtual Texturing** -- GPU feedback buffer, tile-level streaming (Phase 4+ for UzEngine)

### Godot 4 Patterns (lean, ~1200 lines total)
1. **WorkerThreadPool** -- ResourceLoader submits stbi_load to shared thread pool
2. **Two-Phase RID** -- texture_allocate() returns handle instantly, texture_initialize() deferred
3. **CommandQueueMT** -- Linear command buffer for thread-safe render commands (~300 lines)
4. **Staging Ring** -- Frame-tracked blocks, dynamic growth, segmented uploads for >4MB textures
5. **Proxy Textures** -- texture_replace() swaps data without rebinding materials
6. **Default Textures** -- Pre-created WHITE/BLACK/NORMAL defaults as placeholders

### UzEngine Adaptation Strategy
- **Phase 0**: Staging ring frame tracking + async CPU decode on worker threads (Godot pattern)
- **Phase 1**: Pre-compressed .uztex format (skip stbi, GPU-ready BC7/BC5 data)
- **Phase 2**: Per-mip streaming + memory budget (simplified UE5 pattern)
- **Phase 3**: Virtual Texturing (separate task: phase9-virtual-texturing)

## What Changes

### Phase 0: Async Decode + Staging Ring Improvements (P0 -- highest impact)

**RHI Layer (rhi_dx12.c):**
- Add `frame_used` tracking per staging block (Godot pattern: recycle after frame N-2)
- Staging ring already exists (8x4MB blocks) -- add frame recycling

**TextureManager (material.cpp → texture_manager.h/cpp):**
- Move `stbi_load()` to worker threads via `std::async` / `std::thread` pool
- Main thread `tick()` only polls completed decodes + submits GPU batch upload
- Proxy pattern: allocate RhiTexture handle immediately, bind placeholder texture, swap when ready
- Eliminate O(N) material re-scan (`refresh_textures()`) via in-place handle update

**Placeholder textures:**
- Pre-create 1x1 defaults: WHITE (albedo), FLAT_BLUE (normal), BLACK (displacement)
- Materials bind placeholders immediately; real textures swap in asynchronously

### Phase 1: Pre-Compressed Format (.uztex) -- deferred
- `.uztex` binary format: header + per-mip BC7/BC5 compressed data
- Import tool converts PNG/JPG -> .uztex at asset time (DirectXTex texconv)
- Skip stbi_load entirely for .uztex files
- 4x less upload data (2K BC7 = 4MB vs RGBA8 = 16MB)

### Phase 2: Per-Mip Streaming + Budget -- deferred
- Asset pipeline generates full mipchain
- Mip 0-1 inline (always resident, loads instantly)
- Higher mips streamed on demand by screen-size priority (simplified UE5 pattern)
- VRAM budget with eviction by retention priority

## Impact

- **Performance**: Scene load 50x faster (Phase 0), 200x faster (Phase 0+1)
- **Affected files**: `rhi_dx12.c`, `rhi_dx12_internal.h`, `rhi.h`, `texture.h`, `texture.cpp`, `material.cpp`
- **No breaking changes**: existing PNG/JPG path preserved as fallback
- **Specs affected**: `renderer.md`, `rhi-abstraction.md`
- **Based on**: Unreal Engine 5 source analysis + Godot 4 source analysis
