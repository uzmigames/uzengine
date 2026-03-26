## 1. Infrastructure — Mock RHI & Engine Integration

- [ ] 1.1 Extract shared mock RHI device to `runtime/demos/common/mock_device.h`. Consolidate the 10 duplicate DemoMockDevice implementations into a single reusable header. Update all demos to `#include "mock_device.h"`.
- [ ] 1.2 Integrate VFXSystem lifecycle into SceneRenderer. Add `m_vfxSystem` ownership to SceneRenderer, call `Initialize()` in `SceneRenderer::Init()`, `Tick(dt)` in `RenderFrame()` setup phase, `Shutdown()` in `SceneRenderer::Shutdown()`. Gate behind a config flag.
- [ ] 1.3 Add VFX emitter ECS component (`VfxEmitterComponent`) to `engine/ecs/environment/`. Stores emitter config reference (asset path) + runtime state. SceneRenderer reads these components during frame setup.
- [ ] 1.4 Wire `RegisterParticlePasses()` into the render graph frame pipeline (after lighting, before post-process). Currently declared but possibly not called in the main frame flow.

## 2. Asset Format — .uzfx Emitter Definitions

- [ ] 2.1 Define `.uzfx` INI/JSON asset format for particle emitter configurations. Fields: modules[], max_particles, emission_rate, lifetime, gravity, damping, spawn_velocity, blend_mode, texture_path, flipbook config, etc.
- [ ] 2.2 Implement `UzfxLoader` in `engine/vfx/asset/` that parses `.uzfx` files and constructs a `ParticleEmitter` + module stack via `ModuleCompositor`.
- [ ] 2.3 Register `.uzfx` extension in VFS for asset discovery and hot-reload.
- [ ] 2.4 Create `.uzfx` files for each demo effect: particle_fountain.uzfx, fire_effect.uzfx, explosion.uzfx, attractor.uzfx, orbit.uzfx, sub_emitter.uzfx, flipbook.uzfx, vector_field.uzfx, distortion.uzfx.

## 3. Demo Conversion — Thin Consumers

- [x] 3.1 Convert `particle_fountain` demo: loads from assets/particle_fountain.uzfx via UzfxLoader, keeps sort buffer + spawn count validation.
- [x] 3.2 Convert `fire_effect` demo: loads from assets/fire_effect.uzfx, keeps ModuleCompositor HLSL composition + spawn validation.
- [x] 3.3 Convert `explosion_composite` demo: loads 3 .uzfx files (fire/smoke/debris), keeps burst event + multi-emitter validation.
- [x] 3.4 Convert `attractor_demo`: loads from assets/attractor_demo.uzfx, finds AttractorModule in stack for validation.
- [x] 3.5 Convert `orbit_demo`: loads from assets/orbit_demo.uzfx, finds orbit/pin/distance_constraint modules for validation.
- [x] 3.6 Convert `sub_emitter_demo`: loads 3 .uzfx files (rocket/burst/sparkle), keeps manual readback cascade validation.
- [x] 3.7 Convert `flipbook_animation`: loads from assets/flipbook_animation.uzfx, validates flipbook frame selection from loaded sprite module.
- [x] 3.8 Convert `vector_field_demo`: loads from assets/vector_field_demo.uzfx, validates VectorFieldModule params + shader code.
- [x] 3.9 Convert `distortion_effect`: loads 2 .uzfx files (heat_shimmer/glass_sphere), keeps DistortionConstants struct validation.
- [ ] 3.10 Convert `trails` demo: use TrailSystem from engine, load config from asset. ~668 → ~40 lines.
- [ ] 3.11 Convert `decals` demo: use DecalManager from engine, load config from asset. ~893 → ~40 lines.
- [ ] 3.12 Convert `vfx_advanced` demo: use engine LOD + budget systems, load config from asset. ~448 → ~40 lines.
- [ ] 3.13 Verify `vfx_showcase` Lua script works with engine-integrated VFX system.

## 4. Test Extraction

- [ ] 4.1 Move spawn count validation (from particle_fountain) to `tests/engine/vfx/test_vfx_spawn.cpp`.
- [ ] 4.2 Move budget allocation/return validation to `tests/engine/vfx/test_particle_budget.cpp` (may already exist).
- [ ] 4.3 Move module compositor validation (fire_effect module stack) to `tests/engine/vfx/test_module_compositor.cpp`.
- [ ] 4.4 Move trail ribbon mesh validation to `tests/engine/vfx/test_trail_system.cpp`.
- [ ] 4.5 Move decal projection/blend validation to `tests/engine/vfx/test_decal_system.cpp`.
- [ ] 4.6 Ensure all existing VFX tests still pass after refactor.

## 5. Documentation

- [ ] 5.1 Document `.uzfx` asset format in `docs/specs/particle-system.md`.
- [ ] 5.2 Update `docs/guides/` with VFX demo authoring guide (how to create a new VFX demo using .uzfx).
- [ ] 5.3 Add VFX subsystem to MCP docs index (currently not indexed).
