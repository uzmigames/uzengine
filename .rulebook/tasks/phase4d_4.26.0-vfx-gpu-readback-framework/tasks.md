# Phase 4d — 4.26.0 VFX GPU Readback Framework

Unified GPU→CPU readback system for VFX. Manages a ring of staging buffers with
N-frame latency, dispatches copy commands, and invokes typed callbacks when data
is available on the CPU. Replaces ad-hoc readback in LightRenderModule and serves
as the foundation for dynamic AABB, death events, gameplay queries, etc.

---

## 1. Core Readback Manager

- [ ] 1.1 Create `runtime/engine/vfx/core/vfx_readback.h` — `VFXReadbackManager` class:
      `Create(device, maxRequestsPerFrame, ringSize=3)`, `Destroy()`,
      `RequestReadback(srcBuffer, byteOffset, byteSize, callbackFn)`,
      `BeginFrame(frameIndex)`, `EndFrame(cmdList)`, `ProcessCallbacks()`.
- [ ] 1.2 `ReadbackRequest` struct: `RhiBufferHandle src; uint32_t offset; uint32_t size;
      uint32_t ringSlot; std::function<void(const void* data, uint32_t size)> callback;`.
- [ ] 1.3 Ring buffer of staging heaps: `std::array<StagingFrame, 3>` where each
      `StagingFrame` has a single large READBACK buffer (e.g., 64KB) with a bump allocator.
      Requests within a frame suballocate from the same staging buffer.
- [ ] 1.4 `EndFrame()` issues CopyBufferRegion commands for all requests in the current
      frame's staging slot. Advances ring index.
- [ ] 1.5 `ProcessCallbacks()` maps the staging buffer from `ringIndex - ringSize` (oldest
      complete frame), invokes each request's callback with a pointer to the mapped data,
      then unmaps. Callbacks execute on the calling thread (main thread).

## 2. Fence Synchronization

- [ ] 2.1 Each `StagingFrame` tracks a fence value. `EndFrame()` signals the fence after
      copy commands. `ProcessCallbacks()` waits on the oldest frame's fence before mapping.
- [ ] 2.2 Use the existing frame fence from the renderer (avoid creating a separate fence
      per readback manager). Accept `uint64_t fenceValue` in `EndFrame()`.
- [ ] 2.3 If the oldest frame's fence is not yet signaled when `ProcessCallbacks()` is called,
      skip processing (non-blocking). Data arrives next frame instead of stalling the CPU.

## 3. Integration Points

- [ ] 3.1 Create `VFXReadbackManager` instance in `VFXSystem` or `FrameParticles`.
      Call `BeginFrame()` at frame start, `EndFrame()` after all VFX dispatches,
      `ProcessCallbacks()` before the next frame's simulation.
- [ ] 3.2 Migrate `LightRenderModule`'s existing readback to use `RequestReadback()`:
      replace manual staging buffer + map/unmap with a single callback that populates
      the light data array.
- [ ] 3.3 Wire `BoundsBuffer` readback (from 4.25.0) through the framework: register
      a readback request for the 32-byte bounds UAV, callback populates `EmitterBounds`.

## 4. Capacity & Limits

- [ ] 4.1 Default staging buffer size: 64KB per ring slot (supports ~1000 small readbacks
      or a few large ones per frame). Log warning if a frame exceeds capacity.
- [ ] 4.2 `maxRequestsPerFrame` default: 64. Requests beyond the limit are dropped with
      `UZ_LOG(VFX, Warn)`.
- [ ] 4.3 Total VRAM overhead: 3 × 64KB = 192KB (negligible).

## 5. Tests

- [ ] 5.1 Unit test: ring buffer index wrapping (frame 0,1,2,3,4 → slots 0,1,2,0,1).
- [ ] 5.2 Unit test: bump allocator suballocation (3 requests of 32B, 64B, 128B fit in 64KB).
- [ ] 5.3 Unit test: capacity overflow drops requests gracefully.
- [ ] 5.4 Integration test: write known data to GPU buffer, request readback, verify callback
      receives correct data after ringSize frames.
