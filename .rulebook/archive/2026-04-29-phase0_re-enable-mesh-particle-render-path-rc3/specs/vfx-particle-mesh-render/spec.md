# VFX Particle Mesh Render Path — Spec Delta

## ADDED Requirements

### Requirement: Mesh Asset System for VFX

The engine MUST provide a `MeshAssetSystem` that returns shared
`RHIBufferHandle` vertex and index buffers for `.uzmesh` paths
referenced by `MeshRenderModuleConfig::meshPath`. Repeated requests
for the same path MUST return identical handles. Failed loads MUST
return `RHIBufferHandle::Invalid()` and emit exactly one warning per
unique path.

#### Scenario: Cache hit on duplicate path

Given a `MeshAssetSystem` with no prior cache entry for `meshes/cube.uzmesh`
When `GetVertexBuffer("meshes/cube.uzmesh")` is called twice in succession
Then the first call uploads vertex data to a new buffer and returns its handle
And the second call returns the identical handle without performing a new upload
And no duplicate "MeshAssetSystem: uploaded" log line appears

#### Scenario: Submesh metadata forwarded to particle batch

Given a `.uzmesh` with one LOD containing one submesh of 12 indices at offset 0
When `MeshAssetSystem::GetSubmeshDesc(path, 0)` is called
Then the returned descriptor reports `indexCount=12` and `indexOffset=0`
And `frame_particles.cpp` populates `MeshParticleBatch::indexCountPerInstance` from this value

#### Scenario: Missing path returns Invalid + one-shot warning

Given a `MeshAssetSystem` and a path `meshes/does_not_exist.uzmesh`
When `GetVertexBuffer` is called twice for that path
Then both calls return `RHIBufferHandle::Invalid()`
And exactly one warning is emitted: `MeshAssetSystem: failed to load 'meshes/does_not_exist.uzmesh'`

### Requirement: Material → Particle Mesh PSO Resolver

`MaterialPipeline` MUST expose `GetParticleMeshPSO(const MaterialAsset&,
MeshVertexLayout)` which returns a PSO compatible with the mesh-particle
vertex shader (per-vertex `SV_VertexID` + per-instance SRV). The PSO
MUST honour the material's `EBlendMode` (Opaque, Translucent, Additive,
Masked) and `twoSided` field. Failure to resolve the material MUST fall
back to the existing `GetParticleMeshOpaque()` PSO and emit one warning
per emitter.

#### Scenario: Distinct PSOs per blend mode

Given two materials, one Opaque and one Translucent, otherwise identical
When `GetParticleMeshPSO` is called for each with the same vertex layout
Then the returned PSOs are distinct objects (different cache keys)
And each PSO's blend state matches the material's `blendMode`

#### Scenario: PSO cache hit on duplicate material

Given a single Opaque material requested twice with the same vertex layout
When `GetParticleMeshPSO` is called for both
Then both calls return the identical PSO handle (cache hit)

#### Scenario: Missing material falls back to opaque

Given an emitter whose `materialPath` references a `.uzmat` that fails to parse
When the frame builds its mesh particle batches
Then the batch's PSO equals `GetParticleMeshOpaque()`
And exactly one warning is emitted per affected emitter:
`MeshRender: material '<path>' failed to load — falling back to opaque PSO`

### Requirement: Particle Mesh Render Pass Re-enabled

`AddParticleMeshRenderPass` MUST be invoked from the per-frame render
graph builder for any frame containing at least one mesh-render emitter
with valid mesh + material handles. The pass MUST declare its inputs
(sorted index SRV, mesh instance UAV, depth SRV) and outputs (HDR
colour, depth) so the auto-barrier scheduler emits correct
`D3D12_RESOURCE_STATE_*` transitions.

#### Scenario: Pass executes for a valid mesh emitter

Given a scene with one mesh-render emitter and 200 alive particles
When the render graph compiles and executes one frame
Then the log records `ParticleMeshRenderPass: first execution -- 1 emitter`
And the GPU profiler reports a `VFX_MeshRender` scope with non-zero ms
And no `D3D12 ERROR` is reported by the debug layer

#### Scenario: Pass skipped when no mesh emitters

Given a scene with only sprite emitters
When the render graph compiles
Then `AddParticleMeshRenderPass` is not added to the graph
And no `ParticleMeshRenderPass` log line is emitted

#### Scenario: Pass skipped when mesh handles invalid

Given a mesh emitter whose `meshPath` failed to load (Invalid buffer handles)
When the render graph compiles
Then the pass is not added for that emitter
And the affected emitter is omitted from the per-frame `MeshParticleBatch`
list (warning already emitted at load time)

### Requirement: Billboard Loop Skips Mesh Emitters

The billboard render loop in `frame_particles.cpp` MUST NOT emit
billboard quads for emitters whose render module is `mesh_render`.
A mesh emitter MUST contribute zero vertices to the billboard pass.

#### Scenario: Mesh emitter does not double-render as billboard

Given a single mesh-render emitter with 200 alive particles
When the frame builds the billboard batch
Then the billboard batch contains zero vertices from this emitter
And the log line `AddParticleRenderPass: ... vtx=...` reports zero from this emitter
And the mesh batch contains one entry pointing to the mesh's index buffer

## MODIFIED Requirements

### Requirement: VFX Sort Pipeline Consumer

The hardware-validation suite for the VFX inline sort pipeline
(`phase16_vfx-sort-pipeline-hardware-validation`) DEPENDS on this task
and MUST run after this spec is satisfied. Before this task ships, item
1.1 of that suite (load `mesh_sort_shards.uzvfx` and emit 200
transparent shard particles) is impossible — the asset loads but the
mesh draw is silently skipped and the billboard fallback emits 6-vert
white quads instead.

#### Scenario: Sort hardware-validation unblocked

Given this task is archived and the engine is built at the matching commit
When the demo is launched with
`--vfs-root sample/content --vfx-asset vfx/tests/mesh_sort_shards.uzvfx`
Then the screenshot at frame 90 shows visibly-rendered cyan tetrahedron shards
And the `ParticleMeshRenderPass: first execution` log line is present
And the `vtx=` log line for the billboard pass is zero
