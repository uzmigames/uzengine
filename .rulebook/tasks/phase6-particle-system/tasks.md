## 1. `.uzfx` Asset Format and Parser

- [ ] 1.1 Define `UzFxDef` struct: emitter shape, emission rate, lifetime min/max, color, size, velocity, gravity, drag, modules list
- [ ] 1.2 `UzFxParser::load(path)` — INI-like parser, returns `UzFxDef`
- [ ] 1.3 Support all emitter types: `point`, `sphere`, `cone`, `box`, `mesh_surface`
- [ ] 1.4 Parse optional module sections: `velocity_over_lifetime`, `color_over_lifetime`, `size_over_lifetime`, `rotation_over_lifetime`, `noise_velocity`
- [ ] 1.5 Parse renderer block: `texture`, `blend_mode`, `sort_order`, `sheet_columns/rows/fps`
- [ ] 1.6 Parse collision block: `collision`, `collision_bounce`, `collision_friction`
- [ ] 1.7 Asset registration: `.uzfx` added to asset pipeline, imported as binary blob to `.import/`
- [ ] 1.8 Hot-reload: FileWatcher triggers `VFXSystem::reload_effect(path)` on `.uzfx` change

## 2. GPU Simulation Infrastructure

- [ ] 2.1 `Particle` HLSL struct (64 bytes): position, lifetime, velocity, color, size, rotation, seed
- [ ] 2.2 `ParticleBuffer` StructuredBuffer<Particle> per emitter (65,536 entries, 4 MB)
- [ ] 2.3 `DeadList` AppendStructuredBuffer (counter buffer, initially all indices free)
- [ ] 2.4 `AliveList` AppendStructuredBuffer (counter buffer, rebuilt each frame)
- [ ] 2.5 Indirect args buffer (populated by Simulate dispatch, consumed by DrawIndexedInstanced)
- [ ] 2.6 GPU root signature: SRV/UAV for particle + dead/alive buffers, CBV for per-emitter params
- [ ] 2.7 Compute PSO for `simulate.hlsl`
- [ ] 2.8 Compute PSO for `emit.hlsl`

## 3. Simulate and Emit Compute Shaders

- [ ] 3.1 `simulate.hlsl`: per-thread processes one particle, integrates velocity + gravity + drag
- [ ] 3.2 Simulate: age-based color interpolation (sample color gradient)
- [ ] 3.3 Simulate: age-based size interpolation
- [ ] 3.4 Simulate: billboard rotation update
- [ ] 3.5 Simulate: module stack — `ApplyVelocityOverLifetime`, `ApplyNoiseVelocity`, `ApplyRotationOverLifetime`
- [ ] 3.6 Simulate: dead particle → `DeadList.Append(idx)` + set `lifetime_remain = -1`
- [ ] 3.7 Simulate: alive particle → `AliveList.Append(idx)` + write updated particle
- [ ] 3.8 `emit.hlsl`: per-thread spawns one new particle using `DeadList.Consume()`
- [ ] 3.9 Emit: position sampling per emitter shape (point/sphere/cone/box/mesh_surface)
- [ ] 3.10 Emit: randomized lifetime, size, speed, rotation from min/max range (per-particle seed)
- [ ] 3.11 Emit: burst emission (spawn burst_count at t=0)
- [ ] 3.12 Constant buffer upload: per-emitter params (gravity, drag, color stops, size stops, etc.)

## 4. Behavior Module Shaders

- [ ] 4.1 `velocity_over_lifetime`: linear blend from `velocity_start` to `velocity_end` by age_t
- [ ] 4.2 `color_over_lifetime`: multi-stop gradient sampling (up to 8 stops) by age_t
- [ ] 4.3 `size_over_lifetime`: multi-stop curve sampling by age_t
- [ ] 4.4 `rotation_over_lifetime`: constant angular velocity per particle (from per-particle seed)
- [ ] 4.5 `noise_velocity`: curl-noise turbulence via gradient of 3D value noise; `strength`, `frequency`, `scroll_speed`, `octaves`

## 5. Billboard Rendering

- [ ] 5.1 `particle_vs.hlsl`: reads `AliveList[SV_InstanceID]` → `ParticleBuffer[idx]` → expands quad corners
- [ ] 5.2 Billboard mode: quad aligned to camera view plane (CameraRight, CameraUp from view matrix)
- [ ] 5.3 Per-corner UV from `float2(corner_x * 0.5 + 0.5, corner_y * 0.5 + 0.5)`
- [ ] 5.4 `particle_ps.hlsl`: sample texture, multiply by particle color, apply blend mode
- [ ] 5.5 Additive blend PSO and alpha blend PSO (selected by `blend_mode` in `UzFxDef`)
- [ ] 5.6 `DrawIndexedInstanced(6, alive_count, ...)` — 6 indices = 2 triangles = 1 billboard quad
- [ ] 5.7 Flipbook animation: UV offset computed from `sheet_columns`, `sheet_rows`, `sheet_fps`, particle age
- [ ] 5.8 Depth sorting: for alpha-blended emitters, sort `AliveList` by `particle.position · CameraForward` (GPU sort, Phase 5 stretch goal; accept incorrect blending in Phase 3)

## 6. VFXSystem (C++ Manager)

