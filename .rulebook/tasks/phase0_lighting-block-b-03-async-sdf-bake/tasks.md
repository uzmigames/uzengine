## 1. Baseline and gates
- [ ] 1.1 Record Sponza's preload timeline (project configuration): total, bake batches, worst batch, `WaitIdle` count, from the startup log and block B item 1 timings; freeze as the before numbers.
- [ ] 1.2 Pre-register: C1 a cache hit yields an SDF bit-identical to a fresh bake (R16F texture readback and BVH buffer); C2 warm preload <= 50 % of cold; C3 no `WaitIdle` issued by the bake path inside the render loop and no frame > 2x the median during a bake; C4 Sponza Camera C ROIs bit-identical cold vs warm; C5 enabling `r.RadianceCache` at runtime bakes what is missing and the GI appears without a reload.

## 2. Disk cache
- [ ] 2.1 Cache key = mesh content hash + resolution + twoSided + baker/shader version; payload = dense R16F volume, origin/extent, triangle records and BVH (`mesh_sdf_baker.cpp:87-218`), stored through `AssetCache` (`asset_cache.h:70-124`).
- [ ] 2.2 Hit path: texture created with CopyDst, per-slice 3D upload on the copy queue (`scene_texture_assets.cpp:492-498, 321-361` pattern), `valid` after the copy fence; C1 and C4.
- [ ] 2.3 Miss path writes the cache after the bake fence; corrupt or stale entries fall back to a bake and are rewritten.

## 3. Asynchronous bake
- [ ] 3.1 Initialise the job system in the host (`runtime/tools/uzengine.cpp`; `job.MaxWorkerCount`) or adopt the `AsyncTextureDecoder` thread pattern; move the CPU half of `BakeMeshGPU` (AABB, triangle records, BVH) to a worker.
- [ ] 3.2 GPU half recorded without `WaitIdle` (`scene_mesh_assets.cpp:279-282`), completion by fence, `valid` only after it (`mesh_sdf_baker.cpp:265-274`); `IsSceneFullyLoaded` (`:200`) counts in-flight bakes; preload drains them without stalling the frame.
- [ ] 3.3 A late SDF enters the GDF as an instance change (block B item 2), with the global invalidation as the fallback until that task lands; C2, C3, C5.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation (docs/specs/rendering/gi-system.md cache row, loading docs, known-issues).
- [ ] 4.2 Write tests covering the new behavior with behavioral assertions and negative controls (`scripts/test_sdf_bake_cache.py`: cold/warm runs, C1-C5, a poisoned cache entry as the negative control).
- [ ] 4.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately.
