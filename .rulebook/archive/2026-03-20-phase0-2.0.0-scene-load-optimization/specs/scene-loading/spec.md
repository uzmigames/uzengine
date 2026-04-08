# Scene Load Optimization — Spec Delta

## ADDED Requirements

### Requirement: Batched Mesh Upload
The mesh upload system MUST support a deferred mode where multiple meshes are recorded into a single command list and flushed with a single GPU submit + WaitIdle, regardless of mesh count.

#### Scenario: Batch Upload 10 Meshes
Given a scene with 10 mesh entities pending upload
When SyncWorldToDrawList() processes all entities
Then all 10 meshes SHALL be uploaded via UploadMeshDeferred() into one command list
And FlushMeshUploads() SHALL be called exactly once
And only 1 WaitIdle() call SHALL occur for all 10 meshes

#### Scenario: Single Mesh Hot-Reload
Given a single mesh is modified at runtime
When the mesh hot-reload path triggers
Then UploadMesh() (synchronous, single-mesh) SHALL still work correctly
And the deferred path SHALL NOT be required for single-mesh updates

### Requirement: Non-Blocking Texture Decode Polling
The texture loading system MUST NOT block the render thread waiting for all async decodes to complete. It SHALL upload whatever textures are ready each frame and defer the rest.

#### Scenario: Partial Texture Readiness
Given 45 textures are being decoded asynchronously and 12 are complete
When FlushPendingLoads() runs on frame N
Then only the 12 completed textures SHALL be uploaded
And the remaining 33 SHALL remain pending for subsequent frames
And the render thread SHALL NOT sleep or busy-wait

#### Scenario: Placeholder Rendering
Given a mesh references a texture that has not yet been uploaded
When the mesh is rendered
Then a 1x1 fallback texture (magenta for albedo, flat blue for normal) SHALL be used
And no visual corruption SHALL occur

### Requirement: Deferred SDF Bake
The SDF baking system MUST NOT run inside the per-mesh LoadMesh() path. SDF bakes SHALL be queued and executed in a single batch after all mesh uploads are flushed.

#### Scenario: Batch SDF After Mesh Upload
Given 10 meshes are loaded via UploadMeshDeferred()
When FlushMeshUploads() completes
Then FlushSdfBakes() SHALL dispatch all 10 SDF compute shaders in one command list
And only 1 additional WaitIdle() SHALL occur for all SDF bakes

### Requirement: PSO Pre-Warm
The pipeline system MUST pre-compile all essential shader pipelines during SceneRenderer::Init(), before any scene is loaded. Lazy compilation on the render thread during frame rendering MUST be eliminated for core passes.

#### Scenario: No Lazy Compile After Warm
Given WarmEssentialPipelines() has completed during Init()
When the first frame of a PBR scene is rendered
Then zero DXC/FXC shader compilation calls SHALL occur on the render thread
And all PSOs for GBuffer, Shadow, Deferred Lighting, Tonemap, GTAO, Bloom, HiZ, TAA, Fog, Atmosphere SHALL already exist

### Requirement: BRDF LUT Disk Cache
The IBL system MUST cache the BRDF integration LUT to disk after first bake. Subsequent runs SHALL load from cache instead of re-baking.

#### Scenario: Cache Miss (First Run)
Given no BRDF LUT cache file exists at `cache/ibl/brdf_lut.dds`
When SceneRenderer::Init() runs BakeBRDFLut()
Then the LUT SHALL be computed via GPU dispatch
And the result SHALL be saved to `cache/ibl/brdf_lut.dds`

#### Scenario: Cache Hit (Subsequent Run)
Given a valid BRDF LUT cache exists at `cache/ibl/brdf_lut.dds`
When SceneRenderer::Init() runs BakeBRDFLut()
Then the LUT SHALL be loaded from disk without any GPU dispatch
And load time for this step SHALL be < 50ms

### Requirement: Sky IBL Multi-Frame Amortization
The atmosphere sky IBL bake MUST NOT block the render thread with multiple WaitIdle calls on the first frame. The bake SHALL be spread across 2-3 frames using fence-based completion detection.

#### Scenario: First Frame With Atmosphere
Given a scene with SkyAtmosphere component and skyIBLDirty=true
When the first frame is rendered
Then irradiance + specular compute dispatches SHALL be submitted on frame 1
And SH readback SHALL occur on frame 2 (after fence signals completion)
And no mid-frame WaitIdle SHALL occur

### Requirement: Scene Load Time Target
A PBR test scene with 10 meshes and 45 textures MUST load in under 3 seconds on a mid-range GPU (RTX 3060 class), measured from SceneRenderer::Init() to first fully-rendered frame.

#### Scenario: Load Time Benchmark
Given the standard PBR test scene (9 spheres + 1 ground, 5 textures each)
When the scene loads from cold start (no shader cache)
Then time from Init() to first rendered frame SHALL be < 5 seconds

#### Scenario: Load Time With Warm Cache
Given the standard PBR test scene with shader cache and BRDF LUT cache populated
When the scene loads
Then time from Init() to first rendered frame SHALL be < 2 seconds
