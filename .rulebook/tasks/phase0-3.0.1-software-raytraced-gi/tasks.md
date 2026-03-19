# Task 3.54.0 — Software Ray-Traced Global Illumination

## 1. Mesh SDF Generation

- [x] 1.1 Read UE5 `MeshDistanceFieldUtilities.cpp` and `DistanceFieldAtlas.cpp` — extract the SDF generation pipeline (voxelization, JFA distance transform, atlas packing). Analyzed UE5 DistanceFieldAtlas.h:36-50, MeshDistanceFieldCommon.ush:9-38, LumenSoftwareRayTracing.ush:124-259, Common.ush:1883-1903. Created dataflow doc at docs/analysis/dataflow-sdf-gi-pipeline.md.
- [x] 1.2 Create `runtime/engine/renderer/gi/mesh_sdf_baker.h/.cpp` — MeshSdfBaker class with BakeMesh() (CPU dense SDF) and UploadToGPU() (R16F 3D texture). MeshSDFData struct for CPU data, MeshSDF struct for GPU handle.
- [x] 1.3 Implement CPU-side SDF generation: spatial hash accelerated nearest-triangle queries, pseudo-normal sign determination, dense float grid output. No JFA needed — direct distance computation with O(V*T/B) complexity.
- [ ] 1.4 Implement GPU-accelerated SDF generation (compute shader) for runtime baking — voxelize triangles via conservative rasterization or point splatting, then JFA compute passes.
- [x] 1.5 Per-mesh SDF storage: `MeshSDF` struct with R16F 3D texture handle, SRV index, volumeOrigin, volumeExtent, resolution. Created in mesh_sdf_baker.h.
- [ ] 1.6 SDF atlas: pack multiple mesh SDFs into a single 3D texture atlas (or use individual textures with bindless access). UE5 uses a combined atlas (`DistanceFieldAtlas.cpp:234`).

## 2. Global Distance Field

