## 1. Cook Sponza textures (existing uzcook pipeline)
- [x] 1.1 Add `*.uztexture` + `*.uzimport` to .gitignore (already present — verified, plus 28 legacy cooked files predate the ignore)
- [x] 1.2 Fix uzcook SelectTextureFormat: glTF combined metallicRoughness maps must cook to BC7 linear (G=roughness, B=metallic), not BC4 single-channel
- [x] 1.3 Run uzcook --textures-only over sample/content/textures/sponza (70 companions generated; 2K BC7 = 5.59 MB, formats verified fmt=4/BC7 + fmt=3/BC5 in runtime log)
- [x] 1.4 Verify runtime picks .uztexture fast path ("Loaded .uztexture via ring" — parse 0.0ms, upload 0.3-0.5ms per texture, zero PNG decodes)

## 2. Mesh upload flush without WaitIdle
- [x] 2.1 FlushDeferredMeshUploads: End/Execute kept (implicit-state correctness), WaitIdle removed via `deferredRelease` sink; staging retires through frames-in-flight-aged buckets (m_stagingRetireBuckets, destroyed in SetupFrameData + Shutdown)
- [x] 2.2 Build + run Sponza, confirm meshes render correctly with no mid-frame stall (screenshot verification in 6.4)

## 3. Parallel mesh loading
- [x] 3.1 AsyncMeshLoader created (worker pool, Submit/SubmitVFS/PollCompleted, filesystem fast path bypasses VFS mutex)
- [x] 3.2 Integrated: RequestMeshAsync reserves placeholder slot + submits; IntegrateAsyncMeshLoads (called from FlushPendingLoads) polls + batch-uploads; SyncWorldToDrawList claims slots once resident (LOD0 + LOD1..7 paths)
- [x] 3.3 r.LazyLoad.MeshesPerFrame redefined as GPU-upload integration budget (default 4 → 32, 0 = unlimited); definition moved to scene_mesh_assets.cpp
- [x] 3.4 Build + verified: Sponza mesh phase 4.9 s → 1.25 s (138 meshes), startup texture drain 92 → 0 iterations

## 4. Material load cost
- [x] 4.1 .uzmat parse memoized per unique path during LoadScene (GetOrParseUzmat + s_uzmatCache, cleared at LoadScene entry; 138 reads → 28)
- [x] 4.2 Material PSO bytecode disk cache: VERIFIED ALREADY EXISTS — ShaderPipeline::LoadBytecodeFromSource caches DXIL by content hash (source+defines+backend+bindless) in cache/shaders/<backend>/; warm Sponza run adds 0 new entries and skips all DXC compiles. Remaining warm-run cost is driver-side PSO creation (26 PSOs ≈ 0.41 s) — ID3D12PipelineLibrary caching is a future RHI task, out of scope here
- [x] 4.3 Build + verified warm-run behavior (cache bins 105 → 105 across a full Sponza load)

## 5. Small fixes
- [x] 5.1 r.Scene.MaxTextureUploadsPerFrame CVar wired (MaxTextureUploadsPerFrame() reads the CVar; hardcoded constant removed)
- [x] 5.2 3-arg LoadScene prefab loss fixed: runtime.cpp uz_load_scene and application_platform.cpp FlushPendingSceneLoad now pass their VFS to the 4-arg overload

## 6. Tail (docs + tests — check or waive with tailWaiver)
- [x] 6.1 Update or create documentation covering the implementation — runtime/CHANGELOG.md v0.9.7 entry + new docs/runtime/asset-loading.md (pipeline, cook workflow, CVars, diagnosis guide)
- [x] 6.2 Write tests covering the new behavior — tests/engine/renderer/test_async_mesh_loader.cpp: 3 tests (valid-file round-trip with resultKey, missing-file failure signal, PendingCount drain + idempotent Shutdown)
- [x] 6.3 Run tests and confirm they pass — AsyncMeshLoader 3/3; task-area filter (mesh/scene-loader/cook/texture-importer) 142/143; the 1 failure (SceneLoaderTest.CameraComponent_DefaultValues) is pre-existing drift (scene_loader.cpp:280 defaults "active" to true since before this task); full-suite VFX failures + RuntimeViewportTest teardown assert are pre-existing (Y-up/meters test expectations vs Z-up/cm code; FontAtlas D3D12MA leak reproduced on pre-change binary)
- [x] 6.4 Visual verification: Sponza screenshot after full load (IsSceneFullyLoaded gate) — albedo/normals/roughness correct with BC formats, all 138 meshes present, no gloss explosion from the metallicRoughness fix; screenshot deleted after review. Final measurement: LoadScene → last texture = 3.0 s (was 5.3 s), startup drain 0.12 s (was 0.84 s), mesh phase 1.7 s at default budget (was 4.9 s)
