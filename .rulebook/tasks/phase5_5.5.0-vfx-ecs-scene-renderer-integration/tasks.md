## 1. ECS entity creation for emitters

- [ ] 1.1 In `VFXViewportApp::OnStart()` (from task 5.4.0), replace the bare `ParticleEmitter` struct approach with proper ECS entity creation. Each emitter entity receives three components: `TransformComponent` (world position/rotation/scale), `ParticleEmitter` (GPU particle state), and a new `VFXEmitterTag` marker component that SceneRenderer uses to identify VFX entities during world iteration.
- [ ] 1.2 Define `struct VFXEmitterTag {}` in `runtime/engine/vfx/components/vfx_emitter_tag.h` — a zero-byte marker component. Register it with the ECS World component registry alongside the existing ECS component types so `World::Has<VFXEmitterTag>()` and `World::View<VFXEmitterTag, ParticleEmitter>()` work correctly.
- [ ] 1.3 Add `World::View<TransformComponent, ParticleEmitter, VFXEmitterTag>()` iteration in `SceneRenderer`'s internal VFX collection pass (inside `RenderFrame()`, before the particle simulation pass). For each entity in the view, read `TransformComponent::position` and write it to `ParticleEmitter`'s world-space spawn origin field so emitters follow entity transforms.
- [ ] 1.4 Verify that moving a VFX entity's `TransformComponent::position` from game code (in `OnUpdate()`) causes particles to spawn at the updated world-space location within one frame.

## 2. VFXEmitterComponent — full config wiring

- [x] 2.0 (Foundation) Create `emitter_config_bridge.h/.cpp` — a two-function bridge (`ApplyConfigToEmitter`, `ExtractConfigFromEmitter`) that converts between `VFXEmitterConfig` (demo POD) and `ParticleEmitter` (ECS component) without modifying either struct. Header lives in `engine/vfx/components/`; .cpp compiled as demo extra_source (needs both headers in scope). Spread angle rad↔deg conversion, damping convention translation (drag coefficient → per-frame multiplier), and lifetime min/max ↔ base+variation mapping all documented with why comments. Full gap registry in the header documents every VFXEmitterConfig field with no current ParticleEmitter equivalent and the component extension required to bridge it.
- [ ] 2.1 Rename the `VFXEmitterTag` added in step 1.2 — it is NOT a full config component. Separately, audit `ParticleEmitter` fields against the full `VFXEmitterConfig` from the standalone demo. Identify fields present in `VFXEmitterConfig` but missing from `ParticleEmitter`: `spreadAngle`, `initialSize`, `initColor` (RGBA), `sizeOverLife` curve (start/peak/end/peakTime), `colorOverLife` (start/end RGBA), `facingMode`, `stretchFactor`, `spawnShape`, `blendMode`, `litMode`, `orbitEnabled/Radius/Speed`, `groundCollision`, `restitution`. Add these fields to `ParticleEmitter` with the same defaults as `VFXEmitterConfig`.
- [ ] 2.2 Update `ParticleEmitter::Initialize()` documentation to note the new fields. Ensure each new field has a `/// @ref` comment citing the same published reference as the corresponding field in `VFXEmitterConfig` (copy from `vfx_viewport_vfx.h`).
- [ ] 2.3 Update the VFX spawn compute shader constant buffer (`VFXSpawnCB` or equivalent) to include the new fields. The shader must read `spreadAngle`, `initialSize`, `initColor`, and `facingMode` from the CB. Cite the shader function header with `/// @ref` per the CLAUDE.md shader citation rule.
- [ ] 2.4 Verify no regression in the legacy `vfx_viewport` demo: build it with `--clean` and confirm it still renders correctly at the same quality as before this task.

## 3. SceneRenderer VFX pass registration

- [ ] 3.1 Confirm `SceneRenderer` already calls `RegisterParticlePasses()` (or equivalent) during `RenderFrame()`. Read `scene_renderer.h` lines 200-300 to find the VFX pass hook. If the pass is already registered via the owned `VFXSystem`, document this in a comment. If not, add the registration call.
- [ ] 3.2 The `ParticleSimulationPass` must iterate `VFXSystem::GetActiveEmitters()` and dispatch one compute group per emitter. Verify the pass already does this for the owned `VFXSystem`. If ECS-sourced emitters (from step 1.3) are registered into the same `VFXSystem`, they flow through the existing pass automatically — confirm this path.
- [ ] 3.3 The `ParticleRenderPass` (billboard draw) must iterate `VFXSystem::GetActiveEmitters()` and issue one indirect draw per emitter. Verify the render pass reads `ParticleEmitter::buffer` for the vertex/index SRV and `ParticleEmitter::facingMode` for the billboard shader variant. No new passes are needed if both already work — just confirm coverage.
- [ ] 3.4 Add a `SceneRenderer` diagnostic log at `VFX/Debug` level that prints the emitter count from `VFXSystem::GetActiveEmitterCount()` once per 60 frames. This makes it easy to verify that ECS-sourced emitters are correctly registered during integration testing.

## 4. Multi-emitter compound effects via ECS parent/child

