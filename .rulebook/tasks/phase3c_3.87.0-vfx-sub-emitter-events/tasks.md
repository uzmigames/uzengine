# Task 3.87.0 — VFX Sub-Emitter Event System

Wires the existing `SubEmitterModule` and `VFXEventQueue` infrastructure into the
demo. Implements GPU readback of per-particle death/collision flags, CPU-side child
emitter spawn from parent particle state, and two demo presets (explosion debris +
fire sparks).

**Reference:** UE5 Niagara Event Handler — parent-to-child event spawning
(Niagara NiagaraDataInterfaceEventWrite / EventRead pattern, GDC 2019).

---

## 1. Verify Infrastructure

- [ ] 1.1 Read `runtime/engine/vfx/events/vfx_event.h` and `vfx_event.cpp`.
  Confirm `VFXEventQueue`, `EventPayload`, and `VFXPendingEvent` are defined and
  fully implemented (no stubs). If any method is stubbed, implement it now before
  proceeding.
- [ ] 1.2 Read `runtime/engine/vfx/core/sub_emitter_readback.h` and `.cpp`.
  Confirm `SubEmitterReadback` readback logic is complete. If `GatherFlaggedParticles()`
  or equivalent is stubbed, implement it: iterate `SubEmitterFlags[]` GPU readback
  data, collect indices where flag != 0, build `EventPayload` from position/velocity/
  color/age at those indices.
- [ ] 1.3 Read `runtime/engine/vfx/modules/sub_emitter_module.h`. Confirm
  `SetChildEventQueue()` exists and `Execute()` is not a stub. If stubbed, implement
  `Execute()`: iterate `ctx.parameterData` (cast to `SubEmitterReadback*`), apply
  `inheritVelocity` blend, build payloads, call `childEventQueue->Push(payload)`.

## 2. GPU — SubEmitterFlags Buffer

- [ ] 2.1 Add `RHIBufferHandle m_subEmitterFlags` to `GPUParticleBuffer` in
  `runtime/engine/vfx/core/gpu_particle_buffer.h`. Size: `capacity * sizeof(uint32_t)`.
  Usage: `UnorderedAccess | ShaderResource | CopySrc` (UAV for CS write, CopySrc
  for CPU readback).
- [ ] 2.2 Allocate `m_subEmitterFlags` in `GPUParticleBuffer::Create()` in
  `runtime/engine/vfx/core/gpu_particle_buffer.cpp`. Add `RHIBufferHandle::Invalid()`
  init in the struct defaults.
