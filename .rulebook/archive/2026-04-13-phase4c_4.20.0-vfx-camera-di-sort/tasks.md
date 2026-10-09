# Phase 4c — 4.20.0 VFX Camera DI Sort (Distance-Based Particle Sorting)

Distance-based GPU sort for correct back-to-front alpha blending. The existing
`particle_sort_cs.hlsl` implements a bitonic sort. This task verifies it works
with multi-emitter, adds per-emitter blend-mode awareness (additive emitters
skip sorting), and validates transparency ordering with overlapping smoke in the demo.

---

## 1. Verify Existing GPU Sort

- [ ] 1.1 Open `runtime/shaders/runtime/vfx/particle_sort_cs.hlsl`. Confirm:
      - Sort key is `float cameraDistSq` computed as
        `dot(pos - cameraPos, pos - cameraPos)` (no sqrt needed for sort).
      - Key-value pair is `uint2 { distBits, particleIndex }` packed as
        `asuint(distSq)` in the high word — ensures float-as-uint comparison
        preserves order for non-negative distances.
      - Sort is descending (farthest first) — confirm the comparison operator is
        `sortKeyA > sortKeyB` (farther = drawn first).
      - Algorithm is bitonic merge sort: `O(N log^2 N)` passes, each
        dispatched as a separate CS invocation.
- [ ] 1.2 If the sort key is computed incorrectly or the comparison direction is
      wrong, fix it now. The key must be non-negative and monotonic with distance.
- [ ] 1.3 Confirm `gpu_particle_sort.h/cpp` implements `GpuParticleSort::Sort()`
      that iterates the correct number of bitonic passes:
      ```
      passes = ceil(log2(N)) * (ceil(log2(N)) + 1) / 2
      ```
      and dispatches each pass with the correct `(step, stage)` constants.

## 2. Sorted Index Buffer Integration

- [ ] 2.1 Confirm the sort produces a `RWStructuredBuffer<uint> g_sortedIndices`
      (UAV) of `maxParticles` entries. After sort, `g_sortedIndices[i]` gives
      the particle index to render at draw position `i`.
- [ ] 2.2 In `particle_billboard_vs.hlsl`, verify the vertex shader reads the
      alive-list through the sorted index:
      ```hlsl
      StructuredBuffer<uint> g_sortedIndices : register(t6);
      uint particleIdx = g_sortedIndices[SV_VertexID / 4]; // 4 verts per billboard
      ```
      If it reads directly from the alive-list unsorted, fix it to go through
      `g_sortedIndices`.
- [ ] 2.3 Apply the same sorted-index read to `particle_mesh_vs.hlsl` (reads via
      `g_sortedIndices[SV_InstanceID]`).

## 3. Blend Mode Gating

- [ ] 3.1 In `VFXEmitterConfig` / `SpriteRenderModule`, add:
      ```cpp
      enum class ParticleBlendMode : uint8_t {
          Translucent, // alpha blend — MUST sort back-to-front
          Additive,    // src + dst — sorting unnecessary (commutative)
          Masked,      // alpha test — no sort needed
      };
      ParticleBlendMode blendMode = ParticleBlendMode::Translucent;
      ```
- [ ] 3.2 In `vfx_system.cpp`, before issuing the sort dispatch for an emitter,
      check `blendMode == Translucent`. Skip the sort CS entirely for
      `Additive` and `Masked` emitters. This saves GPU time and correctly
      avoids unnecessary work.
- [ ] 3.3 Ensure that when sort is skipped, `g_sortedIndices` is populated with
      the identity mapping (0,1,2,...,N-1) by a cheap fill CS or CPU upload,
      so the VS still reads through `g_sortedIndices` uniformly.

## 4. Multi-Emitter Sort Isolation

- [ ] 4.1 Each emitter must have its own `GpuParticleSort` instance (its own
      `g_sortedIndices` UAV and sort scratch buffers). Confirm `ParticleEmitter`
      owns a `GpuParticleSort` member.
- [ ] 4.2 Verify sort dispatches for different emitters do not share scratch
      buffers (would corrupt sort results for simultaneous emitters).
- [ ] 4.3 In `vfx_system.cpp`, sort dispatches must be serialized per emitter
      (UAV barrier after each emitter's sort before its draw call), but emitters
      can be sorted in any order relative to each other.

## 5. Camera Distance Constant Buffer

- [ ] 5.1 Confirm `particle_sort_cs.hlsl` reads camera position from `PerFrameCB`
      or a dedicated `SortCB`:
      ```hlsl
      cbuffer SortCB : register(b2) {
          float3 cameraWorldPos;
          uint   particleCount;
          uint   sortStep;
          uint   sortStage;
          float2 _pad;
      };
      ```
      Each bitonic pass dispatches with different `(sortStep, sortStage)` values.
- [ ] 5.2 In `GpuParticleSort::Sort(Vec3 camPos, uint32_t aliveCount)`, upload
      `cameraWorldPos` each frame (camera may move each frame).

## 6. Demo Validation

- [ ] 6.1 In `vfx_viewport_demo.cpp`, add preset "Smoke Overlap":
      two emitters sharing the same screen space, both `Translucent`.
      Position emitter A at (0,0,50) cm and emitter B at (0,0,150) cm.
      Both emit grey smoke-like particles (low opacity, large size).
- [ ] 6.2 With sort disabled (comment out sort dispatch temporarily), capture
      screenshot and confirm visible blending artifacts (bright halos, wrong
      layer ordering). Re-enable sort and capture again — artifacts must be gone.
- [ ] 6.3 Add ImGui toggle "Force Sort Off" for debugging, which switches all
      emitters to identity index mapping without rebuilding PSOs.
- [ ] 6.4 Add preset "Mixed Blend": one additive (sparks) + one translucent (smoke)
      emitter. Verify only the translucent emitter triggers the sort pass.

## 7. Build and Verification

- [ ] 7.1 `cd runtime && zig build --clean && zig build` — no errors.
- [ ] 7.2 `cd demos/vfx_viewport && zig build --clean && zig build`.
- [ ] 7.3 Capture "Smoke Overlap" at frame 10 with sort enabled:
      verify smooth alpha layering, no bright outlines between particles.
- [ ] 7.4 Capture "Mixed Blend" at frame 10: verify additive sparks glow
      through the smoke without sort artifacts.

## 8. Documentation

- [ ] 8.1 Add to `particle_sort_cs.hlsl`:
      ```hlsl
      /// @ref Batcher 1968 "A New Algorithm for Sorting" — bitonic merge sort
      /// @constants sortStep, sortStage — control the bitonic network pass index
      ```
- [ ] 8.2 Document in `gpu_particle_sort.h` why `asuint(distSq)` preserves
      order for non-negative floats (IEEE 754 property: bit pattern of positive
      floats is monotonic with magnitude).
- [ ] 8.3 Document `Additive` blend mode sort-skip rationale: `A + B = B + A`,
      so draw order is irrelevant for purely additive blending.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
