# Task 3.54.0 — Software Ray-Traced Global Illumination

## 1. Mesh SDF Generation

- [x] 1.1 Read UE5 `MeshDistanceFieldUtilities.cpp` and `DistanceFieldAtlas.cpp` — extract the SDF generation pipeline (voxelization, JFA distance transform, atlas packing). Analyzed UE5 DistanceFieldAtlas.h:36-50, MeshDistanceFieldCommon.ush:9-38, LumenSoftwareRayTracing.ush:124-259, Common.ush:1883-1903. Created dataflow doc at docs/analysis/dataflow-sdf-gi-pipeline.md.
- [x] 1.2 Create `runtime/engine/renderer/gi/mesh_sdf_baker.h/.cpp` — MeshSdfBaker class with BakeMesh() (CPU dense SDF) and UploadToGPU() (R16F 3D texture). MeshSDFData struct for CPU data, MeshSDF struct for GPU handle.
- [x] 1.3 Implement CPU-side SDF generation: spatial hash accelerated nearest-triangle queries, pseudo-normal sign determination, dense float grid output. No JFA needed — direct distance computation with O(V*T/B) complexity.
- [ ] 1.4 Implement GPU-accelerated SDF generation (compute shader) for runtime baking — voxelize triangles via conservative rasterization or point splatting, then JFA compute passes.
- [x] 1.5 Per-mesh SDF storage: `MeshSDF` struct with R16F 3D texture handle, SRV index, volumeOrigin, volumeExtent, resolution. Created in mesh_sdf_baker.h.
- [ ] 1.6 SDF atlas: pack multiple mesh SDFs into a single 3D texture atlas (or use individual textures with bindless access). UE5 uses a combined atlas (`DistanceFieldAtlas.cpp:234`).

## 2. Global Distance Field

- [ ] 2.1 Read UE5 `GlobalDistanceField.cpp` — extract the clipmap structure (4 cascades, centered on camera, each 128^3 voxels).
- [ ] 2.2 Create `runtime/engine/renderer/gi/global_distance_field.h/.cpp` — GlobalDistanceField class with 4 cascaded 3D textures (R16F, 128^3 each). Cascade 0 covers ~50m, cascade 3 covers ~400m.
- [ ] 2.3 Implement cascade update compute shader `global_sdf_update_cs.hlsl` — for each voxel in the cascade, sample all nearby mesh SDFs (transformed to world space) and take the minimum distance. Only update dirty voxels (camera moved, objects changed).
- [ ] 2.4 Clipmap scrolling: when camera moves, shift cascade data and only re-compute the newly exposed border voxels. UE5 uses `UpdateGlobalDistanceFieldVolume()` with page-based updates.
- [ ] 2.5 Composite: final SDF at any world point = sample the finest cascade that contains the point. Shader helper `SampleGlobalSDF(worldPos)` returns distance.

## 3. SDF Ray Tracer

- [x] 3.0 Create `runtime/shaders/runtime/include/sdf_common.hlsli` — SDF sampling, LineBoxIntersect, SphereMarchMeshSDF utilities. UE5 sources: Common.ush:1883-1903, MeshDistanceFieldCommon.ush:9-38, LumenSoftwareRayTracing.ush:124-259.
- [ ] 3.1 Read UE5 `LumenTracingCommon.ush` and `LumenHardwareRayTracing.ush` — extract the sphere marching algorithm against the global SDF.
- [ ] 3.2 Create `runtime/shaders/runtime/gi/sdf_trace_cs.hlsl` — sphere march compute shader. Per ray: start at origin, step by `SampleGlobalSDF(pos)` distance, repeat until hit (distance < epsilon) or max distance.
- [ ] 3.3 Ray parameters: max distance = cascade 3 extent (~400m), max steps = 64, hit epsilon = 0.5cm. Output: hit position (float3), hit normal (float3 from SDF gradient), hit distance (float).
- [ ] 3.4 Material fetch at hit: read the GBuffer at the hit screen position (if visible) or use a simplified albedo from the mesh SDF metadata. UE5 uses surface cache for this.
- [ ] 3.5 Cone tracing variant: instead of single ray, trace a cone (wider = softer GI). Cone opening angle based on distance for LOD-like behavior. UE5 `TraceMeshSDFs()` uses cone tracing.

