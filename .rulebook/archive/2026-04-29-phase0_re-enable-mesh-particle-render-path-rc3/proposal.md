# Proposal: phase0_re-enable-mesh-particle-render-path-rc3

## Why

The VFX mesh-particle render path was disabled during an "RC3" milestone
(`// DISABLED: RC3 — needs BeginRenderPass fix`) and has stayed offline
ever since. While the GPU sort pipeline, deterministic-slot mesh CS, and
`MeshRenderModule` schema parsing all shipped as part of
`phase16_vfx-particle-sort-pipeline-refactor` (archived
2026-04-25), the actual draw path that consumes those resources is
silently bypassed today. Concretely:

| Site | Current state |
| --- | --- |
| `runtime/engine/renderer/frame/frame_particles.cpp:916` | `AddParticleMeshRenderPass(...)` call commented out — the render graph never registers a draw pass for mesh emitters. |
| `runtime/engine/renderer/frame/frame_particles.cpp:851-852` | `batch.meshVertexBuffer = batch.meshIndexBuffer = RHIBufferHandle::Invalid()` hardcoded — `MeshRenderModuleConfig::meshPath` is parsed but never resolved into a real GPU buffer. |
| `runtime/engine/renderer/frame/frame_particles.cpp:635` | PSO comes from `ctx.pipelines->GetParticleMeshOpaque()` — a fixed engine pipeline that ignores the per-emitter `materialPath`. |
| `runtime/engine/renderer/frame/frame_particles.cpp:375` | The billboard render loop has no `if (renderModule == "mesh_render") continue;` guard, so mesh emitters get re-emitted as 6-vert white billboards. The `vtx=1200` line our hardware-validation logs prove this fallback firing in place of the real mesh draw. |
| `runtime/engine/renderer/frame/frame_particles.cpp:623` | Trail render pass — same `DISABLED: RC3 — needs BeginRenderPass fix` annotation, evidence of a known cluster of unfinished RC3 work. |

