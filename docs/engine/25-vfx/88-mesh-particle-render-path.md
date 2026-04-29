# VFX Mesh-Particle Render Path

> Status: shipped 2026-04-28 (`phase0_re-enable-mesh-particle-render-path-rc3`).

The VFX mesh-particle render path renders 3D mesh instances per particle
instead of camera-facing billboards. Each alive particle places a
world-oriented mesh instance (shard, debris, casing, etc.) in the scene.
The path was disabled during RC3 milestone due to a `BeginRenderPass`
scaffolding bug and has been re-enabled with full render-graph
integration.

## What participates

| Emitter type | Render path | Instancing |
|--------------|-------------|-----------|
| Sprite billboards | `AddParticleRenderPass` | per-particle vertex cluster |
| Mesh | `AddParticleMeshRenderPass` | per-particle world-transform + indirect draw |
| Trails | `AddTrailRenderPass` (RC3 sibling, not yet re-enabled) | line segments |

Mesh particles are authored in `.uzvfx` via the `MeshRenderModule` —
set `type: "mesh"`, `mesh_path`, `material_path`, scale, and optional
`sort_mode` (ViewDepth, CustomAttribute, or None).

## Asset wiring

### Mesh loading: `SceneRenderer` mesh cache

`SceneRenderer::LoadMesh(const std::string& path)` returns a `meshId`
(reference-counted handle). `GetGPUMesh(meshId)` yields the `GPUMesh*`
with vertex buffer, index buffer, and per-LOD submesh metadata (LOD 0
only for particles — no LOD selection). Cite
`runtime/engine/renderer/scene/scene_mesh_assets.cpp:30–80` and
`runtime/engine/renderer/mesh/gpu_mesh.h:43–73`.

In `frame_particles.cpp:734–797`, the mesh-resolution loop resolves
`MeshRenderModuleConfig::meshPath` via `ctx.sceneRenderer->LoadMesh`,
caches the handle per emitter, and extracts `vertexCount`, `indexCount`,
and `indexOffset` for indirect-draw dispatch. On lookup failure, a
one-shot warning logs the missing path.

### Material loading: `MaterialPipeline::GetParticleMeshPSO`

`MeshRenderModuleConfig::materialPath` resolves through the asset
loader. At batch-build time (`frame_particles.cpp:905–916`), the
material's `shadingModel`, `blendMode`, and `twoSided` are packed into
a bit-compact cache key:

```
MakeParticleMeshKey(shadingModel, blendMode, twoSided) → uint32
```

`MaterialPipeline::GetParticleMeshPSO(const MaterialAsset&, MeshVertexLayout)`
returns the PSO from the cache. Cite
`runtime/engine/renderer/material/material_pipeline.cpp:2104–2156`.
PSO variants:

- **Opaque**: no blend, write depth
- **Translucent**: `SrcAlpha/InvSrcAlpha` blend, read depth (for soft particles)
- **Additive**: `SrcAlpha/One` blend, read depth
- **Modulate**: `DstColor/Zero` blend, read depth

On material-load failure, falls back to `GetParticleMeshOpaque()` with
one-shot warning per emitter.

## Per-frame flow

### 1. Sync

`scene_vfx_sync.cpp` reads `VfxEmitterComponent::assetPath`, parses
the `.uzvfx` file via `UzfxLoader`, and instantiates a `ParticleEmitter`
with capacity, module configs, and GPU buffer allocation. Cite
`runtime/engine/renderer/scene/scene_vfx_sync.cpp:367–393`.

### 2. Spawn / update

`ParticleSpawnCS` + update modules write alive particles into the SoA
attribute buffers (positions, velocities, colors, ages, lifespans, user
floats) at their allocated slot indices.

### 3. Sort (optional)

When `sort_mode != None` and the emitter is transparent, `ParticleSortCS`
produces a sorted index buffer in back-to-front order (view-depth or
custom-attribute key). Cite `docs/engine/25-vfx/99-sort-pipeline.md`.

### 4. Instance build: `ParticleMeshInstanceCS`

Runs as an inline dispatch (same pattern as sort, no render-graph
overhead). Reads:

- `instanceTransforms[capacity]` — float4x4 from the emitter's ECS
  component or module config (scale, rotation mode)
- Alive particles from positions/ages/lifespans
- Sorted indices if valid, else identity [0, 1, ..., aliveCount–1]

Writes:

- `instanceTransforms[aliveCount]` — world-space float4x4 per alive particle
- `indirectArgs[5]` — `DrawIndexedInstancedIndirectArgs` (IndexCountPerInstance,
  InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation)
- `instanceCount` — single uint32 written atomically

Cite `runtime/shaders/runtime/vfx/particle_mesh_instance_cs.hlsl:1–58`.

