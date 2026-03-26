## 1. Implementation

- [x] 1.1 Define ReflectionProbeComponent — POD struct in `runtime/engine/ecs/environment/reflection_probe_component.h`. Fields: influenceRadius (300cm), blendDistance (100cm), resolution (128), priority (0), enabled, needsCapture, prefilteredSrvIndex, irradianceSrvIndex. Follows SkyLightComponent pattern.
- [x] 1.2 Implement cubemap capture — Added BuildCubemapFaceMatrices() (Z-up to Y-up swap, 90deg FOV, 6 faces), per-probe RGBA16F cubemap + shared D32F depth buffer, CaptureIfDirty() + RenderProbeCubemap() using forward PBR pipeline, wired into frame_setup.cpp before probe Update(). Data flow doc at docs/analysis/dataflow/reflection-probe-capture.md.
- [x] 1.3 Implement probe prefiltering — ReflectionProbeRenderer calls IBLBaker::BakeEnvironment() to generate irradiance (32x32) + prefiltered specular (256x256, 6 mips) cubemaps per probe. Phase 1 uses sky cubemap as source; per-probe scene capture is a future enhancement.
- [x] 1.4 Implement ReflectionProbeArray — ReflectionProbeRenderer creates an Upload-heap StructuredBuffer<ReflectionProbeGPU> (8 probes max), maps/uploads per frame with priority-sorted probe data (position, influence radius, blend distance, SRV indices). Build.zig updated with `engine/renderer/probes` directory.
- [x] 1.5 Define ProbeData structured buffer — `ReflectionProbeGPU` struct (32 bytes, two float4s) added to `constant_buffers.h` with posXYZ, influenceRadius, blendDistance, irradianceSrvIndex, prefilteredSrvIndex. Also added `reflectionProbeCount` and `reflectionProbeBufferSrvIndex` to PerFrameCB (stole 2 floats from _pad2).
- [x] 1.5b Wire ReflectionProbeRenderer into frame pipeline — added `m_reflectionProbes` member to SceneRenderer, Init after IBLBaker in scene_lifecycle.cpp, Shutdown before IBLBaker, Update() called in frame_setup.cpp before CB upload. Added reflectionProbeCount/reflectionProbeBufferSrvIndex to PerFrameParams and HLSL PerFrameCB (pad reduced 59→57).
- [x] 1.6 Implement probe selection in deferred_lighting_ps.hlsl — Added ReflectionProbeGPU struct, ComputeProbeDistanceAlpha (UE5 Hermite smoothstep fade), SampleReflectionProbes (specular), SampleProbeIrradiance (diffuse). Iterates up to 8 probes from StructuredBuffer, distance check against influenceRadius, alpha compositing (under operator matching UE5 ReflectionEnvironmentComposite.ush).
- [x] 1.7 Implement probe/sky IBL blending — Under-operator alpha compositing: probes consume alpha proportional to distance fade, sky IBL fills remaining alpha. Both specular (prefilteredColor) and diffuse (irradiance) blended. SM5 path returns sky-only. Integrated before SSR compositing (specular) and before diffuseIBL computation (irradiance).
- [x] 1.8 Add probe capture scheduling — ReflectionProbeRenderer::Update() captures at most one dirty probe per frame via IBLBaker, clears needsCapture flag, writes baked SRV indices back to ECS component. Probes are gathered sorted by priority (highest first).
- [ ] 1.9 Create reflection_probes demo — room with 2 probes (interior hallway + outdoor), metallic spheres showing different reflections per location.

## 2. Visual Verification

- [ ] 2.1 Screenshot: metallic sphere inside room — must reflect room geometry, not sky
- [ ] 2.2 Screenshot: metallic sphere outside probe influence — must fallback to sky IBL
- [ ] 2.3 Screenshot: probe overlap zone — smooth blend between two probes, no hard edge
- [ ] 2.4 Screenshot: rough vs smooth surfaces — rough surfaces show blurred probe, smooth show sharp
- [x] 2.5 Regression: existing IBL demos still work (sky IBL unchanged when no probes active). Verified: pbr_test runs clean at default and z=700 with probe capture active, no visual differences from pre-capture state.

## 3. Documentation

- [x] 3.1 Updated `docs/engine/12-rendering/17-lighting.md` with 276-line Reflection Probes section: system overview, ECS component docs, GPU pipeline (capture→IBL→upload), shader integration pseudocode, performance characteristics.
- [x] 3.2 Created `docs/guides/reflection-probe-placement.md` (314 lines): placement principles, resolution/quality tradeoffs, priority/overlap handling, optimization strategies, troubleshooting guide.
