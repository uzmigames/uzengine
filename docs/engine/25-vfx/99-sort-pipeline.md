# VFX Particle Sort Pipeline

> Status: shipped 2026-04-25 (`phase16_vfx-particle-sort-pipeline-refactor`).
> Hardware visual / perf validation tracked separately by
> `phase16_vfx-sort-pipeline-hardware-validation`.

The VFX particle sort pipeline produces a back-to-front draw order for
emitters that need correct alpha-blend compositing. It runs as an inline
compute dispatch on the main command list immediately after particle
simulation and before the mesh-instance / billboard-render passes.

## What participates

| Emitter category | Auto-sorts? | Sort key |
|------------------|-------------|----------|
| Sprite, `blendMode = AlphaBlend` (mode 0) | yes | View depth |
| Sprite, `blendMode = Additive` (mode 1) | no — order-independent | n/a |
| Sprite, `blendMode = PremulAlpha` (mode 2) | no (current) | n/a |
| Mesh, `MeshSortMode::None` | no | n/a |
| Mesh, `MeshSortMode::ViewDepth` | yes | View depth |
| Mesh, `MeshSortMode::CustomAttribute` | yes | User-declared `Float` channel |

`MeshSortMode` is authored per-emitter in `MeshRenderModule` and
serialised through both the editor `.uzfx` schema (`stages` array) and
the production `.uzvfx` schema (`emitters[].modules.render`).

## Data flow

```
DispatchVFXSimulation     [scene_render_frame.cpp]
  └─ inline spawn / update / indirect-args CS
DispatchVFXSort           [frame_vfx_inline.cpp]            ← this doc
  └─ for each opt-in emitter:
       └─ GPUParticleSort::DispatchSort  [gpu_particle_sort.cpp]
            ├─ Phase 0: LocalSortPhase   [particle_sort_cs.hlsl]
            │     - GenerateSortKey reads ages/lifespans (alive check)
            │     - keyGenCount = capacity (full SoA range)
            │     - Dead particles -> SORT_KEY_DEAD (sort to tail)
            └─ Phase 1: GlobalMergePhase [particle_sort_cs.hlsl]
                 - bitonic merge across thread groups
                 - log2(paddedCount/256) * (log2(paddedCount/256)+1)/2
                   dispatches, UAV barrier between each
RegisterParticlePasses   [frame_particles.cpp]
  └─ AddParticleRenderPass  ─ billboard VS reads sortedIndices SRV
  └─ VFX_MeshInstanceCS     ─ mesh CS reads sortedIndices SRV when valid
  └─ AddParticleMeshRenderPass (currently disabled — see RC3 note)
```

## Sort core (`particle_sort_cs.hlsl`)

### `VFXSortConstants` (96 bytes, cbuffer at b0)

| Offset | Field | Purpose |
|-------:|-------|---------|
| 0 | `keyGenCount` | Highest slot index that gets a real key. Pass `capacity` so alive detection covers every potentially-alive slot. |
| 4 | `sortPhase` | 0 = local sort + key generation; 1 = global merge. |
| 8 | `paddedCount` | `NextPow2(keyGenCount)` — bitonic requires power-of-two. |
| 12 | `sortMode` | `GpuSortMode` ordinal: 0 = ViewDepth, 1 = CustomAttribute. |
| 16 | `cameraPos` (float3) | World-space camera position (ViewDepth only). |
| 32 | `cameraForward` (float3) | World-space camera forward, normalised (ViewDepth only). |
| 48 | `positionsSrvIndex` | Bindless SRV — `StructuredBuffer<float4>` particle positions. |
| 52 | `sortKeysUavIndex` | Bindless UAV — output sort keys. |
| 56 | `sortIndicesUavIndex` | Bindless UAV — sorted particle indices. |
| 64 | `bitonicBlockSize` | Phase 1: current merge block size (power of two). |
| 68 | `bitonicSubBlockSize` | Phase 1: sub-block stride within merge block. |
| 80 | `agesSrvIndex` | Bindless SRV — `StructuredBuffer<float>` ages (alive check). |
| 84 | `lifespansSrvIndex` | Bindless SRV — `StructuredBuffer<float>` lifespans. |
| 88 | `customSortKeySrvIndex` | Bindless SRV — `StructuredBuffer<float>` per-particle key (CustomAttribute only). |

The struct sits behind `static_assert(sizeof(VFXSortConstants) == 96)`
on the CPU side and a per-row offset comment in the HLSL cbuffer.
Either side mutating without the other will refuse to compile.

### Why `keyGenCount = capacity`, not `aliveCount`

The spawn shader allocates particle slots from a free-list LIFO:

```hlsl
// particle_spawn_cs.hlsl
InterlockedAdd(freeListHead[0], -1, prevHead);
particleIdx = freeList[prevHead - 1];
```

Alive particles end up scattered across `[0..capacity-1]`. The earlier
sort assumed `[0..aliveCount-1] = alive` and assigned `SORT_KEY_DEAD`
to every slot beyond — which silently dropped half the alive particles
on long-lived emitters. The refactor moved alive detection inside
`GenerateSortKey`: the CS reads `agesBuf[idx]` + `lifespansBuf[idx]`
and short-circuits to `SORT_KEY_DEAD` when `age >= lifespan`, regardless
of slot index. Slots past `keyGenCount` (the padding range up to
`paddedCount`) still get `SORT_KEY_DEAD` via the existing branch.

### Sort modes

| Mode | Key |
|------|-----|
| `kSortMode_ViewDepth` (0) | `dot(positionsBuf[idx].xyz - cameraPos, cameraForward)` -> sortable uint -> `~key` (descending). |
| `kSortMode_CustomAttribute` (1) | `customSortKeyBuf[idx]` (float) -> sortable uint -> `~key`. |