### 5. Mesh draw: `AddParticleMeshRenderPass`

Issues a single `DrawIndexedInstancedIndirect` reading the indirect args.
The VS at `runtime/shaders/runtime/vfx/particle_mesh_vs.hlsl` reads
`instanceTransforms[SV_InstanceID]` via bindless `ResourceDescriptorHeap`
and transforms vertices: `vertexWorldPos = vertexLocalPos * transform`.
The PS at `runtime/shaders/runtime/vfx/particle_mesh_ps.hlsl` applies
per-instance color and half-Lambert N-dot-L shading.

## RC3 disable and fix

### Root cause

Commit `d0626fed` disabled mesh + trail + billboard render passes due to
a "BeginRenderPass fix". The billboard pass received the fix in `c8560ba9`
but mesh + trail stayed offline. Diagnosis: `particle_mesh_render_pass.cpp`
called `SetPipeline` + `DrawIndexedIndirect` without ever calling
`BeginRenderPass` / `SetViewport` / `SetScissor` / `EndRenderPass`. DX12
backend's `BeginRenderPass` is the sole site that calls `OMSetRenderTargets`,
so the indirect draw landed with stale RTVs/DSVs from a prior pass
(typically GBuffer — wrong format for HDR composite).

### Fix scaffold

Header gained `colorTexture`, `depthTexture`, `renderWidth`, `renderHeight`
fields (mirrors `ParticleRenderPassDesc`). Pass setup declares:

- **HDR target**: `Read+Write(RenderTarget)` — ensures the scheduler
  places the pass AFTER lighting writes (Write-only would allow reordering
  before lighting and blanking the scene)
- **Depth**: `Write(DepthStencilWrite)` for opaque, `Read(DepthStencilRead)`
  for transparent
- **Indirect args**: `Read(IndirectArgument)` per batch

Execute callback:

1. Opens with `BeginRenderPass(HDR + depth, clearFlags=0)`
2. Sets viewport + scissor (render-resolution units)
3. Iterates emitters: `SetPipeline` → `DrawIndexedInstancedIndirect`
4. Closes with `EndRenderPass`

Cite `runtime/engine/renderer/passes/vfx/particle_mesh_render_pass.cpp:1–180`.

## Render graph + barriers

```
Read(sortedIndices, NonPixelShaderResource)
Read(meshInstanceTransforms, NonPixelShaderResource)
Read(indirectArgs, IndirectArgument)
Read(depth, DepthStencilRead)               [transparent only]
Write(depth, DepthStencilWrite)             [opaque only]
Read+Write(hdr, RenderTarget)
```

Auto-barrier scheduler emits transitions to/from these states. The
`Read+Write` on HDR ensures correct ordering.

## Validation + reproduction

### Test asset

`sample/content/vfx/tests/mesh_sort_shards.uzvfx`:

```json
{
  "emitters": [
    {
      "name": "transparent_shards",
      "spawn_rate": 10,
      "max_alive": 200,
      "modules": {
        "render": {
          "type": "mesh",
          "mesh_path": "meshes/test_shard.uzmesh",
          "material_path": "materials/test_glass.uzmat",
          "scale": 1.0,
          "sort_mode": "ViewDepth"
        }
      }
    }
  ]
}
```

`test_shard.uzmesh`: 4-vertex tetrahedron.  
`test_glass.uzmat`: cyan PBR, DefaultLit, twoSided, Translucent blend.

### Reproduction recipe

```powershell
$exe = 'e:\UzmiEngine\uzengine\runtime\demos\vfx_viewport_app\build\bin\vfx_viewport_app.exe'
$vfs = 'e:/UzmiEngine/uzengine/sample/content'
& $exe --vfs-root $vfs --vfx-asset 'vfx/tests/mesh_sort_shards.uzvfx' `
       --headless --frames 30 --verbose --gpu-validation
```

### Expected output

```
[VFX] RegisterParticlePasses: mesh emitter loaded (200 alive)
[VFX] AddParticleMeshRenderPass: first execution -- 1 emitters
```

No `D3D12 ERROR` lines. Exit code 0.

### Visibility caveat

The pipeline is structurally re-enabled and GPU-validation clean. The
test asset's shards are not yet visible at the demo's default camera
angle — likely a per-instance color or per-frame spawn-tick issue outside
this task's scope. Filed for follow-up in a separate hardware-validation
task.

## Cross-references

- Spec: `.rulebook/tasks/phase0_re-enable-mesh-particle-render-path-rc3/specs/vfx-particle-mesh-render/spec.md`
- Sort pipeline: `docs/engine/25-vfx/99-sort-pipeline.md`
- Diagnostic tools: `.claude/rules/rendering-diagnostics.md`
