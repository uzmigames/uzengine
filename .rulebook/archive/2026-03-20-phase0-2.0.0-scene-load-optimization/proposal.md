# Proposal: Scene Load Optimization — From 25s to <2s (Phase 0)

## Why

The PBR test scene (10 meshes, 45 textures) takes 20-30 seconds to load. Root cause: **serial WaitIdle() calls per asset** — up to 55 GPU stalls on the first frame. Every production engine (Godot, Unity, Unreal) batches GPU uploads into a single submit and pre-warms pipeline state objects at startup. UzEngine does neither, making iteration painfully slow even for trivial scenes. With larger scenes (thousands of meshes), load times would be minutes.

10 bottlenecks identified via profiling analysis:
1. `gpu_mesh.cpp:110-113` — WaitIdle per mesh upload (10 serial stalls)
2. `texture_loader.cpp:324-327` — WaitIdle per texture in sync fallback path (up to 45 stalls)
3. `scene_assets.cpp:335-343` — Busy-wait loop blocking render thread until all async decodes finish
4. `scene_assets.cpp:82-118` — SDF bake runs inside LoadMesh() with lazy shader compile per mesh
5. `ibl_baker.cpp:293-368` — BRDF LUT bake synchronous in SceneRenderer::Init (1-3s cold)
6. `atmosphere_renderer.cpp:443-474` — Sky IBL + SH readback triple WaitIdle on frame 1 (2-8s)
7. `pipeline_lifecycle.cpp` — All 20-35 PSOs compiled lazily on first use via DXC
8. `pbr_test.cpp:181-210` — MaterialBakerGpu::Bake() synchronous in OnStart
9. `frame_setup.cpp:51-76` — StatsOverlay init WaitIdle on frame 1
10. `texture_loader.cpp:240-284` — CPU mip generation serial per texture (~1GB box-filter for 45 textures)

## What Changes

### New Systems

1. **Batched mesh upload** — `UploadMesh` gains a deferred mode that records copy commands without flushing. A new `FlushMeshUploads()` submits all pending mesh copies in one command list with a single WaitIdle.
2. **PSO pre-warm system** — `PipelineLibrary::WarmEssentialPipelines()` compiles the ~25 core shaders in parallel during `SceneRenderer::Init()`, before any scene loads.
3. **BRDF LUT disk cache** — `IBLBaker::BakeBRDFLut()` checks for a cached DDS file before baking. If found, loads from disk (instant). If not, bakes and saves to disk for next run.
4. **Deferred SDF batch** — SDF baking moves out of `LoadMesh()` into a separate `FlushSdfBakes()` that runs after all meshes are uploaded.

### Refactoring

1. `gpu_mesh.cpp` `UploadMesh()` — split into `UploadMeshDeferred()` (record only) + `FlushMeshUploads()` (submit + wait once)
2. `scene_assets.cpp` `FlushPendingLoads()` — remove blocking while-loop; upload whatever is ready each frame (trickle mode)
3. `scene_assets.cpp` `LoadMesh()` — extract SDF bake to deferred queue
4. `atmosphere_renderer.cpp` — move sky IBL bake to amortized multi-frame path, eliminate mid-frame WaitIdle chain
5. `frame_setup.cpp` — batch StatsOverlay init upload with other first-frame uploads

## Impact

- Affected specs: `docs/specs/rendering/ibl-pipeline.md`, `docs/engine/12-rendering/`
- Affected code: `runtime/engine/renderer/mesh/gpu_mesh.cpp`, `runtime/engine/renderer/upload/texture_loader.cpp`, `runtime/engine/renderer/scene/scene_assets.cpp`, `runtime/engine/renderer/scene/scene_lifecycle.cpp`, `runtime/engine/renderer/ibl/ibl_baker.cpp`, `runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp`, `runtime/engine/renderer/core/pipeline/pipeline_lifecycle.cpp`, `runtime/engine/renderer/frame/frame_setup.cpp`, `runtime/engine/shader/shader_pipeline.cpp`
- Breaking change: NO — all changes are internal to the renderer; public API unchanged
- User benefit: Scene load time drops from ~25s to <2s, enabling rapid iteration
- Enables: Large scene support (thousands of meshes), editor viewport responsiveness
