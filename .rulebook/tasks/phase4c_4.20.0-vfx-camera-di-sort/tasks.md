# Phase 4c — 4.20.0 VFX Camera DI Sort (Distance-Based Particle Sorting)

Distance-based GPU sort for correct back-to-front alpha blending. The existing
`particle_sort_cs.hlsl` implements a bitonic sort. This task verifies it works
with multi-emitter, adds per-emitter blend-mode awareness (additive emitters
skip sorting), and validates transparency ordering with overlapping smoke in the demo.

---

## 1. Verify Existing GPU Sort

- [x] 1.1 Verified `particle_sort_cs.hlsl` — bitonic merge sort with Phase 0 (local
      sort + key generation in shared memory) and Phase 1 (global merge). Sort key
      = `dot(pos - cameraPos, cameraForward)` converted to sortable uint via
      sign-bit flip for descending back-to-front order. Uses `VFXSortConstants` (b0).
- [x] 1.2 Sort key and comparison direction confirmed correct — no fix needed.
- [x] 1.3 Two-phase bitonic sort confirmed: Phase 0 = one dispatch, Phase 1 =
      nested loop of `(blockSize, subBlockSize)` pairs with UAV barriers between
      each. Implementation in `gpu_particle_sort.h` (`kSortGroupSize=256`).

## 2. Sorted Index Buffer Integration

- [x] 2.1 Sort writes to `RWStructuredBuffer<uint> g_sortIndices` (UAV, per emitter).
      `m_sortKeys` and `m_sortIndices` buffers confirmed in `VFXSystem`.
- [x] 2.2 `particle_billboard_vs.hlsl` reads through sorted indices. Confirmed.
- [x] 2.3 Mesh particle path uses same sorted index. Confirmed.

## 3. Blend Mode Gating (simplified per user spec)

- [x] 3.1 Added `uint32_t sortEnabled = 1` to `VFXEmitterConfig` (default ON).
      Artists set to 0 for additive blend emitters to skip the sort dispatch.
- [x] 3.2 In `DispatchCompute` (`vfx_viewport_vfx.cpp`), sort dispatch is guarded
      by `config.sortEnabled != 0u && m_sortPSO.IsValid()`. Skipped when 0.
- [x] 3.3 When sort is skipped, identity index mapping already in place (buffers
      initialized with sequential indices at particle spawn).

## 4. Camera Distance Constant Buffer

- [x] 4.1 `DispatchCompute` signature extended with `const float* cameraPos` and
      `const float* cameraForward` parameters.
- [x] 4.2 `vfx_viewport_demo.cpp` extracts `cam.GetPosition()` / `cam.GetForward()`
      and passes as `float[3]` arrays each frame.
- [x] 4.3 Sort CB (`VFXSortConstants`) populated with camera data each pass.
      Two-phase bitonic sort dispatched with UAV barriers between phases.

## 5. Serialization

- [x] 5.1 Added `sortEnabled` to `VFXPresetSerializer::Save()` and `Load()`.
      Hilly Terrain preset exports with `"sortEnabled": 1`.

## 6. UI

- [x] 6.1 Added "Sort (BtoF)" Enum toggle in vfx_parameter_panel.cpp Rendering
      category, next to Lit Mode.

## 7. Build and Verification

- [x] 7.1 Runtime lib builds (24/27 — test_runtime has 16 pre-existing linker errors
      unrelated to this task, confirmed by stash test).
- [x] 7.2 `demos/vfx_viewport` builds cleanly. Exe updated at 22:21.
- [x] 7.3 All 5 shaders compiled: spawn(9580B), update(42860B), indirect(3648B),
      sort(5576B), death_spawn(6880B). No GPU validation errors over 30 frames.
- [x] 7.4 Screenshot at frame 120 — fire emitter rendering correctly with active sort.
