# Phase 4d — 4.27.0 VFX Emitter Pool / GPU Resource Recycling

Pre-allocate GPU particle buffer slots and recycle them when emitters are destroyed.
Eliminates per-emitter GPU heap allocation, turning create/destroy into O(1) slot
assignment. Niagara reference: `FNiagaraGPUInstanceCountManager` pooled data buffers.

---

## 1. Pool Data Structure

- [x] 1.1 Created `runtime/engine/vfx/core/vfx_emitter_pool.h` — `VFXEmitterPool` class:
      `Create(device, poolSize, defaultCapacity)`, `Destroy()`,
      `AcquireSlot(requestedCapacity) → GPUParticleBuffer*`,
      `ReleaseSlot(GPUParticleBuffer*)`.
- [x] 1.2 Internal storage: `std::vector<PoolSlot>` where each `PoolSlot` has a
      `GPUParticleBuffer buffer; uint32_t capacity; bool inUse;`.
- [x] 1.3 Free list: `std::vector<uint32_t> freeSlots_` (indices into the pool).
      `AcquireSlot()` pops from freeSlots, `ReleaseSlot()` pushes back.
- [x] 1.4 Default pool: 32 slots (8x1K + 8x4K + 8x8K + 4x16K + 2x32K + 2x64K).
      Configurable via `VFXSystemConfig`.

## 2. Slot Reset (No GPU Dealloc)

- [ ] 2.1 `ReleaseSlot()` does NOT call `GPUParticleBuffer::Destroy()`. Instead calls
      `ResetSlot()`: sets alive_count=0, refills free_list with [0..capacity-1],
      clears id_to_index to 0xFFFFFFFF.
- [ ] 2.2 `ResetSlot()` uses a single `UpdateSubresource` or `CopyBufferRegion` from
      a pre-computed "initial state" buffer to reset the free_list and counters.
      Avoids per-slot CPU loop over capacity elements.
- [ ] 2.3 Sort buffers (if allocated) are also retained in the slot — no reallocation.

## 3. Capacity Matching

- [ ] 3.1 `AcquireSlot(requestedCapacity)` finds the smallest free slot with
      `capacity >= requestedCapacity`. If no slot is large enough, falls back to
      dynamic `GPUParticleBuffer::Create()` (logged as warning).
- [ ] 3.2 Pool slots are created at fixed capacity tiers: 1K, 4K, 8K, 16K, 32K, 64K.
      Distribution configurable (e.g., 8×1K, 8×4K, 8×8K, 4×16K, 2×32K, 2×64K = 32 slots).
- [ ] 3.3 Emitters requesting > 64K particles always use dynamic allocation (rare case).

## 4. VFXSystem Integration

- [ ] 4.1 Create `VFXEmitterPool` in `VFXSystem::Init()` with configured pool size.
- [ ] 4.2 Route `VFXSystem::SpawnEmitter()` through `pool.AcquireSlot()` instead of
      `GPUParticleBuffer::Create()`.
- [ ] 4.3 Route emitter destruction through `pool.ReleaseSlot()` instead of
      `GPUParticleBuffer::Destroy()`.
- [ ] 4.4 On `VFXSystem::Shutdown()`, destroy all pool slots (actual GPU dealloc).

## 5. Stats & Debug

- [ ] 5.1 Track pool stats: `totalSlots`, `inUseSlots`, `dynamicOverflowCount`,
      `peakInUse`. Expose via `VFXEmitterPool::GetStats()`.
- [ ] 5.2 Display pool stats in VFX stats overlay: "Pool: 12/32 in use, 0 overflow".
- [ ] 5.3 Log warning when pool is > 80% full: `UZ_LOG(VFX, Warn, "Emitter pool %u/%u")`.

## 6. Tests

- [ ] 6.1 Unit test: acquire 32 slots, all succeed, 33rd returns nullptr (dynamic fallback).
- [ ] 6.2 Unit test: acquire + release + re-acquire returns same slot (recycled).
- [ ] 6.3 Unit test: capacity tier matching (request 5000 → gets 8K slot, not 1K).
- [ ] 6.4 Unit test: release resets alive_count to 0 without destroying GPU buffers.
- [ ] 6.5 Stress test: rapid acquire/release 1000 times, no memory leak (pool size stable).

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