After sort, `sortIndices[0..aliveCount-1]` lists alive particles in
descending key order — back-to-front for ViewDepth, largest-first for
CustomAttribute.

## Inline dispatch (`SceneRenderer::DispatchVFXSort`)

Lives in `frame_vfx_inline.cpp` next to `DispatchVFXSimulation`. The
implementation:

1. Walks `VFXSystem::GetActiveEmitters()`.
2. For each emitter:
   - Skips uninitialised / culled / first-frame emitters.
   - Skips when `cpuAliveCount == 0` (no work).
   - Resolves sort mode:
     - Mesh: forwards `MeshRenderModule::sortMode`. CustomAttribute
       requires `customSortAttribute` to map to a Float user-attribute
       slot via `ParticleEmitter::FindUserAttribute` → resolves to a
       `gpuBuf.WriteSet().userFloat[slotIndex]` SRV. Lookup failure
       logs a one-time warning per emitter (`customSortWarned` flag)
       and downgrades to `None` for that emitter.
     - Sprite: opt-in only when `blendMode == AlphaBlend` (0).
   - Builds a `GPUParticleSort::DispatchParams` with `keyGenCount =
     capacity`, the camera vectors as `float[3]`, and bindless SRV
     indices for ages / lifespans / positions / customSortKey.
3. Calls `gpuBuf.sort.DispatchSort(...)` once per opt-in emitter.

Manual UAV barriers between phases are issued by `GPUParticleSort`
itself (`EmitSortUAVBarriers`); the inline dispatch path bypasses the
render graph's barrier scheduler entirely (matches the inline-sim
pattern, the RG sort path is dead code wrapped in `if (false)`).

## Sprite VS (`particle_billboard_vs.hlsl`)

The VS now translates `instanceID -> particleID` through the bound
sortedIndices buffer when its descriptor index is valid:

```hlsl
uint particleID = sortedSlot;       // identity fallback
if (sortedIndicesSrvIndex != 0 && sortedIndicesSrvIndex != 0xFFFFFFFF) {
    StructuredBuffer<uint> sortedIndices =
        ResourceDescriptorHeap[sortedIndicesSrvIndex];
    particleID = sortedIndices[sortedSlot];
}
```

All downstream attribute reads (`positions[particleID]`, `colors[...]`,
etc.) use the translated index. When the sort dispatch was disabled
the field existed in the CB but the VS never read it — that's why
sprites previously showed alpha-blend artefacts on overlapping
particles even though `sortedIndicesSrvIndex` was wired all the way
through `ParticleBillboardIndices`.

## Mesh CS (`particle_mesh_instance_cs.hlsl`)

Sorted path: `dtid.x` IS the back-to-front output slot. The CS reads
`sortedIndices[sortedSlot]` to resolve the source particle index and
writes the transform to slot `sortedSlot` deterministically (no
`InterlockedAdd`). Thread 0 pins the indirect-args instance count to
`aliveCount` once.

Unsorted path: existing behaviour — `particleIdx = dtid.x`,
`InterlockedAdd` claims an output slot, `InterlockedMax` writes the
running upper bound to indirect-args.

## Backward compatibility

- `MeshSortMode::None` (default) preserves the legacy unsorted path
  byte-identically. Existing `.uzvfx` assets without `sort_mode` keep
  current behaviour.
- Sprite emitters with `blendMode != AlphaBlend` are not opted in.
  Additive blending is order-independent so sort would be wasted work.
- The CS `if (!isSorted)` branch keeps the atomic compaction so
  authoring a sorted emitter and then changing mode at runtime works
  without reload.

## Testing

| Test suite | What it covers |
|------------|----------------|
| `VFXSortConstants` (11 tests) | CPU/GPU layout — size = 96, per-field offsets, AllFields_Roundtrip. |
| `GPUParticleSort` (16 tests) | Create/Destroy lifecycle, paddedCount, double-Destroy safety. |
| `UZ_MeshSortMode` (9 tests) | Enum string round-trip, case-sensitivity, FromString unknown defaults. |
| `UZ_MeshRenderModule_Sort` (11 tests) | Defaults, ParseConfig wrong-type, CustomAttribute downgrade, GetParameters integration. |
| `UZ_MeshSortUzfxPipeline` (6 tests) | Editor-schema `.uzfx` deserialize -> ApplyEmitterStackToModuleStack -> runtime `MeshRenderModule.sortMode`. |
| `UzfxLoader` mesh-render fields (4 tests) | Production-schema `.uzvfx` parses `rotation_mode`/`sort_mode`/`custom_sort_attribute`/`max_instances` with backward-compat defaults. |
| `UZ_MeshRenderModule_LeakFix` + `UZ_ParticleEmitter_LeakFix` (5 tests) | Section 3.3-3.5 CB mismatch fix + the unrelated mesh module GPU buffer leak fix shipped with phase0 closure. |

132/132 sort + mesh tests passing on CPU. GPU integration tests for
key correctness on scattered alive sets (`test_particle_sort_alive_detection`),
mesh deterministic-slot writes (`test_mesh_sort_deterministic_slots`)
and custom-attribute key dispatch (`test_custom_sort_attribute`) are
gated by mock dispatch support in `MockRHIDevice` and tracked by
`phase16_vfx-sort-pipeline-hardware-validation`.

## Sample assets

- `sample/content/vfx/tests/mesh_sort_shards.uzfx` — editor schema
  (`stages`), 200 transparent shards with `sortMode=ViewDepth`.
- `sample/content/vfx/tests/mesh_sort_shards.uzvfx` — production
  schema (`emitters[].modules.render`), same effect.
