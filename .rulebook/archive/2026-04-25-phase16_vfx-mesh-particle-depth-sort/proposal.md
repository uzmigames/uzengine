# Mesh Particle Depth Sort — Data Model + CB Foundation

> **Re-scoped 2026-04-25.** During implementation we discovered the existing
> particle sort pipeline is broken in three independent ways (see follow-up
> task `phase16_vfx-particle-sort-pipeline-refactor`). The original proposal
> assumed reuse of working sort infrastructure that does not exist. This task
> now ships the safely-deliverable subset: the data model, JSON serialization,
> mesh CS CB foundation, and the runtime gate that keeps the path dormant
> until the follow-up wires the dispatch.

## Why

`MeshRenderModule` lacks a `sortMode` knob. Authors cannot express "this
emitter renders transparent meshes — please sort back-to-front" in `.uzfx`
files. The data-model gap blocks the actual pipeline work because the
follow-up cannot opt emitters into sorting if there is no field to opt in
through. This task closes the data-model gap so the pipeline refactor can
focus on the GPU work.

## What Changes (delivered)

- `MeshRenderModule::Config` gains:
  - `MeshSortMode sortMode = None` — `{None, ViewDepth, CustomAttribute}`.
  - `std::string customSortAttribute` — name of a user-declared per-particle
    float attribute, used as the sort key when `sortMode == CustomAttribute`.
- `MeshSortModeToString` / `MeshSortModeFromString` round-trip helpers
  matching the existing `MeshRotationMode` style.
- `ParseConfig` accepts new JSON keys `sort_mode` and
  `custom_sort_attribute`; missing fields preserve defaults; an empty
  `customSortAttribute` with `sortMode == CustomAttribute` downgrades to
  `None` and warns.
- `GetParameters()` exposes `sort_mode` as `Int` (enum ordinal) for editor
  binding and CB packing.
- `VFXMeshInstanceConstants` (CPU + HLSL) repurposes the previously unused
  `_pad0`/`_pad1` slots as `sortMode` and `sortedIndicesSrvIndex` (zero
  growth — CB stays at 64 bytes on the CPU side, untouched HLSL bindless
  block). The mesh instance CS reads these and switches to deterministic-
  slot output when both are valid.
- `frame_particles.cpp` populates the new CB fields under a
  `constexpr bool sortDispatched = false` gate so authoring
  `sort_mode: "ViewDepth"` in a `.uzfx` is harmless until the follow-up
  task flips the gate. This avoids landing a feature flag whose default
  produces broken visuals.

## What Changes (deferred to `phase16_vfx-particle-sort-pipeline-refactor`)

- Re-enabling the disabled sort dispatch (`if (false)` in
  `frame_particles.cpp:224`).
- Fixing `particle_sort_cs.hlsl` so it handles the scattered free-list
  spawn pattern (alive particles are not contiguous in `[0..aliveCount-1]`).
- Sprite VS actually reading `sortedIndicesSrvIndex` (today the CB field is
  unused).
- Mesh CS deterministic-slot write path (the foundation reads sortedIndices
  but still uses `InterlockedAdd` for slot allocation in unsorted path —
  unchanged from existing behaviour).
- Custom-attribute SRV resolution.
- Visual demo + perf test.
- Mesh `VFXMeshInstanceConstants` 16-byte CPU/HLSL mismatch fix
  (pre-existing, separate from this task's surface).

## Impact

- Affected specs: VFX module data model.
- Affected code:
  - `runtime/engine/vfx/modules/mesh_render_module.{h,cpp}`
  - `runtime/engine/renderer/frame/frame_particles.cpp` (CB struct + gate)
  - `runtime/shaders/runtime/vfx/particle_mesh_instance_cs.hlsl` (CB layout
    + dormant sortedIndices read path)
  - New: `runtime/tests/engine/vfx/test_mesh_sort_mode.cpp`
- Breaking change: NO. Default `sortMode = None` preserves current
  behaviour. The runtime gate forces `sortedIndicesSrvIndex = UINT32_MAX`
  regardless of config until the follow-up task flips it.
- User benefit:
  - Authors can write `sort_mode: "ViewDepth"` in `.uzfx` files today; the
    follow-up automatically activates them.
  - Editor can render a "Sort Mode" dropdown (covered by follow-up task 1.6
    after the dispatch is live).
  - Unblocks the pipeline refactor.
