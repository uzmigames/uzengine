# Tasks: Fast Texture Loading Pipeline

## Phase 0: Async Decode + Batched Upload (P0 -- highest impact)

### 0A. Staging Ring Frame Tracking (RHI layer)

- [x] 0A.1 Add `frame_used` field to `Dx12StagingBlock` for per-block frame tracking
- [x] 0A.2 Update `dx12_staging_ring_alloc()` to stamp `frame_used = current_frame` on alloc
- [x] 0A.3 Add frame-based recycling: blocks from frame N-2 safe to reuse without stall
- [x] 0A.4 Add `dx12_staging_ring_begin_frame(uint64_t frame_number)` to advance frame counter

### 0B. Async CPU Decode on Worker Threads

- [x] 0B.1 Create `src/engine/renderer/resources/texture_loader.h` -- AsyncTextureLoader class
- [x] 0B.2 Implement worker thread pool (2 threads) for `stbi_load()` / `decode_texture_from_file()`
- [x] 0B.3 `submit(path)` queues path; decode runs on worker thread
- [x] 0B.4 `poll_completed()` returns vector of completed DecodeResult (path, pixels, w, h)
- [x] 0B.5 Thread-safe queue: worker pushes to completed queue, main thread drains it

### 0C. TextureManager Async Pipeline

- [x] 0C.1 Refactor `TextureManager::tick()` -- remove main-thread `decode_texture_from_file()` call
- [x] 0C.2 `get_or_queue()` submits directly to AsyncTextureLoader (no intermediate queue)
- [x] 0C.3 `tick()` polls completed decodes, creates RhiTexture, adds to pending batch
- [x] 0C.4 `tick()` flushes pending batch via `texture_upload_batch()`
- [x] 0C.5 Deferred results: excess decoded textures stashed for next frame (budget limiting)
- [x] 0C.6 Proxy pattern: `get_or_queue()` returns placeholder handle immediately (not RHI_NULL)
- [x] 0C.7 Pre-create placeholder textures: 1x1 WHITE, 1x1 FLAT_BLUE (0x8080FF), 1x1 BLACK
- [x] 0C.8 Eliminate `refresh_textures()` O(N) scan via `texture_replace()` in-place swap

### 0D. Verification

- [x] 0D.1 Scene loads with zero per-texture GPU stalls (only 1 batch fence per tick)
- [x] 0D.2 Textures pop in visually (shader defaults -> real) within 1-2 frames
- [x] 0D.3 No main-thread stbi_load calls (all decode on worker threads)
- [x] 0D.4 All 1378 tests pass (zero regressions, 14 async loader + 5 benchmarks)
- [x] 0D.5 Benchmark: tick() avg 0.945ms for 50 textures (< 1ms target met)

## Phase 1: Proxy Replace + refresh_textures Elimination — COMPLETE

- [x] 1.1 Add `texture_replace()` to RHI vtable (swap D3D12 resource in existing slot)
- [x] 1.2 `get_or_queue()` creates unique 1x1 handle per path (not shared placeholder)
- [x] 1.3 `tick()` calls `texture_replace()` on existing handle with real dimensions
- [x] 1.4 `refresh_textures()` → no-op (materials auto-see updated textures via same handle)
- [x] 1.5 Removed refresh_textures + cache_material calls from renderer loop
- [x] 1.6 All 1376 tests pass + 2 skipped (zero regressions)

## Deferred to Phase 2: Pre-Compressed Format (.uztex)

- [ ] 2.1 Define `.uztex` binary format (header + per-mip BC7/BC5 compressed data)
- [ ] 2.2 Create import tool (DirectXTex texconv wrapper) to convert PNG/JPG -> .uztex
- [ ] 2.3 Add `.uztex` loader in `texture.cpp` (skip stbi, upload compressed data directly)
- [ ] 2.4 Add BC7/BC5/BC6H format support to `texture_create` + `texture_upload_batch`
- [ ] 2.5 Convert all project textures to .uztex
- [ ] 2.6 Update material files to reference .uztex paths

## Deferred to Phase 3: Per-Mip Streaming + Budget

- [ ] 3.1 Asset pipeline generates full mipchain at import time
- [ ] 3.2 Mip 0-1 stored inline (always resident, instant load)
- [ ] 3.3 StreamingTexture metadata: resident_mips, wanted_mips, screen_size_priority
- [ ] 3.4 Per-frame streaming manager: compute wanted mips from camera distance
- [ ] 3.5 Stream individual mip levels via `texture_upload_subresource()`
- [ ] 3.6 VRAM budget tracker: pool size limit, eviction by retention priority
- [ ] 3.7 `gpu_memory_info()` integration for budget enforcement

## Testing

- [x] T1. Existing material system tests updated for async loading (17/17 pass)
- [x] T2. All 1378 tests pass with zero regressions
- [x] T3. Unit test: AsyncTextureLoader submit/poll lifecycle (10 tests)
- [x] T4. Unit test: TextureManager placeholder + free functions (3 tests)
- [x] T5. Unit test: AsyncTextureLoader concurrent workers, shutdown safety, thread clamping
- [x] T6. Benchmark: 50-texture throughput (async decode, tick simulate, end-to-end, scaling)
- [x] T7. MaterialSystemTest updated: expects non-null handle from get_or_queue
- [x] T8. texture_clear_all() exposed for clean test isolation
- [ ] T9. Integration test: scene load with async texture pipeline
- [ ] T10. Performance test: real GPU timing with production textures

## Documentation

- [ ] D1. Update `docs/architecture/rhi-abstraction.md` with staging ring frame tracking + texture_replace
- [ ] D2. Update `docs/specs/renderer.md` with async texture loading pipeline
- [ ] D3. Add architecture decision record: UE5 vs Godot pattern choices
