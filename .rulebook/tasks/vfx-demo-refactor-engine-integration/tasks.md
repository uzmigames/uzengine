## 1. Infrastructure — Mock RHI & Engine Integration

- [x] 1.1 Extract shared mock RHI device to `runtime/demos/common/mock_device.h`. Consolidated 10 duplicate DemoMockDevice into single reusable header. All demos updated.
- [x] 1.2 Integrate VFXSystem lifecycle into SceneRenderer. Owned via unique_ptr, Init/Tick/Shutdown wired. m_vfxSystemOverride kept for backward compat.
- [x] 1.3 Add VfxEmitterComponent ECS component to `engine/ecs/environment/`. JSON serialize/deserialize via UZ_REGISTER_COMPONENT_SERIALIZER. Scene key: "vfx_emitter".
- [x] 1.4 Wire `RegisterParticlePasses()` into render graph frame pipeline. Called after lighting, before post-process.

## 2. Asset Format — .uzfx Emitter Definitions

- [x] 2.1 Define `.uzfx` JSON asset format. Supports emitter base params + spawn/update/render module descriptors. Spec: `docs/specs/uzfx-format.md`.
- [x] 2.2 Implement `UzfxLoader` in `engine/asset/formats/`. Load() parses JSON → UzfxDesc, Instantiate() creates ParticleEmitter with module stack.
- [ ] 2.3 Register `.uzfx` extension in VFS for asset discovery and hot-reload. Deferred — demos currently use direct file reads.
- [x] 2.4 Created 14 `.uzfx` files across 9 demos (including multi-emitter: explosion 3, distortion 2, sub_emitter 3).

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
- [x] 3.10 Convert `trails` demo: already clean (no inline mock, no duplicated engine logic). No changes needed.
- [x] 3.11 Convert `decals` demo: fixed DemoMockDevice namespace qualification. Already used shared mock_device.h.
- [x] 3.12 Convert `vfx_advanced` demo: added phase_lod.cpp + phase_budget.cpp source files, updated build.zig.
- [ ] 3.13 Verify `vfx_showcase` Lua script works with engine-integrated VFX system. Deferred — requires runtime Lua VFX API testing.

## 4. Test Extraction

- [x] 4.1 Spawn count validation already covered by `tests/engine/vfx/test_particle_emitter.cpp` + `test_vfx_system.cpp`.
- [x] 4.2 Budget allocation/return already covered by `tests/engine/vfx/test_particle_budget.cpp` + `test_vfx_budget.cpp`.
- [x] 4.3 Module compositor already covered by `tests/engine/vfx/test_module_compositor.cpp`.
- [x] 4.4 Trail system already covered by `tests/engine/vfx/test_trail_system.cpp` + 4 other trail test files.
- [x] 4.5 Decal system already covered by `tests/engine/vfx/test_decal_manager.cpp` + `test_decal_pool.cpp` + `test_decal_component.cpp`.
- [x] 4.6 Added `tests/engine/vfx/test_uzfx_loader.cpp` — 5 tests for UzfxLoader (Load valid/empty/invalid JSON, Instantiate, all module types).

## 5. Documentation

- [x] 5.1 Created `docs/specs/uzfx-format.md` — full .uzfx format specification with all module types, fields, defaults, and ECS integration.
- [x] 5.2 Created `docs/guides/vfx-demo-authoring.md` — step-by-step guide for creating new VFX demos using .uzfx assets.
- [ ] 5.3 Add VFX subsystem to MCP docs index (currently not indexed). Requires re-running mcp doc generator.

## 6. VSCode Extension

- [x] 6.1 Added `.uzfx` file extension support: JSON syntax highlighting + custom particle-burst SVG icon.
- [x] 6.2 Added `.uzimport` file extension support: JSON syntax highlighting + custom import-arrow SVG icon.