## 4. Radiance Cache (Irradiance Probes)

- [ ] 4.1 Read UE5 `LumenRadianceCache.usf` and `LumenRadianceCacheUpdate.cpp` — extract the probe grid structure, update strategy, and interpolation.
- [ ] 4.2 Create `runtime/engine/renderer/gi/radiance_cache.h/.cpp` — RadianceCache class. 3D grid of probes in world space. Each probe: position (float3) + L2 SH coefficients (9 float3 = 27 floats) + visibility (for Chebyshev test).
- [ ] 4.3 Probe grid layout: uniform grid centered on camera, 3 cascades (near=2m spacing, mid=8m, far=32m). Total ~4096-8192 probes. Stored as StructuredBuffer.
- [ ] 4.4 Probe update compute shader `radiance_cache_update_cs.hlsl`: each frame, update a subset of probes (round-robin or priority-based). Per probe: trace 32-64 rays via SDF tracer, accumulate irradiance into SH coefficients. Exponential moving average with history.
- [ ] 4.5 Probe visibility: store mean depth and depth^2 per direction for Chebyshev inequality test. This prevents light leaking through walls by rejecting probes that are occluded from the shading point.
- [ ] 4.6 Probe atlas: pack all probe SH into a single StructuredBuffer for GPU access. Layout: `probeData[probeIndex * 9 + shBand]`.

## 5. Deferred Lighting Integration

- [ ] 5.1 Read UE5 `LumenScreenProbeGather.usf` — extract how Lumen composites probe data into the deferred lighting pass.
- [ ] 5.2 Create `radiance_cache_sample_cs.hlsl` — per-pixel compute shader that reads the radiance cache. For each pixel: find 8 nearest probes, trilinear interpolate SH, apply Chebyshev visibility, evaluate SH at surface normal.
- [ ] 5.3 Modify `deferred_lighting_ps.hlsl` — replace single-tier ambient with 3-tier:
  ```
  float3 giNear = ssgiResult;                    // 0-5m: screen-space traces
  float3 giMid  = radianceCacheSample;            // 5-50m: probe interpolation
  float3 giFar  = GetSkySHDiffuse(N) * skyLight;  // 50m+: sky SH (existing)
  float3 gi = lerp(lerp(giFar, giMid, midWeight), giNear, nearWeight);
  ambient = gi * kD * albedo * ao;
  ```
- [ ] 5.4 Distance-based blending: `nearWeight = saturate(1 - dist/5m)`, `midWeight = saturate(1 - dist/50m)`. Smooth transitions between tiers.
- [ ] 5.5 Intensity control: `r.GI.Intensity` CVar (default 1.0) scales the combined GI contribution.

## 6. Render Pipeline Integration

- [ ] 6.1 Register passes in `frame_lighting.cpp`: GlobalSDF Update → Radiance Cache Update → SSGI → Deferred Lighting.
- [ ] 6.2 Resource creation in `scene_lifecycle.cpp`: global SDF 3D textures (4 cascades × 128^3 × R16F = ~32MB), radiance cache buffer (~8192 probes × 27 floats × 4 bytes = ~880KB).
- [ ] 6.3 Per-mesh SDF baking: on mesh load, schedule SDF bake. Cache to disk as `.uzsdf` files alongside `.uzmesh`.
- [ ] 6.4 Pipeline cache: add compute PSOs for `global_sdf_update_cs`, `sdf_trace_cs`, `radiance_cache_update_cs`, `radiance_cache_sample_cs`.
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
