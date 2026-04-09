## 1. GPU Particle Sort

- [x] 1.1 Create runtime/shaders/vfx/particle_sort_cs.hlsl — Bitonic merge sort (Batcher 1968) with two phases via sortPhase constant. Phase 0: local sort + key generation (dot product depth, FloatToSortableUint, descending for back-to-front). Phase 1: global merge passes. 256 threads/group, groupshared memory for local sort, UAV barriers between dispatches. 381 lines.
- [x] 1.2 Create runtime/engine/vfx/core/gpu_particle_sort.h/.cpp — GPUParticleSort with Create()/Destroy()/DispatchSort(). Allocates sortKeys + sortIndices RWStructuredBuffer<uint> (power-of-two padded). DispatchSort fills VFXSortConstants (80 bytes), dispatches phase 0 then nested loop of phase 1 global merges with UAV barriers.
- [x] 1.3 Integrate sorted index buffer with billboard and mesh render passes — Added GPUParticleSort to GPUParticleBuffer (auto-created with emitter). Added GetParticleSortCS() to PipelineLibrary. Added VFX_ParticleSort render graph compute pass between sim and render in frame_particles.cpp. Extended VFXBillboardIndices CB with sortedIndicesSrvIndex. Added rgSortIndices to EmitterRenderData with pb.Read(SRV) barrier tracking. Updated buffer count test (20→22).

## 2. Flipbook Animation

- [x] 2.1 Create runtime/shaders/vfx/vfx_flipbook.hlsli — FlipbookResult struct, ComputeFlipbookFrame (age*fps animated), ComputeFlipbookFrameRandom (Wang hash), SampleFlipbookUV (atlas sub-rect remap). Division-by-zero guards, #ifndef include guard. 231 lines.
- [x] 2.2 Add flipbook parameters to SpriteRenderModule — Added FlipbookBlendMode enum (None/Linear/Random), 5 fields (rows, cols, totalFrames, fps, blendMode) with defaults. HasFlipbook() helper. Updated GetParameters() (8 params), GetShaderCode(). 15 new tests.
- [x] 2.3 Sub-UV blending in particle_billboard_ps.hlsl — Included vfx_flipbook.hlsli. Extended PSInput (age, lifetime, particleIdx), VFXBillboardPS_CB (+32 bytes flipbook params). Sprite sampling branches on flipbook grid size: 1x1=original path, NxM=flipbook with blend mode selection. Both UZ_BINDLESS and SM5 paths.

## 3. Distortion / Refraction

- [x] 3.1 Create runtime/engine/vfx/passes/distortion_pass.h/.cpp — DistortionConstants (16 bytes), DistortionEmitterData, DistortionPassDesc. AddDistortionAccumulationPass writes RGBA16F target, reads depth, sorts emitters back-to-front, DrawIndexedIndirect per emitter.
- [x] 3.2 Create runtime/shaders/vfx/distortion_ps.hlsl — Samples normal map, decodes tangent-space XY as UV offset, scales by DISTORTION_UV_SCALE (0.01) * strength. Outputs float4(offset.xy, 0, intensity). UZ_BINDLESS + SM5 paths.
- [x] 3.3 Create distortion resolve pass — distortion_resolve_pass.h/.cpp + distortion_resolve_ps.hlsl. Fullscreen triangle pass reads distortion texture + pre-distortion HDR copy, warps UV by offset * alpha, outputs warped color.

## 4. Motion Vectors for TAA

- [x] 4.1 Add motion vector output to billboard VS — Extended particle_billboard_vs.hlsl with prevPositionCS output (TEXCOORD5) computed from particlePos - velocity*dt through prevViewProj. Extended VFXBillboardIndices with velocity/ages/lifespans SRV indices. Added prevViewProj to VFXEmitterCB (b2).
- [x] 4.2 Integrate VFX motion vectors with existing TAA pass — Created vfx_motion_vectors.hlsli with ComputeMotionVector(currentCS, prevCS) using NDC-to-UV with DX Y-flip. Updated particle_billboard_ps.hlsl and frame_particles.cpp for interface compatibility. CPU-side prevViewProj wiring noted as gap for runtime integration.

## 5. Testing

- [x] 5.1 Unit tests for GPU radix sort — 42 tests: NextPowerOfTwo (10), VFXSortConstants layout (7), GPUParticleSort defaults (6), Create/Destroy with MockDevice (18), kSortGroupSize (1). All pass.
- [x] 5.2 Unit tests for flipbook frame calculation — 38 tests: FrameIndexToUV (6), WangHash (5), ComputeFlipbookFrame (13), ComputeFlipbookFrameRandom (8), SampleFlipbookUV (5). CPU mirrors of HLSL math. All pass.
- [x] 5.3 Integration test for distortion pass — 30 tests: DistortionConstants layout (6), DistortionResolveConstants (5), EmitterData defaults (6), PassDesc (3), ResolveDesc (3), back-to-front sort (4), MockDevice integration (2). All pass.
- [ ] 5.4 Visual test: alpha-blended particles with sort enabled vs disabled — BLOCKED: requires VFX integrated into SceneRenderer with real GPU rendering (same blocker as 3.66.0 tasks 7.5/8.x)

## 6. Demo

- [x] 6.1 Create flipbook demo — demos/flipbook_animation/ with 4x4 fire sprite sheet, FlipbookBlendMode::Linear at 24fps, headless validation of HasFlipbook(), module stack, and parameter inspection.
- [x] 6.2 Create distortion demo — demos/distortion_effect/ with heat_shimmer + glass_sphere emitters, validates DistortionConstants/ResolveConstants struct sizes and field defaults.
- [x] 6.3 Update particle_fountain with GPU sort — Added gpu_particle_sort.h include, sort buffer validation (IsValid, paddedCount == NextPowerOfTwo), sort memory reporting in summary. Updated build.zig with gpu_particle_sort.cpp source.

## Shader Standards Compliance

- [x] Fix entry point CSMain → main in 4 compute shaders (particle_indirect_args_cs, particle_mesh_instance_cs, particle_spawn_cs, particle_update_cs). UzEngine ShaderManager requires entryPoint="main".
- [x] Add #ifdef UZ_BINDLESS guards + SM5 fallback register bindings to 6 shaders (particle_indirect_args_cs, particle_mesh_instance_cs, particle_sort_cs, particle_spawn_cs, particle_update_cs, particle_mesh_vs). Each shader now has SM5 register layout comment block and explicit t0/u0 style fallback declarations.
- [x] Verified 3 shaders already compliant: decal_deferred_ps, decal_deferred_vs, trail_vs (no ResourceDescriptorHeap, entry point=main).

## 7. Documentation

- [x] 7.1 Document GPU sort algorithm and performance characteristics — Created docs/guides/vfx-gpu-particle-sort.md with bitonic merge sort algorithm, phase 0/1 dispatch pattern, buffer layout, performance analysis (O(n log²n)), and API reference.
- [x] 7.2 Document flipbook/SubUV configuration guide — Created docs/guides/vfx-flipbook-subuv-guide.md with blend modes, frame ordering, HLSL functions, usage examples, and edge cases.
- [x] 7.3 Document distortion rendering pipeline — Created docs/guides/vfx-distortion-pipeline.md with architecture overview, two-pass pipeline explanation, accumulation pass details, resolve pass details, shader algorithms, render graph integration, implementation examples, performance considerations, troubleshooting guide, and references.

## 8. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 8.1 Update or create documentation covering the implementation
- [ ] 8.2 Write tests covering the new behavior
- [ ] 8.3 Run tests and confirm they pass
