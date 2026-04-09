## 1. Staging Buffer Lifetime (HIGHEST PRIORITY)
- [ ] 1.1 Add `uint32_t initFrameIndex = UINT32_MAX` to GPUParticleBuffer in gpu_particle_buffer.h
- [ ] 1.2 Set `initFrameIndex = frameIndex` in InitializeData caller (frame_vfx_inline.cpp) after successful init
- [ ] 1.3 Replace immediate staging destroy (frame 1) with deferred destroy: only destroy when `frameIndex >= initFrameIndex + 3` (kFramesInFlight)
- [ ] 1.4 Also defer zeroStagingBuffer destruction by the same frame count
- [ ] 1.5 Verify no resource leaks at shutdown (DestroyStaging handles cleanup)

## 2. GPU Readback Validation (DIAGNOSTIC)
- [ ] 2.1 After InitializeData succeeds, add one-time GPU readback of freeListHead buffer
- [ ] 2.2 Use CopySrc barrier, CopyBuffer to readback buffer, fence wait, MapBuffer, log value
- [ ] 2.3 Assert freeListHead == capacity (10000). If not, root cause confirmed.
- [ ] 2.4 Remove readback code after bug is fixed (or gate behind UZ_DEBUG flag)

## 3. First-Frame Common→UAV Barriers
- [ ] 3.1 Before first spawn dispatch (!hasCompletedCycle), add Common→UAV barriers for WriteSet positions, velocities, sizes, colors, rotations
- [ ] 3.2 Note: lifespans and ages already transitioned by InitializeData's CopyDst→UAV barriers
- [ ] 3.3 Verify no duplicate transitions (check if hasCompletedCycle guard covers frame 0 only)

## 4. Testing & Verification
- [ ] 4.1 Run vfx_viewport_app for 200 frames — count visible particles (expect 100+)
- [ ] 4.2 Screenshot at frame 80 — visible fountain arc with dozens of particles spread vertically
- [ ] 4.3 Compare with standalone vfx_viewport demo — similar particle count at same emission rate
- [ ] 4.4 Run with DX12 debug layer — zero severity-1 errors
- [ ] 4.5 Verify clean exit (no FATAL errors, no device removal)

## 5. Cleanup
- [ ] 5.1 Remove debug logging (SpawnCB dump, frame spawn count, Billboard posSRV)
- [ ] 5.2 Remove CPU upload test code (if(false) block in frame_vfx_inline.cpp)
- [ ] 5.3 Restore billboard VS to production state (no debug overrides)
- [ ] 5.4 Commit final working state

## 6. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
