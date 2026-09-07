# Proposal: Fix VFX Spawn Multi-Particle Rendering

## Why

The GPU particle spawn shader dispatches correctly (CPU confirms ~400 particles spawned), but only 2-3 particles are visible on screen. A hardcoded grid test in the billboard VS confirmed the render pipeline works for hundreds of particles — the issue is exclusively that the compute spawn shader's buffer writes don't persist across frames. Root cause analysis identified premature staging buffer destruction under triple-buffered frame pacing as the most likely cause: InitializeData's CopyBuffer for freeList/freeListHead records on frame 0's command list, but frame 1 destroys the staging buffers before the GPU finishes executing frame 0.

## What Changes

### 1. Defer staging buffer destruction by kFramesInFlight frames
Add `initFrameIndex` to GPUParticleBuffer. Only destroy freeListStaging, freeListHeadStaging, and zeroStagingBuffer after 3+ frames have passed since InitializeData ran.

### 2. GPU readback validation of freeListHead
One-time diagnostic: after InitializeData, read back freeListHead value and verify it equals capacity (10000). Confirms whether the CopyBuffer actually delivered the data.

### 3. Explicit Common→UAV barriers for WriteSet on first frame
On frame 0, WriteSet attribute buffers (positions, velocities, sizes, colors, rotations) are in Common state (never transitioned). Add explicit Common→UAV barriers before the first spawn dispatch.

### 4. Verify aliveCount reset timing
Ensure the UAV→CopyDst→UAV barrier sequence for the per-frame aliveCount reset doesn't race with the spawn shader's InterlockedAdd.

## Impact

- Affected specs: VFX GPU particle simulation, buffer lifecycle management
- Affected code: frame_vfx_inline.cpp, gpu_particle_buffer.h/.cpp
- Breaking change: NO
- User benefit: VFX fountain renders hundreds of particles instead of 2-3