- [x] 2.1 Read UE5 `GlobalDistanceField.cpp` — extract the clipmap structure (4 cascades, centered on camera, each 128^3 voxels). Analyzed GlobalDistanceField.cpp:23-46, 120-126, 383-403, 446-457, GlobalDistanceFieldUtils.ush:90-200, GlobalDistanceFieldShared.ush:183-247.
- [x] 2.2 Create `runtime/engine/renderer/gi/global_distance_field.h/.cpp` — GlobalDistanceField class with 4 cascaded R16F 3D textures (64^3 simplified from UE5's 128^3 paged). Cascades: 2500/10000/40000/160000 cm extents. Camera-tracking with voxel-snapped centers, dirty flags, per-clipmap compute dispatch.
- [x] 2.3 Implement cascade update compute shader `global_sdf_composite_cs.hlsl` — [numthreads(4,4,4)] per-voxel composite: computes world position from clipmap parameters, iterates mesh SDFs via cbuffer array, transforms world to local space, samples SDF, stores minimum distance. AABB early-out culling per mesh.
- [x] 2.4 Clipmap scrolling: implemented in GlobalDistanceField::Update() — snaps center to voxel grid, marks dirty when camera moves >= 1 voxel. Full recomposite on dirty (partial border-only update deferred to optimization pass).
- [x] 2.5 Composite: SampleGlobalSdfClipmap() and SphereMarchGlobalSDF() added to sdf_common.hlsli. Cascaded trace from finest to coarsest, surface expansion, normal estimation via central differences. 128 max steps.

## 3. SDF Ray Tracer

- [x] 3.0 Create `runtime/shaders/runtime/include/sdf_common.hlsli` — SDF sampling, LineBoxIntersect, SphereMarchMeshSDF utilities. UE5 sources: Common.ush:1883-1903, MeshDistanceFieldCommon.ush:9-38, LumenSoftwareRayTracing.ush:124-259.
- [x] 3.1 Read UE5 `LumenTracingCommon.ush` and `GlobalDistanceFieldUtils.ush` — extracted sphere marching against cascaded GDF. SphereMarchGlobalSDF() in sdf_common.hlsli implements per-cascade box clipping, surface expansion, and cascade fallthrough.
- [x] 3.2 SDF trace functions in `sdf_common.hlsli` — SampleGlobalSdfClipmap(), EstimateGlobalSdfNormal(), SphereMarchGlobalSDF(). Used by radiance cache trace shader. No standalone sdf_trace_cs needed — trace is embedded in radiance_cache_trace_cs.hlsl.
- [x] 3.3 Ray parameters: max distance = grid diameter (configurable per consumer), max steps = 128, hit from surface expansion (voxelExtent * saturate(t/(2*voxelExtent))). Normal via central differences.
- [ ] 3.4 Material fetch at hit: read the GBuffer at the hit screen position (if visible) or use a simplified albedo from the mesh SDF metadata. UE5 uses surface cache for this.
- [ ] 3.5 Cone tracing variant: instead of single ray, trace a cone (wider = softer GI). Cone opening angle based on distance for LOD-like behavior. UE5 `TraceMeshSDFs()` uses cone tracing.

## 4. Radiance Cache (Irradiance Probes)

- [x] 4.1 Read UE5 `LumenRadianceCache.usf` and `LumenRadianceCacheInterpolation.ush` — extracted probe grid structure (clipmap layout, cell size, trilinear interpolation, sphere parallax). Analyzed lines 1-95, 114-128, 154-172, 221-395.
- [x] 4.2 Create `runtime/engine/renderer/gi/radiance_cache.h/.cpp` — RadianceCache class. 16^3 = 4096 probes in uniform grid. L2 SH coefficients packed into 7 float4s per probe (UE5 SkyIrradianceEnvironmentMap packing). StructuredBuffer<float4> with SRV+UAV.
- [x] 4.3 Probe grid layout: single uniform grid centered on camera, 16^3 probes, configurable extent (default 5000cm = 50m). ProbeSpacing = (extent*2)/16. Camera-tracking with grid-snapped center. ~449 KB GPU memory.
- [x] 4.4 Probe update compute shader `radiance_cache_trace_cs.hlsl`: [numthreads(64,1,1)], one thread per probe. Traces 32 rays via Fibonacci spiral with temporal jitter, accumulates irradiance into L2 SH (9 basis functions), normalizes by 4*pi/N, packs into 7 float4s, blends with history via EMA (alpha=0.05).
- [ ] 4.5 Probe visibility: store mean depth and depth^2 per direction for Chebyshev inequality test. This prevents light leaking through walls by rejecting probes that are occluded from the shading point.
- [x] 4.6 Probe atlas: StructuredBuffer<float4> with kTotalProbes * 7 elements. Access: probeSHBuffer[probeIndex * 7 + i]. SRV for deferred lighting, UAV for trace compute.

## 5. Deferred Lighting Integration

- [x] 5.1 Read UE5 `LumenScreenProbeGather.usf` — analyzed compositing (skipped screen probes per task spec Phase D). Direct radiance cache → deferred lighting integration.
- [x] 5.2 Created `runtime/shaders/runtime/include/radiance_cache_common.hlsli` — EvaluateProbeSH() (L0+L1+L2 from packed 7 float4s) + SampleRadianceCache() (trilinear interpolation of 8 corner probes). Inline in deferred_lighting_ps.hlsl for the initial path.
- [x] 5.3 Modified `deferred_lighting_ps.hlsl` — added radiance cache sampling after SSGI compositing, before final ambient calculation. PerFrameCB extended with radianceCacheEnabled, rcProbeSHBufferSrv, rcProbesPerAxis, rcGridExtent, rcGridOrigin. Trilinear probe interpolation + L0+L1 SH evaluation + distance-based blend with SH IBL.
- [x] 5.4 Distance-based blending: `rcWeight = saturate(1 - distFromCenter / rcGridExtent)`. Smooth falloff from radiance cache to sky SH IBL at grid boundary.
- [ ] 5.5 Intensity control: `r.GI.Intensity` CVar (default 1.0) scales the combined GI contribution.

## 6. Render Pipeline Integration

- [x] 6.1 Register passes in `frame_lighting.cpp`: GDF_Composite → RadianceCache_Trace, guarded by m_sdfGiEnabled + IsInitialized() + bindless + PassInspector. Runs after SSGI, before SSR and deferred lighting. GDF_Composite now collects mesh SDFs and world transforms from ECS (StaticMeshComponent + SceneComponent iteration) and passes them to CompositeFromMeshSDFs(). m_sdfGiEnabled auto-set to true when both GDF and RadianceCache initialize.
- [x] 6.2 Resource creation in `scene_lifecycle.cpp`: GlobalDistanceField (4 cascades × 64^3 × R16F = ~2MB) + RadianceCache (4096 probes × 7 float4 × 16B = ~449KB). Created after SSGI, destroyed before SSR in Shutdown(). m_sdfGiEnabled flag now auto-enabled when resources initialize.
- [x] 6.3 Per-mesh SDF baking: on mesh load, bake SDF via MeshSdfBaker::BakeMesh() (32^3 resolution) and upload R16F 3D texture via UploadToGPU(). Added MeshSDF field to GPUMesh. Baking integrated into LoadMesh() and FlushPendingLoads() in scene_assets.cpp. SDF textures cleaned up in DestroyGPUMesh(). Fixed 3D texture copy (single subresource, not per-slice). Fixed staging buffer lifetime (flush command list before destroy). Disk caching (.uzsdf) deferred to optimization pass.
- [x] 6.4 Pipeline cache: added GetGdfComposite() and GetRadianceCacheTrace() to PipelineLibrary (pipeline_postprocess.cpp). Auto-compiled from embedded HLSL via EngineShader::GlobalSdfCompositeCS / RadianceCacheTraceCS. Fixed shader entry point mismatch (CSMain → main) to match engine convention. Fixed entry point spec in pipeline_postprocess.cpp (CSMain → main).
- [ ] 6.5 Debug visualization: `vis gi.radiance_cache` shows probe positions as colored spheres. `vis gi.global_sdf` shows SDF distance as heatmap slice.

## 7. Testing

- [ ] 7.1 Unit test: SDF generation for a cube mesh — verify distances match analytical SDF (max error < 1 voxel).
- [ ] 7.2 Unit test: global SDF composite — two meshes, verify minimum distance is correct.
- [ ] 7.3 Integration test: sphere march against global SDF — verify ray hits known geometry.
- [ ] 7.4 Visual test: Cornell box scene (red wall, green wall, white floor) — verify color bleeding (red tint on white floor near red wall).
- [ ] 7.5 Visual test: outdoor scene (pbr_test) — verify shadows have visible fill light matching UE5 with Lumen disabled + probes enabled.
- [ ] 7.6 Performance test: global SDF update + radiance cache update + probe sampling < 3ms total at 1080p on RTX 3060.
- [ ] 7.7 Pass validation: `--pass-validate` confirms all GI passes produce non-zero values.

## 8. Documentation

- [ ] 8.1 Create `docs/specs/rendering/gi-system.md` — full spec for the 3-tier GI system (SDF, radiance cache, SSGI).
- [x] 8.2 Create `docs/analysis/dataflow-sdf-gi-pipeline.md` — data flow document mapping UE5 Lumen → UzEngine GI. Covers all 5 phases (A-E) with UE5 source citations.
- [ ] 8.3 Update `docs/specs/rendering/renderer.md` — add GI passes to the render pipeline diagram.
- [ ] 8.4 Update `ROADMAP.md` — mark SSGI as Phase A done, SDF GI as Phase B in progress.
