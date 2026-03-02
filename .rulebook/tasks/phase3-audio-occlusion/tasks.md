## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-audio-system` is complete — `CAudioSource`, `CAudioListener`, XAudio2 voices
- [ ] 1.2 Confirm `phase3-physics-system` is complete — Jolt raycast API (`JPH::PhysicsSystem::CastRay`)
- [ ] 1.3 Read `docs/specs/audio-system.md` — understand CAudioSource, voice volume API, X3DAudio integration
- [ ] 1.4 Read `docs/specs/physics-system.md` — understand Jolt ray cast interface and collision layer filtering

## 2. CAudioSource Extension

- [ ] 2.1 Add fields to `CAudioSource` in `ecs/components.h`:
  - `occlusion_enabled: bool` (default: false)
  - `occlusion_layer_mask: uint32` — physics layers to test against
  - `occlusion_factor: float` (0=open, 1=fully occluded) — runtime state, updated by system
- [ ] 2.2 Update `.uzscene` parser — serialize `occlusion_enabled`, `occlusion_layer_mask`

## 3. AudioOcclusionSystem

- [ ] 3.1 Create `src/engine/audio/audio_occlusion.h/cpp`
- [ ] 3.2 Implement `AudioOcclusionSystem` — ECS system registered in `LateUpdate` group (after transform updates)
- [ ] 3.3 Implement `AudioOcclusionSystem::tick(registry, dt)`:
  - Find `CAudioListener` entity → get listener world position
  - Throttle: skip entity if last update was within `1.0/occlusion_update_rate_hz` seconds
  - For each `CAudioSource` with `occlusion_enabled`:
    - Get source world position from `CTransform`
    - Skip if distance > `occlusion_max_distance`
    - Call `compute_occlusion(listener_pos, source_pos, layer_mask) -> float`
    - Lerp `occlusion_factor` toward computed value at `lerp_speed * dt`
    - Apply to voice
- [ ] 3.4 Implement `compute_occlusion(from, to, layer_mask) -> float`:
  - Cast `occlusion_ray_count` rays (1 direct + N jittered for soft occlusion)
  - Count hits weighted by `PhysicsMaterial.density` (default density if no material)
  - Return `clamp(hit_weight_sum / occlusion_ray_count, 0, 1)`

## 4. Physics Raycast Integration

- [ ] 4.1 Use `JPH::PhysicsSystem::GetNarrowPhaseQuery().CastRay(...)` for each ray
- [ ] 4.2 Filter by `occlusion_layer_mask` using Jolt `ObjectLayerFilter`
- [ ] 4.3 For each hit: look up `CollisionEvent.surface_type` → `PhysicsMaterialRegistry::get(material_path).density`
- [ ] 4.4 Density-weighted occlusion: `occlusion += min(1.0, density / 1000.0)` — air (0) → no occlusion; concrete (2400 kg/m³) → full occlusion
- [ ] 4.5 Cap total occlusion at 1.0

## 5. Voice Effects Application

- [ ] 5.1 Volume reduction: `voice.set_volume(source_volume * (1.0 - occlusion_factor * max_volume_reduction))` — `max_volume_reduction` default 0.85
- [ ] 5.2 LPF integration: add `UzLpfXapo` XAPO to occluded source voice
- [ ] 5.3 Implement `UzLpfXapo` — single-pole IIR low-pass filter; cutoff = `lerp(20000, 500, occlusion_factor)` Hz
- [ ] 5.4 Update LPF cutoff parameter per frame when occlusion factor changes

## 6. Lerp Smoothing

- [ ] 6.1 Implement exponential lerp: `current = lerp(current, target, 1.0 - exp(-lerp_speed * dt))`
- [ ] 6.2 Config: `occlusion_lerp_speed` from `.uzproj` (default: 5.0 — ~200ms to reach 63% of target)
- [ ] 6.3 Ensure lerp does not snap on scene load (initialize to computed value on first frame)

## 7. .uzproj Configuration

- [ ] 7.1 Extend `.uzproj` `[audio]` section: `occlusion_update_rate_hz = 10`, `occlusion_max_distance = 50`, `occlusion_ray_count = 3`, `occlusion_lerp_speed = 5.0`

## 8. Lua API

- [ ] 8.1 Implement `ctx.audio:set_occlusion_enabled(source_node_ref, bool)`
- [ ] 8.2 Implement `ctx.audio:get_occlusion_factor(source_node_ref) -> float` — current 0–1 factor
- [ ] 8.3 Implement `ctx.audio:set_occlusion_layer_mask(source_node_ref, mask)`

## 9. Testing

- [ ] 9.1 Unit test: `compute_occlusion` with direct path through air → 0.0
- [ ] 9.2 Unit test: `compute_occlusion` with single concrete wall → ≈ 0.85 occlusion factor
- [ ] 9.3 Unit test: throttle — `tick` does not raycast entity if updated within throttle interval
- [ ] 9.4 Unit test: lerp — occlusion_factor converges to target within 5 frames at lerp_speed=10
- [ ] 9.5 Unit test: LPF cutoff at full occlusion (1.0) = 500 Hz; at zero occlusion = 20000 Hz
- [ ] 9.6 Integration test: move wall between listener and source → occlusion_factor rises to > 0.5 within 200ms
- [ ] 9.7 Integration test: wall removed → occlusion_factor returns to 0 within 200ms

## 10. Documentation

- [ ] 10.1 Update `docs/specs/audio-system.md` — "Occlusion and Obstruction" section with config, Lua API, density table
- [ ] 10.2 Update `docs/guides/audio-guide.md` — "Setting Up Audio Occlusion" section
