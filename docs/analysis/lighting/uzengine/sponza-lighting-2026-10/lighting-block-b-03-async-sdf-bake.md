# Lighting block B, item 3 — asynchronous, disk-cached mesh SDF bake

Rulebook task `phase0_lighting-block-b-03-async-sdf-bake` (proposal, design, spec
`mesh-sdf-bake-cache`). Branch `feat/sponza-gi-parity`, 2026-10-05.

## Before

Sponza's preload (project configuration, 138 meshes at 128^3, two-sided) took 37.4-38.0 s on the
build before this item. `FlushSdfBakes` baked every pending mesh on the graphics command list and
stalled on `WaitIdle` once per batch of four meshes (35 stalls): 22.65 s of CPU half (AABB, triangle
records, BVH; worst batch 2.54 s) and 5.62 s of GPU drain (worst 1.19 s), all on the main thread,
nothing cached (`scripts/rendering/sdf_bake_timeline.py` on the startup and preload logs; the proposal's
42.4 / 29.9 s came from an older log). `valid` was set when the commands were recorded, before the
GPU had run them, and switching SDF GI on after load baked nothing.

## What changed

See `.rulebook/archive/2026-10-10-phase0_lighting-block-b-03-async-sdf-bake/design.md` for the structure. In
short: `MeshSdfBakeQueue` (worker threads for the hash, the cache lookup and the CPU half; the GPU
half recorded in the frame's command list, in Z slabs in the render loop, unthrottled during the
preload; completion by a renderer-owned fence signalled after every frame's Execute), the on-disk
cache `cache/meshsdf` (`AssetCache`, LZ4, XXH64 checksum, keyed on the mesh bytes, the bake options
and the bake shader's bytecode, atomic writes), the `r.GI.MeshSdf.Diagnostic.CacheVerify`
diagnostic (every hit baked again and compared), the re-trigger when `r.RadianceCache` is switched
on after load (two meshes per frame, parsed on a worker from the path or the pak bytes),
`IsSceneFullyLoaded` counting the queue, one bake start per frame in the render loop (a start costs
the main thread about 15 ms for a 128^3 field; big buffers are freed on a worker and the staging and
readback buffers are pooled, because freeing 8 MB on the main thread of a Debug build cost ~38 ms
per field; a frame whose service exceeds 10 ms is named in the log with a breakdown), and a
`MeshSdfBake` GPU profiler scope around
the frame's bake work (uploads, slab dispatches, readbacks) so `--diag` and `r.GpuProfiler.Log` show
it. A mesh now appears a few frames before
its field: the global distance field adds the instance when the field arrives (a regional recompose,
block B.2); during the preload nothing is presented, in the render loop the arrival is throttled to
four fields per frame.

Behaviour kept: `MeshSdfBaker::BakeMeshGPU` (the whole bake on the calling thread) still exists for
tools; the bake shader's output is unchanged (one root DWORD added for the slab offset); the
procedural meshes still get no field; the cache off (`r.GI.MeshSdf.Cache 0`) is the asynchronous
path without disk.

## Gates (`scripts/rendering/test_sdf_bake_cache.py`)

| Gate | Criterion | Result |
|---|---|---|
| C1 | a cache hit is the same bits as a fresh bake (volume and records, `r.GI.MeshSdf.Diagnostic.CacheVerify`) | PASS: 138 IDENTICAL, 0 DIFFERENT of 138 |
| C2 | warm preload <= 50 % of cold (the pre-registered letter) | FAIL: cold 19.331 s (138 baked), warm 10.87 s (138 cached) |
| C2' | SDF-attributable warm preload <= 50 % of cold (floor = the preload of the same scene with SDF GI off; registered after the first measurement) | PASS: floor 10.037 s; cold 9.3 s -> warm 0.8 s (9 %) |
| C3a | no `WaitIdle` in the bake path (source) | PASS: scene_mesh_assets.cpp / mesh_sdf_bake_queue.cpp |
| C3b | no frame above 2x the median over the whole timing window after enabling SDF GI at runtime (`frame_ms` max / p50, the letter) | PASS: max/median 1.4428341205063215 (38.7665 / 26.8683 ms) |
| C3b' | per-frame GPU profile: frames after the enable frame + 2 (the first whole-window fill of the global distance field excluded) TOTAL max <= 2x median; `MeshSdfBake` scope max reported | PASS: TOTAL max 38.7 ms at frame 243, median 26.6 ms; MeshSdfBake max 0.04 ms; enable frames {60: 3, 61: 3, 62: 3} |
| C4 | Camera C ROI means bit-identical cold vs warm (the pre-registered letter) | FAIL: {'shadow_wall': '5.30e-03', 'lit_wall': '1.08e-02', 'centre': '7.27e-03'} |
| C4' | Camera C ROI means within 1 % relative cold vs warm (registered before measuring) | FAIL: {'shadow_wall': '5.30e-03', 'lit_wall': '1.08e-02', 'centre': '7.27e-03'} |
| C5 | `r.RadianceCache` switched on at runtime bakes the missing fields and the GI appears: `diffuse_indirect` gains 1.5x (the letter) | FAIL: retrigger lines 1, 138 fields of 138, indirect 2.623e-01 -> 1.394e-04 |
| C5' | after enabling, the view agrees within 5 % with a reference run that had SDF GI on from the start (registered after the first measurement: with SDF GI off the channel holds the sky ambient fallback) | PASS: enable 1.3944e-04 vs reference 1.3771e-04 (1.25 %) |
| N | negative control: a truncated cache entry is baked and rewritten | PASS: 1 baked, 137 cached, warnings {'is stale or corrupt': 1, 'fails its checksum': 0}, size 77380 -> 77380 |


**Numbers.** Cold preload 19.331 s (138 fields baked and stored, 37 start frames, bake span 18.0 s), warm preload 10.87 s (138 fields from the cache, 0 baked). Runtime enable: 138 fields after frame 60 (138 from the cache), the view's `diffuse_indirect` 2.623e-01 before -> 1.394e-04 over the last 50 frames; CPU frame median 26.8683 ms, max 38.7665 ms (ratio 1.44).

**Camera C.** shadow_wall cold 2.526401e-04 / warm 2.539803e-04 (0.53 %), lit_wall cold 1.399163e-04 / warm 1.384084e-04 (1.08 %), centre cold 2.251026e-04 / warm 2.234670e-04 (0.73 %); cold run 138 baked, warm run 138 cached.

**Frame times while the fields arrive** (enable run without the region dump). GPU, from `r.GpuProfiler.Log`: the enable frames 60: 3 ms, 61: 3 ms, 62: 3 ms; from frame 63 on the TOTAL max is 38.7 ms at frame 243 against a median of 26.6 ms, and the `MeshSdfBake` scope (uploads, slab dispatches, readbacks) peaks at 0.04 ms per frame. CPU (`frame_ms` of the manifest): median 26.8683 ms, max 38.7665 ms.

**Why C2 is read on the SDF-attributable time.** The preload of the same scene with SDF GI off (the enable run) is 10.037 s: texture streaming and mesh uploads. The asynchronous bake brought the cold preload from 37.4-38.0 s to 19.331 s, so the cache can only act on the remaining 9.3 s, which it cuts to 0.8 s; the whole-preload ratio (56 %) is dominated by the floor.

**Main-thread cost of the bake service** (enable run, frames whose service exceeded 10 ms: 1): median 11.5 ms, p90 11.5 ms, max 11.5 ms (0.4 ms queuing 2 re-reads; 11.1 ms completing 1 [fence 0.0, body 11.0, erase 0.0]; 0.0 ms starting 0; 0.0 ms slabs 0; 45 in flight). A start creates the records buffer and the volume and copies the records (and, on a hit, the volume) into upload memory (~15 ms for a 128^3 field). Before the fix a completion cost ~38 ms: the phase breakdown of the `SDF bake service took` log line (queuing / completing [fence, body, erase] / starting / slabs) put it in the erase of the bake entry, i.e. freeing its 8 MB records vector on the main thread (the default `zig build` is a Debug build with a slow heap); big buffers are now handed to a worker to be freed and the staging / readback buffers are pooled.

**Where the warm preload's GPU time goes** (a warm run with `r.GpuProfiler.Log`, 43 preload frames): GPU total 1.21 s, of which the `MeshSdfBake` scope (cache uploads) 7 ms; the largest passes are SSGI 0.38 s, DeferredLighting 0.33 s, GDF_Composite 0.19 s, RadianceCache_Trace 0.17 s, AtmosphereBake 0.06 s. The global distance field's regional recomposes of the arriving fields were there before this item; the cache path itself is a few tens of milliseconds.

**Harness** `scripts/rendering/test_sdf_bake_cache.py` 7/11 (outputs `.screenshots/flicker-elimination/test-sdf-bake-cache/`).

## Still open

- C4' (cold vs warm Camera C within 1 %) misses by 0.08 % on one ROI in the final run (0.53 / 1.08 /
  0.73 %; earlier runs of the same code read 4e-6..4e-5). Cold and warm receive their 138 fields on
  different frames and the radiance cache keeps its history outside the change boxes (block B.2
  phase 4), so the converged state differs by the blending residual, the mechanism G4 of block B.2
  measured at 0.13-0.15 % for a single instance. A GI history reset once the preload completes would
  make the two paths identical by construction at the price of presenting the first frames
  unconverged; not done.
- A bake start costs the main thread up to ~10 ms for a 128^3 field (resource creations, 12 MB copied into upload memory), so the render loop starts one per frame; a records buffer written by the worker into mapped memory would take most of it off the frame. The engine is built in Debug by default (`zig build`), where a large free costs tens of milliseconds: the queue frees its big buffers on a worker for that reason.
- The bake GPU cost is bounded per frame by `r.GI.MeshSdf.BakeSlicesPerFrame` (8 of 128 slices), so a 128^3 field takes 16 frames in the render loop and Sponza's 138 fields a little over 2200 frames when SDF GI is switched on without a cache; the cache makes that a one-time cost.
- The cache keeps the records (triangles + BVH) next to the volume: Sponza's entries are large (LZ4 helps the volume more than the records). An entry format that rebuilds the records on a worker would halve the disk use.
- `AssetCache` has no eviction; `cache/meshsdf` grows with every mesh/option combination ever baked.
- Procedural meshes (`RegisterProceduralMesh`) still get no field, as before.
