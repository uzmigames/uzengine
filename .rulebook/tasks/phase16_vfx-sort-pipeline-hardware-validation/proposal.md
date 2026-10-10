# Proposal: phase16_vfx-sort-pipeline-hardware-validation

## Why

`phase16_vfx-particle-sort-pipeline-refactor` shipped the core sort
pipeline (sort_cs alive-detection + custom-attribute keys, inline
dispatch, sprite VS wiring, mesh CS deterministic-slot writes,
`.uzfx` + `.uzvfx` schema parsing). The CPU surface is fully tested
(132/132 unit tests green on `MockRHIDevice`), but six items needed a
real DX12 device to validate end-to-end and were deferred under that
task's section 5/6:

1. Visual A/B comparison between sorted and unsorted transparent mesh
   particles — capture the pre-fix alpha-blend artefacts vs the
   post-fix back-to-front result, prove the deterministic-slot mesh
   CS is hitting the right slots.
2. GPU timestamp budget on 16 384 mesh particles — assert the bitonic
   sort dispatch stays under 0.3 ms on baseline hardware (RTX 3060 /
   Radeon RX 6600).
3. CVar fallback `r.VFX.Sort.MaxParticles` that auto-downgrades to
   `MeshSortMode::None` when the budget would be exceeded — only
   meaningful once we have real timing data.
4. `test_particle_sort_alive_detection` — 1024-particle buffer with
   50% alive at scattered slots, run the full bitonic sort, verify
   `sortedIndices[0..aliveCount-1]` is a bijection with the alive set.
5. `test_mesh_sort_deterministic_slots` — feed known sortedIndices,
   verify the instance buffer matches the input ordering.
6. `test_custom_sort_attribute` — emitter with user attribute "team",
   sort by it descending, verify the emitted instance order.

All six need either a DX12-capable runtime or a substantially extended
`MockRHIDevice` that simulates compute dispatches. The sandboxed dev
env returns `DXGI_ERROR_INVALID_CALL` on `CreateDXGIFactory2`, so they
land in the next on-hardware session.

## What Changes

### Visual + perf (5.2-5.4 from refactor task)

- Author or repurpose `demos/vfx_viewport` to load
  `sample/content/vfx/tests/mesh_sort_shards.uzvfx` (already shipped),
  capture screenshots from 4 camera angles with `--screenshot-stdout`,
  view via the Read tool. Record the before/after delta.
- Wire the existing `m_device->GpuProfilerBeginTimestamp("VFX_InlineSort")`
  measurement (already present in `DispatchVFXSort`) to a CSV log flag
  and run `--frames 600` at three particle counts (1k / 4k / 16k).
- Implement `r.VFX.Sort.MaxParticles` (default INT_MAX) read at
  `DispatchVFXSort` entry; when an emitter would be sorted but its
  capacity exceeds the cap, log a one-time warning and skip the
  dispatch. Document the CVar in `cvars.md`.

### GPU integration tests (6.1-6.4 from refactor task)

- Extend `MockRHIDevice` (or add a new `MockComputeDevice`) with a
  minimal compute simulator: dispatch records the bound CB, captures
  the bound SRV/UAV indices, and the test fixture provides callable
  shader-equivalent C++ code that mutates the buffer mocks. This is
  the unblock for unit-testing GPU-only kernels.
- Implement the three test files listed above using the simulator.

### Documentation

- Capture before/after screenshots in `docs/engine/25-vfx/99-sort-pipeline.md`.
- Add the timing CSV columns + interpretation to the same doc.

## Impact

- Affected specs: VFX sort pipeline (existing).
- Affected code:
  - `runtime/engine/renderer/frame/frame_vfx_inline.cpp` — CVar gate
    on the dispatch.
  - `runtime/engine/cvar/cvar_registry.cpp` — register
    `r.VFX.Sort.MaxParticles`.
  - `runtime/tests/engine/vfx/mock_rhi_device.h` (or new sibling) —
    compute simulator.
  - New: `runtime/tests/engine/vfx/test_particle_sort_alive_detection.cpp`,
    `test_mesh_sort_deterministic_slots.cpp`,
    `test_custom_sort_attribute.cpp`.
  - `docs/engine/25-vfx/99-sort-pipeline.md` — visual + perf section.
- Breaking change: NO. CVar defaults preserve current behaviour.
- User benefit: production-grade verification of the sort pipeline
  with a regression suite that catches scattered-alive bugs at CI
  time, plus an explicit runtime budget for shipping titles.

## Dependencies

- Builds on `phase16_vfx-particle-sort-pipeline-refactor` (already
  archived).
- Depends on access to a DX12-capable host for the visual + perf items.