- [ ] 4.1 Implement compound VFX effects as a parent entity with `TransformComponent` and N child entities, each with `TransformComponent` + `ParticleEmitter` + `VFXEmitterTag`. The child `TransformComponent::position` is the local offset; the SceneRenderer ECS collection pass (step 1.3) must compute the final world-space position as `parent.transform * child.localPosition`.
- [ ] 4.2 Add a helper function `uz::SpawnVFXCompound(World& world, const Vec3& origin, std::span<const VFXCompoundEmitterDef> defs) -> uz::Entity` in `runtime/engine/vfx/vfx_helpers.h`. `VFXCompoundEmitterDef` holds `{Vec3 localOffset, ParticleEmitter config}`. The function creates a root entity with `TransformComponent` at `origin` and one child entity per def. Returns the root entity handle.
- [ ] 4.3 Verify compound effects work by spawning a 3-emitter Fire compound (flame + smoke + sparks) in `VFXViewportApp::OnStart()` using `SpawnVFXCompound()`. All three emitters must appear registered in `VFXSystem::GetActiveEmitterCount()` and simulate independently.
- [ ] 4.4 Verify the parent-child transform path: move the root entity's `TransformComponent::position` each frame and confirm all three child emitters move together in the same frame, with no one-frame lag.

## 5. Editor selection and property panel integration

- [ ] 5.1 Extend `VFXEditorUI` to display a clickable entity list for all ECS entities that have `VFXEmitterTag`. When an entity is selected in the list, the right-side property panel shows the selected entity's `ParticleEmitter` fields (emission rate, lifetime, gravity, spread angle, facing mode, etc.) as editable controls.
- [ ] 5.2 Property edits in the panel must write back directly to the `ParticleEmitter` component via `GetWorld().Get<ParticleEmitter>(selectedEntity)`. Changes must take effect on the NEXT frame (no deferred application needed — the ECS collection pass reads the component each frame).
- [ ] 5.3 Integrate the gizmo renderer (move/rotate gizmo from `vfx_viewport_demo.h`) into the new app via `SceneRenderer`'s existing `GizmoRenderer` reference. When a VFX entity is selected in the editor UI, the gizmo attaches to its `TransformComponent` position. Dragging the gizmo writes new position to `TransformComponent::position`.
- [ ] 5.4 Verify full round-trip: select emitter in list → gizmo appears at entity world position → drag gizmo → particles spawn at the new position within one frame → property panel shows updated position.

## 6. Preset load/save via ECS component

- [ ] 6.1 When the user saves a preset from the editor UI (via `VFXPresetSerializer::Save()`), serialize the selected entity's `ParticleEmitter` fields to JSON. The JSON format must be backward-compatible with the schema used by the legacy `VFXEmitterConfig` serializer (same field names and version=1 header).
- [ ] 6.2 When the user loads a preset, deserialize via `VFXPresetSerializer::Load()` and write all fields directly into the entity's `ParticleEmitter` component using `GetWorld().Get<ParticleEmitter>(selectedEntity)`. No emitter restart is required — the spawn compute shader reads config from the CB each frame.
- [ ] 6.3 Verify save/load round-trip: save a preset with `emissionRate = 700`, restart the app, load the preset, confirm `ParticleEmitter::emissionRate == 700` in the component. The particle system must reflect the loaded rate visually within 1 second of loading.

## 7. Testing and visual verification

- [ ] 7.1 Build with `--clean` on both runtime and the new app: `cd runtime && zig build --clean && zig build`, then `cd demos/vfx_viewport_app && zig build --clean && zig build`.
- [ ] 7.2 Run with `--screenshot-stdout --screenshot-frame 10 --frames 15` and verify particles from all ECS-registered emitters appear in the frame. Read the base64 PNG with the Read tool and confirm visual presence.
- [ ] 7.3 Run `--diag` and verify no `WARN_ZERO` on any VFX-related render targets (particle simulation UAV, billboard render target).
- [ ] 7.4 Spawn the 3-emitter Fire compound and run for 300 frames headless. Confirm `VFXSystem::GetTotalAliveParticles()` is non-zero and stable (no death of all particles every frame), and no GPU validation errors appear in the log.
- [ ] 7.5 Verify the legacy `vfx_viewport` demo still builds and produces the same screenshot PSNR (> 40 dB compared to the pre-task baseline) — this task must NOT regress the standalone demo.

## 8. Documentation

- [ ] 8.1 Add `/// @ref` comments to every new field added to `ParticleEmitter` in step 2.1, citing the same paper references as the corresponding `VFXEmitterConfig` fields (GDC Niagara 2019, Latta 2004, etc.).
- [ ] 8.2 Document `SpawnVFXCompound()` in `vfx_helpers.h` with full `///` Rust-style docs: purpose, parameters, returns, example usage, and lifetime notes (caller must unregister emitters before destroying entities).
- [ ] 8.3 Update `docs/analysis/` with a data flow document `vfx-ecs-scene-renderer.md` tracing the path from `TransformComponent + ParticleEmitter` entity → SceneRenderer ECS collection → VFXSystem registration → ParticleSimulationPass compute dispatch → ParticleRenderPass billboard draw.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
