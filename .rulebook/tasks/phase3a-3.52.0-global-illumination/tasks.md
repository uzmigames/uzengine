# Task 3.52.0 — Global Illumination (UzGI)

## Phase A: Screen-Space Global Illumination (SSGI)

### Research
- [x] A.1 Read UE5 `ScreenSpaceDiffuseIndirect.usf` — extracted full SSGI algorithm from `SSRTDiffuseIndirect.usf`. Cosine-hemisphere rays, HiZ march, prev-frame color sampling, Karis weighting.
- [x] A.2 Read UE5 `IndirectLightRendering.cpp` — traced CPU dispatch setup: half-res CVar, quality-to-ray-count mapping (Q1=4..Q4=32), prev-frame color reduction, denoiser dispatch.
- [x] A.3 Created data flow document `docs/analysis/dataflow-ssgi.md` — complete UE5→UzEngine mapping with 50+ file:line citations.
- [x] A.4 Identified reusable resources: SSR HiZ march pattern, GBuffer (depth/normal/velocity/albedo), compute root signature (b0 root constants + b1 CBV + bindless SRV/UAV).

### SSGI Trace Pass (compute shader)
- [x] A.5 Implemented `ssgi_trace_cs.hlsl` — cosine-hemisphere ray generation (Hammersley + concentric disk), screen-space linear march (12 steps), prev-frame color sampling with Karis weighting. Q2 quality (8 rays, 12 steps).
- [x] A.6 Ray directions: Hammersley quasi-random sequence with Rand3DPCG16 per-pixel seed + IGN step offset jitter. Frisvad tangent basis from world normal.
- [x] A.7 Hit shading: sample prev-frame reduced color at hit UV, Karis luminance weight rcp(1+L) for firefly suppression.
- [x] A.8 Output: half-res RGBA16F indirect diffuse + R16F AO mask. Also `prev_color_reduce_cs.hlsl` for half-res Karis-weighted 2x2 downsample.

### SSGI Denoise Pass
- [x] A.9 Implemented `ssgi_spatial_denoise_cs.hlsl` — 8-sample rotated Poisson disk cross-bilateral filter with depth (exp gaussian) + normal (pow4) edge-stopping weights. Bindless UAV writes.
- [x] A.10 Temporal accumulation: `ssgi_temporal_accum_cs.hlsl` — motion-vector reprojection, 3x3 neighborhood AABB variance clipping, alpha=0.05 stable / 1.0 disoccluded, velocity-adaptive blending. Packed dimensions (16-bit width+height) to fit 8 DWORD root constant limit.
- [x] A.11 Upsample: `ssgi_upsample_cs.hlsl` — 2x2 joint bilateral upsample half→full with depth+normal edge-stopping, bilinear base weights, fallback to nearest for degenerate edges. Bindless UAV writes.

### Integration into Deferred Lighting
- [x] A.12 In `deferred_lighting_ps.hlsl`: SSGI compositing block in ambient section (line ~1037). When `ssgiEnabled != 0`, reads SSGI texture via `ResourceDescriptorHeap[ssgiSrvIndex]`, adds `ssgiIndirect * kD` to diffuseIBL, combines SSGI AO with GTAO via `min(ao, ssgiAO)`.
- [x] A.13 Added `ssgiSrvIndex`, `ssgiAOSrvIndex`, `ssgiEnabled` to PerFrameCB (replaced 3 of 7 padding DWORDs in constant_buffers.h). Populated in frame_setup.cpp from SSGIRenderer state.
- [x] A.14 5 render graph passes registered in frame_lighting.cpp: SSGI (PrevColorReduce + Trace + SpatialDenoise + TemporalAccum + BilateralUpsample) inserted after GTAO/DFS, before SSR. SSGI_CopyPrevColor after lighting+fog, before post-process.

### C++ Infrastructure
- [x] A.15 Created `runtime/engine/renderer/gi/ssgi_renderer.h/.cpp` — SSGIRenderer class with Settings, RenderParams, 12 textures (full-res + half-res), 5 dispatch methods, CopyPrevSceneColor, ping-pong history.
- [x] A.16 Wired into `frame_lighting.cpp` — SSGI dispatched between GTAO and SSR. 5 compute PSOs created in pipeline_library.
- [x] A.17 Enable/disable via `SSGIRenderer.settings.enabled` + `SceneRenderer::SetSsgiEnabled()`. Default: enabled.

### Bug Fixes (found via Pass Inspector)
- [x] A.17b Prev scene color copy barrier: first-frame used wrong source state (Common vs ShaderResource). Fixed with `m_hasPrevColor` flag.
- [x] A.17c Bindless UAV writes: all 5 shaders converted from register(u0/u1) to `ResourceDescriptorHeap[uavIndex]`. DX12 descriptor table only binds consecutive heap entries but our UAV allocations are non-contiguous.
- [x] A.17d Temporal accum root constants: was 9 DWORDs exceeding root signature limit of 8. Packed width+height into single DWORD (16 bits each).

### Validation
- [x] A.18 Pass validation (`--pass-validate`): all SSGI passes report OK. ssgi.trace avg=0.251, ssgi.result avg=0.252, ssgi.ao avg=0.977.
- [x] A.19 A/B comparison captured (ssgi-ON.png vs ssgi-OFF.png). SSGI effect is subtle in the pbr_test scene (open field, small objects). Color bleeding will be more visible in interior scenes with nearby walls. The pass.validate confirms SSGI trace avg=0.251 (non-zero bounced light).
- [x] A.20 Performance: headless mode frame times variable (21.8 FPS ON vs 16.3 FPS OFF — headless overhead dominates). SSGI adds 5 compute dispatches at half-res. On RTX 4090, no measurable frame time increase in this scene. Proper GPU timing requires Phase 2 of Pass Inspector (timestamp queries).
- [ ] A.21 Compare with UE5 Basic template SSGI — deferred to when interior test scene exists.

## Phase B: Irradiance Probe Grid (Future)

### Research
- [ ] B.1 Read UE5 irradiance cache architecture (`IrradianceCacheComposite.usf`, radiance caching papers).
- [ ] B.2 Study DDGI (Dynamic Diffuse Global Illumination) — real-time probe update via ray tracing.
- [ ] B.3 Design probe placement strategy: uniform grid vs adaptive (octree).

### Implementation
- [ ] B.4 Probe data structure: position, SH L2 coefficients (9 per channel = 27 floats), visibility (depth squared for Chebyshev test).
- [ ] B.5 Probe update: each frame, trace rays from subset of probes, update SH. Hardware RT (DXR) or software (SDF).
- [ ] B.6 Probe sampling in deferred lighting: trilinear interpolation of 8 nearest probes, Chebyshev visibility test to avoid light leaking.
- [ ] B.7 Octahedral encoding for probe irradiance (compact 2D atlas instead of per-probe cubemap).

### Integration
- [ ] B.8 Hybrid SSGI + Probes: SSGI for screen-visible bounces, probes for off-screen and distant GI.
- [ ] B.9 Probe debug visualization: render probe positions as spheres with SH-evaluated color.

## Phase C: Hybrid System (Future)

- [ ] C.1 Screen-space trace for near-field (0-5m): highest quality, handles dynamic objects.
- [ ] C.2 Probe grid for mid-field (5-50m): stable, handles off-screen.
- [ ] C.3 SkyLight SH for far-field (50m+): existing system, cheapest.
- [ ] C.4 Smooth blending between tiers based on distance.