- [ ] 2.3 Release `m_subEmitterFlags` in `GPUParticleBuffer::Destroy()`.
- [ ] 2.4 Add `uint32_t subEmitterFlagsUavIndex` and `uint32_t subEmitterFlagsSrvIndex`
  to `VFXUpdateCB` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h` (pack into an
  existing pad float4 slot). Update static_assert.
- [ ] 2.5 In `particle_update_cs.hlsl`, add `uint subEmitterFlagsUavIndex` to
  `VFXUpdateConstants`. In `main()`, clear the flag at the start of each alive thread:
  ```hlsl
  RWBuffer<uint> subEmitterFlags = ResourceDescriptorHeap[subEmitterFlagsUavIndex];
  subEmitterFlags[threadIdx] = 0u;
  ```
  After the death check (when `age >= lifespan`), before pushing to free list, set:
  ```hlsl
  subEmitterFlags[threadIdx] = 1u; // OnDeath event
  ```

## 3. CPU — GPU Readback Pipeline

- [ ] 3.1 Add `RHIBufferHandle m_subEmitterFlagsReadback` to `VFXParticleSystem`
  in `vfx_viewport_vfx.h`. This is a CPU-readable staging buffer, size
  `maxParticles * sizeof(uint32_t)`.
- [ ] 3.2 In `VFXParticleSystem::Init()`, create `m_subEmitterFlagsReadback` as
  a readback buffer (`CPURead` usage). Use the existing pattern for other staging
  buffers already in the demo.
- [ ] 3.3 In `VFXParticleSystem::DispatchCompute()`, after the update CS dispatch,
  issue a `CopyBufferRegion` from `m_subEmitterFlags` (GPU) to
  `m_subEmitterFlagsReadback` (CPU). Insert the appropriate UAV→CopySrc transition
  before the copy and CopySrc→UAV after.
- [ ] 3.4 Add `[[nodiscard]] SubEmitterReadback GatherSubEmitterEvents(
  const float* positionsPtr, const float* velocitiesPtr, const float* colorsPtr,
  const float* agesPtr, uint32_t particleCount) const` to `VFXParticleSystem`.
  Implementation: map `m_subEmitterFlagsReadback`, iterate `particleCount` elements,
  build `EventPayload` for each flagged particle from the provided SoA pointers.
  Unmap after read. Returns a `SubEmitterReadback` with the flagged payloads.
  Note: caller must ensure GPU work for the frame is complete before calling
  (use the existing per-frame fence the demo already waits on).

## 4. Sub-Emitter Wiring in Demo

- [ ] 4.1 In `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp`, create two
  `VFXParticleSystem` instances: `g_parentSystem` and `g_childSystem`.
  Init both in `OnInit()` with `maxParticles = 4096` each.
- [ ] 4.2 Create a `VFXEventQueue g_childEventQueue` and a `SubEmitterModule g_subEmitterModule`.
  Configure `g_subEmitterModule`:
  - `trigger = SubEmitterTrigger::OnDeath`
  - `inheritVelocity = 0.4f`, `inheritColor = true`
  - Call `g_subEmitterModule.SetChildEventQueue(&g_childEventQueue)`.
- [ ] 4.3 In the per-frame loop (after `g_parentSystem.DispatchCompute()` and the
  fence wait for the previous frame's readback):
  - Call `g_parentSystem.GatherSubEmitterEvents(...)` to get flagged payloads.
  - Build an `ExecuteContext` with `parameterData = &readback`.
  - Call `g_subEmitterModule.Execute(ctx)`.
  - Drain `g_childEventQueue`: for each pending event, call
    `g_childSystem.SpawnFromPayload(event)` (see step 4.4).
- [ ] 4.4 Add `void SpawnFromPayload(const EventPayload& payload, uint32_t count = 5)`
  to `VFXParticleSystem` in the demo. Implementation: build a `VFXSpawnCB` that
  overrides emitter position with `payload.position`, adds `payload.velocity * inheritVelocity`
  to the base spawn velocity, then dispatches the spawn CS for `count` particles.

## 5. Demo — Explosion + Fire Spark Presets

- [ ] 5.1 Add `"Explosion w/ Debris"` preset:
  - Parent: `spawnRate = 1.0f`, large burst (`spawnCount = 50` on key press),
    `spawnVelocity = {0, 10, 0}`, `velocityVariation = 8.0f`, `spreadAngle = 1.5f`,
    `gravity = {0, -9.81, 0}`, `blendMode = 1` (Additive).
  - Child (debris): `initialSize = 0.03f`, `blendMode = 2`, `gravity = {0, -12.0, 0}`.
  - `inheritVelocity = 0.6f`.
- [ ] 5.2 Add `"Fire with Sparks"` preset:
  - Parent: upward fire stream, `spawnRate = 40.0f`, `blendMode = 1`.
  - Child: sparks, `initialSize = 0.015f`, `blendMode = 1` (Additive),
    `minLifetime = 0.2f`, `maxLifetime = 0.8f`, `inheritVelocity = 0.3f`.
- [ ] 5.3 Add keyboard toggle `'E'` to cycle sub-emitter presets:
  `Off` → `Explosion` → `Fire Sparks`. Print mode to ImGui overlay.
  On `'E'` press with `Explosion` active, also trigger a burst spawn on the parent.

## 6. Visual Verification

- [ ] 6.1 Build runtime, then `runtime/demos/vfx_viewport/`. Fix all errors.
- [ ] 6.2 Capture the Fire Sparks preset:
  `vfx_viewport.exe --screenshot-stdout --screenshot-frame 20 --frames 30`
  View via Read tool. Verify secondary spark particles are visible branching from
  the parent fire stream at particle death positions.
- [ ] 6.3 Capture with sub-emitter off. Verify no secondary particles and no regression.

## 7. Tasks Update

- [ ] 7.1 Mark all checklist items `[x]` in this file as they are completed.
