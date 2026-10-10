# Proposal: Asynchronous, disk-cached mesh SDF bake (lighting block B, item 3)

## Why
Sponza's preload takes 42.4 s, of which 29.9 s are mesh SDF bakes (138 meshes at 128³, two-sided,
35 batches, worst batch 3.49 s; `scripts/sponza-loading-fixed-startup.log`). `FlushSdfBakes`
(`scene_mesh_assets.cpp:232-286`) bakes the whole pending list on the graphics command list and
stalls on `WaitIdle` (`:279-282`); `BakeMeshGPU` marks the SDF valid at record time, before the
GPU ran (`mesh_sdf_baker.cpp:265-274`). Nothing is cached: `.uzsdf` is an 8-bit brick format for
distance-field shadows with no runtime reader, and the dense R16F SDF the GI uses (4 MiB per mesh,
~552 MiB for Sponza) is rebuilt on every load. The job system exists (`runtime/engine/job/`) but no
host initialises it, so every CPU half of a bake (AABB, triangle records, BVH) runs on the main
thread. The assessment lists this with the incremental GDF as the pair that unlocks dynamic GI and
every local-light task.

## What Changes
- Disk cache for the dense mesh SDF plus its triangle/BVH buffer through `AssetCache`
  (`asset_cache.h:70-124`), keyed on mesh content hash + resolution + two-sided + baker/shader
  version; a hit uploads (copy queue) instead of baking; the cached result is bit-identical to a
  fresh bake and is verified as such.
- Asynchronous bake: CPU half on a worker (job system initialised in the host, or the
  `AsyncTextureDecoder` thread pattern), GPU half recorded without `WaitIdle`, completion by fence,
  `valid` set only after the fence; `IsSceneFullyLoaded` counts in-flight bakes; a late SDF is an
  instance change for the GDF (block B item 2), not a global invalidation.
- Re-trigger path: enabling SDF GI after load queues the bakes the comment at
  `scene_mesh_assets.cpp:102-105` promises.
- Gates measured with block B item 1 timings: preload time cold and warm, no `WaitIdle` inside the
  frame loop, no frame above the budget during a bake, field identity cached vs baked.

## Impact
- Affected specs: mesh-sdf-bake-cache (new)
- Affected code: runtime/engine/renderer/gi/mesh_sdf_baker.{h,cpp}; runtime/engine/renderer/scene/scene_mesh_assets.cpp, scene_texture_assets.cpp, scene_sync.cpp; runtime/engine/asset/asset_cache.{h,cpp}; runtime/engine/job/* (initialisation in runtime/tools/uzengine.cpp); new scripts/rendering/test_sdf_bake_cache.py
- Breaking change: NO (cache miss = today's result; cache format is new, nothing reads `.uzsdf` differently)
- User benefit: Sponza loads in seconds instead of 40, no stall when GI is enabled or a mesh streams in, and the GI SDFs are the same bits as before.
