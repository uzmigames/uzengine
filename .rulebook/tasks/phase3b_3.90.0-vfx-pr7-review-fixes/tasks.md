## 1. Critical Fixes (Review Blockers)
- [x] 1.1 Fix CSMain -> main in module_compositor.cpp/.h. ShaderManager entryPoint="main".
- [x] 1.2 Fix Y-up -> Z-up: gravity (0,0,-981) cm/s², spawnVelocity (0,0,1000) cm/s. Files: particle_emitter.h, update_module.h, vfx_graph.h, vfx_module.h.
- [x] 1.3 Revert non-VFX changes to main: grid_ps.hlsl, mesh_ps.hlsl, constant_buffers.h, sky_atmosphere_component.h, sky_light_component.h, dx12_agility.cpp, lua_vm.cpp.
- [x] 1.4 Implement DXC runtime compilation for composed VFX shaders. CompileComposedCS() + EmitterSimDispatch PSO fields.
- [x] 1.5 Remove inline point lights from PerFrameCB (reverted to falcor-parity state).

## 2. Major Fixes
- [x] 2.1 Fix metres -> centimetres: vfx_lod.h distances {5000,10000,20000}, significance kDistanceFalloff 0.0001, collision offset 1.0cm, minBounceSpeed 10.0 cm/s, softParticleFadeRange 100cm.
- [x] 2.2 Implement curve texture upload in vfx_curve.cpp BakeToTexture (staging buffer + MapBuffer).
- [x] 2.3 Implement config deserialization: IVFXModule::ApplyJsonConfig() virtual + UpdateModule override + vfx_graph.cpp call site.
- [x] 2.4 Remove build artifacts: build2/, build_decal_chk/, .claude/agent-memory/.
- [x] 2.5 Document slang dependency in thirdparty/README.md. Already in setup.ps1.

## 3. Material System Integration
- [x] 3.1 VFXMaterial inherits MaterialInterface. Implement virtual getters.
- [x] 3.2 Add EMaterialDomain::Particle to material_types.h + switch case.
- [x] 3.3 Write spec: docs/analysis/vfx/vfx-material-integration-spec.md.

## 4. Flipbook / SubUV
- [x] 4.1 Integrate vfx_flipbook.hlsli into particle_billboard_ps.hlsl.
- [x] 4.2 Add flipbookRows/Cols/TotalFrames to VFXBillboardCB.
- [x] 4.3 Implement SubUV blending: sample both frames, lerp by blendFactor.

## 5. Wiring & Integration
- [x] 5.1 Wire composed PSOs in dispatch (EmitterSimDispatch gains PSO fields, DispatchSpawn/Update check .IsValid()).
- [x] 5.2 Create vfx_common.hlsli (VFXSeed, RandomFloat, RandomInSphere, RandomOnSphere).
- [x] 5.3 Fix VFX demo: remove inline point light CB writes, remove grid reflection 7th arg, fix duplicate LogApp.

## 6. Merge & Verification
- [x] 6.1 Merge VFX into feature/falcor-parity (engine core files preserved from falcor-parity on all conflicts).
- [x] 6.2 Engine runtime builds clean (zig build EXIT 0).
- [x] 6.3 PBR showcase renders correctly (5 draws, 85K vertices, 357 FPS).
- [x] 6.4 VFX demo builds and runs (523 FPS, UI functional, shaders compile).

## 7. Remaining (GPU Pipeline Debug)
- [ ] 7.1 Debug why particles are not visible: spawn dispatch runs but alive count stays 0. Investigate free list initialization, UAV barrier ordering, readback timing.
- [ ] 7.2 Verify billboard VS expands quads from alive particle data.
- [ ] 7.3 Capture RenderDoc frame to inspect compute shader UAV writes.
- [ ] 7.4 Update VFX unit tests for new defaults (cm, Z-up) — 20 test failures expected.