- [ ] 6.1 `VFXSystem` class: manages pool of 256 GPU emitter slots
- [ ] 6.2 `EmitterSlot`: owns `ParticleBuffer`, `DeadList`, `AliveList`, indirect args buffer
- [ ] 6.3 `register_emitter(NodeId, CParticleEmitter&)` — allocates slot, initializes dead list
- [ ] 6.4 `unregister_emitter(NodeId)` — frees slot back to pool
- [ ] 6.5 `VFXHandle` type: opaque handle (slot index + generation), returned to Lua/IPC
- [ ] 6.6 `spawn(path, pos, scale, parent)` → `VFXHandle` — transient VFX not tied to scene node
- [ ] 6.7 `stop(handle, immediate)` — halts emission; `immediate=true` kills live particles instantly
- [ ] 6.8 `stop_all()` — called on play mode stop / scene unload
- [ ] 6.9 `set_rate(handle, rate)` — runtime emission rate override
- [ ] 6.10 `update(dt, cam)` — per frame: LOD distances, compute dispatches, indirect arg writes
- [ ] 6.11 `render(cmd, frame)` — per frame: billboard draw calls (opaque then additive then alpha)

## 7. ECS Integration

- [ ] 7.1 `CParticleEmitter` component struct: `effect`, `playing`, `playback_time`, `emit_accumulator`, `gpu_emitter_index`
- [ ] 7.2 `ParticleEmitter` node type registered in SceneManager
- [ ] 7.3 `.uzscene` parser handles `type=ParticleEmitter` with `effect` and `playing` properties
- [ ] 7.4 `system_vfx_update()` ECS system: calls `VFXSystem::update()`, syncs `CParticleEmitter` state

## 8. Lua API

- [ ] 8.1 `ctx.scene:spawn_vfx(path, pos, opts?)` → handle
- [ ] 8.2 `ctx.vfx:stop(handle)`
- [ ] 8.3 `ctx.vfx:stop_immediate(handle)`
- [ ] 8.4 `ctx.vfx:set_rate(handle, rate)`
- [ ] 8.5 `ctx.vfx:is_alive(handle)` → bool
- [ ] 8.6 `ctx.vfx:get_particle_count(handle)` → int

## 9. AnimNotify Integration

- [ ] 9.1 EventBus handler for `spawn_vfx` event: parse `asset`, `attach_bone`, `offset` from params_json
- [ ] 9.2 `VFXSystem::spawn_at_bone(path, skeleton, bone_idx, offset)` — world transform from bone
- [ ] 9.3 Integration test: footstep VFX fires at correct bone position and correct animation frame

## 10. Distance LOD

- [ ] 10.1 Per-emitter camera distance computation (emitter world pos → camera pos)
- [ ] 10.2 LOD bands: 0–10m = full rate, 10–50m = full rate, >50m = 25% rate (every 4th frame), >100m = paused
- [ ] 10.3 Hysteresis: resume threshold at 80m (avoid flickering at boundary)
- [ ] 10.4 Render skip: >100m emitters do not issue draw calls

## 11. Optional Particle Collision

- [ ] 11.1 `collision = true` in `.uzfx` enables per-particle sphere-vs-mesh sweep in simulate shader
- [ ] 11.2 Cook step bakes simplified collision mesh triangles into a GPU buffer (4 closest per chunk)
- [ ] 11.3 Compute shader: sphere sweep test, apply bounce (restitution) and friction on hit
- [ ] 11.4 Disabled by default — enabled only when `collision = true` in effect

## 12. IPC Handlers

- [ ] 12.1 `vfx.spawn { path, position, scale?, rotation?, parent_id? }` → `{ handle }`
- [ ] 12.2 `vfx.stop { handle }`
- [ ] 12.3 `vfx.setRate { handle, rate }`
- [ ] 12.4 `vfx.getState { handle }` → `{ alive, particle_count, playback_time }`
- [ ] 12.5 `vfx.stopAll {}`

## 13. Editor Integration

- [ ] 13.1 Auto-play effect in viewport when `ParticleEmitter` node is selected in Scene Tree
- [ ] 13.2 Inspector: "Preview VFX" button (restarts effect from t=0)
- [ ] 13.3 Inspector: live particle count display (read from GPU via readback, max once per second)
- [ ] 13.4 Drag-drop `.uzfx` from Asset Browser onto viewport → creates `ParticleEmitter` node at cursor

## 14. Testing

- [ ] 14.1 Campfire smoke: emission rate 40/s, smoke particles rise, fade, shrink — visual pass
- [ ] 14.2 Burst explosion: `burst_count = 200`, single-shot, all particles die within `lifetime_max`
- [ ] 14.3 Color gradient module: 4 stops, correct color at each age fraction
- [ ] 14.4 Noise velocity module: visible turbulence, no particles escaping gravity
- [ ] 14.5 LOD: verify sim halts at >100m, resumes at <80m
- [ ] 14.6 Max particles: 65,536 active particles at once — no GPU crash or buffer overflow
- [ ] 14.7 Max emitters: 256 simultaneous emitters — all simulate and render correctly
- [ ] 14.8 AnimNotify: footstep dust fires exactly at `time=0.42` in animation clip
