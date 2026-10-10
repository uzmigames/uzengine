# Design: asynchronous, disk-cached mesh SDF bake (lighting block B, item 3)

## Implementation notes (2026-10-05)

**Flow.** `SceneRenderer::QueueSdfBake` hands a loaded mesh to `MeshSdfBakeQueue`
(`runtime/engine/renderer/gi/mesh_sdf_bake_queue.{h,cpp}`) with the bake options captured at that
moment (`r.GI.MeshSdf.Resolution`, `r.GI.MeshSdf.TwoSided`, the vertex normal offset; 16-bit
indices widened — the old flush read them as 32-bit). Worker threads (`hardware_concurrency / 4`,
1..4; the `AsyncTextureDecoder` pattern: deque + mutex + condition variable, results polled on the
main thread) hash the mesh, look the field up in the cache and, on a miss, run the CPU half
(`MeshSdfBaker::PrepareCPU`: volume placement, triangle records, BVH — the old `BakeMeshGPU` split
in two, which stays as the whole-bake wrapper for tools). `SceneRenderer::ServiceSdfBakes`, once per
frame at the end of the sync, starts the prepared bakes within a budget (`CreateResources`: records
buffer, R16F volume with copy usage), uploads a cached volume in one copy (256-byte row pitch) or
records the dispatch in Z slabs (`sliceOffset`, the 9th root DWORD of `mesh_sdf_bake_cs.hlsl`),
reads a fresh bake back for the cache, and completes a bake when `m_bakeFence` has passed the value
signalled after the frame's Execute: only then `sdf.valid = true`, the mesh gets the field and the
global distance field sees an added instance (a regional recompose since block B.2). No `WaitIdle`
anywhere in the path; no RHI call off the main thread (D3D12 resource creation is not thread-safe
here).

**Budgets.** `r.GI.MeshSdf.BakeStartsPerFrame` (1), `r.GI.MeshSdf.BakeSlicesPerFrame` (8, multiples
of 4) and 4 completions per frame (below `r.GI.GDF.MaxRegions`) bound the per-frame cost in the
render loop; the host sets `SceneRenderer::SetPreloading(true)` around its preload loop (nothing is
presented, it waits for the GPU every iteration) and the queue runs unthrottled there (starts and
slabs; completions at 8 per frame, the regional limit). A start costs the main thread about 20 ms for
a 128^3 field (three resource creations and 12 MB copied into upload memory: the records and, on a
hit, the volume), measured with the `SDF bake service took` log line that names any frame whose
service exceeded 10 ms; the GPU side of a bake is a fraction of a millisecond per frame
(`MeshSdfBake` profiler scope). The first form started two per frame and the re-trigger parsed the
meshes on the main thread: 265 ms frames; now the re-trigger hands the path (or the pak bytes) to a
worker, which parses it there (and reads the file there when the path resolves to a real file; pak
bytes are read on the main thread and parsed on the worker). The phase breakdown (`SDF bake
service took ... [fence, body, erase]`) showed the completion cost in the *erase* of the bake entry:
freeing its 8 MB records vector on the main thread took ~38 ms (the default `zig build` is a Debug
build with a slow heap), so big buffers are handed to a worker to be freed (`DiscardLater`, a
Discard task), and staging / readback buffers are kept in a small pool per size (8) rather than
destroyed per field. Measuring note: the per-frame region dump of the harness stalls the main
thread for seconds (file writes), reference runs included — frame-time gates are read on a run
without it.

**Global distance field.** Seven fields completing in one frame exhausted the 2 MiB frame ring: the
B.2 composite allocated a full `kGdfMeshesPerBatch` (64 KiB) batch per region and clipmap. The
batches are now sized by their entry count (`SetRootCBV` size likewise).

**Cache.** `AssetCache("cache/meshsdf")`, relative to the working directory like the shader and PSO
caches. Key = `ComputeKey("meshsdf", 1, sourceHash, optionsHash)`: source = XXH64 of the vertex
bytes and of the indices plus counts and stride; options = resolution, two-sided, normal offset and
the FNV-1a hash of the bake shader's bytecode (`PipelineLibrary::GetMeshSdfBakeShaderHash`), so a
shader change misses. Entry = `MeshSdfCacheHeader` (104 bytes: magic `UMSC`, version, placement,
counts, sizes, XXH64 of the body) + LZ4 block of (R16F volume ‖ records). The volume is the GPU's
own readback, so a hit uploads the bits the bake produced; a header, size or checksum mismatch is a
miss that is rewritten. `AssetCache::Store` now writes to a temporary name and renames (a crash or a
concurrent reader never sees a partial entry; uzcook benefits too). `r.GI.MeshSdf.Cache 0` disables
it; `r.GI.MeshSdf.Diagnostic.CacheVerify 1` bakes every hit again into scratch resources and logs
`MeshSDF cache verify: ... result=IDENTICAL|DIFFERENT` (gate C1). The entry format lives in two
static functions, `MeshSdfBakeQueue::EncodeCacheEntry` / `DecodeCacheEntry`, covered by the
`MeshSdfCacheEntry` gtests (round trip of every field and byte, a header that does not describe its
body is not encoded, truncated / damaged / other-version / other-count entries are rejected) — a
header written from a moved-from vector once made every entry read as corrupt, which only the full
harness run caught.

**Re-trigger.** `m_sdfGiEnabled` false → true after meshes loaded without fields: `ServiceSdfBakes`
walks the mesh array a few per frame (`kSdfRetriggerPerFrame` 2), re-reads each mesh by path
(`LoadCpuMeshByPath`, the loader LoadMesh uses; procedural meshes have no source) and queues the
bake. The Lua script `sample/content/scripts/gi_enable_at_frame.lua` with the scene variant written
by `scripts/sponza_gi_runtime_enable.py` exercises it (gates C3, C5).

**Readiness.** `IsSceneFullyLoaded` counts the queue (`InFlight`: queued, preparing, prepared,
started); `scripts/test_scene_readiness.ps1`'s mock gained the member. `DestroyAssets` is unchanged:
the renderer stops the workers and frees the scratch of unfinished bakes right after its shutdown
`WaitIdle`, before the assets go.

**Log lines** (`scripts/sdf_bake_timeline.py` parses them): `batch SDF bake — N meshes` (starts this
frame), `GPU-baked mesh SDF` (completed fresh bake), `mesh SDF from cache` (completed hit),
`SDF GI enabled after load — missing mesh SDFs queued`.

## Rejected / deferred
- Initialising the job system in the host: it would also switch the skeletal mesh and shader batch
  paths that probe `JobSystem::Get()`; two to four dedicated threads give the same overlap without
  touching them.
- The compute queue for the dispatch: the RHI has no CPU-side completion query for it, and a
  GPU-side wait on the graphics queue would stall the frame; Z slabs on the frame's list bound the
  cost instead.
- Caching only the volume and rebuilding the records on a worker: the records are needed by the
  probe side classifier and bit-identity is simpler with both cached.