This was uncovered while attempting item 1.1 of
`phase16_vfx-sort-pipeline-hardware-validation` ("load
`mesh_sort_shards.uzvfx` and emit 200 transparent shard particles"). The
asset loads end-to-end (`UzfxLoader` parses it, `ParticleEmitter`
allocates capacity 200, `ParticleSortCS` pipeline is created,
`AddParticleRenderPass` registers 1 active emitter), but ZERO mesh
pixels reach the framebuffer. The screenshot shows only sky + ground
because the shards quietly fall through the billboard fallback path
above. This blocks every downstream item in the sort-pipeline
hardware-validation task — and probably blocks the
`phase16_vfx-mesh-particle-depth-sort` follow-ups too, which are also
mesh-render-dependent.

The four sites form one coherent change: each fixes one half of the
producer/consumer pair (asset loading produces the GPU buffer; the
draw pass consumes it). Splitting them across separate tasks would
ship intermediate non-renderable states.

## What Changes

### 1. Mesh asset loading

- Add a `RHIBufferHandle MeshAssetSystem::GetVertexBuffer(const std::string& path)` /
  `GetIndexBuffer` API on top of the existing `.uzmesh` loader (`runtime/engine/asset/formats/uzmesh_format.{h,cpp}`).
  The system caches one upload per asset path, returns shared handles to
  every emitter that references the same path, and uploads on the same
  copy queue that `scene_vfx_sync.cpp` already uses to initialise
  particle buffers.
- In `frame_particles.cpp:851-852`, replace the `Invalid()` placeholders
  with a call into the new API, keyed by `meshConfig.meshPath`. Drop a
  per-frame log at Verbose level only.
- Submesh information (LOD 0 only for now — particles are not LOD'd):
  forward `vertexCount`, `indexCount`, `indexStride`, and the LOD 0
  submesh's `indexCount` + `indexOffset` into `MeshParticleBatch` so
  the indirect draw can issue `DrawIndexedInstancedIndirect` with the
  correct counts.

### 2. Material → mesh-particle PSO bridge

- Resolve `MeshRenderModuleConfig::materialPath` through the existing
  `MaterialAsset` loader. Translate the loaded `MaterialGraph` into a
  `MaterialPSO` slot key compatible with the mesh-particle vertex
  layout (`SV_VertexID` + instance SRVs, see
  `runtime/shaders/runtime/vfx/particle_mesh_*`).
- Replace the fixed `GetParticleMeshOpaque()` lookup at
  `frame_particles.cpp:635` with a `GetParticleMeshPSO(material,
  meshVertexLayout)` resolver that respects the material's blend mode
  (Opaque / Translucent / Additive) and `twoSided` field. The PSO
  cache lives where `MaterialPipeline` already lives —
  `runtime/engine/renderer/material/`.
- Materials that fail to resolve fall back to `GetParticleMeshOpaque()`
  with a one-shot warning per emitter (matches the pattern
  `scene_vfx_sync.cpp` uses when an asset path fails to read).

### 3. Re-enable `AddParticleMeshRenderPass`

- Diagnose the original `BeginRenderPass` failure that caused the RC3
  disable. Likely candidates: missing render target transitions,
  outdated render-graph descriptor layout, or stale
  `RHIRenderTarget` descriptor. Verify with `--gpu-validation`.
- Un-comment the call at `frame_particles.cpp:916`. Mesh and trail
  passes are siblings (`frame_particles.cpp:623` has the same RC3
  annotation), so re-enable both together to keep the parity.
- Update the resource declarations on the pass so its inputs (sorted
  index SRV, mesh instance UAV, depth SRV for soft particles) and
  outputs (HDR colour, depth) are correctly tracked by the render
  graph — the auto-barrier pass needs accurate read/write metadata.

### 4. Billboard fallback guard

- Add `if (cfg.renderModule == "mesh_render") continue;` (or the
  equivalent enum check) at `frame_particles.cpp:375` so mesh emitters
  do not double-render as billboard particles. The current symptom of
  this missing guard is a faint dot pattern in the screenshot for any
  mesh emitter.

### 5. Sample asset hygiene

- Keep `sample/content/meshes/test_shard.{obj,uzmesh}` and
  `sample/content/materials/test_glass.uzmat` (already authored under
  the predecessor task). They cover the asset-existence half of the
  hardware-validation prerequisites.
- Re-tune `sample/content/vfx/tests/mesh_sort_shards.uzvfx` if
  necessary so 200 particles read clearly under the default camera (current spawn velocity / scale tuned at the previous step assuming a
  working render path).

## Impact

- Affected specs: `vfx-particle-mesh-render` (new — see `specs/`),
  `vfx-sort-pipeline` (existing — depends on this).
- Affected code:
  - `runtime/engine/renderer/frame/frame_particles.cpp` — sites 375,
    623, 635, 851-852, 916.
  - `runtime/engine/renderer/passes/vfx/particle_mesh_render_pass.cpp` —
    re-enable + verify `BeginRenderPass` correctness.
  - `runtime/engine/asset/formats/uzmesh_format.{h,cpp}` (new public
    accessors) and a small `MeshAssetSystem` header (new,
    `runtime/engine/asset/mesh_asset_system.{h,cpp}`).
  - `runtime/engine/renderer/material/material_pipeline.{h,cpp}` — new
    `GetParticleMeshPSO` resolver, blend-mode aware.
  - `runtime/engine/vfx/modules/mesh_render_module.cpp` — assert that
    `meshConfig.meshPath` and `materialPath` are non-empty before
    forwarding to the batch struct.
- Breaking change: NO. Existing CPU surface (sort pipeline,
  `MeshRenderModule` schema parsing, `.uzvfx` schema) does not change.
  Existing sprite particles continue through the billboard path
  unchanged.
- User benefit: unblocks `phase16_vfx-sort-pipeline-hardware-validation`
  + `phase16_vfx-mesh-particle-depth-sort` (archived but probably
  stale) + every future task that wants mesh particles. Removes a
  silent failure mode where mesh emitters quietly render as
  billboards.

## Dependencies

- Builds on `phase16_vfx-particle-sort-pipeline-refactor` (archived) —
  the producer side of the deterministic-slot mesh CS already ships
  the data this task needs to consume.
- Blocks: `phase16_vfx-sort-pipeline-hardware-validation` (entire
  task), and any future hardware-validation that exercises the mesh
  render path.
- Surfaced by: shader-engineer agent diagnosis on 2026-04-28, while
  attempting item 1.1 of the sort-pipeline hardware-validation task.
